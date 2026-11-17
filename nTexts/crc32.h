#ifndef NTEXTS_CRC32_H
#define NTEXTS_CRC32_H

#include <stddef.h>
#include <stdint.h>

#define CRC32_INITIAL 0xffffffffu

uint32_t crc32_update(uint32_t state, const void *data, size_t size);
uint32_t crc32_finish(uint32_t state);

#endif
