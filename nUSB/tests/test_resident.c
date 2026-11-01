#include "resident_check.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_api(void) {
    static const char build_id[65] = "resident-test";
    ResidentApi api = {0};
    api.magic = NUSB_RESIDENT_MAGIC;
    api.abi = NUSB_RESIDENT_ABI;
    api.state = RESIDENT_READY;
    memcpy(api.build_id, build_id, sizeof(api.build_id));
    /* Synthetic SDRAM addresses are checked, never called. */
#define CODE(member, address) ((__typeof__(api.member))(uintptr_t)(address))
    api.open = CODE(open, 0x10001000u);
    api.read = CODE(read, 0x10001000u);
    api.write = CODE(write, 0x10001000u);
    api.sync = CODE(sync, 0x10001000u);
    api.close = CODE(close, 0x10001000u);
    api.poll = CODE(poll, 0x10001000u);
    api.folder_ready = CODE(folder_ready, 0x10001000u);
    api.internal_stat = CODE(internal_stat, 0x10001000u);
    api.internal_rename = CODE(internal_rename, 0x10001000u);
    api.internal_rmdir = CODE(internal_rmdir, 0x10001000u);
    api.internal_set_metadata = CODE(internal_set_metadata, 0x10001000u);
    api.fattime = CODE(fattime, 0x10001000u);
    api.portal_path = CODE(portal_path, 0x10001000u);
    api.namespace_changed = CODE(namespace_changed, 0x10001000u);
    api.internal_open = CODE(internal_open, 0x10001000u);
    api.internal_read = CODE(internal_read, 0x10001000u);
    api.internal_write = CODE(internal_write, 0x10001000u);
    api.internal_seek = CODE(internal_seek, 0x10001000u);
    api.internal_sync = CODE(internal_sync, 0x10001000u);
    api.internal_close = CODE(internal_close, 0x10001000u);
    assert(NUSB_RESIDENT_ABI == 4);
    assert(resident_check_api(&api, build_id) == RESIDENT_COMPATIBLE);
    api.session_active = 1;
    assert(resident_check_api(&api, build_id) == RESIDENT_COMPATIBLE);
    api.session_active = 2;
    assert(resident_check_api(&api, build_id) == RESIDENT_CONFLICT);
    for (unsigned active = 0; active <= 1; ++active) {
        api.session_active = active;
        api.state = RESIDENT_INSTALLING;
        assert(resident_check_api(&api, build_id) == RESIDENT_BUSY);
        api.state = RESIDENT_FAILED;
        assert(resident_check_api(&api, build_id) == RESIDENT_UNAVAILABLE);
    }
    api.session_active = 0;
    api.state = RESIDENT_FAILED + 1;
    assert(resident_check_api(&api, build_id) == RESIDENT_CONFLICT);
    api.state = RESIDENT_READY;
    api.magic = 0;
    assert(resident_check_api(&api, build_id) == RESIDENT_CONFLICT);
    api.magic = NUSB_RESIDENT_MAGIC;
    api.abi = 3;
    assert(resident_check_api(&api, build_id) == RESIDENT_UPDATE);
    api.abi = NUSB_RESIDENT_ABI + 1;
    assert(resident_check_api(&api, build_id) == RESIDENT_UPDATE);
    api.abi = NUSB_RESIDENT_ABI;
    api.build_id[64] = 'x';
    assert(resident_check_api(&api, build_id) == RESIDENT_UPDATE);
    api.build_id[64] = 0;
#define REQUIRE_POINTER(member) do { \
    api.member = NULL; \
    assert(resident_check_api(&api, build_id) == RESIDENT_CONFLICT); \
    api.member = CODE(member, 0x10001000u); \
} while (0)
    REQUIRE_POINTER(open);
    REQUIRE_POINTER(read);
    REQUIRE_POINTER(write);
    REQUIRE_POINTER(sync);
    REQUIRE_POINTER(close);
    REQUIRE_POINTER(poll);
    REQUIRE_POINTER(folder_ready);
    REQUIRE_POINTER(internal_stat);
    REQUIRE_POINTER(internal_rename);
    REQUIRE_POINTER(internal_rmdir);
    REQUIRE_POINTER(internal_set_metadata);
    REQUIRE_POINTER(fattime);
    REQUIRE_POINTER(portal_path);
    REQUIRE_POINTER(namespace_changed);
    REQUIRE_POINTER(internal_open);
    REQUIRE_POINTER(internal_read);
    REQUIRE_POINTER(internal_write);
    REQUIRE_POINTER(internal_seek);
    REQUIRE_POINTER(internal_sync);
    REQUIRE_POINTER(internal_close);
#undef REQUIRE_POINTER
    api.fattime = CODE(fattime, 0x10001001u);
    assert(resident_check_api(&api, build_id) == RESIDENT_COMPATIBLE);
    api.fattime = CODE(fattime, 0x10001003u);
    assert(resident_check_api(&api, build_id) == RESIDENT_COMPATIBLE);
    api.fattime = CODE(fattime, 0x10001002u);
    assert(resident_check_api(&api, build_id) == RESIDENT_CONFLICT);
    api.fattime = CODE(fattime, 0x14000000u);
    assert(resident_check_api(&api, build_id) == RESIDENT_CONFLICT);
    api.fattime = CODE(fattime, 0x0ffffffcu);
    assert(resident_check_api(&api, build_id) == RESIDENT_CONFLICT);
#if UINTPTR_MAX > UINT32_MAX
    api.fattime = CODE(fattime, UINT64_C(0x110001000));
    assert(resident_check_api(&api, build_id) == RESIDENT_CONFLICT);
#endif
#undef CODE
}

int main(void) {
    assert(resident_ram_range(0x10000000u, 20));
    assert(resident_ram_range(0x13ffffecu, 20));
    assert(!resident_ram_range(0x13fffff0u, 20));
    assert(!resident_ram_range(0x10000001u, 4));
    assert(!resident_ram_range(0x0ffffffcu, 4));
    assert(!resident_ram_range(0x14000000u, 0));
    assert(!resident_ram_range(0xfffffffcu, 8));
    assert(!resident_ram_range(0x10000000u, SIZE_MAX));

    assert(resident_prologue(0xe92d41f0u, 0xe1a04000u, true));
    assert(!resident_prologue(0xe92d4010u, 0xe1a04000u, true));
    /* Synthetic non-CAS samples test the decoder, not firmware bytes. */
    assert(resident_prologue(0xe92d4010u, 0xe1a05001u, false));
    assert(!resident_prologue(0x192d4010u, 0xe1a05001u, false));
    assert(!resident_prologue(0xe92d0000u, 0xe1a05001u, false));
    assert(!resident_prologue(0xe92dc010u, 0xe1a05001u, false));
    assert(!resident_prologue(0xe92d6010u, 0xe1a05001u, false));
    assert(!resident_prologue(0xe8bd4010u, 0xe1a05001u, false));
    assert(!resident_prologue(0xe92d4010u, 0xe1b05001u, false));
    assert(!resident_prologue(0xe92d4010u, 0xe3a05001u, false));
    assert(!resident_prologue(0xe92d4010u, 0xe1a05081u, false));
    assert(!resident_prologue(0xe92d4010u, 0xe1a05f01u, false));
    assert(!resident_prologue(0xe92d4010u, 0xe1a0f001u, false));
    assert(!resident_prologue(0xe92d4010u, 0xe1a0500fu, false));
    test_api();
    puts("resident tests passed");
    return 0;
}
