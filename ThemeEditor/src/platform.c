#include "platform.h"
#include "resident.h"

#include <string.h>

#define RAM_LO 0x10000000u
#define RAM_HI 0x14000000u
#define SYST 0x73797374u

static const uint16_t menu_caption[] = {'T','h','e','m','e',' ','E','d','i','t','o','r',0};

static bool range(uintptr_t p, size_t size) {
    return !(p & 3) && p >= RAM_LO && p < RAM_HI && size <= RAM_HI - p;
}

static bool wide_equal(const uint16_t *p, const char *text) {
    while (*text && *p == (unsigned char)*text) { ++p; ++text; }
    return !*text && !*p;
}

/* Both accessors bound-check the index and then load from the table literal.
   CX selects one of two columns; CX II removed the monochrome column. */
static bool accessor(uintptr_t table, unsigned count, bool cx2) {
    for (const uint32_t *p = (const uint32_t *)0x10001000u;
         p < (const uint32_t *)0x10a80000u; ++p) {
        if (*p != table) continue;
        if (cx2) {
            const uint32_t *q = p - 17;
            if (q[0] == 0xe92d4008u && q[1] == 0xe59f3028u &&
                q[2] == 0xe1500003u && q[3] == 0x9a000005u &&
                q[10] == 0xe59f1014u && q[11] == 0xe7910100u &&
                q[12] == 0xe8bd8008u && q[13] == count - 1) return true;
        } else {
            const uint32_t *q = p - 23;
            if (q[0] == 0xe59f3044u && q[1] == 0xe92d4010u &&
                q[2] == 0xe1500003u && q[4] == 0x9a000005u &&
                q[12] == 0xe59f1024u && q[13] == 0xe3500000u &&
                q[14] == 0x13a00004u && q[15] == 0x03a00000u &&
                q[16] == 0xe0804184u && q[17] == 0xe7940001u &&
                q[18] == 0xe8bd8010u && q[19] == count - 1) return true;
        }
    }
    return false;
}

bool theme_platform_open(ThemePlatform *platform, Theme *theme, const char **error) {
    uint32_t marker = *(volatile uint32_t *)0x10000020u;
    bool cx2 = marker == 0x10429ec0u || marker == 0x1042aee0u;
    const uint16_t *names = NULL;
    *error = "Unsupported OS theme table";
    if (hwtype() != 1 || is_cm || nl_ndless_rev() < 2022 ||
        (marker != 0x103b18e0u && marker != 0x103b1f80u && !cx2) ||
        (bool)is_cx2 != cx2) return false;
    memset(platform, 0, sizeof(*platform));
    memset(theme, 0, sizeof(*theme));
    for (const uint32_t *p = (const uint32_t *)0x10800000u;
         p < (const uint32_t *)0x11000000u; ++p) {
        if (*p == 0x00690044u && wide_equal((const uint16_t *)p, "DialogFillBackgroundScratchapps")) {
            if (names) return false;
            names = (const uint16_t *)p;
        }
    }
    if (!names) return false;
    const uint16_t *p = names;
    while (theme->count < THEME_MAX_ITEMS) {
        char *name = theme->names[theme->count];
        unsigned len = 0;
        while (range((uintptr_t)p & ~(uintptr_t)3, 4) && *p && len < THEME_NAME_SIZE - 1) {
            unsigned c = *p++;
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) return false;
            name[len++] = (char)c;
        }
        if (!len || *p || len == THEME_NAME_SIZE - 1) return false;
        ++theme->count;
        if (!strcmp(name, "NoItemUsed")) break;
        p = (const uint16_t *)(((uintptr_t)(p + 1) + 3) & ~(uintptr_t)3);
    }
    unsigned color_count = cx2 ? 454u : 399u;
    if (theme->count > color_count ||
        strcmp(theme->names[theme->count - 1], "NoItemUsed") ||
        strcmp(theme->names[1], "DialogFillBackground") ||
        strcmp(theme->names[2], "ShadowFirstLine")) return false;
    unsigned stride = cx2 ? 1 : 2;
    /* The accessor includes trailing unnamed colors; edit the named prefix. */
    uintptr_t table = (uintptr_t)names - color_count * stride * sizeof(uint32_t);
    if (!range(table, color_count * stride * 4) || !accessor(table, color_count, cx2)) return false;
    platform->palette = (volatile uint32_t *)table;
    platform->stride = stride;
    platform->marker = marker;
    theme_platform_read(platform, theme);
    *error = NULL;
    return true;
}

void theme_platform_read(const ThemePlatform *platform, Theme *theme) {
    for (unsigned i = 0; i < theme->count; ++i) {
        uint32_t color = platform->palette[i * platform->stride];
        theme->colors[i][0] = color;
        theme->colors[i][1] = platform->stride == 2 ? platform->palette[i * 2 + 1] : color;
    }
}

void theme_platform_apply(const ThemePlatform *platform, const Theme *theme) {
    unsigned mask = resident_lock();
    for (unsigned i = 0; i < theme->count; ++i) {
        platform->palette[i * platform->stride] = theme->colors[i][platform->stride == 1];
        if (platform->stride == 2) platform->palette[i * 2 + 1] = theme->colors[i][1];
    }
    resident_unlock(mask);
}

static bool caption_slot(ThemePlatform *platform) {
    const uint32_t *get = resident_native(e_get_res_string);
    /* The maintained SDK resolves the native resource accessor for this OS. */
    if (!range((uintptr_t)get, 0x12c) || get[0] != 0xe92d40f0u ||
        get[5] != 0xe59f610cu || !range(get[74], 4)) return false;
    uintptr_t registry = *(const uint32_t *)(uintptr_t)get[74];
    if (!range(registry, 8 * 64)) return false;
    const uint32_t *entries = (const uint32_t *)registry;
    uintptr_t strings = 0;
    for (unsigned i = 0; i < 64 && entries[i * 2]; ++i)
        if (entries[i * 2] == SYST) { strings = entries[i * 2 + 1]; break; }
    if (!range(strings, 4)) return false;
    uint32_t count = *(const uint32_t *)strings;
    if (count < 0x41 || count > 2000 || !range(strings, (count + 1) * 4)) return false;
    unsigned id = platform->stride == 1 ? 0x40 : 0x3e;
    volatile uint32_t *slot = (volatile uint32_t *)strings + 1 + id;
    uintptr_t base = strings + (count + 1) * 4;
    uintptr_t text = base + *slot * 2;
    if (!range(text & ~(uintptr_t)3, 64)) return false;
    if (platform->stride == 2) return true;
    /* CX II dropped the old Theme Editor caption. Borrow only the unused
       factory/debug menu caption, never a user-facing production string. */
    if (!wide_equal((const uint16_t *)text, "Build TP Menus")) return false;
    platform->caption_slot = slot;
    platform->caption_old = *slot;
    memcpy(platform->caption, menu_caption, sizeof(menu_caption));
    platform->caption_new = ((uint32_t)(uintptr_t)platform->caption - (uint32_t)base) / 2;
    return true;
}

static bool menu_find(ThemePlatform *platform, void (*callback)(void *, unsigned, unsigned)) {
    const uint32_t commands[] = {1, 2, 4, 5, 6, 7, 8, 9};
    const uint32_t *records = NULL;
    for (const uint32_t *p = (const uint32_t *)0x10a00000u;
         p < (const uint32_t *)0x10d00000u - 45; ++p) {
        if (p[0] != 1 || p[3] != 1 || p[4] != SYST ||
            p[5] != 1 || p[8] != 2 || p[9] != SYST) continue;
        unsigned i;
        for (i = 0; i < 8; ++i)
            if (p[i * 5] != 1 || p[i * 5 + 2] != p[2] ||
                p[i * 5 + 3] != commands[i] || p[i * 5 + 4] != SYST) break;
        if (i != 8 || p[40] || p[41] || p[42] || p[43] || p[44]) continue;
        if (!range(p[2], 0x98) || !range((uintptr_t)(p - 9), 9 * 4)) continue;
        if (records) return false;
        records = p;
    }
    if (!records) return false;
    uintptr_t base = (uintptr_t)(records - 9);
    volatile uint32_t *slot = NULL;
    for (uint32_t *p = (uint32_t *)0x10800000u + 87;
         p < (uint32_t *)0x10b00000u; ++p) {
        if (*p != base) continue;
        const uint32_t *q = p - 87;
        if (q[0] != 0xe92d4ff0u || q[1] != 0xe24dd00cu ||
            q[5] != 0xe59f7140u || q[7] != 0xe2876024u ||
            q[11] != 0xe5f65014u || q[15] != 0xe084a104u ||
            q[16] != 0xe087a10au) continue;
        if (slot) return false;
        slot = p;
    }
    if (!slot || !caption_slot(platform)) return false;
    memcpy(platform->menu, (const void *)base, (9 + 40) * 4);
    uint32_t *row = platform->menu + 49;
    row[0] = 1;
    row[1] = platform->stride == 1 ? 0x40 : 0x3e;
    row[2] = (uint32_t)(uintptr_t)callback;
    row[3] = 3;
    row[4] = SYST;
    memset(row + 5, 0, 5 * 4);
    platform->menu_slot = slot;
    platform->menu_old = *slot;
    return true;
}

bool theme_platform_menu(ThemePlatform *platform, bool enabled,
                         void (*callback)(void *, unsigned, unsigned), const char **error) {
    *error = "OS Settings menu changed; no hook installed";
    if (enabled == platform->menu_enabled) { *error = NULL; return true; }
    if (!platform->menu_slot && enabled && !menu_find(platform, callback)) return false;
    if (!platform->menu_slot) { *error = NULL; return true; }
    uint32_t ours = (uint32_t)(uintptr_t)platform->menu;
    uint32_t expected = enabled ? platform->menu_old : ours;
    unsigned mask = resident_lock();
    bool ok = *platform->menu_slot == expected;
    if (platform->caption_slot) {
        uint32_t old = enabled ? platform->caption_old : platform->caption_new;
        ok = ok && *platform->caption_slot == old;
    }
    if (ok) {
        if (platform->caption_slot)
            *platform->caption_slot = enabled ? platform->caption_new : platform->caption_old;
        *platform->menu_slot = enabled ? ours : platform->menu_old;
        platform->menu_enabled = enabled;
    }
    resident_unlock(mask);
    if (ok) *error = NULL;
    return ok;
}
