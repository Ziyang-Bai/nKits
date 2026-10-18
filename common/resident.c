#include "resident.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define ROOT_MAGIC 0x6e4b5253u
#define ROOT_ABI 8u
#define WAKE_MAGIC 0x6e4b574bu
#define RAM_START 0x10000000u
#define RAM_END 0x14000000u

typedef struct {
    uint32_t id;
    size_t size;
    char build[65];
    void *state;
    ResidentTick tick;
    ResidentEvent event;
} ResidentSlot;

typedef struct {
    uintptr_t unused[4];
    int (*open)(uint16_t, const char *, uint16_t, uint16_t);
    int (*read)(int, void *, int);
    int (*write)(int, const void *, int);
    int (*seek)(int, int, int);
    int (*close)(int);
    int (*remove)(const char *);
    uintptr_t unused_paths[7];
    int (*rename)(const char *, const char *);
} FileOps;

typedef struct {
    void *ops;
    void *filesystem, *device;
    uint32_t flags, users;
    uint16_t device_id, disk;
} FileMount;

typedef struct {
    uint32_t marker, refresh, save_dialog;
    bool buffered;
} ResidentPlatform;

/* Each entry identifies an OS image; the hook and dialog instructions are
   checked before using its native addresses. */
static const ResidentPlatform platforms[] = {
    {0x103b18e0u, 0x10352cf8u, 0x102b19e8u, false},
    {0x103b1f80u, 0x10353398u, 0x102b2014u, false},
    {0x10429ec0u, 0x10006de4u, 0, true},
    {0x1042aee0u, 0x10006de4u, 0x102f4158u, true}
};

typedef struct {
    uint32_t magic, abi, size;
    ResidentSlot slots[2];
    volatile uint32_t *syscall_busy;
    const uint32_t *natives;
    int (*get_event)(struct s_ns_event *);
    void (*display_refresh)(unsigned);
    volatile uint32_t *display_context;
    void (*time_refresh)(unsigned);
    void *event_queue;
    int (*queue_send)(void *, const uint32_t *, unsigned, unsigned);
    void (*event_activity)(struct s_ns_event *);
    volatile uint32_t *activity;
    uintptr_t save_dialog;
    FileOps files;
    const ResidentPlatform *platform;
    uintptr_t physical;
    uint16_t *portrait_tick, *portrait_event;
    bool portrait, tick_ready, event_ready;
    uint16_t disk;
    int (*file_take)(unsigned);
    int (*file_give)(unsigned);
    int error;
    bool drawing, reading, tick_painted, event_painted, wake_queued;
    unsigned pressed_actions;
    unsigned last_action;
} ResidentRoot;

extern uint32_t resident_display_gate[5], resident_time_gate[5], resident_event_gate[5];
extern uint32_t resident_event_trampoline[5];
extern uint32_t resident_activity_gate[5], resident_activity_trampoline[5];
extern int resident_publish(volatile uint32_t *, uint32_t *, uint32_t, uint32_t);

static ResidentRoot storage;
static ResidentRoot *root;

static bool ram_range(uintptr_t addr, size_t size) {
    return !(addr & 3) && addr >= RAM_START && addr < RAM_END && size <= RAM_END - addr;
}

static uint32_t *branch(uint32_t *p) {
    if (!ram_range((uintptr_t)p, 4) || (*p & 0xff000000u) != 0xeb000000u) return NULL;
    uintptr_t target = (uintptr_t)p + 8 + ((int32_t)(*p << 8) >> 6);
    return ram_range(target, 4) ? (uint32_t *)target : NULL;
}

static bool time_callback(const uint32_t *callback) {
    return (uintptr_t)callback < 0x11000000u && ram_range((uintptr_t)callback, 28) &&
        callback[0] == 0xe59f3014u && callback[1] == 0xe59f1014u &&
        callback[2] == 0xe593c02cu && callback[3] == 0xe5810000u &&
        callback[4] == 0xe28c2001u && callback[5] == 0xe583202cu &&
        callback[6] == 0xe12fff1eu;
}

static volatile uint32_t *time_slot(uintptr_t head) {
    uintptr_t address = head;
    volatile uint32_t *found = NULL;
    /* The bounded native timer ring supplies a one-second idle callback. */
    for (unsigned count = 0; count < 256; ++count) {
        if (!ram_range(address, 68)) return NULL;
        volatile uint32_t *timer = (volatile uint32_t *)address;
        if (timer[3] != 0x54494d45u || !ram_range(timer[0], 68) ||
            !ram_range(timer[1], 68)) return NULL;
        if (timer[4] == 0x54737953u && timer[5] == 0x00656d69u &&
            timer[7] == 1 && (timer[8] & 0xff) == 1 &&
            timer[10] == 100 && timer[11] == 100) {
            if (found) return NULL;
            found = timer + 6;
        }
        address = timer[1];
        if (address == head) return found;
    }
    return NULL;
}

static volatile uint32_t *display_slot(void) {
    if (!root->platform->buffered) {
        const uint32_t *refresh = (const uint32_t *)root->platform->refresh;
        if (!time_callback(refresh)) return NULL;
        const uint32_t *create = branch((uint32_t *)refresh + 27);
        if (!create || !ram_range((uintptr_t)create, 0x100) ||
            create[0] != 0xe92d41f0u || create[43] != 0xe59f0048u ||
            !ram_range(create[63], 4)) return NULL;
        return time_slot(*(const volatile uint32_t *)create[63]);
    }
    if (*(const uint32_t *)0x10006c94u != 0xe59f406cu) return NULL;
    uintptr_t slot = *(const uint32_t *)0x10006d08u;
    if (!ram_range(slot, 4)) return NULL;
    uintptr_t context = *(const uint32_t *)slot;
    if (!ram_range(context, 0x84)) return NULL;
    const uint8_t *timer = (const uint8_t *)context + 0x10;
    if (*(const uint32_t *)(timer + 0x0c) != 0x54494d45u ||
        memcmp(timer + 0x10, "DISPCPY", 8)) return NULL;
    return (volatile uint32_t *)(timer + 0x18);
}

/* Ndless r2022's checked SWI dispatcher exposes the resolved native table.
   Resident callbacks call the captured OS entries directly. */
static const uint32_t *native_table(void) {
    uintptr_t addr = *(volatile uint32_t *)0x10000028u;
    if (addr < RAM_START + 8 || !ram_range(addr - 8, 0xf0)) return NULL;
    const uint32_t *p = (const uint32_t *)addr;
    if (p[-2] != 0x4e455854u || p[0] != 0xe92d000fu ||
        p[26] != 0x159f2070u || p[27] != 0xe7920100u) return NULL;
    uintptr_t table = p[56];
    if (!ram_range(table, (__SYSCALLS_LAST + 1) * 4)) return NULL;
    root->syscall_busy = (volatile uint32_t *)(addr + 0xe4);
    return (const uint32_t *)table;
}

static bool bind_wake(const uint32_t *entry) {
    if (entry[8] != 0xe1a01004u || entry[9] != 0xe3a00001u) return false;
    uint32_t *outer = branch((uint32_t *)entry + 10);
    if (!outer || !ram_range((uintptr_t)outer, 12) ||
        outer[0] != 0xe92d4038u || outer[1] != 0xe1a04001u) return false;
    uint32_t *receive = branch(outer + 2);
    if (!receive || !ram_range((uintptr_t)receive, 0x348) ||
        receive[0] != 0xe92d41f0u || receive[19] != 0xe59f0068u ||
        receive[20] != 0xe1a01005u || receive[21] != 0xe3a02007u ||
        receive[22] != 0xe1a03006u || receive[23] != 0xe58d7000u) return false;
    uint32_t *sender = receive + 0x2d8 / 4;
    uintptr_t queue = receive[47];
    if (sender[0] != 0xe92d4070u || sender[18] != 0xe3a02007u ||
        sender[19] != 0xe59f0018u || sender[27] != queue ||
        !ram_range(queue, 64)) return false;
    const volatile uint32_t *header = (const volatile uint32_t *)queue;
    uint32_t *send = branch(sender + 20);
    if (header[3] != 0x51554555u || header[9] != 7 || !send ||
        (uintptr_t)send >= 0x11000000u) return false;
    root->event_queue = (void *)queue;
    root->queue_send = (void *)send;
    return true;
}

static bool is_wake(const struct s_ns_event *event) {
    return event->timestamp == WAKE_MAGIC && event->type == 0 &&
        ((const uint32_t *)event)[4] == (uint32_t)root;
}

static void resident_event_activity(struct s_ns_event *event) {
    if (is_wake(event)) {
        /* Native idle notifications preserve the APD inactivity deadline. */
        if (root->activity[0]) root->activity[1] = 0;
    } else root->event_activity(event);
}

static void wake_events(void) {
    if (root->reading || root->wake_queued) return;
    const uint32_t message[7] = {WAKE_MAGIC, 0, 0, 0, (uint32_t)root, 0, 0};
    /* A private zero-type notification wakes the OS event task. The hook
       consumes it after servicing deferred work, preserving caller input. */
    root->wake_queued = true;
    if (root->queue_send(root->event_queue, message, 7, 0)) root->wake_queued = false;
}

static bool bind_files(const uint32_t *table) {
    uint32_t *first = (uint32_t *)table[e_NU_Get_First];
    if (!ram_range((uintptr_t)first, 16) || first[0] != 0xe92d40f8u ||
        first[1] != 0xe1a04000u || first[2] != 0xe1a07001u) return false;
    uint32_t *check = branch(first + 3);
    if (!check || !ram_range((uintptr_t)check, 84) || check[0] != 0xe92d4010u ||
        check[1] != 0xe3a00006u || check[19] != 0xe3a00006u) return false;
    uint32_t *take = branch(check + 2), *give = branch(check + 20);
    if (!take || !give || take[0] != 0xe59f300cu || give[0] != 0xe59f3008u)
        return false;
    root->file_take = (void *)take;
    root->file_give = (void *)give;
    DSTAT stat;
    memset(&stat, 0, sizeof(stat));
    if (NU_Get_First(&stat, "A:\\documents\\*")) return false;
    FileMount *mount = stat.unknown6;
    NU_Done(&stat);
    if (!ram_range((uintptr_t)mount, sizeof(*mount)) ||
        !ram_range((uintptr_t)mount->filesystem, 136) ||
        memcmp(mount->filesystem, "REL", 4)) return false;
    /* The REL table remains at filesystem+20 when nUSB owns the mount table. */
    const FileOps *ops = (const FileOps *)((const char *)mount->filesystem + 20);
    const uintptr_t *words = (const uintptr_t *)ops;
    for (unsigned i = 4; i < sizeof(*ops) / 4; ++i) {
        if (i >= 10 && i < 17) continue;
        if (words[i] < RAM_START || words[i] >= 0x11000000u || (words[i] & 3)) return false;
    }
    root->files = *ops;
    root->disk = mount->disk;
    return true;
}

uint32_t resident_seconds(void) {
    return *(volatile uint32_t *)0x90090000u;
}

void *resident_native(unsigned syscall) {
    if (!root || !root->natives || syscall > __SYSCALLS_LAST) return NULL;
    uintptr_t entry = root->natives[syscall];
    return entry >= RAM_START && entry < 0x11000000u ? (void *)entry : NULL;
}

static bool display_available(void) {
    return root && (root->drawing || root->reading) && !*root->syscall_busy &&
        *(volatile uint32_t *)0xc0000010u == root->physical &&
        ((*(volatile uint32_t *)0xc0000018u >> 1) & 7u) == 6u;
}

static void read_screen(uint16_t *pixels) {
    const volatile uint16_t *source = (const uint16_t *)root->physical;
    if (root->portrait) {
        for (unsigned y = 0; y < 240; ++y)
            for (unsigned x = 0; x < 320; ++x)
                pixels[y * 320 + x] = source[x * 240 + y];
    } else {
        for (unsigned i = 0; i < 320 * 240; ++i) pixels[i] = source[i];
    }
}

static void write_portrait(const uint16_t *pixels) {
    volatile uint16_t *target = (volatile uint16_t *)root->physical;
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
            target[x * 240 + y] = pixels[y * 320 + x];
}

bool resident_copy_screen(uint16_t pixels[320 * 240]) {
    if (!display_available()) return false;
    unsigned mask = resident_lock();
    read_screen(pixels);
    resident_unlock(mask);
    return true;
}

volatile uint16_t *resident_framebuffer(void) {
    if (!display_available() || (root->drawing && root->reading)) return NULL;
    bool *painted = root->drawing ? &root->tick_painted : &root->event_painted;
    if (root->platform->buffered) {
        uintptr_t pixels = root->display_context[1];
        if (!ram_range(pixels, 320 * 240 * 2)) return NULL;
        *painted = true;
        return (volatile uint16_t *)pixels;
    }
    if (root->portrait) {
        bool *ready = root->drawing ? &root->tick_ready : &root->event_ready;
        uint16_t *pixels = root->drawing ? root->portrait_tick : root->portrait_event;
        if (!*ready) { read_screen(pixels); *ready = true; }
        *painted = true;
        return pixels;
    }
    *painted = true;
    return (volatile uint16_t *)root->physical;
}

static bool paint(void) {
    bool wake = false;
    root->drawing = true;
    root->tick_ready = false;
    root->tick_painted = false;
    if (!*root->syscall_busy) {
        for (unsigned i = 0; i < 2; ++i) {
            ResidentSlot *slot = &root->slots[i];
            if (slot->id && slot->tick) wake |= slot->tick(slot->state);
        }
    }
    if (root->tick_ready) write_portrait(root->portrait_tick);
    root->drawing = false;
    return wake;
}

static void resident_time_refresh(unsigned context) {
    bool wake = paint();
    root->time_refresh(context);
    if (root->platform->buffered && root->tick_painted)
        root->display_refresh((unsigned)root->display_context);
    if (wake) wake_events();
}

static void resident_display_refresh(unsigned context) {
    paint();
    root->display_refresh(context);
}

int resident_event_read(struct s_ns_event *event) {
    for (;;) {
        int result = root->get_event(event);
        if (result == 1) {
            unsigned action = (event->ascii == '\r' || event->ascii == '\n' ||
                               event->key == 0x10u) ? 1u :
                              (event->ascii == 27 || event->key == 0x96u) ? 2u : 0u;
            if (action && (event->type == 8 || event->type == 16))
                root->last_action = action;
            if (event->type == 8) root->pressed_actions |= action;
            else if (event->type == 16) root->pressed_actions &= ~action;
        }
        bool wake = result == 1 && is_wake(event);
        if (wake) root->wake_queued = false;
        if (result == 1 && !root->reading && !*root->syscall_busy) {
            root->reading = true;
            root->event_ready = false;
            root->event_painted = false;
            for (unsigned i = 0; i < 2; ++i) {
                ResidentSlot *slot = &root->slots[i];
                if (slot->id && slot->event) slot->event(slot->state, event);
            }
            if (root->event_ready) write_portrait(root->portrait_event);
            if (root->platform->buffered && root->event_painted)
                root->display_refresh((unsigned)root->display_context);
            root->reading = false;
        }
        if (!wake) return result;
    }
}

void *resident_install(uint32_t id, const char *build, void *state, size_t size,
                       ResidentTick tick, ResidentEvent event,
                       bool *fresh, const char **error) {
    *fresh = false;
    *error = NULL;
    uint32_t marker = *(volatile uint32_t *)0x10000020u;
    const ResidentPlatform *platform = NULL;
    for (unsigned i = 0; i < sizeof(platforms) / sizeof(platforms[0]); ++i)
        if (platforms[i].marker == marker) platform = platforms + i;
    if (hwtype() != 1 || nl_hwsubtype() == 1 || !platform || nl_ndless_rev() < 2022) {
        *error = "CX OS 4.5.5.79 / CX II OS 6.4.0.74 / Ndless r2022 required";
        return NULL;
    }
    if (!id || !state || strlen(build) != 64) {
        *error = "Invalid resident image";
        return NULL;
    }
    root = &storage;
    root->platform = platform;
    volatile uint32_t *display = display_slot();
    if (!display) {
        *error = "OS display timer unsupported";
        return NULL;
    }
    volatile uint32_t *timer = platform->buffered ? time_slot((uintptr_t)(display - 6)) : display;
    if (!timer) {
        *error = "OS one-second timer unsupported";
        return NULL;
    }
    if (*display != platform->refresh) {
        const uint32_t *gate = (const uint32_t *)*display;
        if (!ram_range((uintptr_t)gate, 20) || gate[0] != 0xe59ff004u ||
            gate[1] != ROOT_MAGIC || gate[2] != ROOT_ABI ||
            !ram_range(gate[4], sizeof(ResidentRoot))) {
            *error = "Display hook conflict; restart calculator";
            return NULL;
        }
        root = (ResidentRoot *)gate[4];
        if (root->magic != ROOT_MAGIC || root->abi != ROOT_ABI ||
            root->size != sizeof(*root)) {
            *error = "Restart calculator to update nKits";
            return NULL;
        }
        if (timer != display) {
            const uint32_t *time_gate = (const uint32_t *)*timer;
            if (!ram_range((uintptr_t)time_gate, 20) || time_gate[0] != 0xe59ff004u ||
                time_gate[1] != ROOT_MAGIC || time_gate[2] != ROOT_ABI ||
                time_gate[4] != (uint32_t)root) {
                *error = "One-second hook conflict; restart calculator";
                return NULL;
            }
        }
    } else {
        uint32_t original_time = *timer;
        if (!time_callback((const uint32_t *)original_time)) {
            *error = "OS time callback unsupported";
            return NULL;
        }
        root = &storage;
        memset(root, 0, sizeof(*root));
        root->magic = ROOT_MAGIC;
        root->abi = ROOT_ABI;
        root->size = sizeof(*root);
        root->platform = platform;
        root->display_refresh = (void *)platform->refresh;
        root->display_context = platform->buffered ? display - 10 : NULL;
        root->time_refresh = (void *)original_time;
        root->save_dialog = platform->save_dialog;
        root->physical = *(volatile uint32_t *)0xc0000010u;
        if (!ram_range(root->physical, 320 * 240 * 2) &&
            !(platform->buffered && root->physical == 0xa8000000u)) {
            *error = "OS framebuffer unsupported";
            return NULL;
        }
        root->portrait = lcd_type() == SCR_240x320_565;
        const uint32_t *table = native_table();
        if (!table || !bind_files(table)) {
            *error = "Native callback ABI unsupported";
            return NULL;
        }
        root->natives = table;
        volatile uint32_t *entry = (volatile uint32_t *)table[e_get_event];
        if (!ram_range((uintptr_t)entry, 0x78) || entry[0] != 0xe92d4070u ||
            entry[1] != 0xe59f5068u) {
            *error = "Event hook conflict; restart calculator";
            return NULL;
        }
        if (!bind_wake((const uint32_t *)entry)) {
            *error = "Native event queue unsupported";
            return NULL;
        }
        volatile uint32_t *activity = branch((uint32_t *)entry + 13);
        if (!activity || !ram_range((uintptr_t)activity, 0x6c) ||
            activity[0] != 0xe59f3060u || activity[1] != 0xe5932000u ||
            !ram_range(activity[26], 8)) {
            *error = "Native event activity ABI unsupported";
            return NULL;
        }
        root->activity = (volatile uint32_t *)activity[26];
        if (root->portrait && !platform->buffered) {
            root->portrait_tick = malloc(2 * 320 * 240 * sizeof(uint16_t));
            if (!root->portrait_tick) {
                *error = "Resident display allocation failed";
                return NULL;
            }
            root->portrait_event = root->portrait_tick + 320 * 240;
        }
        resident_event_trampoline[0] = entry[0];
        resident_event_trampoline[1] = 0xe59f5000u;
        resident_event_trampoline[2] = 0xe59ff000u;
        resident_event_trampoline[3] = entry[29];
        resident_event_trampoline[4] = (uint32_t)(entry + 2);
        root->get_event = (void *)resident_event_trampoline;
        resident_activity_trampoline[0] = 0xe59f3004u;
        resident_activity_trampoline[1] = activity[1];
        resident_activity_trampoline[2] = 0xe59ff000u;
        resident_activity_trampoline[3] = activity[26];
        resident_activity_trampoline[4] = (uint32_t)(activity + 2);
        root->event_activity = (void *)resident_activity_trampoline;
        resident_activity_gate[3] = (uint32_t)resident_event_activity;
        resident_activity_gate[4] = (uint32_t)root;
        resident_display_gate[3] = platform->buffered ?
            (uint32_t)resident_display_refresh : (uint32_t)resident_time_refresh;
        resident_display_gate[4] = (uint32_t)root;
        resident_time_gate[3] = (uint32_t)resident_time_refresh;
        resident_time_gate[4] = (uint32_t)root;
        resident_event_gate[3] = (uint32_t)resident_event_read;
        resident_event_gate[4] = (uint32_t)root;
        unsigned mask = resident_lock();
        if (entry[0] != 0xe92d4070u || entry[1] != 0xe59f5068u ||
            *display != platform->refresh || *timer != original_time ||
            activity[0] != 0xe59f3060u || activity[1] != 0xe5932000u) {
            resident_unlock(mask);
            free(root->portrait_tick);
            *error = "Resident hook changed during installation";
            return NULL;
        }
        nl_set_resident();
        resident_publish(activity, resident_activity_gate, 0xe59f3060u, 0xe5932000u);
        resident_publish(entry, resident_event_gate, 0xe92d4070u, 0xe59f5068u);
        *display = (uint32_t)resident_display_gate;
        if (timer != display) *timer = (uint32_t)resident_time_gate;
        resident_unlock(mask);
    }
    ResidentSlot *empty = NULL;
    for (unsigned i = 0; i < 2; ++i) {
        ResidentSlot *slot = &root->slots[i];
        if (!slot->id) empty = slot;
        else if (slot->id == id) {
            if (slot->size != size || strcmp(slot->build, build)) {
                *error = "Restart calculator to update this program";
                return NULL;
            }
            return slot->state;
        }
    }
    if (!empty) {
        *error = "Resident slots full";
        return NULL;
    }
    nl_set_resident();
    unsigned mask = resident_lock();
    empty->state = state;
    empty->size = size;
    memcpy(empty->build, build, 65);
    empty->tick = tick;
    empty->event = event;
    empty->id = id;
    resident_unlock(mask);
    *fresh = true;
    return state;
}

static int file_result(int result) {
    if (result >= 0) { root->error = 0; return result; }
    switch (result) {
        case -3024: root->error = ENOENT; break;
        case -3025: root->error = EEXIST; break;
        case -3021: root->error = EACCES; break;
        case -3017: root->error = ENAMETOOLONG; break;
        case -3023: root->error = EBUSY; break;
        case -3033: root->error = ENOMEM; break;
        default: root->error = EIO; break;
    }
    return -1;
}

static bool file_path(const char *path, char full[260]) {
    static const char prefix[] = "/documents/";
    for (unsigned i = 0; i < sizeof(prefix) - 1; ++i) {
        if (path[i] != prefix[i]) {
            root->error = EINVAL;
            return false;
        }
    }
    full[0] = 'A'; full[1] = ':';
    for (unsigned i = 0;; ++i) {
        if (i > 256) {
            root->error = ENAMETOOLONG;
            return false;
        }
        char c = path[i];
        full[i + 2] = c == '/' ? '\\' : c;
        if (!c) return true;
    }
}

int resident_open(const char *path, int flags, int mode) {
    char full[260];
    if (!file_path(path, full)) return -1;
    uint16_t access = 0x4000;
    switch (flags & O_ACCMODE) {
        case O_WRONLY: access |= 1; break;
        case O_RDWR: access |= 2; break;
    }
    if (flags & O_CREAT) access |= 0x100;
    if (flags & O_TRUNC) access |= 0x200;
    if (flags & O_EXCL) access |= 0x400;
    if (flags & O_APPEND) access |= 8;
    if (root->file_take(6)) return file_result(-3035);
    int result = root->files.open(root->disk, full, access, mode);
    root->file_give(6);
    return file_result(result);
}

int resident_write(int fd, const void *data, unsigned size) {
    if (size > INT_MAX || root->file_take(6)) return file_result(-3035);
    int result = root->files.write(fd, data, (int)size);
    root->file_give(6);
    return file_result(result);
}

int resident_close(int fd) {
    if (root->file_take(6)) return file_result(-3035);
    int result = root->files.close(fd);
    root->file_give(6);
    return file_result(result);
}

int resident_remove(const char *path) {
    char full[260];
    if (!file_path(path, full)) return -1;
    if (root->file_take(6)) return file_result(-3035);
    int result = root->files.remove(full);
    root->file_give(6);
    return file_result(result);
}

int resident_rename(const char *from, const char *to) {
    char a[260], b[260];
    if (!file_path(from, a) || !file_path(to, b)) return -1;
    if (root->file_take(6)) return file_result(-3035);
    int result = root->files.rename(a, b);
    root->file_give(6);
    return file_result(result);
}

int resident_save_dialog(char folder[256], char name[256], unsigned *pending_actions) {
    static const uint32_t cx_signature[] = {
        0xe92d4ff0u, 0xe59fbe10u, 0xe59f4e10u, 0xe24ddfbfu,
        0xe58d1030u, 0xe58d0038u, 0xe58d203cu, 0xe59fae00u
    };
    static const uint32_t cx2_signature[] = {
        0xe92d4ff0u, 0xe59fbe38u, 0xe59f4e38u, 0xe24ddfbbu,
        0xe58d102cu, 0xe58d0034u, 0xe58d2038u, 0xe59fae28u
    };
    *pending_actions = 0;
    if (!root) return -1;
    const uint32_t *signature = root->platform->buffered ? cx2_signature : cx_signature;
    if (!root->save_dialog) {
        uintptr_t found = 0;
        for (uintptr_t address = 0x10200000u; address <= 0x10400000u - 32; address += 4) {
            const volatile uint32_t *candidate = (const uint32_t *)address;
            if (candidate[0] != signature[0]) continue;
            unsigned i;
            for (i = 1; i < 8 && candidate[i] == signature[i]; ++i) {}
            if (i != 8) continue;
            if (found) { found = 0; break; }
            found = address;
        }
        root->save_dialog = found ? found : UINTPTR_MAX;
    }
    const volatile uint32_t *entry = (const uint32_t *)root->save_dialog;
    if (!ram_range((uintptr_t)entry, 32)) {
        root->error = ENOSYS;
        return -1;
    }
    for (unsigned i = 0; i < 8; ++i) {
        if (entry[i] != signature[i]) {
            root->error = ENOSYS;
            return -1;
        }
    }
    /* The OS chooser edits two 256-byte UTF-8 buffers and returns its button ID.
       Its nested event loop leaves the outer resident callback latched. */
    int (*control)(int) = resident_native(e_TCT_Local_Control_Interrupts);
    int (*dialog)(int, char *, char *, int, int) = (void *)entry;
    int mask = control(0);
    int result = dialog(0, folder, name, 0, 0);
    *pending_actions = root->pressed_actions;
    control(mask);
    root->error = 0;
    return result;
}

int resident_errno(void) {
    return root->error;
}

int resident_question(const uint16_t *title, const uint16_t *text,
                       const uint16_t *first, const uint16_t *second,
                       unsigned *pending_actions) {
    *pending_actions = root ? root->pressed_actions : 0;
    if (!root) return -1;
    int (*control)(int) = resident_native(e_TCT_Local_Control_Interrupts);
    int (*dialog)(int, const uint16_t *, const uint16_t *, const uint16_t *, int,
                  const uint16_t *, int, const char **) = resident_native(e__show_msgbox_2b);
    if (!control || !dialog) {
        root->error = ENOSYS;
        return -1;
    }
    const char *description[] = {"DLG", NULL};
    int mask = control(0);
    root->last_action = 0;
    int result = dialog(0, title, text, first, 1, second, 2, description);
    *pending_actions = root->pressed_actions;
    control(mask);
    root->error = 0;
    /* Native two-button forms map Escape to the second button's ID. */
    return root->last_action == 2 ? 0 : result;
}
