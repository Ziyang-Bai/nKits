#include "protect.h"

#include <stdio.h>
#include <string.h>

static const char *const names[] = {
#define PROTECT_NAME(key, name) name,
    PROTECT_KEYS(PROTECT_NAME)
#undef PROTECT_NAME
};

const char *protect_key_name(ProtectKey key) {
    return (unsigned)key < PROTECT_KEY_COUNT ? names[key] : "?";
}

void protect_defaults(Protect *protect) {
    memset(protect, 0, sizeof(*protect));
    protect->enabled = true;
    protect->combo.count = 1;
    protect->combo.keys[0] = PROTECT_KEY_EQU;
}

bool protect_parse(ProtectCombo *combo, const char *text) {
    ProtectCombo parsed = {0};
    uint64_t used = 0;
    for (;;) {
        char name[16];
        unsigned length = 0, key;
        while (*text == ' ' || *text == '\t') ++text;
        while (*text && *text != '+' && *text != ' ' && *text != '\t') {
            unsigned char ch = (unsigned char)*text++;
            if (length + 1 >= sizeof(name)) return false;
            name[length++] = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : (char)ch;
        }
        name[length] = 0;
        if (!length || parsed.count == PROTECT_MAX_KEYS) return false;
        if (!strcmp(name, "EQU")) strcpy(name, "=");
        for (key = 0; key < PROTECT_KEY_COUNT; ++key)
            if (!strcmp(name, names[key])) break;
        if (key == PROTECT_KEY_COUNT || (used & ((uint64_t)1 << key))) return false;
        used |= (uint64_t)1 << key;
        parsed.keys[parsed.count++] = (ProtectKey)key;
        while (*text == ' ' || *text == '\t') ++text;
        if (!*text) break;
        if (*text++ != '+') return false;
    }
    *combo = parsed;
    return true;
}

void protect_format(const ProtectCombo *combo, char text[PROTECT_CONFIG_SIZE]) {
    text[0] = 0;
    for (unsigned i = 0; i < combo->count; ++i) {
        if (i) strcat(text, "+");
        strcat(text, protect_key_name(combo->keys[i]));
    }
}

bool protect_decode(Protect *protect, const char *data, size_t size) {
    Protect parsed;
    char text[PROTECT_CONFIG_SIZE];
    static const char header[] = "nProtect1\n";
    size_t prefix = sizeof(header) - 1;
    if (size <= prefix + 3 || size >= sizeof(text) ||
        memcmp(data, header, prefix) || (data[prefix] != '0' && data[prefix] != '1') ||
        data[prefix + 1] != '\n' || data[size - 1] != '\n' || memchr(data, 0, size)) return false;
    memcpy(text, data + prefix + 2, size - prefix - 3);
    text[size - prefix - 3] = 0;
    if (!protect_parse(&parsed.combo, text)) return false;
    parsed.enabled = data[prefix] == '1';
    *protect = parsed;
    return true;
}

size_t protect_encode(const Protect *protect, char data[PROTECT_CONFIG_SIZE]) {
    char combo[PROTECT_CONFIG_SIZE];
    protect_format(&protect->combo, combo);
    return (size_t)snprintf(data, PROTECT_CONFIG_SIZE, "nProtect1\n%d\n%s\n", protect->enabled ? 1 : 0, combo);
}
