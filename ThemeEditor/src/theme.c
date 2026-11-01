#include "theme.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool hex_color(const char **text, const char *end, uint32_t *value) {
    const char *p = *text;
    unsigned digits = 0;
    uint32_t color = 0;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (end - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while (p < end && isxdigit((unsigned char)*p)) {
        unsigned c = (unsigned char)*p++;
        c = c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
        if (++digits > 8) return false;
        color = (color << 4) | c;
    }
    if (!digits || (p < end && !isspace((unsigned char)*p))) return false;
    *text = p;
    *value = color;
    return true;
}

bool theme_decode(Theme *theme, const char *data, size_t size) {
    uint32_t colors[THEME_MAX_ITEMS][2];
    bool seen[THEME_MAX_ITEMS] = {false};
    unsigned matched = 0;
    const char *p = data, *end = data + size;
    if (!theme->count || theme->count > THEME_MAX_ITEMS) return false;
    memcpy(colors, theme->colors, sizeof(colors));
    while (p < end) {
        const char *line = p, *name;
        size_t len;
        unsigned i;
        while (p < end && *p != '\n') ++p;
        const char *eol = p;
        if (p < end) ++p;
        while (line < eol && isspace((unsigned char)*line)) ++line;
        if (line == eol || *line == '#') continue;
        name = line;
        while (line < eol && !isspace((unsigned char)*line)) ++line;
        len = (size_t)(line - name);
        for (i = 0; i < theme->count; ++i)
            if (strlen(theme->names[i]) == len && !memcmp(name, theme->names[i], len)) break;
        if (i == theme->count) continue;
        if (seen[i] || !hex_color(&line, eol, &colors[i][0]) ||
            !hex_color(&line, eol, &colors[i][1])) return false;
        while (line < eol && isspace((unsigned char)*line)) ++line;
        if (line != eol && *line != '#') return false;
        seen[i] = true;
        ++matched;
    }
    if (!matched) return false;
    memcpy(theme->colors, colors, sizeof(colors));
    return true;
}

int theme_line(const Theme *theme, unsigned index, char *out, size_t size) {
    if (index >= theme->count) return -1;
    return snprintf(out, size, "%s 0x%08lX 0x%08lX\n", theme->names[index],
                    (unsigned long)theme->colors[index][0],
                    (unsigned long)theme->colors[index][1]);
}

void theme_invert(Theme *theme, unsigned first, unsigned count, unsigned mode) {
    if (mode > 1 || first >= theme->count || count > theme->count - first) return;
    for (unsigned i = first; i < first + count; ++i) theme->colors[i][mode] ^= 0xffffff;
}

void theme_random(Theme *theme, unsigned first, unsigned count, unsigned mode, uint32_t *seed) {
    if (mode > 1 || first >= theme->count || count > theme->count - first) return;
    for (unsigned i = first; i < first + count; ++i) {
        *seed = *seed * 1664525u + 1013904223u;
        theme->colors[i][mode] = *seed & 0xffffff;
    }
}

uint32_t theme_hash(const Theme *theme) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < theme->count; ++i)
        for (const unsigned char *p = (const unsigned char *)theme->names[i]; ; ++p) {
            hash = (hash ^ *p) * 16777619u;
            if (!*p) break;
        }
    return hash;
}
