#include "protect.h"

#include <assert.h>
#include <string.h>

static void combinations(void) {
    ProtectCombo combo;
    assert(protect_parse(&combo, " ctrl + shift + a "));
    assert(combo.count == 3 && combo.keys[0] == PROTECT_KEY_CTRL &&
        combo.keys[1] == PROTECT_KEY_SHIFT && combo.keys[2] == PROTECT_KEY_A);
    ProtectCombo before = combo;
    static const char *const bad[] = {"", " ", "+A", "A+", "A++B", "A+A", "A+a", "CTRL+SHIFT+A+B+C", "HOME", "ENTER", "A B", "ABCDEFGHIJKLMNOP", "A\n"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!protect_parse(&combo, bad[i]));
        assert(!memcmp(&combo, &before, sizeof(combo)));
    }
    assert(protect_parse(&combo, "EQU"));
    assert(combo.count == 1 && combo.keys[0] == PROTECT_KEY_EQU);
    assert(protect_parse(&combo, "Z+9+DEL+TAB"));
    assert(combo.count == PROTECT_MAX_KEYS && combo.keys[0] == PROTECT_KEY_Z &&
        combo.keys[1] == PROTECT_KEY_9 && combo.keys[2] == PROTECT_KEY_DEL &&
        combo.keys[3] == PROTECT_KEY_TAB);
}

static void configuration(void) {
    Protect protect, decoded;
    char data[PROTECT_CONFIG_SIZE], text[PROTECT_CONFIG_SIZE];
    protect_defaults(&protect);
    assert(protect_parse(&protect.combo, "CTRL+SHIFT+A"));
    protect.enabled = false;
    size_t size = protect_encode(&protect, data);
    protect_defaults(&decoded);
    assert(protect_decode(&decoded, data, size));
    assert(!decoded.enabled);
    protect_format(&decoded.combo, text);
    assert(!strcmp(text, "CTRL+SHIFT+A"));
    Protect before = decoded;
    for (size_t i = 0; i < size; ++i) {
        assert(!protect_decode(&decoded, data, i));
        assert(!memcmp(&decoded, &before, sizeof(decoded)));
    }
    static const char *const bad[] = {
        "nProtect2\n1\n=\n", "nProtect1\n2\n=\n", "nProtect1\n1\n\n",
        "nProtect1\n1\nA+A\n", "nProtect1\n1\n=\nextra", "nProtect1\n1\n=\n\n"
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!protect_decode(&decoded, bad[i], strlen(bad[i])));
        assert(!memcmp(&decoded, &before, sizeof(decoded)));
    }
    data[12] = 0;
    assert(!protect_decode(&decoded, data, size));
    memset(data, 'A', sizeof(data));
    assert(!protect_decode(&decoded, data, sizeof(data)));
}

int main(void) {
    combinations();
    configuration();
    return 0;
}
