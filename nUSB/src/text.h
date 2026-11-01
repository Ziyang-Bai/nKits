#ifndef NUSB_TEXT_H
#define NUSB_TEXT_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned (*TextWidth)(uint32_t scalar);

/* Decode one scalar; malformed input advances one byte and yields U+FFFD. */
size_t text_next(const char *text, uint32_t *scalar);
/* Capacity includes NUL; supplementary scalars occupy complete surrogate pairs. */
size_t text_utf16(const char *text, uint16_t *out, size_t capacity);

/* Valid scalar; out has room for two units. The caller appends NUL. */
static inline size_t text_put_utf16(uint32_t scalar, uint16_t *out) {
    if (scalar > 0xffff) {
        scalar -= 0x10000;
        out[0] = (uint16_t)(0xd800 | (scalar >> 10));
        out[1] = (uint16_t)(0xdc00 | (scalar & 0x3ff));
        return 2;
    }
    out[0] = (uint16_t)scalar;
    return 1;
}

#endif
