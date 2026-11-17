#include "editor.h"
#include "text_buffer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_PATH "build/nedit-input.txt"
#define OUTPUT_PATH "build/nedit-output.txt"
#define BACKUP_PATH OUTPUT_PATH ".nedit.backup"
#define CREATE_PATH "build/nedit-created.data"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static bool write_fixture(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    size_t length = strlen(text);
    CHECK(file != NULL);
    CHECK(fwrite(text, 1, length, file) == length);
    CHECK(fclose(file) == 0);
    return true;
}

static bool expect_buffer(const TextBuffer *buffer, const char *expected) {
    size_t length = strlen(expected);
    size_t index;
    uint8_t value;
    CHECK(text_buffer_size(buffer) == length);
    for (index = 0; index < length; ++index) {
        CHECK(text_buffer_get(buffer, index, &value));
        CHECK(value == (uint8_t)expected[index]);
    }
    return true;
}

static bool expect_file(const char *path, const char *expected) {
    FILE *file = fopen(path, "rb");
    size_t length = strlen(expected);
    char *data;
    CHECK(file != NULL);
    data = (char *)malloc(length + 1);
    CHECK(data != NULL);
    CHECK(fread(data, 1, length, file) == length);
    data[length] = '\0';
    CHECK(fgetc(file) == EOF);
    CHECK(fclose(file) == 0);
    CHECK(!memcmp(data, expected, length + 1));
    free(data);
    return true;
}

static bool test_edit_navigation_and_save(void) {
    TextBuffer buffer;
    Editor editor;
    char long_line[61];
    FILE *backup;
    size_t index;

    remove(INPUT_PATH);
    remove(OUTPUT_PATH);
    remove(BACKUP_PATH);
    CHECK(write_fixture(INPUT_PATH, "alpha\nbeta\ngamma"));
    CHECK(text_buffer_open(&buffer, INPUT_PATH));
    CHECK(expect_buffer(&buffer, "alpha\nbeta\ngamma"));

    editor_init(&editor, &buffer);
    CHECK(editor_go_to_line(&editor, "2"));
    CHECK(editor.cursor_offset == 6 && editor_line_number(&editor) == 2);
    CHECK(editor_move(&editor, EDIT_MOVE_END));
    CHECK(editor.cursor_offset == 10);
    CHECK(editor_insert_character(&editor, '!'));
    CHECK(editor_insert_newline(&editor));
    CHECK(editor_insert_tab(&editor));
    CHECK(editor_backspace(&editor));
    CHECK(expect_buffer(&buffer, "alpha\nbeta!\n   \ngamma"));
    CHECK(editor_go_to_line(&editor, "4"));
    CHECK(editor_insert_character(&editor, 'G'));
    CHECK(expect_buffer(&buffer, "alpha\nbeta!\n   \nGgamma"));
    CHECK(!editor_go_to_line(&editor, "0"));
    CHECK(!editor_go_to_line(&editor, "99"));
    CHECK(!editor_move(&editor, EDIT_MOVE_DOWN));

    CHECK(text_buffer_save_as(&buffer, OUTPUT_PATH));
    CHECK(!text_buffer_modified(&buffer));
    CHECK(expect_file(INPUT_PATH, "alpha\nbeta\ngamma"));
    CHECK(expect_file(OUTPUT_PATH, "alpha\nbeta!\n   \nGgamma"));

    CHECK(editor_backspace(&editor));
    backup = fopen(BACKUP_PATH, "wb");
    CHECK(backup != NULL);
    CHECK(fputs("recovery", backup) >= 0);
    CHECK(fclose(backup) == 0);
    CHECK(!text_buffer_save(&buffer));
    CHECK(text_buffer_modified(&buffer));
    CHECK(expect_file(OUTPUT_PATH, "alpha\nbeta!\n   \nGgamma"));
    CHECK(remove(BACKUP_PATH) == 0);
    CHECK(text_buffer_save(&buffer));
    CHECK(expect_file(OUTPUT_PATH, "alpha\nbeta!\n   \ngamma"));

    editor.cursor_offset = text_buffer_size(&buffer);
    memset(long_line, 'x', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\0';
    for (index = 0; index < sizeof(long_line) - 1; ++index)
        CHECK(editor_insert_character(&editor, (uint32_t)long_line[index]));
    CHECK(editor.left_column > 0);
    CHECK(text_buffer_delete(&buffer, editor.cursor_offset - 10, 10));
    editor.cursor_offset -= 10;
    editor_ensure_visible(&editor);

    text_buffer_close(&buffer);
    remove(INPUT_PATH);
    remove(OUTPUT_PATH);
    return true;
}

static bool test_empty_file(void) {
    TextBuffer buffer;
    Editor editor;
    CHECK(write_fixture(INPUT_PATH, ""));
    CHECK(text_buffer_open(&buffer, INPUT_PATH));
    editor_init(&editor, &buffer);
    CHECK(text_buffer_size(&buffer) == 0);
    CHECK(editor_line_number(&editor) == 1);
    CHECK(editor_insert_character(&editor, 'A'));
    CHECK(editor_insert_newline(&editor));
    CHECK(editor_insert_character(&editor, 'B'));
    CHECK(expect_buffer(&buffer, "A\nB"));
    CHECK(text_buffer_save(&buffer));
    CHECK(expect_file(INPUT_PATH, "A\nB"));
    text_buffer_close(&buffer);
    remove(INPUT_PATH);
    return true;
}

static bool test_create_arbitrary_extension(void) {
    TextBuffer buffer;
    TextBuffer duplicate;
    Editor editor;
    remove(CREATE_PATH);
    CHECK(text_buffer_create(&buffer, CREATE_PATH));
    CHECK(text_buffer_size(&buffer) == 0);
    CHECK(!text_buffer_create(&duplicate, CREATE_PATH));
    text_buffer_close(&duplicate);
    editor_init(&editor, &buffer);
    CHECK(editor_insert_character(&editor, 'd'));
    CHECK(editor_insert_character(&editor, 'a'));
    CHECK(editor_insert_character(&editor, 't'));
    CHECK(editor_insert_character(&editor, 'a'));
    CHECK(text_buffer_save(&buffer));
    CHECK(expect_file(CREATE_PATH, "data"));
    text_buffer_close(&buffer);
    CHECK(text_buffer_open(&buffer, CREATE_PATH));
    CHECK(expect_buffer(&buffer, "data"));
    text_buffer_close(&buffer);
    remove(CREATE_PATH);
    return true;
}

static bool test_unicode_edit_and_save(void) {
    static const uint32_t scalars[] = {'A', 0x03b1, 0x2264, 0x1f642, '!'};
    static const size_t offsets[] = {0, 1, 3, 6, 10, 11};
    static const uint32_t invalid[] = {0, '\n', 0x1f, 0x7f, 0x9f, 0xd800, 0xdfff,
                                       0x110000, UINT32_MAX};
    static const char text[] = "A\xce\xb1\xe2\x89\xa4\xf0\x9f\x99\x82!";
    TextBuffer buffer;
    Editor editor;
    uint32_t value;
    size_t index;

    CHECK(write_fixture(INPUT_PATH, ""));
    CHECK(text_buffer_open(&buffer, INPUT_PATH));
    editor_init(&editor, &buffer);
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        CHECK(!editor_insert_character(&editor, invalid[index]));
        CHECK(editor.cursor_offset == 0 && editor.preferred_column == 0);
        CHECK(text_buffer_size(&buffer) == 0 && !text_buffer_modified(&buffer));
    }
    for (index = 0; index < sizeof(scalars) / sizeof(scalars[0]); ++index) {
        CHECK(editor_insert_character(&editor, scalars[index]));
        CHECK(editor.cursor_offset == offsets[index + 1]);
        CHECK(editor_column(&editor) == index + 1);
        CHECK(editor_character_at(&editor, offsets[index], &value) ==
              offsets[index + 1] - offsets[index]);
        CHECK(value == scalars[index]);
    }
    CHECK(editor_character_at(&editor, sizeof(text) - 1, &value) == 0);
    CHECK(expect_buffer(&buffer, text));
    CHECK(text_buffer_save(&buffer));
    CHECK(expect_file(INPUT_PATH, text));
    for (index = 5; index > 0; --index) {
        CHECK(editor_move(&editor, EDIT_MOVE_LEFT));
        CHECK(editor.cursor_offset == offsets[index - 1]);
        CHECK(editor_column(&editor) == index - 1);
    }
    CHECK(!editor_move(&editor, EDIT_MOVE_LEFT));
    for (index = 1; index <= 5; ++index) {
        CHECK(editor_move(&editor, EDIT_MOVE_RIGHT));
        CHECK(editor.cursor_offset == offsets[index]);
    }
    CHECK(!editor_move(&editor, EDIT_MOVE_RIGHT));
    CHECK(editor_move(&editor, EDIT_MOVE_LEFT));
    CHECK(editor_backspace(&editor));
    CHECK(editor.cursor_offset == 6 && editor_column(&editor) == 3);
    CHECK(expect_buffer(&buffer, "A\xce\xb1\xe2\x89\xa4!"));
    CHECK(editor_move(&editor, EDIT_MOVE_LEFT));
    CHECK(editor_delete(&editor));
    CHECK(editor.cursor_offset == 3 && editor_column(&editor) == 2);
    CHECK(expect_buffer(&buffer, "A\xce\xb1!"));
    CHECK(editor_backspace(&editor));
    CHECK(editor.cursor_offset == 1 && editor_column(&editor) == 1);
    CHECK(expect_buffer(&buffer, "A!"));
    CHECK(editor_insert_character(&editor, 0x03b2));
    CHECK(text_buffer_save(&buffer));
    CHECK(expect_file(INPUT_PATH, "A\xce\xb2!"));
    CHECK(editor_move(&editor, EDIT_MOVE_DOCUMENT_HOME));
    CHECK(!editor_backspace(&editor));
    CHECK(editor_move(&editor, EDIT_MOVE_DOCUMENT_END));
    CHECK(!editor_delete(&editor));
    text_buffer_close(&buffer);
    remove(INPUT_PATH);
    return true;
}

static bool test_utf8_boundaries(void) {
    static const struct {
        const char *text;
        uint32_t scalar;
        size_t length;
    } cases[] = {
        {"\x7f", 0x7f, 1},
        {"\xc2\x80", 0x80, 2},
        {"\xdf\xbf", 0x7ff, 2},
        {"\xe0\xa0\x80", 0x800, 3},
        {"\xed\x9f\xbf", 0xd7ff, 3},
        {"\xee\x80\x80", 0xe000, 3},
        {"\xef\xbf\xbf", 0xffff, 3},
        {"\xf0\x90\x80\x80", 0x10000, 4},
        {"\xf4\x8f\xbf\xbf", 0x10ffff, 4},
        {"\xc0\xaf", 0xfffd, 1},
        {"\xc1\xbf", 0xfffd, 1},
        {"\xe0\x9f\xbf", 0xfffd, 1},
        {"\xed\xa0\x80", 0xfffd, 1},
        {"\xed\xbf\xbf", 0xfffd, 1},
        {"\xf0\x8f\xbf\xbf", 0xfffd, 1},
        {"\xf4\x90\x80\x80", 0xfffd, 1},
        {"\xf5\x80\x80\x80", 0xfffd, 1},
        {"\xff", 0xfffd, 1},
        {"\xfe", 0xfffd, 1},
        {"\x80", 0xfffd, 1},
        {"\xbf", 0xfffd, 1},
        {"\xc2", 0xfffd, 1},
        {"\xe2\x82", 0xfffd, 1},
        {"\xf0\x9f\x92", 0xfffd, 1},
        {"\xe2(\xa1", 0xfffd, 1},
        {"\xc2\n", 0xfffd, 1}
    };
    TextBuffer buffer;
    Editor editor;
    uint32_t value;
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        CHECK(write_fixture(INPUT_PATH, cases[index].text));
        CHECK(text_buffer_open(&buffer, INPUT_PATH));
        editor_init(&editor, &buffer);
        CHECK(editor_character_at(&editor, 0, &value) == cases[index].length);
        CHECK(value == cases[index].scalar);
        CHECK(editor_move(&editor, EDIT_MOVE_RIGHT));
        CHECK(editor.cursor_offset == cases[index].length);
        CHECK(editor_column(&editor) == 1);
        CHECK(editor_move(&editor, EDIT_MOVE_LEFT));
        CHECK(editor.cursor_offset == 0);
        text_buffer_close(&buffer);
    }
    remove(INPUT_PATH);
    return true;
}

static bool test_malformed_bytes(void) {
    static const char text[] = "\xc0\xaf\xe0\x80\x80\xed\xa0\x80\xf4\x90\x80\x80"
                               "\xff\x80\xc2\n\xe2\x82";
    TextBuffer buffer;
    Editor editor;
    uint32_t value;
    size_t index;

    CHECK(write_fixture(INPUT_PATH, text));
    CHECK(text_buffer_open(&buffer, INPUT_PATH));
    editor_init(&editor, &buffer);
    for (index = 0; index < sizeof(text) - 1; ++index) {
        CHECK(editor_character_at(&editor, index, &value) == 1);
        CHECK(value == (text[index] == '\n' ? '\n' : 0xfffd));
        CHECK(editor_move(&editor, EDIT_MOVE_RIGHT));
        CHECK(editor.cursor_offset == index + 1);
    }
    CHECK(editor_insert_character(&editor, '!'));
    CHECK(text_buffer_save(&buffer));
    CHECK(expect_file(INPUT_PATH, "\xc0\xaf\xe0\x80\x80\xed\xa0\x80\xf4\x90\x80\x80"
                                  "\xff\x80\xc2\n\xe2\x82!"));
    CHECK(editor_backspace(&editor));
    for (index = sizeof(text) - 1; index > 0; --index) {
        CHECK(editor_move(&editor, EDIT_MOVE_LEFT));
        CHECK(editor.cursor_offset == index - 1);
    }
    CHECK(editor_delete(&editor));
    CHECK(editor.cursor_offset == 0);
    CHECK(expect_buffer(&buffer, text + 1));
    CHECK(editor_move(&editor, EDIT_MOVE_DOCUMENT_END));
    CHECK(editor_backspace(&editor));
    CHECK(expect_buffer(&buffer, "\xaf\xe0\x80\x80\xed\xa0\x80\xf4\x90\x80\x80"
                                "\xff\x80\xc2\n\xe2"));
    CHECK(text_buffer_save(&buffer));
    CHECK(expect_file(INPUT_PATH, "\xaf\xe0\x80\x80\xed\xa0\x80\xf4\x90\x80\x80"
                                  "\xff\x80\xc2\n\xe2"));
    text_buffer_close(&buffer);

    CHECK(write_fixture(INPUT_PATH, "\xce\n\xb1"));
    CHECK(text_buffer_open(&buffer, INPUT_PATH));
    editor_init(&editor, &buffer);
    CHECK(editor_move(&editor, EDIT_MOVE_DOWN));
    CHECK(editor_backspace(&editor));
    CHECK(expect_buffer(&buffer, "\xce\xb1"));
    CHECK(editor.cursor_offset == 0 && editor_column(&editor) == 0);
    CHECK(editor_delete(&editor));
    CHECK(expect_buffer(&buffer, ""));
    text_buffer_close(&buffer);

    CHECK(write_fixture(INPUT_PATH, "\xe2\x89!\xa4"));
    CHECK(text_buffer_open(&buffer, INPUT_PATH));
    editor_init(&editor, &buffer);
    CHECK(editor_move(&editor, EDIT_MOVE_RIGHT));
    CHECK(editor_move(&editor, EDIT_MOVE_RIGHT));
    CHECK(editor_delete(&editor));
    CHECK(expect_buffer(&buffer, "\xe2\x89\xa4"));
    CHECK(editor.cursor_offset == 0 && editor_column(&editor) == 0);
    CHECK(editor_move(&editor, EDIT_MOVE_RIGHT));
    CHECK(editor.cursor_offset == 3);
    text_buffer_close(&buffer);
    remove(INPUT_PATH);
    return true;
}

static bool test_unicode_columns(void) {
    static const char text[] = "A\xce\xb1\xe2\x89\xa4\xf0\x9f\x99\x82\n"
                               "\xce\xb2\n"
                               "\xe2\x88\x9e\xce\xb3" "BC\n";
    TextBuffer buffer;
    Editor editor;
    size_t index;

    CHECK(write_fixture(INPUT_PATH, text));
    CHECK(text_buffer_open(&buffer, INPUT_PATH));
    editor_init(&editor, &buffer);
    CHECK(editor_move(&editor, EDIT_MOVE_END));
    CHECK(editor.cursor_offset == 10 && editor_column(&editor) == 4);
    CHECK(editor.preferred_column == 4);
    CHECK(editor_move(&editor, EDIT_MOVE_DOWN));
    CHECK(editor.cursor_offset == 13 && editor_column(&editor) == 1);
    CHECK(editor.preferred_column == 4);
    CHECK(editor_move(&editor, EDIT_MOVE_DOWN));
    CHECK(editor.cursor_offset == 21 && editor_column(&editor) == 4);
    CHECK(editor_move(&editor, EDIT_MOVE_DOWN));
    CHECK(editor.cursor_offset == 22 && editor_column(&editor) == 0);
    CHECK(editor.preferred_column == 4);
    CHECK(editor_move(&editor, EDIT_MOVE_PAGE_UP));
    CHECK(editor.cursor_offset == 10 && editor_column(&editor) == 4);
    CHECK(editor_move(&editor, EDIT_MOVE_PAGE_DOWN));
    CHECK(editor.cursor_offset == 22 && editor.preferred_column == 4);
    CHECK(editor_move(&editor, EDIT_MOVE_DOCUMENT_HOME));
    CHECK(editor.cursor_offset == 0 && editor.preferred_column == 0);
    CHECK(!editor_move(&editor, EDIT_MOVE_DOCUMENT_HOME));
    CHECK(editor_move(&editor, EDIT_MOVE_DOCUMENT_END));
    CHECK(editor.cursor_offset == sizeof(text) - 1);
    CHECK(!editor_move(&editor, EDIT_MOVE_DOCUMENT_END));
    for (index = 0; index < NEDIT_VISIBLE_COLUMNS + 5; ++index)
        CHECK(editor_insert_character(&editor, 0x03b1));
    CHECK(editor_column(&editor) == NEDIT_VISIBLE_COLUMNS + 5);
    CHECK(editor.left_column == 6);
    CHECK(editor_move(&editor, EDIT_MOVE_HOME));
    CHECK(editor.cursor_offset == 22 && editor.left_column == 0);
    CHECK(editor_move(&editor, EDIT_MOVE_END));
    CHECK(editor.left_column == 6);
    CHECK(editor_move(&editor, EDIT_MOVE_UP));
    CHECK(editor.cursor_offset == 21 && editor.left_column == 4);
    CHECK(editor.preferred_column == NEDIT_VISIBLE_COLUMNS + 5);
    text_buffer_close(&buffer);
    remove(INPUT_PATH);
    return true;
}

static bool test_measured_viewport(void) {
    TextBuffer buffer;
    Editor editor;

    CHECK(write_fixture(INPUT_PATH, "abcdef\nabcdef\nabcdef\nabcdef\n"));
    CHECK(text_buffer_open(&buffer, INPUT_PATH));
    editor_init(&editor, &buffer);
    editor.visible_rows = 2;
    editor.visible_columns = 3;
    CHECK(editor_move(&editor, EDIT_MOVE_END));
    CHECK(editor_column(&editor) == 6 && editor.left_column == 4);
    CHECK(editor_move(&editor, EDIT_MOVE_PAGE_DOWN));
    CHECK(editor_line_number(&editor) == 3 && editor_column(&editor) == 6);
    CHECK(editor.top_offset == 7);
    CHECK(editor_move(&editor, EDIT_MOVE_PAGE_UP));
    CHECK(editor_line_number(&editor) == 1 && editor.top_offset == 0);
    CHECK(editor_move(&editor, EDIT_MOVE_HOME));
    CHECK(editor.left_column == 0);
    text_buffer_close(&buffer);
    remove(INPUT_PATH);
    return true;
}

int main(void) {
    if (!test_edit_navigation_and_save() || !test_empty_file() ||
        !test_create_arbitrary_extension() || !test_unicode_edit_and_save() ||
        !test_utf8_boundaries() || !test_malformed_bytes() || !test_unicode_columns() ||
        !test_measured_viewport()) return 1;
    puts("PASS nEdit: UTF-8 editing, navigation, viewport, safe save");
    return 0;
}
