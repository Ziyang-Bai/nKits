#ifndef NKITS_THEME_H
#define NKITS_THEME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THEME_MAX_ITEMS 512
#define THEME_NAME_SIZE 64

typedef struct {
    unsigned count;
    char names[THEME_MAX_ITEMS][THEME_NAME_SIZE];
    uint32_t colors[THEME_MAX_ITEMS][2];
} Theme;

/* Unknown names allow themes to move between OS versions. A bad known entry
   rejects the whole file; callers decode into a working copy before applying. */
bool theme_decode(Theme *theme, const char *data, size_t size);
int theme_line(const Theme *theme, unsigned index, char *out, size_t size);
void theme_invert(Theme *theme, unsigned first, unsigned count, unsigned mode);
void theme_random(Theme *theme, unsigned first, unsigned count, unsigned mode,
                  uint32_t *seed);
uint32_t theme_hash(const Theme *theme);

#endif
