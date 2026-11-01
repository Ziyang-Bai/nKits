#ifndef NUSB_RESIDENT_CHECK_H
#define NUSB_RESIDENT_CHECK_H

#include "resident.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool resident_ram_range(uint32_t addr, size_t size);
bool resident_prologue(uint32_t first, uint32_t second, bool cas);

typedef enum {
    RESIDENT_COMPATIBLE, RESIDENT_CONFLICT, RESIDENT_UPDATE,
    RESIDENT_BUSY, RESIDENT_UNAVAILABLE
} ResidentCheck;

ResidentCheck resident_check_api(const ResidentApi *api, const char *build_id);

#endif
