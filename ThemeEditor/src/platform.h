#ifndef NKITS_THEME_PLATFORM_H
#define NKITS_THEME_PLATFORM_H

#include "theme.h"

typedef struct {
    volatile uint32_t *palette;
    unsigned stride;
    uint32_t marker;
    volatile uint32_t *menu_slot;
    uint32_t menu_old;
    uint32_t menu[64];
    volatile uint32_t *caption_slot;
    uint32_t caption_old, caption_new;
    uint16_t caption[13];
    bool menu_enabled;
} ThemePlatform;

bool theme_platform_open(ThemePlatform *platform, Theme *theme, const char **error);
void theme_platform_read(const ThemePlatform *platform, Theme *theme);
void theme_platform_apply(const ThemePlatform *platform, const Theme *theme);
bool theme_platform_menu(ThemePlatform *platform, bool enabled,
                         void (*callback)(void *, unsigned, unsigned), const char **error);

#endif
