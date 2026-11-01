#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_next(void) {
    static const struct {
        const char *text;
        uint32_t scalar;
    } valid[] = {
        {"", 0}, {"A", 'A'}, {"\x7f", 0x7f},
        {"\xc2\x80", 0x80}, {"\xdf\xbf", 0x7ff},
        {"\xe0\xa0\x80", 0x800}, {"\xed\x9f\xbf", 0xd7ff},
        {"\xee\x80\x80", 0xe000}, {"\xef\xbf\xbd", 0xfffd},
        {"\xef\xbf\xbf", 0xffff}, {"\xf0\x90\x80\x80", 0x10000},
        {"\xf0\x9f\x98\x80", 0x1f600}, {"\xf4\x8f\xbf\xbf", 0x10ffff}
    };
    static const char *invalid[] = {
        "\x80", "\xbf", "\xc0\xaf", "\xc1\xbf", "\xc2", "\xc2x",
        "\xe0\x80\xaf", "\xe2", "\xe2\x82", "\xe2\x82x",
        "\xed\xa0\x80", "\xed\xbf\xbf", "\xf0\x80\x80\xaf",
        "\xf0", "\xf0\x9f", "\xf0\x9f\x98", "\xf0\x9f\x98x",
        "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xfe", "\xff"
    };
    uint32_t scalar;
    size_t i;
    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        CHECK(text_next(valid[i].text, &scalar) == strlen(valid[i].text));
        CHECK(scalar == valid[i].scalar);
    }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(text_next(invalid[i], &scalar) == 1);
        CHECK(scalar == 0xfffd);
    }
}

static void test_utf16(void) {
    static const char text[] = "A\xe8\xb5\x84\xf0\x9f\x98\x80" "B";
    static const uint16_t expected[] = {'A', 0x8d44, 0xd83d, 0xde00, 'B', 0};
    static const size_t lengths[] = {0, 0, 1, 2, 2, 4, 5, 5};
    uint16_t out[8];
    size_t capacity, i, used;
    CHECK(text_utf16(text, NULL, 0) == 0);
    for (capacity = 0; capacity <= 7; ++capacity) {
        for (i = 0; i < 8; ++i) out[i] = 0x1234;
        used = text_utf16(text, out, capacity);
        CHECK(used == lengths[capacity]);
        CHECK(!memcmp(out, expected, used * sizeof(*out)));
        if (capacity) CHECK(out[used] == 0);
        CHECK(out[capacity] == 0x1234);
    }
    CHECK(text_utf16("\xf0\x90\x80\x80", out, 2) == 0 && out[0] == 0);
    CHECK(text_utf16("\xf0\x90\x80\x80", out, 3) == 2);
    CHECK(out[0] == 0xd800 && out[1] == 0xdc00 && out[2] == 0);
    CHECK(text_utf16("\xf4\x8f\xbf\xbf", out, 3) == 2);
    CHECK(out[0] == 0xdbff && out[1] == 0xdfff && out[2] == 0);
    CHECK(text_utf16("\xe2\x82" "A", out, 8) == 3);
    CHECK(out[0] == 0xfffd && out[1] == 0xfffd && out[2] == 'A' && out[3] == 0);
    CHECK(text_utf16("", out, 8) == 0 && out[0] == 0);
}

int main(void) {
    test_next();
    test_utf16();
    puts("text tests passed");
    return 0;
}
