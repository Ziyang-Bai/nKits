#include "name_editor.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static NameEditResult action(NameEditor *editor, InputAction key) {
    InputEvent event = {key, 0};
    return name_editor_handle(editor, event);
}

static void character(NameEditor *editor, uint32_t scalar) {
    InputEvent event = {INPUT_CHARACTER, scalar};
    CHECK(name_editor_handle(editor, event) == NAME_EDITING);
}

static void hex(NameEditor *editor, const char *digits) {
    CHECK(action(editor, INPUT_CATALOG) == NAME_EDITING);
    CHECK(editor->hex_mode);
    while (*digits) character(editor, (unsigned char)*digits++);
}

static unsigned width(uint32_t scalar) {
    if (scalar == 'X') return UINT_MAX;
    if (scalar == 'A') return 3;
    if (scalar == 'B') return 5;
    if (scalar == 0x301) return 0;
    if (scalar > 0xffff) return 14;
    return 10;
}

static void test_edit(void) {
    NameEditor editor;
    char original[] = "A\xe8\xb5\x84\xf0\x9f\x98\x80.txt";
    CHECK(name_editor_init(&editor, original));
    CHECK(editor.text != original);
    CHECK(!strcmp(editor.text, original));
    CHECK(editor.cursor == strlen(original));
    CHECK(name_editor_units(&editor) == 8);
    CHECK(name_editor_scalar(&editor) == 0);
    CHECK(action(&editor, INPUT_HOME) == NAME_EDITING);
    CHECK(name_editor_scalar(&editor) == 'A');
    action(&editor, INPUT_LEFT);
    CHECK(editor.cursor == 0);
    action(&editor, INPUT_RIGHT);
    CHECK(editor.cursor == 1);
    CHECK(name_editor_scalar(&editor) == 0x8d44);
    action(&editor, INPUT_RIGHT);
    CHECK(editor.cursor == 4);
    CHECK(name_editor_scalar(&editor) == 0x1f600);
    action(&editor, INPUT_RIGHT);
    CHECK(editor.cursor == 8);
    action(&editor, INPUT_LEFT);
    CHECK(editor.cursor == 4);
    character(&editor, 0x1f642);
    CHECK(editor.cursor == 8);
    CHECK(!strcmp(editor.text, "A\xe8\xb5\x84\xf0\x9f\x99\x82\xf0\x9f\x98\x80.txt"));
    CHECK(name_editor_units(&editor) == 10);
    action(&editor, INPUT_DELETE);
    CHECK(!strcmp(editor.text, "A\xe8\xb5\x84\xf0\x9f\x99\x82.txt"));
    CHECK(editor.cursor == 8);
    action(&editor, INPUT_BACKSPACE);
    CHECK(editor.cursor == 4);
    CHECK(!strcmp(editor.text, "A\xe8\xb5\x84.txt"));
    action(&editor, INPUT_LEFT);
    CHECK(editor.cursor == 1);
    action(&editor, INPUT_DELETE);
    CHECK(!strcmp(editor.text, "A.txt"));
    character(&editor, 0xe9);
    CHECK(!strcmp(editor.text, "A\xc3\xa9.txt"));
    CHECK(editor.cursor == 3);
    action(&editor, INPUT_HOME);
    action(&editor, INPUT_BACKSPACE);
    CHECK(editor.cursor == 0);
    action(&editor, INPUT_END);
    action(&editor, INPUT_RIGHT);
    action(&editor, INPUT_DELETE);
    CHECK(editor.cursor == strlen(editor.text));
    CHECK(action(&editor, INPUT_ENTER) == NAME_ACCEPTED);
    CHECK(!strcmp(original, "A\xe8\xb5\x84\xf0\x9f\x98\x80.txt"));
    name_editor_destroy(&editor);
    CHECK(!editor.text);
    CHECK(!editor.cursor);
    name_editor_destroy(&editor);
}

static void test_invalid(void) {
    static const uint32_t scalars[] = {0, 0xd800, 0xdfff, 0x110000, 0xffffffff};
    static const char *invalid_utf8[] = {
        "\x80", "\xc0\xaf", "\xc1\xbf", "\xc2", "\xe2\x82", "\xe2x",
        "\xe0\x80\xaf", "\xed\xa0\x80", "\xed\xbf\xbf",
        "\xf0\x80\x80\xaf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
        "\xf0\x9f\x98"
    };
    static const char *invalid_names[] = {
        "", ".", "..", "name.", "name ", "a/b", "a\\b", "a:b", "a*b",
        "a?b", "a\"b", "a<b", "a>b", "a|b", "a\x01" "b", "a\x1f" "b", "a\x7f" "b"
    };
    static const char forbidden[] = "\"*/:<>?\\|\x01\x1f\x7f";
    NameEditor editor;
    size_t i;
    CHECK(name_editor_init(&editor, "ok"));
    for (i = 0; i < sizeof(scalars) / sizeof(scalars[0]); ++i) {
        character(&editor, scalars[i]);
        CHECK(!strcmp(editor.text, "ok"));
        CHECK(editor.cursor == 2);
        CHECK(editor.error[0]);
    }
    for (i = 0; forbidden[i]; ++i) {
        character(&editor, (unsigned char)forbidden[i]);
        CHECK(!strcmp(editor.text, "ok"));
        CHECK(editor.error[0]);
    }
    action(&editor, INPUT_NONE);
    CHECK(editor.error[0]);
    character(&editor, '!');
    CHECK(!editor.error[0]);
    CHECK(!strcmp(editor.text, "ok!"));
    name_editor_destroy(&editor);
    for (i = 0; i < sizeof(invalid_utf8) / sizeof(invalid_utf8[0]); ++i) {
        CHECK(!name_editor_init(&editor, invalid_utf8[i]));
        CHECK(!editor.text);
        CHECK(editor.error[0]);
        name_editor_destroy(&editor);
    }
    for (i = 0; i < sizeof(invalid_names) / sizeof(invalid_names[0]); ++i) {
        CHECK(name_editor_init(&editor, invalid_names[i]));
        CHECK(action(&editor, INPUT_ENTER) == NAME_EDITING);
        CHECK(editor.error[0]);
        CHECK(action(&editor, INPUT_BACK) == NAME_CANCELLED);
        CHECK(!strcmp(editor.text, invalid_names[i]));
        name_editor_destroy(&editor);
    }
    CHECK(name_editor_init(&editor, "name."));
    action(&editor, INPUT_BACKSPACE);
    CHECK(action(&editor, INPUT_ENTER) == NAME_ACCEPTED);
    name_editor_destroy(&editor);
    CHECK(name_editor_init(&editor, "\xef\xbf\xbd"));
    CHECK(action(&editor, INPUT_ENTER) == NAME_ACCEPTED);
    CHECK(!strcmp(editor.text, "\xef\xbf\xbd"));
    name_editor_destroy(&editor);
}

static void test_limits(void) {
    NameEditor editor;
    char text[769], out[768];
    size_t i;
    unsigned x;
    memset(text, 'x', 253);
    text[253] = '\0';
    CHECK(name_editor_init(&editor, text));
    character(&editor, 0x1f600);
    CHECK(name_editor_units(&editor) == 255);
    CHECK(strlen(editor.text) == 257);
    character(&editor, 'x');
    CHECK(strlen(editor.text) == 257);
    CHECK(editor.error[0]);
    CHECK(action(&editor, INPUT_ENTER) == NAME_ACCEPTED);
    action(&editor, INPUT_BACKSPACE);
    character(&editor, 'x');
    character(&editor, 0x1f600);
    CHECK(name_editor_units(&editor) == 254);
    CHECK(editor.error[0]);
    name_editor_destroy(&editor);
    for (i = 0; i < 256; ++i) memcpy(text + i * 3, "\xe8\xb5\x84", 3);
    text[765] = '\0';
    CHECK(name_editor_init(&editor, text));
    CHECK(strlen(editor.text) == 765);
    CHECK(name_editor_units(&editor) == 255);
    name_editor_view(&editor, out, sizeof(out), 2550, width, &x);
    CHECK(!strcmp(out, text) && x == 2550 && editor.cursor == 765);
    name_editor_view(&editor, out, sizeof(out), 309, width, &x);
    CHECK(!strcmp(out, text + 675) && x == 300 && editor.cursor == 765);
    CHECK(!strcmp(editor.text, text));
    character(&editor, 'a');
    CHECK(strlen(editor.text) == 765);
    CHECK(editor.error[0]);
    CHECK(action(&editor, INPUT_ENTER) == NAME_ACCEPTED);
    name_editor_destroy(&editor);
    memcpy(text + 765, "\xe8\xb5\x84", 3);
    text[768] = '\0';
    CHECK(!name_editor_init(&editor, text));
    name_editor_destroy(&editor);
    memset(text, 'x', 256);
    text[256] = '\0';
    CHECK(!name_editor_init(&editor, text));
    name_editor_destroy(&editor);
    for (i = 0; i < 128; ++i) memcpy(text + i * 4, "\xf0\x9f\x98\x80", 4);
    text[512] = '\0';
    CHECK(!name_editor_init(&editor, text));
    name_editor_destroy(&editor);
}

static void test_hex(void) {
    NameEditor editor;
    CHECK(name_editor_init(&editor, ""));
    hex(&editor, "1f600");
    CHECK(!strcmp(editor.hex, "1F600"));
    CHECK(action(&editor, INPUT_ENTER) == NAME_EDITING);
    CHECK(!editor.hex_mode);
    CHECK(!strcmp(editor.text, "\xf0\x9f\x98\x80"));
    CHECK(editor.cursor == 4);
    hex(&editor, "10ffff");
    action(&editor, INPUT_ENTER);
    CHECK(!editor.hex_mode);
    CHECK(!strcmp(editor.text, "\xf0\x9f\x98\x80\xf4\x8f\xbf\xbf"));
    hex(&editor, "");
    action(&editor, INPUT_ENTER);
    CHECK(editor.hex_mode && editor.error[0]);
    character(&editor, 'g');
    CHECK(!editor.hex[0] && editor.error[0]);
    character(&editor, 'D');
    character(&editor, '8');
    character(&editor, '0');
    character(&editor, '0');
    action(&editor, INPUT_ENTER);
    CHECK(editor.hex_mode && editor.error[0]);
    CHECK(editor.cursor == 8);
    CHECK(action(&editor, INPUT_BACK) == NAME_EDITING);
    CHECK(!editor.hex_mode);
    CHECK(!editor.hex[0]);
    hex(&editor, "110000");
    character(&editor, '0');
    CHECK(!strcmp(editor.hex, "110000"));
    CHECK(editor.error[0]);
    action(&editor, INPUT_ENTER);
    CHECK(editor.hex_mode && editor.error[0]);
    action(&editor, INPUT_BACKSPACE);
    CHECK(!strcmp(editor.hex, "11000"));
    action(&editor, INPUT_CATALOG);
    CHECK(!editor.hex_mode);
    hex(&editor, "0");
    action(&editor, INPUT_ENTER);
    CHECK(editor.hex_mode && editor.error[0]);
    action(&editor, INPUT_BACKSPACE);
    character(&editor, '2');
    character(&editor, 'F');
    action(&editor, INPUT_ENTER);
    CHECK(editor.hex_mode && editor.error[0]);
    CHECK(editor.cursor == 8);
    action(&editor, INPUT_BACK);
    hex(&editor, "41");
    action(&editor, INPUT_ENTER);
    CHECK(!editor.hex_mode);
    CHECK(editor.text[8] == 'A');
    CHECK(action(&editor, INPUT_ENTER) == NAME_ACCEPTED);
    CHECK(action(&editor, INPUT_BACK) == NAME_CANCELLED);
    name_editor_destroy(&editor);
}

static void test_preserved(void) {
    static const char text[] = "MiXeD-e\xcc\x81-\xc3\xa9-\xe8\xb5\x84\xe6\x96\x99";
    NameEditor editor;
    CHECK(name_editor_init(&editor, text));
    action(&editor, INPUT_HOME);
    character(&editor, 'x');
    action(&editor, INPUT_BACKSPACE);
    CHECK(!strcmp(editor.text, text));
    CHECK(action(&editor, INPUT_ENTER) == NAME_ACCEPTED);
    CHECK(!strcmp(editor.text, text));
    name_editor_destroy(&editor);
}

static void test_view(void) {
    static const char text[] = "A\xe8\xb5\x84\xf0\x9f\x98\x80" "B";
    NameEditor editor;
    char out[64];
    unsigned x;
    CHECK(name_editor_init(&editor, text));
    name_editor_view(&editor, out, sizeof(out), 32, width, &x);
    CHECK(!strcmp(out, text) && x == 32);
    action(&editor, INPUT_HOME);
    name_editor_view(&editor, out, sizeof(out), 17, width, &x);
    CHECK(!strcmp(out, "A\xe8\xb5\x84") && x == 0);
    action(&editor, INPUT_RIGHT);
    name_editor_view(&editor, out, sizeof(out), 17, width, &x);
    CHECK(!strcmp(out, "A\xe8\xb5\x84") && x == 3);
    action(&editor, INPUT_RIGHT);
    name_editor_view(&editor, out, sizeof(out), 17, width, &x);
    CHECK(!strcmp(out, "\xf0\x9f\x98\x80") && x == 0);
    name_editor_view(&editor, out, 8, 100, width, &x);
    CHECK(!strcmp(out, "\xe8\xb5\x84\xf0\x9f\x98\x80") && x == 10);
    name_editor_view(&editor, out, 5, 100, width, &x);
    CHECK(!strcmp(out, "\xf0\x9f\x98\x80") && x == 0);
    name_editor_view(&editor, out, 4, 100, width, &x);
    CHECK(!strcmp(out, "\xe8\xb5\x84") && x == 10);
    name_editor_view(&editor, out, sizeof(out), 9, width, &x);
    CHECK(!out[0] && x == 0);
    action(&editor, INPUT_RIGHT);
    name_editor_view(&editor, out, sizeof(out), 19, width, &x);
    CHECK(!strcmp(out, "\xf0\x9f\x98\x80" "B") && x == 14);
    action(&editor, INPUT_END);
    name_editor_view(&editor, out, sizeof(out), 19, width, &x);
    CHECK(!strcmp(out, "\xf0\x9f\x98\x80" "B") && x == 19);
    name_editor_view(&editor, out, sizeof(out), 18, width, &x);
    CHECK(!strcmp(out, "B") && x == 5);
    name_editor_view(&editor, out, sizeof(out), 0, width, &x);
    CHECK(!out[0] && x == 0);
    name_editor_view(&editor, out, 1, 32, width, &x);
    CHECK(!out[0] && x == 0);
    out[0] = 'x';
    name_editor_view(&editor, out, 0, 32, width, &x);
    CHECK(out[0] == 'x' && x == 0);
    CHECK(!strcmp(editor.text, text));
    name_editor_destroy(&editor);
    CHECK(name_editor_init(&editor, ""));
    name_editor_view(&editor, out, sizeof(out), 32, width, &x);
    CHECK(!out[0] && x == 0);
    name_editor_destroy(&editor);
    name_editor_view(&editor, out, sizeof(out), 32, width, &x);
    CHECK(!out[0] && x == 0);
    CHECK(name_editor_init(&editor, "AX"));
    name_editor_view(&editor, out, sizeof(out), UINT_MAX, width, &x);
    CHECK(!strcmp(out, "X") && x == UINT_MAX);
    action(&editor, INPUT_LEFT);
    name_editor_view(&editor, out, sizeof(out), UINT_MAX, width, &x);
    CHECK(!strcmp(out, "X") && x == 0);
    name_editor_destroy(&editor);
    CHECK(name_editor_init(&editor, "\xcc\x81" "A"));
    action(&editor, INPUT_LEFT);
    name_editor_view(&editor, out, sizeof(out), 0, width, &x);
    CHECK(!strcmp(out, "\xcc\x81") && x == 0);
    CHECK(editor.cursor == 2);
    name_editor_destroy(&editor);
}

int main(void) {
    test_edit();
    test_invalid();
    test_limits();
    test_hex();
    test_preserved();
    test_view();
    puts("name editor tests passed");
    return 0;
}
