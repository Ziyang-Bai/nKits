#include "theme.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static Theme theme;

static void init(void) {
    memset(&theme, 0, sizeof(theme));
    theme.count = 3;
    strcpy(theme.names[0], "DialogFillBackground");
    strcpy(theme.names[1], "HomeScreenBackground");
    strcpy(theme.names[2], "NoItemUsed");
    theme.colors[0][0] = 0xffffff;
    theme.colors[0][1] = 0xe0e0e0;
    theme.colors[1][0] = 0x123456;
    theme.colors[1][1] = 0xabcdef;
}

static bool decode(const char *text) { return theme_decode(&theme, text, strlen(text)); }

static void compatibility(void) {
    init();
    assert(decode("# theme\r\nHomeScreenBackground 0x00000000 00FFFFFF\r\n"
                  "FutureOSItem 0x00abcdef 0x00123456\n"
                  "DialogFillBackground 0x00123456 0X00AbCdEf"));
    assert(theme.colors[0][0] == 0x123456 && theme.colors[0][1] == 0xabcdef);
    assert(theme.colors[1][0] == 0 && theme.colors[1][1] == 0xffffff);
    assert(theme.colors[2][0] == 0 && theme.colors[2][1] == 0);
}

static void transactional(void) {
    static const char *const bad[] = {
        "", "# empty\n", "OtherOSItem 0 0\n",
        "HomeScreenBackground 123456789 FFFFFF\n",
        "HomeScreenBackground -1 0\n",
        "HomeScreenBackground 0x 1\n",
        "HomeScreenBackground 123456\n",
        "HomeScreenBackground 1 2 trailing\n",
        "HomeScreenBackground 1 2\nHomeScreenBackground 3 4\n",
        "DialogFillBackground 1 2\nHomeScreenBackground 0 GGGGGG\n"
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        init();
        uint32_t before[THEME_MAX_ITEMS][2];
        memcpy(before, theme.colors, sizeof(before));
        assert(!decode(bad[i]));
        assert(!memcmp(before, theme.colors, sizeof(before)));
    }
}

static void roundtrip(void) {
    char data[512];
    unsigned used = 0;
    init();
    /* CX II's TableHeaderBackgroundRow2 uses this native sentinel color. */
    theme.colors[1][1] = 0x0fffffff;
    uint32_t before[THEME_MAX_ITEMS][2];
    memcpy(before, theme.colors, sizeof(before));
    uint32_t hash = theme_hash(&theme);
    for (unsigned i = 0; i < theme.count; ++i) {
        int n = theme_line(&theme, i, data + used, sizeof(data) - used);
        assert(n > 0 && (unsigned)n < sizeof(data) - used);
        used += (unsigned)n;
    }
    memset(theme.colors, 0, sizeof(theme.colors));
    assert(theme_decode(&theme, data, used));
    assert(!memcmp(before, theme.colors, sizeof(before)));
    assert(theme_hash(&theme) == hash);
    theme.names[1][0] = 'h';
    assert(theme_hash(&theme) != hash);
    assert(theme_line(&theme, theme.count, data, sizeof(data)) == -1);
}

static void transforms(void) {
    init();
    theme_invert(&theme, 1, 1, 1);
    assert(theme.colors[1][1] == 0x543210);
    assert(theme.colors[1][0] == 0x123456 && theme.colors[0][1] == 0xe0e0e0);
    theme_invert(&theme, 1, 1, 1);
    assert(theme.colors[1][1] == 0xabcdef);
    uint32_t before[THEME_MAX_ITEMS][2];
    memcpy(before, theme.colors, sizeof(before));
    theme_invert(&theme, 3, 1, 1);
    theme_invert(&theme, 2, 2, 1);
    theme_invert(&theme, 0, 3, 2);
    assert(!memcmp(before, theme.colors, sizeof(before)));
    uint32_t seed = 0;
    theme_random(&theme, 1, 2, 1, &seed);
    assert(seed != 0);
    assert(theme.colors[0][1] == before[0][1]);
    assert(theme.colors[1][0] == before[1][0]);
    assert(theme.colors[1][1] <= 0xffffff && theme.colors[2][1] <= 0xffffff);
    uint32_t after = seed;
    theme_random(&theme, 2, 2, 1, &seed);
    assert(seed == after);
}

int main(void) {
    compatibility(); transactional(); roundtrip(); transforms();
    puts("theme format/state tests passed");
    return 0;
}
