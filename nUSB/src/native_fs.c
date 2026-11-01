#include "native_fs.h"
#include "native_path.h"
#include "fs.h"
#include "native_namespace.h"
#include "resident_check.h"

#include <libndls.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

extern int resident_publish(volatile uint32_t *entry, uint32_t *gate,
                            uint32_t first, uint32_t second);

#define USB_PREFIX "A:\\documents\\USB"
#define USB_PREFIX_LEN (sizeof(USB_PREFIX) - 1)
#define FILE_BASE 0x4e550000
#define FILE_COUNT 10

/* CX II CAS 6.4 REL open (104cb0f4) and error map (104e1584). */
enum { N_ACCESS = -3021, N_LONG_PATH = -3017, N_NO_FILE = -3024,
       N_EXISTS = -3025, N_BUSY = -3023, N_INVALID = -3030,
       N_MEMORY = -3033, N_IO = -3035 };
enum { N_WRITE = 1, N_RDWR = 2, N_APPEND = 8, N_CREATE = 0x100,
       N_TRUNCATE = 0x200, N_EXCLUSIVE = 0x400, N_BINARY = 0x4000,
       N_TEXT = 0x8000 };

typedef struct NativeMount NativeMount;
typedef struct {
    char short_name[13];
    char name[256];
    uint8_t attributes;
    uint8_t dates[18];
    uint32_t size;
    NativeMount *mount;
    void *context;
    uint32_t stamp;
    uint16_t disk, reserved;
} NativeStat;

typedef struct {
    uintptr_t init, mount, unmount, abort;
    int (*open)(uint16_t, const char *, uint16_t, uint16_t);
    int (*read)(int, void *, int);
    int (*write)(int, const void *, int);
    int (*seek)(int, int, int);
    int (*close)(int);
    int (*remove)(const char *);
    int (*utime)(NativeStat *, uint16_t, uint16_t, uint16_t,
                 uint16_t, uint16_t, uint16_t);
    int (*set_attributes)(const char *, uint8_t);
    int (*get_attributes)(uint8_t *, const char *);
    int (*truncate)(int, int);
    int (*flush)(int);
    int (*mkdir)(const char *);
    int (*rmdir)(const char *);
    int (*rename)(const char *, const char *);
    int (*first)(NativeStat *, const char *);
    int (*next)(NativeStat *);
    int (*done)(NativeStat *);
    uintptr_t format, format_info, free_space;
    int (*node_new)(uint16_t, const char *, void **);
    int (*node_free)(uint16_t, void *);
    int (*node_string)(uint16_t, void *, char *);
    uintptr_t release, reclaim;
} NativeOps;

struct NativeMount {
    NativeOps *ops;
    void *filesystem, *device;
    uint32_t flags, users;
    uint16_t device_id, disk;
    uint32_t reserved, stamp;
    char drive[2], name[6];
};

typedef char stat_layout[sizeof(NativeStat) == 308 &&
    offsetof(NativeStat, attributes) == 269 &&
    offsetof(NativeStat, context) == 296 ? 1 : -1];
typedef char ops_layout[sizeof(NativeOps) == 116 ? 1 : -1];
typedef char mount_layout[sizeof(NativeMount) == 40 ? 1 : -1];

typedef struct NativeDir {
    struct NativeDir *next;
    FsDir *dir;
    NativePortal *portal;
    uint32_t generation, session_id;
    uint64_t epoch;
    unsigned phase;
    bool exact, portal_directory;
    char pattern[256];
    char identity_name[256];
    char path[NATIVE_PATH_MAX];
    char real[4096];
    NativeResolved current;
} NativeDir;

typedef struct NativeNode {
    struct NativeNode *next;
    char path[NATIVE_PATH_MAX];
    uint64_t epoch;
    char real[4096];
    bool portal;
    FsIdentity identity;
} NativeNode;

typedef struct {
    FsFile *file;
    uint32_t generation, session_id, position, size;
    unsigned flags;
} NativeFile;

typedef struct {
    int handle;
    unsigned flags;
    uint32_t position;
    bool used;
} InternalFile;

static NativeMount *mount;
static NativeOps original, proxy;
static ResidentApi *resident;
static NativeDir *directories;
static NativeNode *nodes;
static NativeFile files[FILE_COUNT];
static InternalFile internal_files[FILE_COUNT];
static unsigned references;
static bool session;
static uint32_t media_generation, session_id;
static bool native_acquiring;
static uint32_t namespace_generation;
static uint32_t *case_entry;
static uint32_t case_trampoline[5];

/* REL contexts are aligned allocations; bit zero marks nUSB-owned contexts. */
#define EXACT_MATCH ((void *)1)
static uint32_t semaphore[10];
static void *lock_owner;
static int (*sem_create)(void *, const char *, unsigned, uint8_t);
static int (*sem_obtain)(void *, unsigned);
static int (*sem_release)(void *);
static int (*current_clock)(uint64_t *);
static int (*file_user_take)(unsigned);
static int (*file_user_give)(unsigned);
static void (*fat_clock)(uint32_t *, uint64_t *);
static __typeof__(&TCC_Current_Task_Pointer) current_task;
static __typeof__(&TCT_Local_Control_Interrupts) irq_control;
static __typeof__(&NU_Current_Dir) current_directory;

static bool words(const uint32_t *p, unsigned count) {
    return resident_ram_range((uint32_t)p, count * sizeof(*p));
}

static uint32_t *branch(uint32_t *p, bool link) {
    if (!words(p, 1) || (*p & 0xff000000u) !=
        (link ? 0xeb000000u : 0xea000000u)) return NULL;
    int32_t offset = (int32_t)(*p << 8) >> 6;
    uint32_t *target = (uint32_t *)((uintptr_t)p + 8 + offset);
    return words(target, 1) ? target : NULL;
}

static bool bind_case_rename(void) {
    /* OS case-preserving rename uses two physical renames even for equal names.
       Match its checked body in the OS text preceding the IRQ entry point. */
    static const uint32_t code[28] = {
        0xe92d40f0, 0xe59f4064, 0xe24ddf43, 0xe5943000,
        0xe1a07001, 0xe1a05000, 0xe58d3104, 0,
        0xe2501000, 0x1a000007, 0xe28d6f42, 0xe5661104,
        0xe1a00006, 0, 0xe1a00005, 0xe1a01006,
        0, 0xe1a05006, 0xe1a00005, 0xe1a01007,
        0, 0xe59d2104, 0xe5940000, 0xe1520000,
        0x1a000001, 0xe28ddf43, 0xe8bd80f0, 0
    };
    uint32_t *found = NULL;
    uint32_t *end = (uint32_t *)*(volatile uint32_t *)0x10000020u;
    for (uint32_t *p = (uint32_t *)0x10000040u; p + 29 <= end; ++p) {
        if (p[0] != code[0] || p[1] != code[1]) continue;
        unsigned i;
        for (i = 2; i < 28; ++i)
            if (code[i] ? p[i] != code[i] : !branch(p + i, true)) break;
        if (i != 28 || !words((uint32_t *)p[28], 1)) continue;
        uint32_t *temporary = branch(p + 13, true);
        uint32_t *rename = branch(p + 16, true);
        if (!words(temporary, 2) || temporary[0] != 0xe351000bu ||
            temporary[1] != 0xe92d4070u || rename != branch(p + 20, true) ||
            !words(rename, 2) || rename[0] != 0xe92d41f0u ||
            rename[1] != 0xe24ddf82u) continue;
        if (found) return false;
        found = p;
    }
    if (!found) return false;
    case_entry = found;
    case_trampoline[0] = found[0];
    case_trampoline[1] = 0xe59f4000u;
    case_trampoline[2] = 0xe59ff000u;
    case_trampoline[3] = found[28]; /* Relocate the PC-relative stack-guard load. */
    case_trampoline[4] = (uint32_t)(found + 2);
    return true;
}

static bool bind_clock(const NativeOps *ops) {
    /* REL's helpers supply the OS clock and FAT encoding inside filesystem callbacks. */
    uint32_t *p = (uint32_t *)ops->utime;
    if (!words(p, 110) || p[102] != 0xe24b302cu || p[103] != 0xe1a00003u ||
        p[105] != 0xe24b2030u || p[106] != 0xe24b302cu ||
        p[107] != 0xe1a00002u || p[108] != 0xe1a01003u) return false;
    uint32_t *now = branch(p + 104, true);
    uint32_t *pack = branch(p + 109, true);
    if (!words(now, 2) || !words(pack, 2) ||
        now[0] != 0xe92d4830u || now[1] != 0xe28db00cu ||
        pack[0] != 0xe92d4800u || pack[1] != 0xe28db004u) return false;
    current_clock = (void *)now;
    fat_clock = (void *)pack;
    return true;
}

static bool bind_semaphore(bool cas) {
    /* Follow NU_Get_First's file-user lock. Both OS variants expose the same
       checked instruction sequence; private kernel addresses come from it. */
    uint32_t *first = (uint32_t *)(cas ? 0x1049691cu : 0x104958fcu);
    if (!words(first, 4) || first[0] != 0xe92d40f8u ||
        first[1] != 0xe1a04000u || first[2] != 0xe1a07001u) return false;
    uint32_t *check = branch(first + 3, true);
    if (!words(check, 23) || check[0] != 0xe92d4010u ||
        check[1] != 0xe3a00006u || check[19] != 0xe3a00006u) return false;
    uint32_t *take = branch(check + 2, true);
    uint32_t *give = branch(check + 20, true);
    if (!words(take, 5) || !words(give, 4) ||
        take[0] != 0xe59f300cu || take[1] != 0xe0800100u ||
        take[2] != 0xe0830180u || take[3] != 0xe3e01000u ||
        give[0] != 0xe59f3008u || give[1] != take[1] ||
        give[2] != take[2]) return false;
    uint32_t *obtain = branch(take + 4, false);
    uint32_t *release = branch(give + 3, false);
    file_user_take = (void *)take;
    file_user_give = (void *)give;
    if (!obtain || !release || obtain <= release || obtain - release > 256)
        return false;
    uint32_t *create = NULL;
    for (uint32_t *p = release; p + 52 <= obtain; ++p) {
        if (p[0] == 0xe92d40f8u && p[1] == 0xe1a04000u &&
            p[2] == 0xe1a05001u && p[3] == 0xe1a06002u &&
            p[4] == 0xe1a07003u && p[49] == 0x53454d41u) {
            if (create) return false;
            create = p;
        }
    }
    if (!create) return false;
    sem_create = (void *)create;
    sem_obtain = (void *)obtain;
    sem_release = (void *)release;
    return true;
}

static bool enter(void) {
    unsigned cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    void *task = current_task();
    if ((cpsr & 0x80) || !task || task == lock_owner) return false;
    if (sem_obtain(semaphore, UINT32_MAX)) return false;
    lock_owner = task;
    return true;
}

static void close_session(void) {
    if (!session) return;
    if (resident->session_active && resident->session_id == session_id) {
        fs_unmount();
        resident->close();
    }
    session = false;
}

static void leave(void) {
    if (!references) close_session();
    lock_owner = NULL;
    sem_release(semaphore);
}

static uint32_t disk_generation(void *ctx) {
    MscInfo info;
    (void)ctx;
    resident->poll(&info);
    if (!session || !resident->session_active || resident->session_id != session_id)
        return ~media_generation;
    return info.generation;
}

static MscResult disk_read(void *ctx, uint32_t lba, uint32_t count, void *buf) {
    (void)ctx;
    if (!session || resident->session_id != session_id) return MSC_NO_DEVICE;
    return resident->read(lba, count, buf);
}

static MscResult disk_write(void *ctx, uint32_t lba, uint32_t count, const void *buf) {
    (void)ctx;
    if (!session || resident->session_id != session_id) return MSC_NO_DEVICE;
    return resident->write(lba, count, buf);
}

static MscResult disk_sync(void *ctx) {
    (void)ctx;
    if (!session || resident->session_id != session_id) return MSC_NO_DEVICE;
    return resident->sync();
}

static uint32_t disk_fattime(void *ctx) {
    uint64_t now;
    uint32_t packed;
    (void)ctx;
    if (!current_clock(&now)) return 0;
    fat_clock(&packed, &now);
    return packed;
}

static int status(FsResult result) {
    switch (result) {
        case FS_OK: return 0;
        case FS_NOT_FOUND: return N_NO_FILE;
        case FS_PATH_TOO_LONG: return N_LONG_PATH;
        case FS_NO_MEMORY: return N_MEMORY;
        case FS_EXISTS: return N_EXISTS;
        case FS_DENIED:
        case FS_WRITE_PROTECTED: return N_ACCESS;
        case FS_TOO_LARGE:
        case FS_INVALID_ARGUMENT: return N_INVALID;
        case FS_BUSY: return N_BUSY;
        default: return N_IO;
    }
}

static int mutation_status(FsResult result) {
    native_namespace_finish(NULL, result);
    return status(result);
}

static int volume(void) {
    MscInfo info;
    resident->poll(&info);
    if (session && (!info.ready || info.generation != media_generation ||
                    resident->session_id != session_id))
        close_session();
    if (session) return 0;
    native_acquiring = true;
    MscResult opened = resident->open(&info, NULL);
    native_acquiring = false;
    if (opened != MSC_OK) return opened == MSC_BUSY ? N_BUSY : N_IO;
    session = true;
    session_id = resident->session_id;
    media_generation = info.generation;
    BlockDevice disk = {NULL, disk_read, info.sector_size, info.sector_count,
                        disk_generation, disk_write, disk_sync, disk_fattime};
    int result = status(fs_mount(&disk));
    if (result) close_session();
    return result;
}

void native_fs_session_changed(uint32_t id, uint32_t generation) {
    if (generation != namespace_generation || !native_acquiring) {
        native_namespace_reset();
        namespace_generation = generation;
    }
    (void)id;
}

void native_fs_namespace_changed(void) {
    native_namespace_reset();
}

FsResult native_fs_portal_path(const FsEntry *entry, const FsIdentity *parent,
                             char *path, size_t capacity) {
    return native_namespace_portal_path(entry, parent, path, capacity);
}

static int route(const char *path, char *full, bool *usb) {
    char cwd[NATIVE_PATH_MAX];
    const char *p = path;
    if (!p || !*p) return N_INVALID;
    if (strlen(path) >= NATIVE_PATH_MAX) return N_LONG_PATH;
    if (p[1] == ':') p += 2;
    cwd[0] = '\0';
    if (*p != '\\' && *p != '/' && current_directory("A:\\", cwd))
        return N_IO;
    if (native_path(path, cwd, full, usb)) return N_LONG_PATH;
    bool virtual_cwd = !strncasecmp(cwd, USB_PREFIX + 2, USB_PREFIX_LEN - 2) &&
        (!cwd[USB_PREFIX_LEN - 2] || cwd[USB_PREFIX_LEN - 2] == '\\');
    if (!*usb && !virtual_cwd) strcpy(full, path);
    return 0;
}

static void case_rename(const char *from, const char *to) {
    const char *leaf = from;
    for (const char *p = from; *p; ++p)
        if (*p == '/' || *p == '\\') leaf = p + 1;
    if (native_namespace_reserved(leaf)) {
        char a[NATIVE_PATH_MAX], b[NATIVE_PATH_MAX];
        bool usb_a, usb_b;
        /* Alias case is display spelling; the underlying object keeps its name. */
        if (!route(from, a, &usb_a) && usb_a &&
            !route(to, b, &usb_b) && usb_b && !strcasecmp(a, b)) return;
    }
    ((void (*)(const char *, const char *))case_trampoline)(from, to);
}


static void fill(NativeStat *d, const FsEntry *entry) {
    memset(d, 0, offsetof(NativeStat, mount));
    size_t length = strlen(entry->name);
    memcpy(d->name, entry->name, length + 1);
    if (length < sizeof(d->short_name)) memcpy(d->short_name, entry->name, length + 1);
    d->attributes = entry->attributes;
    d->size = (uint32_t)entry->size;
    memcpy((char *)d + 272, &entry->created_time, 2);
    memcpy((char *)d + 274, &entry->created_date, 2);
    memcpy((char *)d + 276, &entry->access_date, 2);
    memcpy((char *)d + 280, &entry->time, 2);
    memcpy((char *)d + 282, &entry->date, 2);
}

static NativeFile *file_slot(int fd) {
    unsigned index = (unsigned)fd - FILE_BASE;
    return index < FILE_COUNT ? &files[index] : NULL;
}

static int file_open(uint16_t disk, const char *path, uint16_t flags, uint16_t mode) {
    char full[NATIVE_PATH_MAX];
    bool usb;
    int result = route(path, full, &usb);
    if (result) return result;
    if (!usb) return original.open(disk, full, flags, mode);
    if ((flags & ~(N_WRITE | N_RDWR | N_APPEND | N_CREATE | N_TRUNCATE |
                   N_EXCLUSIVE | N_BINARY | N_TEXT)) ||
        (flags & (N_WRITE | N_RDWR)) == (N_WRITE | N_RDWR) ||
        ((flags & N_EXCLUSIVE) && !(flags & N_CREATE)) ||
        ((flags & (N_APPEND | N_TRUNCATE)) && !(flags & (N_WRITE | N_RDWR))))
        return N_INVALID;
    if (!full[USB_PREFIX_LEN]) return N_ACCESS;
    unsigned access = flags & N_WRITE ? FS_OPEN_WRITE :
                      flags & N_RDWR ? FS_OPEN_READ | FS_OPEN_WRITE : FS_OPEN_READ;
    if (flags & N_CREATE) access |= FS_OPEN_CREATE;
    if (flags & N_EXCLUSIVE) access |= FS_OPEN_EXCLUSIVE;
    if (flags & N_TRUNCATE) access |= FS_OPEN_TRUNCATE;
    if (flags & N_APPEND) access |= FS_OPEN_APPEND;
    if (!enter()) return N_IO;
    NativeFile *file = NULL;
    unsigned index;
    for (index = 0; index < FILE_COUNT; ++index) {
        if (!files[index].file) { file = &files[index]; break; }
    }
    result = file ? volume() : N_MEMORY;
    NativeResolved *resolved = NULL;
    if (!result) {
        resolved = malloc(sizeof(*resolved));
        if (!resolved) result = N_MEMORY;
    }
    if (!result) result = status(native_namespace_resolve(full, !!(flags & N_CREATE), resolved));
    if (!result && (resolved->synthetic || resolved->entry.is_directory)) result = N_ACCESS;
    /* Check the existing object before CREATE/TRUNCATE can change it. */
    if (!result && resolved->entry.size > INT32_MAX) result = N_INVALID;
    if (!result) result = mutation_status(fs_open(resolved->path, access, &file->file));
    free(resolved);
    if (!result) {
        file->generation = media_generation;
        file->session_id = session_id;
        file->flags = access;
        file->size = (uint32_t)fs_size(file->file);
        file->position = flags & N_APPEND ? file->size : 0;
        if (disk_generation(NULL) != media_generation) {
            fs_close(file->file);
            memset(file, 0, sizeof(*file));
            result = N_IO;
        } else {
            ++references;
            result = FILE_BASE + index;
        }
    }
    leave();
    return result;
}

static int file_ready(NativeFile *file) {
    if (!file->file) return N_INVALID;
    int result = volume();
    if (!result && (file->generation != media_generation ||
                    file->session_id != session_id)) result = N_IO;
    if (!result) {
        uint64_t size = fs_size(file->file);
        if (size > INT32_MAX) result = N_INVALID;
        else file->size = (uint32_t)size;
        if (disk_generation(NULL) != file->generation) result = N_IO;
    }
    return result;
}

static int file_read(int fd, void *buf, int count) {
    NativeFile *file = file_slot(fd);
    if (!file) return original.read(fd, buf, count);
    if (count < 0 || (!buf && count)) return N_INVALID;
    if (!enter()) return N_IO;
    int result = file_ready(file);
    uint32_t actual = 0;
    if (!result && !(file->flags & FS_OPEN_READ)) result = N_ACCESS;
    if (!result && file->position < file->size) {
        result = status(fs_seek(file->file, file->position));
        if (!result) result = status(fs_read(file->file, buf, (uint32_t)count, &actual));
        file->position += actual;
    }
    leave();
    return result ? result : (int)actual;
}

static int file_seek(int fd, int offset, int origin) {
    NativeFile *file = file_slot(fd);
    if (!file) return original.seek(fd, offset, origin);
    if (!enter()) return N_IO;
    int result = file_ready(file);
    int64_t position = offset;
    if (origin == 1) position += file->position;
    else if (origin == 2) position += file->size;
    else if (origin != 0) result = N_INVALID;
    if (position < 0 || position > INT32_MAX) result = N_INVALID;
    if (!result) result = status(fs_seek(file->file, (uint32_t)position));
    if (!result) file->position = (uint32_t)position;
    leave();
    return result ? result : (int)position;
}

static int file_close(int fd) {
    NativeFile *file = file_slot(fd);
    if (!file) return original.close(fd);
    if (!enter()) return N_IO;
    int result = file->file ? 0 : N_INVALID;
    if (file->file) {
        bool current = file->session_id == session_id &&
                       file->generation == namespace_generation &&
                       resident->session_id == session_id;
        FsResult closed = fs_close(file->file);
        result = current ? mutation_status(closed) : status(closed);
        memset(file, 0, sizeof(*file));
        --references;
    }
    leave();
    return result;
}

static int file_write(int fd, const void *buf, int count) {
    NativeFile *file = file_slot(fd);
    if (!file) return original.write(fd, buf, count);
    if (count < 0 || (!buf && count)) return N_INVALID;
    if (!enter()) return N_IO;
    int result = file_ready(file);
    uint32_t actual = 0;
    if (!result && !(file->flags & FS_OPEN_WRITE)) result = N_ACCESS;
    if (!result && (file->flags & FS_OPEN_APPEND)) file->position = file->size;
    if (!result && (file->position > INT32_MAX ||
                    (uint32_t)count > INT32_MAX - file->position)) result = N_INVALID;
    if (!result && count) {
        if (file->position > file->size)
            result = mutation_status(fs_truncate(file->file, file->position));
        if (!result) result = status(fs_seek(file->file, file->position));
        if (!result) result = mutation_status(fs_write(file->file, buf, (uint32_t)count, &actual));
        file->position += actual;
        file->size = (uint32_t)fs_size(file->file);
    }
    leave();
    return result ? result : (int)actual;
}

static int file_truncate(int fd, int size) {
    NativeFile *file = file_slot(fd);
    if (!file) return original.truncate(fd, size);
    if (size < 0) return N_INVALID;
    if (!enter()) return N_IO;
    int result = file_ready(file);
    if (!result && !(file->flags & FS_OPEN_WRITE)) result = N_ACCESS;
    if (!result) result = mutation_status(fs_truncate(file->file, (uint32_t)size));
    if (!result) file->size = (uint32_t)fs_size(file->file);
    leave();
    return result;
}

static int file_flush(int fd) {
    NativeFile *file = file_slot(fd);
    if (!file) return original.flush(fd);
    if (!enter()) return N_IO;
    int result = file_ready(file);
    if (!result) result = mutation_status(fs_sync(file->file));
    leave();
    return result;
}

static unsigned fold(unsigned c) {
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

static bool matches(const char *pattern, const char *name) {
    const char *star = NULL, *retry = NULL;
    if (!strcmp(pattern, "*.*")) pattern = "*";
    while (*name) {
        if (*pattern == '*') { star = ++pattern; retry = name; continue; }
        if (*pattern == '?' || fold((unsigned char)*pattern) == fold((unsigned char)*name)) {
            bool any = *pattern++ == '?';
            ++name;
            if (any) while (((unsigned char)*name & 0xc0) == 0x80) ++name;
            continue;
        }
        if (!star) return false;
        pattern = star;
        name = ++retry;
    }
    while (*pattern == '*') ++pattern;
    return !*pattern;
}

static NativeDir **find_dir(void *context) {
    NativeDir **p = &directories;
    while (*p && *p != context) p = &(*p)->next;
    return p;
}

static int next_entry(NativeDir *dir, NativeStat *d) {
    if (disk_generation(NULL) != dir->generation || dir->session_id != session_id ||
        dir->epoch != native_namespace_epoch()) return N_IO;
    if (dir->exact) return N_NO_FILE;
    FsEntry entry;
    bool end;
    for (;;) {
        char name[256];
        memset(&dir->current, 0, sizeof(dir->current));
        if (dir->phase < 2) {
            memset(&entry, 0, sizeof(entry));
            strcpy(name, dir->phase++ ? ".." : ".");
            entry.is_directory = true;
            entry.attributes = 0x10;
            dir->current.synthetic = true;
        } else if (dir->phase == 2 && !strcmp(dir->real, "0:/") && !dir->portal) {
            ++dir->phase;
            memset(&entry, 0, sizeof(entry));
            strcpy(name, "~nUSB-paths");
            entry.is_directory = true;
            entry.attributes = 0x10;
            dir->current.synthetic = true;
        } else if (dir->portal) {
            int result = status(native_portal_next(dir->portal, &dir->current, name, &end));
            if (result) return result;
            if (end) return N_NO_FILE;
            entry = dir->current.entry;
        } else {
            int result = status(fs_next(dir->dir, &entry, &end));
            if (result) return result;
            if (end) return N_NO_FILE;
            if ((!entry.is_directory && entry.size > INT32_MAX) ||
                (dir->portal_directory && entry.is_directory)) continue;
            result = status(native_namespace_project(&entry, strlen(dir->path), false, name));
            if (result) return result;
            size_t length = strlen(dir->real), leaf = strlen(entry.name);
            bool slash = length && dir->real[length - 1] != '/';
            if (length + slash + leaf >= sizeof(dir->current.path)) return N_LONG_PATH;
            memcpy(dir->current.path, dir->real, length);
            if (slash) dir->current.path[length++] = '/';
            memcpy(dir->current.path + length, entry.name, leaf + 1);
            dir->current.entry = entry;
        }
        if (!matches(dir->pattern, name)) continue;
        if (!dir->current.synthetic) {
            int result = status(native_namespace_project(&dir->current.entry, 0, true,
                                                         dir->identity_name));
            if (result) return result;
        }
        strcpy(entry.name, name);
        fill(d, &entry);
        return 0;
    }
}

static int directory_first(NativeStat *d, const char *path) {
    char full[NATIVE_PATH_MAX];
    bool usb;
    int result = route(path, full, &usb);
    if (result) return result;
    if (!usb) return original.first(d, full);
    if (!full[USB_PREFIX_LEN]) {
        FsEntry entry = {0};
        strcpy(entry.name, "USB");
        entry.is_directory = true;
        entry.attributes = 0x10;
        fill(d, &entry);
        d->context = EXACT_MATCH;
        return 0;
    }
    if (!enter()) return N_IO;
    result = volume();
    char *name = strrchr(full, '\\') + 1;
    bool wildcard = strchr(name, '*') || strchr(name, '?');
    NativeDir *dir = NULL;
    if (!result) {
        dir = calloc(1, sizeof(*dir));
        if (!dir) result = N_MEMORY;
    }
    if (!result) {
        dir->generation = media_generation;
        dir->session_id = session_id;
        dir->epoch = native_namespace_epoch();
        dir->exact = !wildcard;
        if (wildcard) {
            strcpy(dir->pattern, name);
            name[-1] = '\0';
        }
        strcpy(dir->path, full);
        result = status(native_namespace_resolve(full, false, &dir->current));
        if (!result && wildcard) {
            strcpy(dir->real, dir->current.path);
            dir->portal_directory = dir->current.portal && !dir->current.synthetic;
            if (dir->current.synthetic)
                result = status(native_portal_open(&dir->portal));
            else if (!dir->current.entry.is_directory) result = N_NO_FILE;
            else result = status(fs_opendir(dir->real, &dir->dir));
            if (!result) result = next_entry(dir, d);
        } else if (!result) {
            if (!dir->current.entry.is_directory && dir->current.entry.size > INT32_MAX)
                result = N_INVALID;
            else {
                FsEntry entry = dir->current.entry;
                if (!dir->current.synthetic) {
                    char projected[sizeof(d->name)];
                    result = status(native_namespace_project(&entry, 0, true,
                                                             dir->identity_name));
                    /* The OS uses stored spelling to check case when saving. */
                    if (!result)
                        result = status(native_namespace_project(&entry,
                            (size_t)(name - full - 1), native_namespace_reserved(name),
                            projected));
                    if (!result) strcpy(entry.name, projected);
                }
                if (!result) fill(d, &entry);
            }
        }
        if (!result) {
            dir->next = directories;
            directories = dir;
            d->context = (void *)((uintptr_t)dir | 1u);
            ++references;
        }
    }
    if (result && dir) {
        fs_closedir(dir->dir);
        native_portal_close(dir->portal);
        free(dir);
    }
    leave();
    return result;
}

static int directory_next(NativeStat *d) {
    if (d->context == EXACT_MATCH) return N_NO_FILE;
    if (!((uintptr_t)d->context & 1u)) return original.next(d);
    if (!enter()) return N_IO;
    NativeDir *dir = *find_dir((void *)((uintptr_t)d->context & ~1u));
    int result = dir ? next_entry(dir, d) : N_INVALID;
    leave();
    return result;
}

static int directory_done(NativeStat *d) {
    if (d->context == EXACT_MATCH) { d->context = NULL; return 0; }
    if (!((uintptr_t)d->context & 1u)) return original.done(d);
    if (!enter()) return N_IO;
    NativeDir **link = find_dir((void *)((uintptr_t)d->context & ~1u));
    NativeDir *dir = *link;
    if (dir) {
        *link = dir->next;
        fs_closedir(dir->dir);
        native_portal_close(dir->portal);
        free(dir);
        d->context = NULL;
        --references;
    }
    leave();
    return 0;
}

static int attributes(uint8_t *attributes, const char *path) {
    char full[NATIVE_PATH_MAX];
    bool usb;
    int result = route(path, full, &usb);
    if (result) return result;
    if (!usb) return original.get_attributes(attributes, full);
    NativeStat d = {0};
    result = directory_first(&d, full);
    if (!result) { *attributes = d.attributes; directory_done(&d); }
    return result;
}

static int path_unlink(const char *path, bool directory) {
    char full[NATIVE_PATH_MAX];
    bool usb;
    int result = route(path, full, &usb);
    if (result) return result;
    if (!usb) return directory ? original.rmdir(full) : original.remove(full);
    if (!full[USB_PREFIX_LEN]) return N_ACCESS;
    if (!enter()) return N_IO;
    result = volume();
    NativeResolved *resolved = malloc(sizeof(*resolved));
    if (!resolved) result = N_MEMORY;
    if (!result) result = status(native_namespace_resolve(full, false, resolved));
    if (!result && (resolved->synthetic || !strcmp(resolved->path, "0:/") ||
                    resolved->entry.is_directory != directory)) result = N_ACCESS;
    NativeRetirement retirement;
    if (!result) result = status(native_namespace_prepare(&resolved->entry, &retirement));
    if (!result) {
        FsResult changed = fs_unlink(resolved->path);
        native_namespace_finish(&retirement, changed);
        result = status(changed);
        if (!result && directory)
            for (NativeNode *node = nodes; node; node = node->next)
                if (node->identity.directory_cluster == resolved->entry.identity.directory_cluster)
                    node->epoch = 0;
    }
    free(resolved);
    leave();
    return result;
}

static int path_remove(const char *path) {
    return path_unlink(path, false);
}

static int path_rmdir(const char *path) {
    return path_unlink(path, true);
}

static int path_mkdir(const char *path) {
    char full[NATIVE_PATH_MAX];
    bool usb;
    int result = route(path, full, &usb);
    if (result) return result;
    if (!usb) return original.mkdir(full);
    if (!full[USB_PREFIX_LEN]) return N_EXISTS;
    if (!enter()) return N_IO;
    result = volume();
    NativeResolved *resolved = malloc(sizeof(*resolved));
    if (!resolved) result = N_MEMORY;
    if (!result) result = status(native_namespace_resolve(full, true, resolved));
    if (!result && resolved->synthetic) result = N_ACCESS;
    if (!result) result = mutation_status(fs_mkdir(resolved->path));
    free(resolved);
    leave();
    return result;
}

static int path_rename(const char *from, const char *to) {
    char a[NATIVE_PATH_MAX], b[NATIVE_PATH_MAX];
    bool usb_a, usb_b;
    int result = route(from, a, &usb_a);
    if (!result) result = route(to, b, &usb_b);
    if (result) return result;
    if (!usb_a && !usb_b) return original.rename(a, b);
    if (!usb_a || !usb_b || !a[USB_PREFIX_LEN] || !b[USB_PREFIX_LEN]) return N_ACCESS;
    if (!enter()) return N_IO;
    result = volume();
    NativeResolved *source = malloc(sizeof(*source)), *target = malloc(sizeof(*target));
    if (!source || !target) result = N_MEMORY;
    if (!result) result = status(native_namespace_resolve(a, false, source));
    if (!result) result = status(native_namespace_rename_target(source, b, target));
    if (!result && (source->synthetic || target->synthetic ||
                    !strcmp(source->path, "0:/") || !strcmp(target->path, "0:/")))
        result = N_ACCESS;
    if (!result && source->entry.is_directory) {
        size_t length = strlen(source->path);
        for (NativeDir *dir = directories; dir; dir = dir->next) {
            const char *held = dir->exact ? dir->current.path : dir->real;
            if (dir->epoch == native_namespace_epoch() &&
                !strncmp(held, source->path, length) &&
                (!held[length] || held[length] == '/')) { result = N_BUSY; break; }
        }
        for (NativeNode *node = nodes; node; node = node->next)
            if (!node->portal && node->epoch == native_namespace_epoch() &&
                !strncmp(node->real, source->path, length) &&
                (!node->real[length] || node->real[length] == '/')) {
                result = N_BUSY;
                break;
            }
    }
    NativeRetirement retirement;
    if (!result && !source->entry.is_directory)
        result = status(native_namespace_prepare(&source->entry, &retirement));
    if (!result) {
        FsResult changed = fs_rename(source->path, target->path);
        if (!source->entry.is_directory) native_namespace_finish(&retirement, changed);
        else native_namespace_finish(NULL, changed);
        result = status(changed);
    }
    free(target);
    free(source);
    leave();
    return result;
}

static int path_attributes(const char *path, uint8_t value) {
    char full[NATIVE_PATH_MAX];
    bool usb;
    int result = route(path, full, &usb);
    if (result) return result;
    if (!usb) return original.set_attributes(full, value);
    if (!full[USB_PREFIX_LEN]) return N_ACCESS;
    if (value & ~0x37u) return N_INVALID;
    if (!enter()) return N_IO;
    result = volume();
    NativeResolved *resolved = malloc(sizeof(*resolved));
    if (!resolved) result = N_MEMORY;
    if (!result) result = status(native_namespace_resolve(full, false, resolved));
    if (!result && resolved->synthetic) result = N_ACCESS;
    if (!result) result = mutation_status(fs_chmod(resolved->path, value));
    free(resolved);
    leave();
    return result;
}

/* REL 104ccd84 consumes DSTAT, then access, modified, created date/time pairs.
   Its 104ccce0 helper replaces each 0xffff field with the current FAT time. */
static int path_utime(NativeStat *d, uint16_t adate, uint16_t atime,
                      uint16_t mdate, uint16_t mtime, uint16_t cdate, uint16_t ctime) {
    if (!d) return N_INVALID;
    if (!((uintptr_t)d->context & 1u))
        return original.utime(d, adate, atime, mdate, mtime, cdate, ctime);
    if (d->context == EXACT_MATCH) return N_ACCESS;
    if (!enter()) return N_IO;
    NativeDir *dir = *find_dir((void *)((uintptr_t)d->context & ~1u));
    int result = dir ? volume() : N_INVALID;
    if (!result && (dir->generation != media_generation ||
                    dir->session_id != session_id ||
                    dir->epoch != native_namespace_epoch())) result = N_IO;
    if (!result && (dir->current.synthetic || !dir->current.path[0])) result = N_ACCESS;
    if (!result) {
        FsEntry entry;
        result = status(fs_stat(dir->current.path, &entry));
        if (!result && memcmp(&entry.identity, &dir->current.entry.identity,
                              sizeof(entry.identity))) result = N_IO;
        char identity_name[256];
        if (!result) result = status(native_namespace_project(&entry, 0, true, identity_name));
        if (!result && strcmp(identity_name, dir->identity_name)) result = N_IO;
    }
    FsTimes times = {adate, mdate, mtime, cdate, ctime};
    if (!result && (adate == UINT16_MAX || mdate == UINT16_MAX ||
                    mtime == UINT16_MAX || cdate == UINT16_MAX || ctime == UINT16_MAX)) {
        uint32_t packed = disk_fattime(NULL);
        if (!packed) result = N_IO;
        else {
            uint16_t date = packed >> 16, clock = packed;
            if (adate == UINT16_MAX) times.access_date = date;
            if (mdate == UINT16_MAX) times.modified_date = date;
            if (mtime == UINT16_MAX) times.modified_time = clock;
            if (cdate == UINT16_MAX) times.created_date = date;
            if (ctime == UINT16_MAX) times.created_time = clock;
        }
    }
    /* FAT stores access metadata at date precision. */
    if (!result) result = mutation_status(fs_utime(dir->current.path, &times));
    if (!result) {
        memcpy((char *)d + 272, &times.created_time, 2);
        memcpy((char *)d + 274, &times.created_date, 2);
        memcpy((char *)d + 276, &times.access_date, 2);
        memcpy((char *)d + 280, &times.modified_time, 2);
        memcpy((char *)d + 282, &times.modified_date, 2);
    }
    leave();
    return result;
}

static NativeNode **find_node(void *context) {
    NativeNode **p = &nodes;
    while (*p && *p != context) p = &(*p)->next;
    return p;
}

static int node_new(uint16_t disk, const char *path, void **out) {
    char full[NATIVE_PATH_MAX];
    bool usb;
    int result = route(path, full, &usb);
    if (result) return result;
    if (!usb) return original.node_new(disk, full, out);
    if (!enter()) return N_IO;
    NativeNode *node = malloc(sizeof(*node));
    result = node ? volume() : N_MEMORY;
    if (node) {
        NativeResolved *resolved = malloc(sizeof(*resolved));
        if (!resolved) result = N_MEMORY;
        if (!result) result = status(native_namespace_resolve(full, false, resolved));
        if (!result && !resolved->entry.is_directory) result = N_NO_FILE;
        if (!result) {
            strcpy(node->real, resolved->path);
            node->portal = resolved->portal;
            node->identity = resolved->entry.identity;
        }
        free(resolved);
        strcpy(node->path, full + 2);
        strcat(node->path, "\\");
        node->epoch = native_namespace_epoch();
        if (result) free(node);
        else {
            node->next = nodes;
            nodes = node;
            *out = (void *)((uintptr_t)node | 1u);
        }
    }
    leave();
    return result;
}

static int node_free(uint16_t disk, void *context) {
    if (!((uintptr_t)context & 1u)) return original.node_free(disk, context);
    if (!enter()) return N_IO;
    NativeNode **link = find_node((void *)((uintptr_t)context & ~1u));
    NativeNode *node = *link;
    if (node) { *link = node->next; free(node); }
    leave();
    return 0;
}

static int node_string(uint16_t disk, void *context, char *path) {
    if (!((uintptr_t)context & 1u)) return original.node_string(disk, context, path);
    unsigned mask = irq_control(-1);
    NativeNode *node = *find_node((void *)((uintptr_t)context & ~1u));
    bool valid = node && node->epoch && node->epoch == native_namespace_epoch();
    if (valid) strcpy(path, node->path);
    irq_control(mask);
    return valid ? 0 : N_INVALID;
}

static FsResult internal_status(int result) {
    switch (result) {
        case 0: return FS_OK;
        case N_NO_FILE: return FS_NOT_FOUND;
        case N_EXISTS: return FS_EXISTS;
        case N_LONG_PATH: return FS_PATH_TOO_LONG;
        case N_MEMORY: return FS_NO_MEMORY;
        case N_BUSY: return FS_BUSY;
        case N_ACCESS: return FS_DENIED;
        case N_INVALID: return FS_INVALID_ARGUMENT;
        default: return FS_IO_ERROR;
    }
}

static FsResult internal_path(const char *path, char full[NATIVE_PATH_MAX], bool root) {
    bool usb;
    if (!path || path[0] != '/') return FS_INVALID_ARGUMENT;
    if (native_path(path, "", full, &usb)) return FS_PATH_TOO_LONG;
    if (usb || strncasecmp(full, "A:\\documents", 12) ||
        (full[12] && full[12] != '\\') || (!root && !full[12])) return FS_DENIED;
    return FS_OK;
}

static int internal_first(NativeStat *d, const char *path) {
    memset(d, 0, sizeof(*d));
    d->mount = mount;
    d->disk = mount->disk;
    d->stamp = mount->stamp;
    return original.first(d, path);
}

static InternalFile *internal_file(int handle) {
    for (unsigned i = 0; i < FILE_COUNT; ++i)
        if (internal_files[i].used && internal_files[i].handle == handle)
            return &internal_files[i];
    return NULL;
}

FsResult native_fs_internal_open(const char *path, unsigned flags, int *handle,
                                uint64_t *size) {
    char full[NATIVE_PATH_MAX];
    if (!handle || !size) return FS_INVALID_ARGUMENT;
    *handle = -1;
    *size = 0;
    if ((flags & ~(FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE |
                   FS_OPEN_TRUNCATE | FS_OPEN_APPEND)) ||
        !(flags & (FS_OPEN_READ | FS_OPEN_WRITE)) ||
        ((flags & FS_OPEN_EXCLUSIVE) && !(flags & FS_OPEN_CREATE)) ||
        ((flags & (FS_OPEN_CREATE | FS_OPEN_TRUNCATE | FS_OPEN_APPEND)) &&
         !(flags & FS_OPEN_WRITE))) return FS_INVALID_ARGUMENT;
    FsResult result = internal_path(path, full, false);
    if (result != FS_OK) return result;
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    InternalFile *file = NULL;
    for (unsigned i = 0; i < FILE_COUNT; ++i) {
        if (!internal_files[i].used) { file = &internal_files[i]; break; }
    }
    if (!file) result = FS_NO_MEMORY;
    NativeStat d;
    if (result == FS_OK) {
        int found = internal_first(&d, full);
        if (!found) {
            if (d.attributes & 0x10) result = FS_DENIED;
            else if ((uint64_t)d.size > INT32_MAX) result = FS_TOO_LARGE;
            int closed = original.done(&d);
            if (result == FS_OK) result = internal_status(closed);
        } else if (found != N_NO_FILE || !(flags & FS_OPEN_CREATE))
            result = internal_status(found);
    }
    uint16_t access = N_BINARY;
    if (flags & FS_OPEN_WRITE) access |= flags & FS_OPEN_READ ? N_RDWR : N_WRITE;
    if (flags & FS_OPEN_CREATE) access |= N_CREATE;
    if (flags & FS_OPEN_EXCLUSIVE) access |= N_EXCLUSIVE;
    if (flags & FS_OPEN_TRUNCATE) access |= N_TRUNCATE;
    if (flags & FS_OPEN_APPEND) access |= N_APPEND;
    int fd = -1;
    if (result == FS_OK) {
        fd = original.open(mount->disk, full, access, 0600);
        if (fd < 0) result = internal_status(fd);
        else if (file_slot(fd)) result = FS_IO_ERROR;
    }
    int length = 0, position = 0;
    if (result == FS_OK) {
        length = original.seek(fd, 0, 2);
        if (length < 0) result = internal_status(length);
        else if (flags & FS_OPEN_APPEND) position = length;
        else {
            position = original.seek(fd, 0, 0);
            if (position < 0) result = internal_status(position);
            else if (position != 0) result = FS_IO_ERROR;
        }
    }
    if (result == FS_OK) {
        file->handle = fd;
        file->flags = flags;
        file->position = (uint32_t)position;
        file->used = true;
        *handle = fd;
        *size = (uint32_t)length;
    } else if (fd >= 0) {
        int closed = original.close(fd);
        if (closed) result = FS_IO_ERROR;
        if ((flags & (FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE)) ==
            (FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE)) {
            if (original.remove(full)) result = FS_IO_ERROR;
        }
    }
    file_user_give(6);
    return result;
}

FsResult native_fs_internal_read(int handle, void *buf, uint32_t count, uint32_t *actual) {
    if (!actual) return FS_INVALID_ARGUMENT;
    *actual = 0;
    if ((!buf && count) || count > INT32_MAX) return FS_INVALID_ARGUMENT;
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    InternalFile *file = internal_file(handle);
    FsResult result = !file ? FS_INVALID_ARGUMENT :
                      !(file->flags & FS_OPEN_READ) ? FS_DENIED : FS_OK;
    if (result == FS_OK && count) {
        int got = original.read(handle, buf, (int)count);
        if (got < 0) result = internal_status(got);
        else if ((uint32_t)got > count || (uint32_t)got > INT32_MAX - file->position)
            result = FS_IO_ERROR;
        else {
            *actual = (uint32_t)got;
            file->position += *actual;
        }
    }
    file_user_give(6);
    return result;
}

FsResult native_fs_internal_write(int handle, const void *buf, uint32_t count,
                                 uint32_t *actual) {
    if (!actual) return FS_INVALID_ARGUMENT;
    *actual = 0;
    if ((!buf && count) || count > INT32_MAX) return FS_INVALID_ARGUMENT;
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    InternalFile *file = internal_file(handle);
    FsResult result = !file ? FS_INVALID_ARGUMENT :
                      !(file->flags & FS_OPEN_WRITE) ? FS_DENIED : FS_OK;
    if (result == FS_OK && (file->flags & FS_OPEN_APPEND)) {
        int position = original.seek(handle, 0, 2);
        if (position < 0) result = internal_status(position);
        else file->position = (uint32_t)position;
    }
    if (result == FS_OK && count > INT32_MAX - file->position) result = FS_TOO_LARGE;
    if (result == FS_OK && count) {
        int wrote = original.write(handle, buf, (int)count);
        if (wrote < 0) result = internal_status(wrote);
        else if ((uint32_t)wrote > count) result = FS_IO_ERROR;
        else {
            *actual = (uint32_t)wrote;
            file->position += *actual;
            if (*actual != count) result = FS_NO_SPACE;
        }
    }
    file_user_give(6);
    return result;
}

FsResult native_fs_internal_seek(int handle, uint32_t offset, int whence,
                                uint32_t *position) {
    if (!position) return FS_INVALID_ARGUMENT;
    *position = 0;
    if (offset > INT32_MAX || (whence != 0 && (whence != 2 || offset)))
        return FS_INVALID_ARGUMENT;
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    InternalFile *file = internal_file(handle);
    FsResult result = file ? FS_OK : FS_INVALID_ARGUMENT;
    if (result == FS_OK) {
        int moved = original.seek(handle, (int)offset, whence);
        if (moved < 0) result = internal_status(moved);
        else {
            file->position = (uint32_t)moved;
            *position = file->position;
        }
    }
    file_user_give(6);
    return result;
}

FsResult native_fs_internal_sync(int handle) {
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    FsResult result = internal_file(handle) ? internal_status(original.flush(handle)) :
                                            FS_INVALID_ARGUMENT;
    file_user_give(6);
    return result;
}

FsResult native_fs_internal_close(int handle) {
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    InternalFile *file = internal_file(handle);
    FsResult result = FS_INVALID_ARGUMENT;
    if (file) {
        result = internal_status(original.close(handle));
        memset(file, 0, sizeof(*file));
    }
    file_user_give(6);
    return result;
}

FsResult native_fs_internal_stat(const char *path, FsEntry *entry) {
    char full[NATIVE_PATH_MAX];
    if (!entry) return FS_INVALID_ARGUMENT;
    FsResult result = internal_path(path, full, true);
    if (result != FS_OK) return result;
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    NativeStat d;
    int found = internal_first(&d, full);
    if (!found) {
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->name, d.name, sizeof(d.name));
        entry->name[sizeof(d.name)] = '\0';
        entry->attributes = d.attributes;
        entry->is_directory = !!(d.attributes & 0x10);
        entry->size = d.size;
        memcpy(&entry->created_time, (char *)&d + 272, 2);
        memcpy(&entry->created_date, (char *)&d + 274, 2);
        memcpy(&entry->access_date, (char *)&d + 276, 2);
        memcpy(&entry->time, (char *)&d + 280, 2);
        memcpy(&entry->date, (char *)&d + 282, 2);
        found = original.done(&d);
    }
    file_user_give(6);
    return internal_status(found);
}

FsResult native_fs_internal_rename(const char *from, const char *to) {
    char a[NATIVE_PATH_MAX], b[NATIVE_PATH_MAX];
    FsResult result = internal_path(from, a, false);
    if (result == FS_OK) result = internal_path(to, b, false);
    if (result != FS_OK) return result;
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    NativeStat d;
    int found = internal_first(&d, b);
    if (!found) {
        int closed = original.done(&d);
        found = closed ? closed : N_EXISTS;
    } else if (found == N_NO_FILE) found = original.rename(a, b);
    file_user_give(6);
    return internal_status(found);
}

FsResult native_fs_internal_rmdir(const char *path) {
    char full[NATIVE_PATH_MAX];
    FsResult result = internal_path(path, full, false);
    if (result != FS_OK) return result;
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    int removed = original.rmdir(full);
    file_user_give(6);
    return internal_status(removed);
}

FsResult native_fs_internal_set_metadata(const char *path, uint8_t attributes,
                                       const FsTimes *times) {
    char full[NATIVE_PATH_MAX];
    FsResult result = internal_path(path, full, false);
    if (result != FS_OK) return result;
    if (!times || (attributes & ~0x37u)) return FS_INVALID_ARGUMENT;
    if (!mount || !file_user_take || file_user_take(6)) return FS_IO_ERROR;
    NativeStat d;
    int found = internal_first(&d, full);
    if (!found) {
        found = original.utime(&d, times->access_date, 0, times->modified_date,
                               times->modified_time, times->created_date,
                               times->created_time);
        int closed = original.done(&d);
        if (!found) found = closed;
        if (!found) found = original.set_attributes(full, attributes);
    }
    file_user_give(6);
    return internal_status(found);
}

uint32_t native_fs_fattime(void) {
    return current_clock && fat_clock ? disk_fattime(NULL) : 0;
}

bool native_fs_ready(void) {
    return mount && mount->ops == &proxy && case_entry &&
        case_entry[0] == 0xe51ff004u && case_entry[1] == (uint32_t)case_rename;
}

const char *native_fs_install(ResidentApi *api) {
    bool cas = *(volatile uint32_t *)0x10000020u == 0x1042aee0u;
    irq_control = (__typeof__(irq_control))(cas ? 0x1042bd70u : 0x1042ad50u);
    current_task = (__typeof__(current_task))(cas ? 0x10429b58u : 0x10428b38u);
    current_directory = (__typeof__(current_directory))(cas ? 0x10496c5cu : 0x10495c3cu);
    if (!bind_semaphore(cas)) return "Native filesystem ABI unsupported";
    DSTAT d;
    memset(&d, 0, sizeof(d));
    if (NU_Get_First(&d, "A:\\documents\\*")) return "Documents folder unavailable";
    NativeMount *found = d.unknown6;
    NU_Done(&d);
    if (!resident_ram_range((uint32_t)found, sizeof(*found)) ||
        !resident_ram_range((uint32_t)found->filesystem, 136) ||
        found->ops != (NativeOps *)((char *)found->filesystem + 20) ||
        strcmp(found->filesystem, "REL") || strcmp(found->drive, "A"))
        return "Native filesystem hook conflict";
    if (!bind_clock(found->ops)) return "Native filesystem clock ABI unsupported";
    if (!bind_case_rename()) return "Native rename ABI unsupported";
    struct nuc_stat st;
    if (nuc_stat("/documents/USB", &st)) {
        if (mkdir("/documents/USB", 0700)) return "Cannot create USB folder";
    } else if ((st.st_mode & 0xf000) != 0x4000) return "USB name is already in use";
    NUC_DIR *dir = nuc_opendir("/documents/USB");
    if (!dir) return "Cannot inspect USB folder";
    struct nuc_dirent *entry;
    bool empty = true;
    while ((entry = nuc_readdir(dir))) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            empty = false;
            break;
        }
    }
    nuc_closedir(dir);
    if (!empty) return "USB folder must be empty";
    static const char name[8] = "nUSBFS";
    if (sem_create(semaphore, name, 1, 6)) return "USB filesystem lock unavailable";
    original = *found->ops;
    proxy = original;
    proxy.open = file_open;
    proxy.read = file_read;
    proxy.write = file_write;
    proxy.seek = file_seek;
    proxy.close = file_close;
    proxy.remove = path_remove;
    proxy.utime = path_utime;
    proxy.set_attributes = path_attributes;
    proxy.get_attributes = attributes;
    proxy.truncate = file_truncate;
    proxy.flush = file_flush;
    proxy.mkdir = path_mkdir;
    proxy.rmdir = path_rmdir;
    proxy.rename = path_rename;
    proxy.first = directory_first;
    proxy.next = directory_next;
    proxy.done = directory_done;
    proxy.node_new = node_new;
    proxy.node_free = node_free;
    proxy.node_string = node_string;
    resident = api;
    mount = found;
    unsigned mask = irq_control(-1);
    if (!resident_publish(case_entry, (uint32_t *)case_rename,
                          0xe92d40f0u, 0xe59f4064u)) {
        irq_control(mask);
        return "Native rename hook conflict";
    }
    __asm__ volatile("" ::: "memory");
    mount->ops = &proxy;
    irq_control(mask);
    return NULL;
}
