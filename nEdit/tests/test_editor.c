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
        CHECK(editor_insert_character(&editor, (uint8_t)long_line[index]));
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

int main(void) {
    if (!test_edit_navigation_and_save() || !test_empty_file() ||
        !test_create_arbitrary_extension()) return 1;
    puts("PASS nEdit: gap editing, navigation, viewport, safe save");
    return 0;
}
