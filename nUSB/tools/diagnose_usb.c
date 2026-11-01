#include "diagnose.h"
#include "usb_msc_native.h"
#include "resident_check.h"
#include "bot.h"

#include <string.h>

#define LEGACY_BUILD "42a68c85c31b226001a71e5c06e5ff63af6d4d4e70f7c6e1aa1d0d858b13a5c6"
#define SLOT_COUNT 8u
#define EVENT_COUNT 256u
#define DMA_COUNT 128u
#define IN_PROGRESS 2u
#define TICKS(ms) (((ms) * 32768u + 999u) / 1000u)

typedef struct {
    usbd_xfer_handle xfer;
    usbd_pipe_handle pipe;
    usbd_callback callback;
    usbd_private_handle ctx;
    volatile bool pending;
    uint32_t buffer, source, descriptor[3], length, timeout, expected, tag, lba;
    uint16_t flags;
    uint8_t endpoint, opcode;
    uint8_t staging[BOT_TRANSFER_LIMIT + 32] __attribute__((aligned(32)));
} Slot;

typedef struct {
    usbd_pipe_handle pipe;
    usbd_interface_handle iface;
    uint8_t endpoint;
} Pipe;

typedef struct {
    uint32_t sequence, ticks, cpsr, length, expected, tag, lba, buffer, descriptor[2];
    uint32_t timeout, actual, xfer, pipe;
    const char *phase;
    int status;
    uint16_t flags;
    uint8_t endpoint, opcode, reply_length, reply[64];
} Event;

typedef struct {
    bool sampled;
    const char *phase;
    int status;
    uint32_t opcode, tag, endpoint;
    uint32_t sequence, regs[6], xfer[18], descriptor[3], bus_caps, count;
    struct { uint32_t address, qh[12], current[8], next[8]; } queues[4];
} Host;

typedef struct {
    uint32_t sequence, event, opcode, tag, endpoint, length, source, caller;
    uint32_t descriptor[3], toggle, compared, first_difference;
    bool valid;
    uint8_t expected_byte, actual_byte, cbw[31], dma_cbw[31];
} Dma;

static struct {
    NativeUsb hooks;
    const NativeUsb *original;
    const NativeUsb **location;
    ResidentApi *api;
    bool installed, blocked, dumped, map_dumped, local;
    DiagProfile profile;
    Slot slots[SLOT_COUNT];
    Pipe pipes[4];
    usbd_device_handle device;
    uint32_t count, dma_count, dma_missed, slot_missed, stage_missed;
    uint32_t opcode, expected, tag, lba;
    Event events[EVENT_COUNT];
    Dma dma[DMA_COUNT];
    Host first_other_error, first_write_error;
    uint32_t methods[6], *original_methods;
    usbd_pipe_handle watched_pipe;
    struct {
        Bot bot;
        usbd_xfer_handle in, out;
        usbd_pipe_handle in_pipe, out_pipe;
        usb_device_request_t request;
        uint8_t control[32];
        int actual;
        uint32_t generation, session;
    } info;
} diag;

typedef char native_usb_layout[sizeof(NativeUsb) == 64 ? 1 : -1];

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Byte buffers, unlike native objects, may intentionally be unaligned. */
static bool bytes(uint32_t address, uint32_t length) {
    return address >= 0x10000000u && address < 0x14000000u &&
           length <= 0x14000000u - address;
}

static int irq(int mask) {
    return diag.original->TCT_Local_Control_Interrupts(mask);
}

static unsigned wait_irq(void) {
    volatile uint32_t *counter = (void *)0x900d0024u;
    uint16_t start = *counter;
    __asm__ volatile ("mcr p15, 0, %0, c7, c0, 4" : : "r"(0) : "memory");
    return (uint16_t)(start - *counter);
}

static void delay_ms(unsigned ms) {
    for (unsigned elapsed = 0; elapsed < TICKS(ms); elapsed += wait_irq()) {}
}

static Slot *slot_for(usbd_xfer_handle xfer) {
    for (unsigned i = 0; i < SLOT_COUNT; ++i)
        if (diag.slots[i].xfer == xfer) return &diag.slots[i];
    return NULL;
}

static Pipe *pipe_for(usbd_pipe_handle pipe) {
    for (unsigned i = 0; i < 4; ++i)
        if (diag.pipes[i].pipe == pipe) return &diag.pipes[i];
    return NULL;
}

static Slot *reserve_slot(usbd_xfer_handle xfer) {
    Slot *slot = slot_for(xfer);
    if (slot) return slot;
    for (unsigned i = 0; i < SLOT_COUNT; ++i) if (!diag.slots[i].xfer) {
        diag.slots[i].xfer = xfer;
        return &diag.slots[i];
    }
    ++diag.slot_missed;
    diag.blocked = true;
    return NULL;
}

static bool pending(void) {
    for (unsigned i = 0; i < SLOT_COUNT; ++i)
        if (diag.slots[i].pending) return true;
    return false;
}

static void retain_pending(void) {
    if (!pending()) return;
    diag.blocked = true;
    nl_set_resident();
}

/* Read-only CPU view; never acknowledge status or alter queue state. */
static void sample(Host *host, usbd_xfer_handle xfer, uint32_t sequence) {
    memset(host, 0, sizeof(*host));
    host->sampled = true;
    host->sequence = sequence;
    static const unsigned offsets[] = {0x10, 0x14, 0x18, 0x1c, 0x28, 0x30};
    for (unsigned i = 0; i < 6; ++i)
        host->regs[i] = *(volatile uint32_t *)(0xb0000000u + offsets[i]);
    if (resident_ram_range((uint32_t)xfer, sizeof(host->xfer))) {
        memcpy(host->xfer, xfer, sizeof(host->xfer));
        if (resident_ram_range(host->xfer[2], sizeof(host->descriptor)))
            memcpy(host->descriptor, (void *)host->xfer[2], sizeof(host->descriptor));
        uint32_t bus = host->xfer[15];
        if (resident_ram_range(bus, 4)) {
            bus = *(uint32_t *)bus;
            if (resident_ram_range(bus, 0x30)) host->bus_caps = *(uint8_t *)(bus + 0x2d);
        }
    }
    uint32_t address = host->regs[4] & ~31u, head = address;
    while (host->count < 4 && resident_ram_range(address, 48)) {
        unsigned i = host->count++;
        host->queues[i].address = address;
        memcpy(host->queues[i].qh, (void *)address, 48);
        uint32_t current = host->queues[i].qh[3] & ~31u;
        uint32_t next = host->queues[i].qh[4] & ~31u;
        if (resident_ram_range(current, 32))
            memcpy(host->queues[i].current, (void *)current, 32);
        if (!(host->queues[i].qh[4] & 1) && resident_ram_range(next, 32))
            memcpy(host->queues[i].next, (void *)next, 32);
        uint32_t link = host->queues[i].qh[0];
        address = link & ~31u;
        if ((link & 7) != 2 || address == head) break;
    }
}

static void event(const char *phase, const Slot *slot, int status, uint32_t actual) {
    uint32_t cpsr;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
    int mask = irq(-1);
    if (diag.count == UINT32_MAX) { irq(mask); return; }
    uint32_t id = ++diag.count;
    Event *e = &diag.events[(id - 1) % EVENT_COUNT];
    memset(e, 0, sizeof(*e));
    e->sequence = id; e->ticks = *(volatile uint32_t *)0x900d0024u;
    e->cpsr = cpsr; e->phase = phase; e->status = status; e->actual = actual;
    e->opcode = diag.opcode; e->tag = diag.tag; e->lba = diag.lba;
    e->expected = diag.expected;
    if (slot) {
        e->length = slot->length; e->expected = slot->expected;
        e->tag = slot->tag; e->lba = slot->lba; e->buffer = slot->buffer;
        e->descriptor[0] = slot->descriptor[1]; e->descriptor[1] = slot->descriptor[2];
        e->timeout = slot->timeout; e->xfer = (uint32_t)slot->xfer;
        e->pipe = (uint32_t)slot->pipe; e->flags = slot->flags;
        e->endpoint = slot->endpoint; e->opcode = slot->opcode;
        if (!strcmp(phase, "status") && !status && (slot->endpoint & 0x80) &&
            actual && actual <= sizeof(e->reply) && bytes(slot->buffer, actual)) {
            e->reply_length = actual;
            memcpy(e->reply, (void *)slot->buffer, actual);
        }
    }
    if (status && status != (int)IN_PROGRESS) {
        Host *host = slot && (slot->opcode == 0x2a || slot->opcode == 0x35) ?
                     &diag.first_write_error : &diag.first_other_error;
        if (!host->sampled) {
            sample(host, slot ? slot->xfer : NULL, id);
            host->phase = phase; host->status = status;
            host->opcode = e->opcode; host->tag = e->tag; host->endpoint = e->endpoint;
        }
    }
    irq(mask);
}

static usbd_status open_pipe(usbd_interface_handle iface, uint8_t endpoint,
                             uint8_t flags, usbd_pipe_handle *out) {
    usbd_status result = diag.original->usbd_open_pipe(iface, endpoint, flags, out);
    if (!result) {
        int mask = irq(-1);
        for (unsigned i = 0; i < 4; ++i) if (!diag.pipes[i].pipe) {
            diag.pipes[i].pipe = *out; diag.pipes[i].iface = iface;
            diag.pipes[i].endpoint = endpoint; break;
        }
        irq(mask);
    }
    event("open", NULL, result, 0);
    return result;
}

static usbd_status close_pipe(usbd_pipe_handle pipe) {
    /* A failed BOT reset can strand OS 6.4 pipe disposal; retain it until reboot. */
    if (diag.blocked) {
        nl_set_resident();
        event("close_quarantined", NULL, 8, 0);
        return (usbd_status)8;
    }
    usbd_status result = diag.original->usbd_close_pipe(pipe);
    int mask = irq(-1);
    Pipe *p = pipe_for(pipe);
    if (p) memset(p, 0, sizeof(*p));
    irq(mask);
    event("close", NULL, result, 0);
    return result;
}

static usbd_xfer_handle alloc_xfer(usbd_device_handle device) {
    usbd_xfer_handle xfer = diag.original->usbd_alloc_xfer(device);
    if (xfer) {
        int mask = irq(-1);
        diag.device = device;
        reserve_slot(xfer);
        irq(mask);
    }
    event("alloc", NULL, xfer ? 0 : -1, 0);
    return xfer;
}

static usbd_status free_xfer(usbd_xfer_handle xfer) {
    Slot *slot = slot_for(xfer);
    if (diag.blocked) {
        nl_set_resident();
        event("free_quarantined", slot, 8, 0);
        return (usbd_status)8;
    }
    if (slot && slot->pending) {
        retain_pending();
        event("free_pending_refused", slot, 8, 0);
        return (usbd_status)8;
    }
    /* OS 6.4's disposer leaves r0 unspecified after releasing the xfer. */
    diag.original->usbd_free_xfer(xfer);
    int mask = irq(-1);
    if (slot) memset(slot, 0, offsetof(Slot, staging));
    irq(mask);
    event("free", NULL, USBD_NORMAL_COMPLETION, 0);
    return USBD_NORMAL_COMPLETION;
}

static void completed(usbd_xfer_handle xfer, usbd_private_handle ctx, usbd_status status) {
    Slot *slot = slot_for(xfer);
    if (!slot) return;
    usbd_callback callback = slot->callback;
    slot->pending = false;
    event("callback", slot, status, UINT32_MAX);
    if (callback) callback(xfer, ctx, status);
}

static void setup_xfer(usbd_xfer_handle xfer, usbd_pipe_handle pipe, usbd_private_handle ctx,
                       void *buffer, uint32_t len, uint16_t flags, uint32_t timeout,
                       usbd_callback callback) {
    int mask = irq(-1);
    Slot *slot = reserve_slot(xfer);
    Pipe *p = pipe_for(pipe);
    void *native_buffer = buffer;
    if (slot && !slot->pending) {
        slot->pipe = pipe; slot->ctx = ctx; slot->callback = callback;
        slot->length = len; slot->flags = flags; slot->timeout = timeout;
        slot->endpoint = p ? p->endpoint : 0;
        slot->buffer = slot->source = 0;
        if (resident_ram_range((uint32_t)buffer, 12)) {
            memmove(slot->descriptor, buffer, 12);
            slot->buffer = slot->source = slot->descriptor[0];
            native_buffer = slot->descriptor;
        } else memset(slot->descriptor, 0, sizeof(slot->descriptor));
        const uint8_t *data = (void *)slot->buffer;
        if (len == 31 && !(slot->endpoint & 0x80) && bytes(slot->buffer, len) &&
            le32(data) == 0x43425355u) {
            diag.opcode = data[15]; diag.expected = le32(data + 8);
            diag.tag = le32(data + 4);
            diag.lba = (uint32_t)data[17] << 24 | (uint32_t)data[18] << 16 |
                       (uint32_t)data[19] << 8 | data[20];
        }
        slot->opcode = diag.opcode; slot->tag = diag.tag;
        slot->lba = diag.lba; slot->expected = diag.expected;
        if ((diag.profile == DIAG_ALIGNED || diag.profile == DIAG_UNALIGNED) &&
            !(slot->endpoint & 0x80) && len) {
            if (len <= BOT_TRANSFER_LIMIT && bytes(slot->source, len) &&
                !slot->descriptor[1] && !slot->descriptor[2]) {
                uint8_t *stage = slot->staging + (diag.profile == DIAG_UNALIGNED ? 1 : 0);
                memmove(stage, (void *)slot->source, len);
                slot->buffer = slot->descriptor[0] = (uint32_t)stage;
                native_buffer = slot->descriptor;
            } else ++diag.stage_missed;
        }
        if (diag.profile == DIAG_WAIT_15S) slot->timeout = timeout = 15000;
    } else if (slot) {
        ++diag.slot_missed;
        diag.blocked = true;
        slot = NULL;
    }
    irq(mask);
    event("setup", slot, 0, 0);
    diag.original->usbd_setup_xfer(xfer, pipe, ctx, native_buffer, len, flags, timeout,
                                  slot ? completed : callback);
}

static void get_status(usbd_xfer_handle xfer, usbd_private_handle *ctx, void **buffer,
                        uint32_t *actual, usbd_status *status) {
    uint32_t size = 0;
    usbd_status result = USBD_NORMAL_COMPLETION;
    diag.original->usbd_get_xfer_status(xfer, ctx, buffer, &size, &result);
    if (actual) *actual = size;
    if (status) *status = result;
    event("status", slot_for(xfer), result, size);
}

static usbd_status clear_halt(usbd_pipe_handle pipe) {
    Slot *slot = NULL;
    for (unsigned i = 0; i < SLOT_COUNT; ++i)
        if (diag.slots[i].xfer && diag.slots[i].pipe == pipe) { slot = &diag.slots[i]; break; }
    event("clear_begin", slot, 0, 0);
    usbd_status result = diag.original->usbd_clear_endpoint_stall(pipe);
    event("clear_end", slot, result, 0);
    return result;
}

static usbd_status abort_pipe(usbd_pipe_handle pipe) {
    for (unsigned i = 0; i < SLOT_COUNT; ++i) {
        Slot *slot = &diag.slots[i];
        if (slot->xfer && slot->pipe == pipe && slot->pending) {
            uint32_t actual = 0;
            usbd_status status = USBD_NORMAL_COMPLETION;
            diag.original->usbd_get_xfer_status(slot->xfer, NULL, NULL, &actual, &status);
            event("abort_pending", slot, status, actual);
        }
    }
    usbd_status result = diag.original->usbd_abort_pipe(pipe);
    event("abort_return", NULL, result, 0);
    return result;
}

static MscResult usb_result(usbd_status status);

static usbd_status request(usbd_device_handle device, usb_device_request_t *req,
                            void *data, uint16_t flags, int *actual, uint32_t timeout) {
    event("control_begin", NULL, 0, req->bRequest);
    usbd_status result = diag.original->usbd_do_request_flags(device, req, data,
                                                            flags, actual, timeout);
    event("control_end", NULL, result, actual ? (uint32_t)*actual : UINT32_MAX);
    if (req->bmRequestType == 0x21 && req->bRequest == 0xff &&
        result != USBD_NORMAL_COMPLETION) {
        diag.api->last_error = usb_result(result);
        diag.api->state = RESIDENT_FAILED;
        diag.blocked = true;
        nl_set_resident();
        event("reset_failure_quarantined", NULL, result, 0);
    }
    return result;
}

/* Restore the method before inspecting bytes or entering the real HCD. */
static usbd_status dma_submit(usbd_xfer_handle xfer) {
    uint32_t *pipe = (void *)diag.watched_pipe;
    uint32_t *methods = diag.original_methods;
    pipe[12] = (uint32_t)methods;
    Slot *slot = slot_for(xfer);
    if (slot && diag.dma_count != UINT32_MAX) {
        uint32_t id = ++diag.dma_count;
        Dma *r = &diag.dma[(id - 1) % DMA_COUNT];
        memset(r, 0, sizeof(*r));
        r->sequence = id; r->event = diag.count; r->opcode = slot->opcode;
        r->tag = slot->tag; r->endpoint = slot->endpoint; r->length = slot->length;
        r->source = slot->source; r->caller = slot->buffer; r->toggle = pipe[15];
        r->first_difference = UINT32_MAX;
        if (resident_ram_range((uint32_t)xfer, 12)) {
            uint32_t *native = (void *)xfer;
            if (resident_ram_range(native[2], 12)) {
                memcpy(r->descriptor, (void *)native[2], 12);
                r->valid = bytes(r->caller, r->length) && bytes(r->descriptor[0], r->length);
                if (r->valid) {
                    const uint8_t *source = (void *)r->caller;
                    const uint8_t *dma = (void *)r->descriptor[0];
                    r->compared = r->length;
                    for (uint32_t i = 0; i < r->length; ++i) if (source[i] != dma[i]) {
                        r->first_difference = i; r->expected_byte = source[i];
                        r->actual_byte = dma[i]; break;
                    }
                    if (r->length == sizeof(r->cbw)) {
                        memcpy(r->cbw, source, sizeof(r->cbw));
                        memcpy(r->dma_cbw, dma, sizeof(r->dma_cbw));
                    }
                }
            }
        }
        event("dma_ready", slot, 0, r->compared);
    } else ++diag.dma_missed;
    usbd_status (*submit)(usbd_xfer_handle) = (void *)methods[1];
    return submit(xfer);
}

static usbd_status native_transfer(usbd_xfer_handle xfer, Slot *slot) {
    if (!slot || (slot->endpoint & 0x80)) return diag.original->usbd_transfer(xfer);
    uint32_t *pipe = (void *)slot->pipe;
    int mask = irq(-1);
    if (diag.watched_pipe || !resident_ram_range((uint32_t)pipe, 0x40) ||
        !resident_ram_range(pipe[12], sizeof(diag.methods))) {
        ++diag.dma_missed;
        irq(mask);
        return diag.original->usbd_transfer(xfer);
    }
    uint32_t *methods = (void *)pipe[12];
    /* OS 6.4 uses generic enqueue when transfer is NULL; start runs after copying. */
    if ((methods[1] & 3u) == 2u || methods[1] < 0x10000000u || methods[1] >= 0x11000000u) {
        ++diag.dma_missed;
        irq(mask);
        return diag.original->usbd_transfer(xfer);
    }
    diag.watched_pipe = slot->pipe; diag.original_methods = methods;
    memcpy(diag.methods, methods, sizeof(diag.methods));
    diag.methods[1] = (uint32_t)dma_submit;
    pipe[12] = (uint32_t)diag.methods;
    irq(mask);
    usbd_status result = diag.original->usbd_transfer(xfer);
    mask = irq(-1);
    if (pipe[12] == (uint32_t)diag.methods) {
        pipe[12] = (uint32_t)methods;
        ++diag.dma_missed;
    }
    diag.watched_pipe = NULL; diag.original_methods = NULL;
    irq(mask);
    return result;
}

static usbd_status transfer(usbd_xfer_handle xfer) {
    Slot *slot = slot_for(xfer);
    if (slot) {
        unsigned delay = diag.profile == DIAG_PACE_2 ? 2 :
                         diag.profile == DIAG_PACE_20 ? 20 : 0;
        if (slot->length == 13 && (slot->endpoint & 0x80)) {
            if (diag.profile == DIAG_CSW_100) delay = 100;
            if (diag.profile == DIAG_CSW_500) delay = 500;
        }
        if (delay) delay_ms(delay);
    }
    MscInfo info;
    diag.api->poll(&info);
    if (!info.connected || (slot && slot->xfer != xfer)) {
        event("submit_removed", slot, 6, 0);
        return (usbd_status)6;
    }
    if (!diag.api->session_active || diag.api->state != RESIDENT_READY) {
        event("submit_cancelled", slot, 19, 0);
        return (usbd_status)19;
    }
    if (slot) slot->pending = true;
    usbd_status result = native_transfer(xfer, slot);
    if (slot) {
        if ((unsigned)result != IN_PROGRESS) slot->pending = false;
        event("submit", slot, result, UINT32_MAX);
        if (diag.profile == DIAG_WAIT_15S && slot->pending) {
            unsigned elapsed = 0;
            while (slot->pending && elapsed < TICKS(15000) && !diagnose_cancelled() &&
                   diag.api->session_active && diag.api->state == RESIDENT_READY)
                elapsed += wait_irq();
            if (slot->pending) {
                event("longwait_abort", slot, 12, elapsed);
                abort_pipe(slot->pipe);
                for (elapsed = 0; slot->pending && elapsed < TICKS(100); elapsed += wait_irq()) {}
                retain_pending();
                /* Keep IN_PROGRESS when pending so the driver's bounded wait and
                   quarantine still own its Transfer object. */
                if (!slot->pending) result = (usbd_status)12;
            }
        }
    }
    return result;
}

const char *diagnose_profile_name(DiagProfile profile) {
    static const char *const names[] = {
        "baseline", "pace2ms", "pace20ms", "CSWwait100ms", "CSWwait500ms",
        "aligned OUT staging", "unaligned OUT staging", "long15s native wait",
        "baseline repeat"
    };
    return (unsigned)profile < DIAG_PROFILE_COUNT ? names[profile] : "unknown";
}

static bool gate(ResidentApi *api, bool cas) {
    uint32_t addr = cas ? 0x10477278u : 0x10476258u;
    const uint32_t *entry = (void *)addr;
    if (entry[0] != 0xe51ff004u || !resident_ram_range(entry[1], 20)) return false;
    const uint32_t *words = (void *)entry[1];
    if (words[0] != 0xe59ff004u || words[1] != NUSB_RESIDENT_MAGIC ||
        words[2] != NUSB_RESIDENT_ABI || words[4] != (uint32_t)api ||
        !resident_ram_range(words[3], 16)) return false;
    const uint32_t *trampoline = (void *)words[3];
    return resident_prologue(trampoline[0], trampoline[1], cas) &&
           trampoline[2] == 0xe51ff004u && trampoline[3] == addr + 8;
}

bool diagnose_usb_install(ResidentApi *api, bool local_backend) {
    static const uint32_t cas_table[16] = {
        0x1042bd70, 0x10477054, 0x10477094, 0x10a70cf0,
        0x10a700ec, 0x10a70288, 0x10a70168, 0x10a70030,
        0x10a6f7d8, 0x10a6f8f8, 0x10a6f950, 0x10a6f96c,
        0x10a709ec, 0x10a70890, 0x10a6f940, 0x10477278
    };
    static const uint32_t ncas_table[16] = {
        0x1042ad50, 0x10476034, 0x10476074, 0x10a08f4c,
        0x10a08348, 0x10a084e4, 0x10a083c4, 0x10a0828c,
        0x10a07a34, 0x10a07b54, 0x10a07bac, 0x10a07bc8,
        0x10a08c48, 0x10a08aec, 0x10a07b9c, 0x10476258
    };
    if (diag.installed || !is_cx2 || nl_ndless_rev() < 2022 ||
        !resident_ram_range((uint32_t)api, sizeof(*api))) return false;
    uint32_t marker = *(volatile uint32_t *)0x10000020u;
    bool cas = marker == 0x1042aee0u;
    if ((!cas && marker != 0x10429ec0u) || api->state != RESIDENT_READY ||
        api->session_active || resident_check_api(api, api->build_id) != RESIDENT_COMPATIBLE ||
        !gate(api, cas)) return false;
    const NativeUsb **location;
    if (local_backend) location = msc_backend_native();
    else {
        if (!cas || memcmp(api->build_id, LEGACY_BUILD, sizeof(api->build_id)) ||
            (uint32_t)api->write > UINT32_MAX - 0x32a70u) return false;
        location = (void *)((uint32_t)api->write + 0x32a70u);
    }
    if (!resident_ram_range((uint32_t)location, 4) ||
        !resident_ram_range((uint32_t)*location, sizeof(NativeUsb)) ||
        memcmp(*location, cas ? cas_table : ncas_table, sizeof(NativeUsb))) return false;
    memset(&diag, 0, sizeof(diag));
    diag.original = *location; diag.location = location; diag.api = api;
    diag.local = local_backend; diag.profile = DIAG_BASELINE;
    diag.hooks = *diag.original;
    diag.hooks.usbd_open_pipe = open_pipe; diag.hooks.usbd_close_pipe = close_pipe;
    diag.hooks.usbd_alloc_xfer = alloc_xfer; diag.hooks.usbd_free_xfer = free_xfer;
    diag.hooks.usbd_setup_xfer = setup_xfer; diag.hooks.usbd_transfer = transfer;
    diag.hooks.usbd_get_xfer_status = get_status;
    diag.hooks.usbd_abort_pipe = abort_pipe;
    diag.hooks.usbd_clear_endpoint_stall = clear_halt;
    diag.hooks.usbd_do_request_flags = request;
    int mask = irq(-1);
    if (*location != diag.original) { irq(mask); return false; }
    *location = &diag.hooks;
    diag.installed = true;
    irq(mask);
    return true;
}

bool diagnose_usb_profile(DiagProfile profile) {
    if (!diag.installed || (unsigned)profile >= DIAG_PROFILE_COUNT ||
        diag.api->session_active || pending() || diag.blocked ||
        diag.api->state != RESIDENT_READY) return false;
    diag.profile = profile;
    diag.count = diag.dma_count = diag.dma_missed = diag.slot_missed = diag.stage_missed = 0;
    diag.opcode = diag.expected = diag.tag = diag.lba = 0;
    memset(&diag.first_other_error, 0, sizeof(diag.first_other_error));
    memset(&diag.first_write_error, 0, sizeof(diag.first_write_error));
    diag.dumped = false;
    return true;
}

bool diagnose_usb_pending(void) {
    return pending();
}

static MscResult usb_result(usbd_status status) {
    switch ((unsigned)status) {
        case 0: return MSC_OK;
        case 6: case 9: return MSC_NO_DEVICE;
        case 8: return MSC_BUSY;
        case 11: return MSC_UNSUPPORTED;
        case 12: return MSC_TIMEOUT;
        case 17: return MSC_STALLED;
        case 19: return MSC_CANCELLED;
        case 20: return MSC_PROTOCOL_ERROR;
        default: return MSC_IO_ERROR;
    }
}

static MscResult info_connected(void) {
    MscInfo info;
    diag.api->poll(&info);
    if (!info.connected || info.generation != diag.info.generation) return MSC_NO_DEVICE;
    if (!diag.api->session_active || diag.api->session_id != diag.info.session)
        return MSC_CANCELLED;
    if (diag.api->state != RESIDENT_READY) return diag.api->last_error;
    return diagnose_cancelled() ? MSC_CANCELLED : MSC_OK;
}


static MscResult info_bulk(void *ctx, uint8_t endpoint, void *buf, uint32_t len,
                            uint32_t *actual) {
    (void)ctx;
    *actual = 0;
    MscResult result = info_connected();
    if (result != MSC_OK) return result;
    bool input = endpoint == diag.info.bot.ep_in;
    if (!input && endpoint != diag.info.bot.ep_out) return MSC_PROTOCOL_ERROR;
    usbd_xfer_handle xfer = input ? diag.info.in : diag.info.out;
    usbd_pipe_handle pipe = input ? diag.info.in_pipe : diag.info.out_pipe;
    Slot *slot = slot_for(xfer);
    if (!slot || slot->pending || len > BOT_TRANSFER_LIMIT) return MSC_BUSY;
    if (!input && len) memcpy(slot->staging, buf, len);
    slot->descriptor[0] = (uint32_t)slot->staging;
    slot->descriptor[1] = slot->descriptor[2] = 0;
    setup_xfer(xfer, pipe, slot, slot->descriptor, len, input ? USBD_SHORT_XFER_OK : 0,
               BOT_TIMEOUT_MS, NULL);
    usbd_status status = transfer(xfer);
    if ((unsigned)status != IN_PROGRESS && status != USBD_NORMAL_COMPLETION)
        return usb_result(status);
    unsigned elapsed = 0;
    while (slot->pending) {
        result = info_connected();
        if (result != MSC_OK || elapsed >= TICKS(BOT_TIMEOUT_MS)) {
            abort_pipe(pipe);
            for (elapsed = 0; slot->pending && elapsed < TICKS(100); elapsed += wait_irq()) {}
            retain_pending();
            return result == MSC_OK ? MSC_TIMEOUT : result;
        }
        elapsed += wait_irq();
    }
    result = info_connected();
    if (result != MSC_OK) return result;
    uint32_t size = 0;
    get_status(xfer, NULL, NULL, &size, &status);
    if (size > len) return MSC_PROTOCOL_ERROR;
    *actual = size;
    if (input && size) memcpy(buf, slot->staging, size);
    return usb_result(status);
}

static MscResult info_control(void *ctx, const UsbRequest *req, void *data,
                               uint32_t *actual) {
    (void)ctx;
    *actual = 0;
    MscResult result = info_connected();
    if (result != MSC_OK) return result;
    if (req->length > sizeof(diag.info.control)) return MSC_UNSUPPORTED;
    diag.info.request.bmRequestType = req->type;
    diag.info.request.bRequest = req->request;
    USETW(diag.info.request.wValue, req->value);
    USETW(diag.info.request.wIndex, req->index);
    USETW(diag.info.request.wLength, req->length);
    if (!(req->type & 0x80) && req->length) memcpy(diag.info.control, data, req->length);
    diag.info.actual = 0;
    usbd_status status = request(diag.device, &diag.info.request, diag.info.control,
                                 USBD_SHORT_XFER_OK, &diag.info.actual, BOT_TIMEOUT_MS);
    result = info_connected();
    if (result != MSC_OK) return result;
    if (diag.info.actual < 0 || (unsigned)diag.info.actual > req->length)
        return MSC_PROTOCOL_ERROR;
    *actual = diag.info.actual;
    if ((req->type & 0x80) && *actual) memcpy(data, diag.info.control, *actual);
    return usb_result(status);
}

static MscResult info_clear(void *ctx, uint8_t endpoint) {
    (void)ctx;
    MscResult result = info_connected();
    if (result != MSC_OK) return result;
    usbd_pipe_handle pipe = endpoint == diag.info.bot.ep_in ? diag.info.in_pipe :
                            endpoint == diag.info.bot.ep_out ? diag.info.out_pipe : NULL;
    return pipe ? usb_result(clear_halt(pipe)) : MSC_NO_DEVICE;
}

static bool info_recover(FILE *report) {
    if (!diag.info.bot.needs_reset) return info_connected() == MSC_OK;
    if (pending() || info_connected() != MSC_OK) return false;
    const uint8_t ready[6] = {0};
    MscResult result = bot_exec(&diag.info.bot, ready, sizeof(ready), NULL, 0);
    fprintf(report, "information_reset_and_ready=%d\n", result);
    return !diag.info.bot.needs_reset && info_connected() == MSC_OK;
}

static bool info_command(FILE *report, const char *name, const uint8_t *cdb,
                           uint8_t cdb_len, uint8_t *data, uint32_t len) {
    memset(data, 0, len);
    MscResult result = bot_exec(&diag.info.bot, cdb, cdb_len, data, len);
    fprintf(report, "%s_result=%d\n%s_transferred=%lu\n%s_residue=%lu\n",
            name, result, name, (unsigned long)diag.info.bot.transferred,
            name, (unsigned long)diag.info.bot.residue);
    fprintf(report, "%s_csw_tag_expected=%08lx\n%s_csw_hex=", name,
            (unsigned long)diag.info.bot.tag, name);
    for (unsigned i = 0; i < sizeof(diag.info.bot.csw); ++i)
        fprintf(report, "%02x", diag.info.bot.csw[i]);
    fputc('\n', report);
    if (result == MSC_OK) {
        uint32_t size = len - diag.info.bot.residue;
        fprintf(report, "%s_hex=", name);
        for (uint32_t i = 0; i < size; ++i) fprintf(report, "%02x", data[i]);
        fputc('\n', report);
        if (cdb[0] == 0x12 && size >= 36) {
            fputs("inquiry_vendor_model=", report);
            for (unsigned i = 8; i < 36; ++i)
                fputc(data[i] >= 32 && data[i] <= 126 ? data[i] : '.', report);
            fputc('\n', report);
        } else if (cdb[0] == 0x1a && size >= 4)
            fprintf(report, "mode_sense6_write_protect=%u\n", !!(data[2] & 0x80));
        else if (cdb[0] == 0x5a && size >= 8)
            fprintf(report, "mode_sense10_write_protect=%u\n", !!(data[3] & 0x80));
        else fputs("information_header=short_or_not_applicable\n", report);
    } else if (result == MSC_COMMAND_FAILED) {
        BotSense sense = {0};
        MscResult sr = bot_sense(&diag.info.bot, &sense);
        fprintf(report, "%s_unsupported_or_failed=1\n%s_sense_result=%d\n"
                "%s_sense=%02x/%02x/%02x\n", name, name, sr, name,
                sense.key, sense.asc, sense.ascq);
    } else fprintf(report, "%s_unavailable=transport_error\n", name);
    return info_recover(report);
}

static void info_release(void) {
    usbd_xfer_handle *xfers[] = {&diag.info.in, &diag.info.out};
    for (unsigned i = 0; i < 2; ++i) if (*xfers[i]) {
        Slot *slot = slot_for(*xfers[i]);
        if (slot && slot->pending) { retain_pending(); continue; }
        if (!free_xfer(*xfers[i])) *xfers[i] = NULL;
    }
}

bool diagnose_usb_information(FILE *report) {
    if (!report) return false;
    fputs("[read_only_scsi_information]\n", report);
    if (!diag.installed || diag.blocked || pending() || !diag.api->session_active ||
        diag.api->state != RESIDENT_READY) {
        fputs("information_skipped=session_not_idle_or_native_hooks_unavailable\n", report);
        return false;
    }
    Pipe *in = NULL, *out = NULL;
    for (unsigned i = 0; i < 4; ++i) if (diag.pipes[i].pipe) {
        if (diag.pipes[i].endpoint & 0x80) in = &diag.pipes[i];
        else out = &diag.pipes[i];
    }
    if (!in || !out || in->iface != out->iface || !diag.device) {
        fputs("information_skipped=driver_interface_device_or_pipes_not_captured\n", report);
        return false;
    }
    usb_interface_descriptor_t *id = diag.original->usbd_get_interface_descriptor(in->iface);
    if (!id || id->bInterfaceClass != 8 || id->bInterfaceSubClass != 6 ||
        id->bInterfaceProtocol != 0x50) {
        fputs("information_skipped=interface_is_not_SCSI_BOT\n", report);
        return false;
    }
    MscInfo info;
    diag.api->poll(&info);
    if (!info.connected) {
        fputs("information_skipped=device_removed\n", report);
        return false;
    }
    if (diag.info.in || diag.info.out) {
        fputs("information_skipped=previous_temporary_transfers_not_released\n", report);
        return false;
    }
    diag.info.generation = info.generation; diag.info.session = diag.api->session_id;
    diag.info.in_pipe = in->pipe; diag.info.out_pipe = out->pipe;
    UsbIo io = {NULL, info_bulk, info_control, info_clear};
    bot_init(&diag.info.bot, &io, id->bInterfaceNumber, in->endpoint, out->endpoint);
    /* Diagnostic tags are separate; no command overlaps the synchronous driver. */
    diag.info.bot.tag = 0xd1000000u;
    diag.info.in = alloc_xfer(diag.device);
    diag.info.out = alloc_xfer(diag.device);
    if (!diag.info.in || !diag.info.out || !slot_for(diag.info.in) ||
        !slot_for(diag.info.out)) {
        fputs("information_skipped=temporary_transfer_allocation_failed\n", report);
        info_release();
        return false;
    }
    static const uint8_t inquiry[6] = {0x12, 0, 0, 0, 36, 0};
    static const uint8_t mode6[6] = {0x1a, 8, 0x3f, 0, 64, 0};
    static const uint8_t mode10[10] = {0x5a, 8, 0x3f, 0, 0, 0, 0, 0, 64, 0};
    uint8_t data[64];
    bool safe = info_command(report, "inquiry", inquiry, sizeof(inquiry), data, 36);
    if (safe) safe = info_command(report, "mode_sense6", mode6, sizeof(mode6), data, 64);
    if (safe) safe = info_command(report, "mode_sense10", mode10, sizeof(mode10), data, 64);
    if (safe) {
        BotSense sense = {0};
        MscResult result = bot_sense(&diag.info.bot, &sense);
        fprintf(report, "request_sense_result=%d\nrequest_sense=%02x/%02x/%02x\n",
                result, sense.key, sense.asc, sense.ascq);
        safe = info_recover(report);
    }
    if (!safe) fputs("information_resume=unsafe_end_and_reopen_session\n", report);
    else fputs("information_resume=BOT_synchronized\n", report);
    info_release();
    if (pending()) {
        /* The driver cannot quarantine a transfer it does not own. */
        if (diag.api->state != RESIDENT_FAILED) diag.api->last_error = MSC_TIMEOUT;
        diag.api->state = RESIDENT_FAILED;
        diag.blocked = true;
        retain_pending();
        return false;
    }
    return safe;
}

static void dump_words(FILE *report, const char *name, const uint32_t *words, unsigned count) {
    fputs(name, report);
    for (unsigned i = 0; i < count; ++i) fprintf(report, "\t%08lx", (unsigned long)words[i]);
    fputc('\n', report);
}

static void dump_map(FILE *report) {
    uint32_t control, ttbr, domain;
    uint32_t ram[64] = {0}, pages[256] = {0}, page_table = 0;
    bool mapped = false;
    int mask = irq(-1);
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(control));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr));
    __asm__ volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(domain));
    if ((control & 1) && (ttbr & 0xffffc000u) == 0xa4004000u) {
        const volatile uint32_t *table = (void *)0xa4004000u;
        if ((table[0xa40] & 0xfff00003u) == 0xa4000002u) {
            for (unsigned i = 0; i < 64; ++i) ram[i] = table[0x100 + i];
            if ((ram[63] & 3) == 1) {
                page_table = ram[63] & 0xfffffc00u;
                if (resident_ram_range(page_table, sizeof(pages)) &&
                    (ram[(page_table >> 20) - 0x100] & 0xfff00003u) ==
                    ((page_table & 0xfff00000u) | 2)) {
                    const volatile uint32_t *coarse = (void *)page_table;
                    for (unsigned i = 0; i < 256; ++i) pages[i] = coarse[i];
                    mapped = true;
                }
            } else if ((ram[63] & 3) == 2) mapped = true;
        }
    }
    irq(mask);
    fprintf(report, "[cpu_mapping]\ncontrol=%08lx\nttbr=%08lx\ndomain=%08lx\n"
            "mmu=%u\ndcache=%u\nicache=%u\nwrite_buffer=%u\n"
            "dma_mapping_captured=%u\ndma_l2_table=%08lx\n"
            "mapping_flags=ARM_L1_section_or_L2_small_page_CB_bits_3_2\n",
            (unsigned long)control, (unsigned long)ttbr, (unsigned long)domain,
            !!(control & 1), !!(control & 4), !!(control & 4096), !!(control & 8),
            mapped, (unsigned long)page_table);
    for (unsigned i = 0; i < 64; ++i)
        fprintf(report, "l1\t%08lx\t%08lx\n", (unsigned long)(0x10000000u + (i << 20)),
                (unsigned long)ram[i]);
    if (mapped && page_table) for (unsigned i = 0; i < 256; ++i)
        fprintf(report, "l2\t%08lx\t%08lx\n", (unsigned long)(0x13f00000u + (i << 12)),
                (unsigned long)pages[i]);
    diag.map_dumped = true;
}

static void dump_host(FILE *report, const char *name, const Host *h) {
    fprintf(report, "[%s]\nsampled=%u\n", name, h->sampled);
    if (!h->sampled) return;
    fprintf(report, "event=%lu\nphase=%s\nstatus=%d\nopcode=%02lx\ntag=%lu\nendpoint=%02lx\n"
            "register_order=USBCMD,USBSTS,USBINTR,FRINDEX,ASYNCLISTADDR,PORTSC\n",
            (unsigned long)h->sequence, h->phase, h->status, (unsigned long)h->opcode,
            (unsigned long)h->tag, (unsigned long)h->endpoint);
    dump_words(report, "registers", h->regs, 6);
    dump_words(report, "xfer", h->xfer, 18);
    dump_words(report, "active_descriptor", h->descriptor, 3);
    fprintf(report, "bus_caps=%02lx\n", (unsigned long)h->bus_caps);
    for (unsigned i = 0; i < h->count; ++i) {
        fprintf(report, "queue=%08lx\n", (unsigned long)h->queues[i].address);
        dump_words(report, "qh", h->queues[i].qh, 12);
        dump_words(report, "current_td", h->queues[i].current, 8);
        dump_words(report, "next_td", h->queues[i].next, 8);
    }
}

void diagnose_usb_finish(FILE *report) {
    if (!report || !diag.original || diag.dumped) return;
    retain_pending();
    fprintf(report, "[native_transport]\nprofile=%s\nlocal_backend=%u\n"
            "total=%lu\ndropped=%lu\nslot_missed=%lu\nstaging_missed=%lu\n"
            "pending=%u\nblocked=%u\nactual_unknown=4294967295\n"
            "seq\tphase\tticks16\tcpsr\topcode\ttag\tlba\texpected\tep\tlength\tflags\t"
            "timeout\tstatus\tactual\tbuffer\tdesc1\tdesc2\txfer\tpipe\treply_hex\n",
            diagnose_profile_name(diag.profile), diag.local, (unsigned long)diag.count,
            (unsigned long)(diag.count > EVENT_COUNT ? diag.count - EVENT_COUNT : 0),
            (unsigned long)diag.slot_missed, (unsigned long)diag.stage_missed, pending(), diag.blocked);
    uint32_t last = diag.count;
    uint32_t first = last > EVENT_COUNT ? last - EVENT_COUNT + 1 : 1;
    for (uint32_t id = first; id && id <= last; ++id) {
        int mask = irq(-1);
        Event e = diag.events[(id - 1) % EVENT_COUNT];
        irq(mask);
        if (e.sequence != id) { fprintf(report, "event_overwritten=%lu\n", (unsigned long)id); continue; }
        fprintf(report, "%lu\t%s\t%u\t%08lx\t%02x\t%lu\t%lu\t%lu\t%02x\t%lu\t%u\t%lu\t"
                "%d\t%lu\t%08lx\t%08lx\t%08lx\t%08lx\t%08lx\t",
                (unsigned long)e.sequence, e.phase, (unsigned)(uint16_t)e.ticks,
                (unsigned long)e.cpsr, e.opcode, (unsigned long)e.tag, (unsigned long)e.lba,
                (unsigned long)e.expected, e.endpoint, (unsigned long)e.length, e.flags,
                (unsigned long)e.timeout, e.status, (unsigned long)e.actual,
                (unsigned long)e.buffer, (unsigned long)e.descriptor[0],
                (unsigned long)e.descriptor[1], (unsigned long)e.xfer, (unsigned long)e.pipe);
        if (!e.reply_length) fputc('-', report);
        for (unsigned i = 0; i < e.reply_length; ++i) fprintf(report, "%02x", e.reply[i]);
        fputc('\n', report);
    }
    dump_host(report, "first_write_error_controller_cpu_view", &diag.first_write_error);
    dump_host(report, "first_other_error_controller_cpu_view", &diag.first_other_error);
    fprintf(report, "[preHCD_DMA_CPU_compare]\ntotal=%lu\ndropped=%lu\nmissed=%lu\n"
            "first_difference_none=4294967295\n"
            "seq\tevent_before\topcode\ttag\tep\tlength\tsource\tcaller\tdma\tdesc1\tdesc2\t"
            "toggle_observed\tvalid\tcompared\tfirst_difference\texpected_byte\tactual_byte\tcbw\tdma_cbw\n",
            (unsigned long)diag.dma_count,
            (unsigned long)(diag.dma_count > DMA_COUNT ? diag.dma_count - DMA_COUNT : 0),
            (unsigned long)diag.dma_missed);
    last = diag.dma_count;
    first = last > DMA_COUNT ? last - DMA_COUNT + 1 : 1;
    for (uint32_t id = first; id && id <= last; ++id) {
        int mask = irq(-1);
        Dma r = diag.dma[(id - 1) % DMA_COUNT];
        irq(mask);
        if (r.sequence != id) continue;
        fprintf(report, "%lu\t%lu\t%02lx\t%lu\t%02lx\t%lu\t%08lx\t%08lx\t%08lx\t%08lx\t%08lx\t"
                "%lu\t%u\t%lu\t%lu\t%02x\t%02x\t",
                (unsigned long)id, (unsigned long)r.event, (unsigned long)r.opcode,
                (unsigned long)r.tag, (unsigned long)r.endpoint, (unsigned long)r.length,
                (unsigned long)r.source, (unsigned long)r.caller, (unsigned long)r.descriptor[0],
                (unsigned long)r.descriptor[1], (unsigned long)r.descriptor[2],
                (unsigned long)r.toggle, r.valid, (unsigned long)r.compared,
                (unsigned long)r.first_difference, r.expected_byte, r.actual_byte);
        if (r.valid && r.length == 31) {
            for (unsigned i = 0; i < 31; ++i) fprintf(report, "%02x", r.cbw[i]);
            fputc('\t', report);
            for (unsigned i = 0; i < 31; ++i) fprintf(report, "%02x", r.dma_cbw[i]);
        } else fputs("-\t-", report);
        fputc('\n', report);
    }
    if (!diag.map_dumped) dump_map(report);
    diag.dumped = true;
}

void diagnose_usb_remove(void) {
    if (!diag.installed) return;
    info_release();
    retain_pending();
    int mask = irq(-1);
    if (*diag.location == &diag.hooks) *diag.location = diag.original;
    else { diag.blocked = true; nl_set_resident(); }
    diag.installed = false;
    irq(mask);
}
