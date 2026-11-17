#include "crc32.h"

uint32_t crc32_update(uint32_t state, const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t i;
    int bit;

    for (i = 0; i < size; ++i) {
        state ^= bytes[i];
        for (bit = 0; bit < 8; ++bit) {
            state = (state >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(state & 1u));
        }
    }
    return state;
}

uint32_t crc32_finish(uint32_t state) {
    return state ^ 0xffffffffu;
}
