#include "explorer.h"

#include <string.h>

#define RAM_START 0x10000000u
#define CODE_END 0x11000000u
#define RAM_END 0x14000000u

extern uint32_t explorer_gate[], explorer_trampoline[5];
extern void explorer_original(void);
extern int resident_publish(volatile uint32_t *, uint32_t *, uint32_t, uint32_t);

static ResidentExplore (*explore_decide)(void);
static volatile char *documents;

static bool code(uintptr_t address, unsigned size) {
    return !(address & 3) && address >= RAM_START && address < CODE_END && size <= CODE_END - address;
}

static uint32_t *branch(uint32_t *instruction, uint32_t opcode) {
    if (!code((uintptr_t)instruction, 4) || (*instruction & 0xff000000u) != opcode) return NULL;
    uintptr_t address = (uintptr_t)instruction + 8 + ((int32_t)(*instruction << 8) >> 6);
    return code(address, 4) ? (uint32_t *)address : NULL;
}

void explorer_open(void) {
    ResidentExplore action = explore_decide();
    if (action == RESIDENT_EXPLORE_DENY) {
        documents[1] = 'd';
        return;
    }
    /* Native file actions retain this base until the next explorer visit. */
    documents[1] = action == RESIDENT_EXPLORE_ROOT ? 0 : 'd';
    explorer_original();
}

bool explorer_install(uint32_t marker, const uint32_t *natives,
                      ResidentExplore (*decide)(void), const char **error) {
    *error = "Native explorer unsupported; no explorer changes made";
    if (!decide || !natives || marker != *(const volatile uint32_t *)0x10000020u ||
        (marker != 0x103b18e0u && marker != 0x103b1f80u &&
         marker != 0x10429ec0u && marker != 0x1042aee0u)) return false;
    const uint32_t *refresh = (const uint32_t *)natives[e_refresh_docbrowser];
    if (!code((uintptr_t)refresh, 0x200) || refresh[0] != 0xe92d41f0u ||
        refresh[1] != 0xe59f4610u || refresh[2] != 0xe59f2610u) return false;
    uint32_t *entry = branch((uint32_t *)refresh + 0x1fc / 4, 0xeb000000u);
    if (!entry || !code((uintptr_t)entry, 0x88) ||
        entry[0] != 0xe59f3078u || entry[1] != 0xe92d4070u ||
        entry[2] != 0xe8930003u || entry[3] != 0xe24dd008u ||
        entry[4] != 0xe58d0000u || entry[5] != 0xe1cd10b4u ||
        entry[12] != 0xe59f504cu || entry[14] != 0xe59c66e4u ||
        entry[16] != 0xe5860010u || entry[24] != 0xe3a00004u ||
        entry[30] != 0xe28dd008u || entry[31] != 0xe8bd8070u) return false;
    uint32_t *getter = branch((uint32_t *)natives[e_get_documents_dir], 0xea000000u);
    if (!getter || !code((uintptr_t)getter, 12) ||
        getter[0] != 0xe59f0000u || getter[1] != 0xe12fff1eu) return false;
    uintptr_t path = getter[2];
    if (path < RAM_START || path >= RAM_END - 12 ||
        memcmp((const void *)path, "/documents/", 12)) return false;
    /* Relocate the opener's PC-relative load, not its instruction bytes. */
    explorer_trampoline[0] = 0xe59f3004u;
    explorer_trampoline[1] = entry[1];
    explorer_trampoline[2] = 0xe59ff000u;
    explorer_trampoline[3] = entry[32];
    explorer_trampoline[4] = (uint32_t)(entry + 2);
    documents = (volatile char *)path;
    explore_decide = decide;
    if (!resident_publish(entry, explorer_gate, 0xe59f3078u, 0xe92d4070u)) {
        documents = NULL;
        explore_decide = NULL;
        *error = "Explorer hook changed; no explorer changes made";
        return false;
    }
    *error = NULL;
    return true;
}
