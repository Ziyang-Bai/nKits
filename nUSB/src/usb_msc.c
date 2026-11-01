#include "usb_msc.h"
#include "bot.h"
#include "resident.h"
#include "resident_internal.h"
#include "native_fs.h"
#include "usb_msc_native.h"

#include <libndls.h>
#include <string.h>

#define CANCEL_WAIT_MS 100u
enum { OS_USB_IN_PROGRESS = 2 };

/* OS 6.4.0.74 only: Ndless 9484d8da7c7a4dde9766138c2e42e1d1e3acfcd4,
   ndless/src/tools/MakeSyscalls/idc/OS_{ncas,cas}cx2-6.4.0.74.idc.
   Use the last MakeName for duplicate symbols, as the syscall generator does.
   Direct calls avoid Ndless's shared SWI frames and >4-argument saved-LR stack. */
#define OS_CALL(name, addr) .name = (__typeof__(&name))(addr)
static const NativeUsb native_ncas = {
    OS_CALL(TCT_Local_Control_Interrupts, 0x1042ad50),
    OS_CALL(device_get_ivars, 0x10476034),
    OS_CALL(device_get_softc, 0x10476074),
    OS_CALL(usbd_open_pipe, 0x10a08f4c),
    OS_CALL(usbd_close_pipe, 0x10a08348),
    OS_CALL(usbd_transfer, 0x10a084e4),
    OS_CALL(usbd_alloc_xfer, 0x10a083c4),
    OS_CALL(usbd_free_xfer, 0x10a0828c),
    OS_CALL(usbd_setup_xfer, 0x10a07a34),
    OS_CALL(usbd_get_xfer_status, 0x10a07b54),
    OS_CALL(usbd_interface2endpoint_descriptor, 0x10a07bac),
    OS_CALL(usbd_abort_pipe, 0x10a07bc8),
    OS_CALL(usbd_clear_endpoint_stall, 0x10a08c48),
    OS_CALL(usbd_do_request_flags, 0x10a08aec),
    OS_CALL(usbd_get_interface_descriptor, 0x10a07b9c),
    OS_CALL(usb_register_driver, 0x10476258)
};

static const NativeUsb native_cas = {
    OS_CALL(TCT_Local_Control_Interrupts, 0x1042bd70),
    OS_CALL(device_get_ivars, 0x10477054),
    OS_CALL(device_get_softc, 0x10477094),
    OS_CALL(usbd_open_pipe, 0x10a70cf0),
    OS_CALL(usbd_close_pipe, 0x10a700ec),
    OS_CALL(usbd_transfer, 0x10a70288),
    OS_CALL(usbd_alloc_xfer, 0x10a70168),
    OS_CALL(usbd_free_xfer, 0x10a70030),
    OS_CALL(usbd_setup_xfer, 0x10a6f7d8),
    OS_CALL(usbd_get_xfer_status, 0x10a6f8f8),
    OS_CALL(usbd_interface2endpoint_descriptor, 0x10a6f950),
    OS_CALL(usbd_abort_pipe, 0x10a6f96c),
    OS_CALL(usbd_clear_endpoint_stall, 0x10a709ec),
    OS_CALL(usbd_do_request_flags, 0x10a70890),
    OS_CALL(usbd_get_interface_descriptor, 0x10a6f940),
    OS_CALL(usb_register_driver, 0x10477278)
};
#undef OS_CALL

static const NativeUsb *os;

const NativeUsb **msc_backend_native(void) {
    return &os;
}

typedef struct {
    usbd_pipe_handle pipe;
    usbd_xfer_handle xfer;
    volatile bool pending;
    struct {
        void *data;
        uint32_t flags[2];
    } buffer;
} Transfer;

typedef struct {
    device_t device;
} MscSoftc;

static struct {
    ResidentApi *api;
    device_t owner;
    usbd_device_handle device;
    usbd_interface_handle iface;
    MscInfo info;
    uint32_t notified_generation;
    MscHooks hooks;
    Bot bot;
    Transfer in;
    Transfer out;
    bool operation;
    bool closing;
    bool reap;
    bool reaping;
    uint8_t in_buf[BOT_TRANSFER_LIMIT];
    uint8_t out_buf[BOT_TRANSFER_LIMIT];
    usb_device_request_t request;
    uint8_t control_buf[32];
    int control_actual;
    int (*methods[4])(device_t);
    char name[9];
} usb;

static MscResult usb_result(usbd_status status) {
    /* OS 6.4's usbd_errstr table uses Jungo status codes. */
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

static void completed(usbd_xfer_handle xfer, usbd_private_handle ctx,
                      usbd_status status) {
    Transfer *transfer = ctx;
    (void)xfer;
    (void)status;
    transfer->pending = false;
    /* Do not free the xfer on its callback stack. The next API call reaps it. */
}

static void release_transfer(usbd_xfer_handle xfer, usbd_pipe_handle pipe) {
    if (xfer) os->usbd_free_xfer(xfer);
    if (pipe) os->usbd_close_pipe(pipe);
}

static void reap(void) {
    int mask = os->TCT_Local_Control_Interrupts(-1);
    /* Detach must release xfers before the OS destroys their device handle.
       An interrupted foreground operation checks connected before reusing them. */
    if (!usb.reap || usb.reaping || (usb.operation && usb.info.connected) ||
        usb.closing || usb.in.pending || usb.out.pending) {
        os->TCT_Local_Control_Interrupts(mask);
        return;
    }
    usbd_xfer_handle in_xfer = usb.in.xfer, out_xfer = usb.out.xfer;
    usbd_pipe_handle in_pipe = usb.in.pipe, out_pipe = usb.out.pipe;
    usb.in.xfer = usb.out.xfer = NULL;
    usb.in.pipe = usb.out.pipe = NULL;
    usb.reap = false;
    usb.reaping = true;
    os->TCT_Local_Control_Interrupts(mask);
    /* Detach sees no shared handles while close waits for EHCI unlink. */
    release_transfer(in_xfer, in_pipe);
    release_transfer(out_xfer, out_pipe);
    usb.reaping = false;
}

static void quarantine(MscResult error) {
    if (usb.api->state != RESIDENT_FAILED) usb.api->last_error = error;
    usb.api->state = RESIDENT_FAILED;
    usb.info.ready = false;
}

static void abort_transfers(void) {
    usb.hooks.pump = NULL;
    usb.hooks.ctx = NULL;
    usb.closing = true;
    if (usb.in.pipe) os->usbd_abort_pipe(usb.in.pipe);
    if (usb.out.pipe) os->usbd_abort_pipe(usb.out.pipe);
    usb.closing = false;
    usb.reap = true;
}

static unsigned wait_irq(void) {
    /* Timer 2's second counter measures elapsed time across an OS interrupt. */
    volatile uint32_t *counter = (volatile uint32_t *)0x900d0024;
    uint16_t start = *counter;
    __asm__ volatile ("mcr p15, 0, %0, c7, c0, 4" : : "r"(0) : "memory");
    return (uint16_t)(start - *counter);
}

unsigned msc_wait(void) {
    unsigned elapsed = 0;
    do { elapsed += wait_irq(); } while (elapsed < 328);
    return elapsed;
}

static void wait_abort(MscResult error) {
    abort_transfers();
    for (unsigned elapsed = 0; (usb.in.pending || usb.out.pending) &&
         elapsed < (CANCEL_WAIT_MS * 32768u + 999) / 1000; elapsed += wait_irq()) {}
    if (usb.in.pending || usb.out.pending) quarantine(error);
}

static MscResult interrupted(void) {
    if (!usb.info.connected) return MSC_NO_DEVICE;
    if (usb.api->state == RESIDENT_FAILED) return usb.api->last_error;
    if (!usb.api->session_active) return MSC_CANCELLED;
    int mask = os->TCT_Local_Control_Interrupts(-1);
    MscHooks hooks = usb.hooks;
    os->TCT_Local_Control_Interrupts(mask);
    if (hooks.pump && hooks.pump(hooks.ctx)) return MSC_CANCELLED;
    if (!usb.info.connected) return MSC_NO_DEVICE;
    if (!usb.api->session_active) return MSC_CANCELLED;
    return MSC_OK;
}

static MscResult bulk(void *ctx, uint8_t endpoint, void *buf, uint32_t len,
                      uint32_t *actual) {
    (void)ctx;
    *actual = 0;
    bool input = endpoint == usb.bot.ep_in;
    if (!input && endpoint != usb.bot.ep_out) return MSC_PROTOCOL_ERROR;
    Transfer *transfer = input ? &usb.in : &usb.out;
    uint8_t *staging = input ? usb.in_buf : usb.out_buf;
    if (len > (input ? sizeof(usb.in_buf) : sizeof(usb.out_buf)))
        return MSC_PROTOCOL_ERROR;
    MscResult result = interrupted();
    if (result != MSC_OK) {
        wait_abort(result);
        return result;
    }
    if (!transfer->pipe || !transfer->xfer) return MSC_NO_DEVICE;
    if (!input && len) memcpy(staging, buf, len);
    transfer->pending = true;
    /* Match the OS control path's {data, 0, 0} copy-buffer descriptor. */
    transfer->buffer.data = staging;
    os->usbd_setup_xfer(transfer->xfer, transfer->pipe, transfer, &transfer->buffer,
                       len, input ? USBD_SHORT_XFER_OK : 0, BOT_TIMEOUT_MS, completed);
    usbd_status status = os->usbd_transfer(transfer->xfer);
    if ((unsigned)status != OS_USB_IN_PROGRESS) {
        transfer->pending = false;
        if (status != USBD_NORMAL_COMPLETION) return usb_result(status);
    }
    unsigned elapsed = 0;
    while (transfer->pending) {
        result = interrupted();
        if (result != MSC_OK || elapsed >= BOT_TIMEOUT_MS * 32768u / 1000) {
            if (result == MSC_OK) result = MSC_TIMEOUT;
            wait_abort(result);
            return result;
        }
        elapsed += wait_irq();
    }
    if (!usb.info.connected) return MSC_NO_DEVICE;
    uint32_t size = 0;
    os->usbd_get_xfer_status(transfer->xfer, NULL, NULL, &size, &status);
    if (size > len) return MSC_PROTOCOL_ERROR;
    *actual = size;
    if (input && size) memcpy(buf, staging, size);
    return usb_result(status);
}

static MscResult control(void *ctx, const UsbRequest *req, void *data,
                         uint32_t *actual) {
    (void)ctx;
    *actual = 0;
    if (req->length > sizeof(usb.control_buf)) return MSC_PROTOCOL_ERROR;
    MscResult result = interrupted();
    if (result != MSC_OK) {
        wait_abort(result);
        return result;
    }
    usb.request.bmRequestType = req->type;
    usb.request.bRequest = req->request;
    USETW(usb.request.wValue, req->value);
    USETW(usb.request.wIndex, req->index);
    USETW(usb.request.wLength, req->length);
    if (!(req->type & UT_READ) && req->length)
        memcpy(usb.control_buf, data, req->length);
    usb.control_actual = 0;
    usbd_status status = os->usbd_do_request_flags(usb.device, &usb.request,
        usb.control_buf, USBD_SHORT_XFER_OK, &usb.control_actual, BOT_TIMEOUT_MS);
    if (!usb.info.connected) return MSC_NO_DEVICE;
    if (usb.control_actual < 0 || (unsigned)usb.control_actual > req->length)
        return MSC_PROTOCOL_ERROR;
    *actual = (uint32_t)usb.control_actual;
    if ((req->type & UT_READ) && *actual)
        memcpy(data, usb.control_buf, *actual);
    result = interrupted();
    if (result != MSC_OK) {
        wait_abort(result);
        return result;
    }
    return usb_result(status);
}

static MscResult clear_halt(void *ctx, uint8_t endpoint) {
    (void)ctx;
    MscResult result = interrupted();
    if (result != MSC_OK) {
        wait_abort(result);
        return result;
    }
    usbd_pipe_handle pipe = endpoint == usb.bot.ep_in ? usb.in.pipe :
                            endpoint == usb.bot.ep_out ? usb.out.pipe : NULL;
    if (!pipe) return MSC_NO_DEVICE;
    usbd_status status = os->usbd_clear_endpoint_stall(pipe);
    if (!usb.info.connected) return MSC_NO_DEVICE;
    return usb_result(status);
}

static int match(device_t self) {
    struct usb_attach_arg *args = os->device_get_ivars(self);
    if (!args || !args->iface) return UMATCH_NONE;
    usb_interface_descriptor_t *id = os->usbd_get_interface_descriptor(args->iface);
    return id && id->bInterfaceClass == 8 && id->bInterfaceSubClass == 6 &&
           id->bInterfaceProtocol == 0x50 ?
           UMATCH_IFACECLASS_IFACESUBCLASS_IFACEPROTO : UMATCH_NONE;
}

static int attach(device_t self) {
    MscSoftc *sc = os->device_get_softc(self);
    struct usb_attach_arg *args = os->device_get_ivars(self);
    if (!sc || !args || !match(self) || usb.owner || usb.operation || usb.reaping ||
        usb.in.pending || usb.out.pending || usb.api->state == RESIDENT_FAILED)
        return ENXIO;
    reap();
    usb_interface_descriptor_t *id = os->usbd_get_interface_descriptor(args->iface);
    uint8_t in = 0, out = 0;
    for (unsigned i = 0; i < id->bNumEndpoints; ++i) {
        usb_endpoint_descriptor_t *ep =
            os->usbd_interface2endpoint_descriptor(args->iface, (uint8_t)i);
        if (!ep || (ep->bmAttributes & UE_XFERTYPE) != UE_BULK) continue;
        if (!(ep->bEndpointAddress & 15u)) return ENXIO;
        if (ep->bEndpointAddress & UE_DIR_IN) {
            if (in) return ENXIO;
            in = ep->bEndpointAddress;
        } else {
            if (out) return ENXIO;
            out = ep->bEndpointAddress;
        }
    }
    if (!in || !out) return ENXIO;
    UsbIo io;
    io.ctx = NULL;
    io.bulk = bulk;
    io.control = control;
    io.clear_halt = clear_halt;
    bot_init(&usb.bot, &io, id->bInterfaceNumber, in, out);
    sc->device = self;
    usb.owner = self;
    usb.device = args->device;
    usb.iface = args->iface;
    usb.info.connected = true;
    usb.info.ready = false;
    usb.info.sector_size = 0;
    usb.info.sector_count = 0;
    ++usb.info.generation;
    return 0;
}

static int detach(device_t self) {
    MscSoftc *sc = os->device_get_softc(self);
    if (!sc || usb.owner != self) return 0;
    usb.info.connected = false;
    usb.info.ready = false;
    ++usb.info.generation;
    abort_transfers();
    if (usb.in.pending || usb.out.pending) quarantine(MSC_NO_DEVICE);
    usb.owner = NULL;
    usb.device = NULL;
    usb.iface = NULL;
    sc->device = NULL;
    reap();
    return 0;
}

static MscResult open_transfer(Transfer *transfer, uint8_t endpoint) {
    if (!usb.info.connected) return MSC_NO_DEVICE;
    usbd_status status = os->usbd_open_pipe(usb.iface, endpoint, USBD_EXCLUSIVE_USE,
                                          &transfer->pipe);
    if (status != USBD_NORMAL_COMPLETION) return usb_result(status);
    if (!usb.info.connected) return MSC_NO_DEVICE;
    transfer->xfer = os->usbd_alloc_xfer(usb.device);
    return transfer->xfer ? MSC_OK : MSC_IO_ERROR;
}

static void backend_close(void) {
    /* OS 6.4 reopens pipes at DATA0. Synchronize the attached device's
       endpoint toggles through BOT reset recovery before the next command. */
    if (usb.in.pipe || usb.out.pipe) usb.bot.needs_reset = true;
    wait_abort(MSC_CANCELLED);
    usb.info.ready = false;
    usb.api->session_active = 0;
    reap();
}

static void session_changed(void) {
    uint32_t generation = usb.info.generation;
    usb.notified_generation = generation;
    native_fs_session_changed(usb.api->session_id, generation);
}

static MscResult backend_open(MscInfo *info, const MscHooks *hooks) {
    reap();
    if (info) *info = usb.info;
    if (usb.api->state == RESIDENT_FAILED) return usb.api->last_error;
    if (usb.api->session_active || usb.operation || usb.reaping) return MSC_BUSY;
    if (!usb.info.connected) {
        if (info) *info = usb.info;
        return MSC_NO_DEVICE;
    }
    usb.api->session_active = 1;
    ++usb.api->session_id;
    usb.operation = true;
    if (hooks) usb.hooks = *hooks;
    else memset(&usb.hooks, 0, sizeof(usb.hooks));
    MscResult result = open_transfer(&usb.in, usb.bot.ep_in);
    if (result == MSC_OK) result = open_transfer(&usb.out, usb.bot.ep_out);
    if (result == MSC_OK) result = bot_probe(&usb.bot, &usb.info);
    usb.operation = false;
    if (result != MSC_OK) backend_close();
    else session_changed();
    usb.api->last_error = result;
    if (info) *info = usb.info;
    return result;
}

static MscResult backend_read(uint32_t lba, uint32_t count, void *buf) {
    if (usb.api->state == RESIDENT_FAILED) return usb.api->last_error;
    if (!usb.api->session_active || !usb.info.ready) return MSC_NO_DEVICE;
    if (usb.operation) return MSC_BUSY;
    usb.operation = true;
    MscResult result = bot_read(&usb.bot, lba, count, buf);
    usb.operation = false;
    usb.api->last_error = result;
    if (result != MSC_OK) {
        usb.info.ready = false;
        backend_close();
    }
    return result;
}

static MscResult backend_write(uint32_t lba, uint32_t count, const void *buf) {
    if (usb.api->state == RESIDENT_FAILED) return usb.api->last_error;
    if (!usb.api->session_active || !usb.info.ready) return MSC_NO_DEVICE;
    if (usb.operation) return MSC_BUSY;
    usb.operation = true;
    MscResult result = bot_write(&usb.bot, lba, count, buf);
    usb.operation = false;
    usb.api->last_error = result;
    if (result != MSC_OK) {
        usb.info.ready = false;
        backend_close();
    }
    return result;
}

static MscResult backend_sync(void) {
    if (usb.api->state == RESIDENT_FAILED) return usb.api->last_error;
    if (!usb.api->session_active || !usb.info.ready) return MSC_NO_DEVICE;
    if (usb.operation) return MSC_BUSY;
    usb.operation = true;
    MscResult result = bot_sync(&usb.bot);
    usb.operation = false;
    usb.api->last_error = result;
    if (result != MSC_OK) {
        usb.info.ready = false;
        backend_close();
    }
    return result;
}

static void backend_poll(MscInfo *info) {
    reap();
    /* Attach/detach only mark the generation; invalidate outside USB callbacks. */
    if (!usb.operation && usb.notified_generation != usb.info.generation)
        session_changed();
    if (info) *info = usb.info;
}

void msc_backend_init(ResidentApi *api) {
    /* resident.c has already checked the CX II model and OS marker. */
    os = *(volatile uint32_t *)0x10000020u == 0x1042aee0u ?
         &native_cas : &native_ncas;
    usb.api = api;
    api->open = backend_open;
    api->read = backend_read;
    api->write = backend_write;
    api->sync = backend_sync;
    api->close = backend_close;
    api->poll = backend_poll;
    usb.methods[0] = match;
    usb.methods[1] = attach;
    usb.methods[2] = detach;
    usb.methods[3] = NULL;
    memcpy(usb.name, "nusb_msc", sizeof(usb.name));
}

MscResult msc_backend_register(void) {
    /* Call the OS entry, including the resident's transparent registration gate. */
    return os->usb_register_driver(2, usb.methods, usb.name, 0, sizeof(MscSoftc)) == 0 ?
           MSC_OK : MSC_IO_ERROR;
}

MscResult msc_open(MscInfo *info, const MscHooks *hooks) {
    ResidentApi *api = resident_api();
    return api ? api->open(info, hooks) : MSC_IO_ERROR;
}

MscResult msc_read(uint32_t lba, uint32_t count, void *buf) {
    ResidentApi *api = resident_api();
    return api ? api->read(lba, count, buf) : MSC_IO_ERROR;
}

MscResult msc_write(uint32_t lba, uint32_t count, const void *buf) {
    ResidentApi *api = resident_api();
    return api ? api->write(lba, count, buf) : MSC_IO_ERROR;
}

MscResult msc_sync(void) {
    ResidentApi *api = resident_api();
    return api ? api->sync() : MSC_IO_ERROR;
}

void msc_close(void) {
    ResidentApi *api = resident_api();
    if (api) api->close();
}

void msc_poll(MscInfo *info) {
    ResidentApi *api = resident_api();
    if (api) api->poll(info);
    else if (info) memset(info, 0, sizeof(*info));
}
