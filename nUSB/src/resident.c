#include "resident.h"
#include "resident_check.h"
#include "resident_internal.h"
#include "resident_build.h"
#include "native_fs.h"

#include <libndls.h>
#include <string.h>

extern uint32_t resident_gate_words[5];
extern uint32_t resident_trampoline[4];
extern int resident_publish(volatile uint32_t *entry, uint32_t *gate,
                            uint32_t first, uint32_t second);

static ResidentApi api;
static ResidentApi *active;

static bool usb_available(void) {
    return nl_hassyscall(TCT_Local_Control_Interrupts) &&
           nl_hassyscall(usb_register_driver) &&
           nl_hassyscall(device_get_ivars) && nl_hassyscall(device_get_softc) &&
           nl_hassyscall(usbd_get_interface_descriptor) &&
           nl_hassyscall(usbd_interface2endpoint_descriptor) &&
           nl_hassyscall(usbd_open_pipe) && nl_hassyscall(usbd_close_pipe) &&
           nl_hassyscall(usbd_alloc_xfer) && nl_hassyscall(usbd_free_xfer) &&
           nl_hassyscall(usbd_setup_xfer) && nl_hassyscall(usbd_transfer) &&
           nl_hassyscall(usbd_get_xfer_status) && nl_hassyscall(usbd_abort_pipe) &&
           nl_hassyscall(usbd_clear_endpoint_stall) &&
           nl_hassyscall(usbd_do_request_flags);
}

static const char *last_error(MscResult result) {
    switch (result) {
        case MSC_NO_DEVICE: return "USB disconnected; restart nUSB";
        case MSC_UNSUPPORTED: return "USB device unsupported";
        case MSC_TIMEOUT: return "USB timeout; restart calculator";
        case MSC_PROTOCOL_ERROR: return "USB protocol error";
        case MSC_CANCELLED: return "USB cancelled; restart calculator";
        case MSC_BUSY: return "USB busy";
        case MSC_STALLED: return "USB endpoint stalled";
        case MSC_COMMAND_FAILED: return "USB command failed";
        case MSC_WRITE_PROTECTED: return "USB drive is write-protected";
        default: return "USB I/O error; restart calculator";
    }
}

static const char *reuse(uint32_t addr, uint32_t entry, bool cas) {
    if (!resident_ram_range(addr, 5 * sizeof(uint32_t))) return "USB hook conflict";
    const uint32_t *gate = (const uint32_t *)addr;
    if (gate[0] != 0xe59ff004u || gate[1] != NUSB_RESIDENT_MAGIC)
        return "USB hook conflict";
    if (gate[2] != NUSB_RESIDENT_ABI) return "Restart to update nUSB";
    if (!resident_ram_range(gate[3], 4 * sizeof(uint32_t)) ||
        !resident_ram_range(gate[4], sizeof(ResidentApi))) return "USB hook conflict";
    const uint32_t *trampoline = (const uint32_t *)gate[3];
    if (!resident_prologue(trampoline[0], trampoline[1], cas) ||
        trampoline[2] != 0xe51ff004u || trampoline[3] != entry + 8)
        return "USB hook conflict";
    ResidentApi *found = (ResidentApi *)gate[4];
    switch (resident_check_api(found, NUSB_BUILD_ID)) {
        case RESIDENT_CONFLICT: return "USB hook conflict";
        case RESIDENT_UPDATE: return "Restart to update nUSB";
        case RESIDENT_BUSY: return "USB busy";
        case RESIDENT_UNAVAILABLE:
            if (!found->session_active) found->poll(NULL);
            return last_error(found->last_error);
        case RESIDENT_COMPATIBLE: break;
    }
    if (!found->folder_ready()) return "Restart to restore USB folder";
    active = found;
    return NULL;
}

const char *resident_init(bool *publisher) {
    static const unsigned entries[50] = {
        [47] = 0x10476258u, [49] = 0x10477278u
    };
    *publisher = false;
    active = NULL;
    if (!is_cx2) return "CX II required";
    /* r2022 guarantees these extensions; nl_hassyscall queries OS calls only. */
    if (nl_ndless_rev() < 2022 || !usb_available()) return "USB API unavailable";
    uint32_t marker = *(volatile uint32_t *)0x10000020u;
    uint32_t addr = nl_osvalue(entries, 50);
    bool cas = marker == 0x1042aee0u;
    if ((!cas && marker != 0x10429ec0u) ||
        addr != (cas ? 0x10477278u : 0x10476258u)) return "OS 6.4 required";
    volatile uint32_t *entry = (volatile uint32_t *)addr;
    uint32_t first = entry[0], second = entry[1];
    if (first == 0xe51ff004u) return reuse(second, addr, cas);
    if ((first & 0x0f000000u) == 0x0a000000u ||
        (first & 0x0f7ff000u) == 0x051ff000u) return "USB hook conflict";
    if (!resident_prologue(first, second, cas)) return "USB entry unsupported";

    memset(&api, 0, sizeof(api));
    api.magic = NUSB_RESIDENT_MAGIC;
    api.abi = NUSB_RESIDENT_ABI;
    api.state = RESIDENT_INSTALLING;
    memcpy(api.build_id, NUSB_BUILD_ID, sizeof(api.build_id));
    msc_backend_init(&api);
    api.folder_ready = native_fs_ready;
    api.internal_stat = native_fs_internal_stat;
    api.internal_rename = native_fs_internal_rename;
    api.internal_rmdir = native_fs_internal_rmdir;
    api.internal_set_metadata = native_fs_internal_set_metadata;
    api.fattime = native_fs_fattime;
    api.portal_path = native_fs_portal_path;
    api.namespace_changed = native_fs_namespace_changed;
    api.internal_open = native_fs_internal_open;
    api.internal_read = native_fs_internal_read;
    api.internal_write = native_fs_internal_write;
    api.internal_seek = native_fs_internal_seek;
    api.internal_sync = native_fs_internal_sync;
    api.internal_close = native_fs_internal_close;
    resident_trampoline[0] = first;
    resident_trampoline[1] = second;
    resident_trampoline[3] = addr + 8;
    resident_gate_words[3] = (uint32_t)resident_trampoline;
    resident_gate_words[4] = (uint32_t)&api;
    if (*(volatile uint32_t *)0x10000020u != marker ||
        nl_osvalue(entries, 50) != addr) return "OS 6.4 required";

    nl_set_resident();
    *publisher = true;
    if (!resident_publish(entry, resident_gate_words, first, second)) {
        api.state = RESIDENT_FAILED;
        api.last_error = MSC_BUSY;
        return "USB hook conflict";
    }
    api.last_error = msc_backend_register();
    api.state = api.last_error == MSC_OK ? RESIDENT_READY : RESIDENT_FAILED;
    if (api.state == RESIDENT_FAILED) return last_error(api.last_error);
    active = &api;
    const char *error = native_fs_install(&api);
    if (error) {
        api.state = RESIDENT_FAILED;
        api.last_error = MSC_IO_ERROR;
        return error;
    }
    return NULL;
}

ResidentApi *resident_api(void) {
    return active;
}
