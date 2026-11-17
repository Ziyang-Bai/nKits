#ifndef NTEXTS_APP_LANGUAGE_H
#define NTEXTS_APP_LANGUAGE_H

#include <stddef.h>
#include <stdint.h>

int copy_u16(uint16_t *dst, int cap, const uint16_t *src);
void shorten_tail(char *out, size_t cap, const char *text);
const char *base_name(const char *path);

#endif
