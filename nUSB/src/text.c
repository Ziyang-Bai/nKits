#include "text.h"

size_t text_next(const char *text, uint32_t *scalar) {
    const unsigned char *p = (const unsigned char *)text;
    uint32_t value = p[0], minimum;
    size_t count, i;
    if (value < 0x80) {
        *scalar = value;
        return value ? 1 : 0;
    }
    *scalar = 0xfffd;
    if (value >= 0xc2 && value <= 0xdf) {
        count = 2; minimum = 0x80; value &= 0x1f;
    } else if (value >= 0xe0 && value <= 0xef) {
        count = 3; minimum = 0x800; value &= 0x0f;
    } else if (value >= 0xf0 && value <= 0xf4) {
        count = 4; minimum = 0x10000; value &= 7;
    } else {
        return 1;
    }
    for (i = 1; i < count; ++i) {
        if ((p[i] & 0xc0) != 0x80) return 1;
        value = (value << 6) | (p[i] & 0x3f);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) return 1;
    *scalar = value;
    return count;
}

size_t text_utf16(const char *text, uint16_t *out, size_t capacity) {
    size_t used = 0, count, units;
    uint32_t scalar;
    if (!capacity) return 0;
    while ((count = text_next(text, &scalar)) != 0) {
        units = scalar > 0xffff ? 2 : 1;
        if (units > capacity - 1 - used) break;
        used += text_put_utf16(scalar, out + used);
        text += count;
    }
    out[used] = 0;
    return used;
}
