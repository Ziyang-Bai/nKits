#include "app_language.h"
#include <stdio.h>
#include <string.h>


int copy_u16(uint16_t *dst, int cap, const uint16_t *src) {
    int n = 0;
    if (!cap) return 0;
    while (n + 1 < cap && src && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
    return n;
}


void shorten_tail(char *out, size_t cap, const char *text) {
    size_t len;
    if (!cap) return;
    if (!text) {
        out[0] = 0;
        return;
    }
    len = strlen(text);
    if (len + 1 <= cap) {
        memcpy(out, text, len + 1);
        return;
    }
    if (cap <= 4) {
        out[0] = 0;
        return;
    }
    snprintf(out, cap, "...%s", text + len - (cap - 4));
}

const char *base_name(const char *path) {
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}
