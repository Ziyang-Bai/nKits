#include "resident_check.h"

#include <string.h>

bool resident_ram_range(uint32_t addr, size_t size) {
    return !(addr & 3u) && addr >= 0x10000000u && addr < 0x14000000u &&
           size <= 0x14000000u - addr;
}

bool resident_prologue(uint32_t first, uint32_t second, bool cas) {
    if (cas) return first == 0xe92d41f0u && second == 0xe1a04000u;
    /* AL STMDB sp!, nonempty register list without sp/pc. */
    if ((first & 0xffff0000u) != 0xe92d0000u ||
        !(first & 0xffffu) || (first & 0xa000u)) return false;
    /* AL MOV Rd, Rm: no flags, shift, immediate, or PC operand. */
    return (second & 0xffff0ff0u) == 0xe1a00000u &&
           ((second >> 12) & 15u) != 15u && (second & 15u) != 15u;
}

static bool code_pointer(uintptr_t addr) {
    /* Thumb function pointers are valid too, but never outside SDRAM. */
#if UINTPTR_MAX > UINT32_MAX
    if (addr > UINT32_MAX) return false;
#endif
    return (addr & 3u) != 2u && resident_ram_range((uint32_t)addr & ~3u, 4);
}

ResidentCheck resident_check_api(const ResidentApi *api, const char *build_id) {
    if (api->magic != NUSB_RESIDENT_MAGIC ||
        api->state > RESIDENT_FAILED || api->session_active > 1)
        return RESIDENT_CONFLICT;
    if (api->abi != NUSB_RESIDENT_ABI) return RESIDENT_UPDATE;
    if (!code_pointer((uintptr_t)api->open) ||
        !code_pointer((uintptr_t)api->read) ||
        !code_pointer((uintptr_t)api->write) ||
        !code_pointer((uintptr_t)api->sync) ||
        !code_pointer((uintptr_t)api->close) ||
        !code_pointer((uintptr_t)api->poll) ||
        !code_pointer((uintptr_t)api->folder_ready) ||
        !code_pointer((uintptr_t)api->internal_stat) ||
        !code_pointer((uintptr_t)api->internal_rename) ||
        !code_pointer((uintptr_t)api->internal_rmdir) ||
        !code_pointer((uintptr_t)api->internal_set_metadata) ||
        !code_pointer((uintptr_t)api->fattime) ||
        !code_pointer((uintptr_t)api->portal_path) ||
        !code_pointer((uintptr_t)api->namespace_changed) ||
        !code_pointer((uintptr_t)api->internal_open) ||
        !code_pointer((uintptr_t)api->internal_read) ||
        !code_pointer((uintptr_t)api->internal_write) ||
        !code_pointer((uintptr_t)api->internal_seek) ||
        !code_pointer((uintptr_t)api->internal_sync) ||
        !code_pointer((uintptr_t)api->internal_close)) return RESIDENT_CONFLICT;
    if (memcmp(api->build_id, build_id, sizeof(api->build_id)))
        return RESIDENT_UPDATE;
    if (api->state == RESIDENT_FAILED) return RESIDENT_UNAVAILABLE;
    if (api->state == RESIDENT_INSTALLING) return RESIDENT_BUSY;
    return RESIDENT_COMPATIBLE;
}
