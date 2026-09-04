#include "editor.h"
#include "file_buffer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_PATH "build/nhex-test-input.bin"
#define OUTPUT_PATH "build/nhex-test-output.bin"
#define BACKUP_PATH OUTPUT_PATH ".nhex.backup"
#define EMPTY_PATH "build/nhex-test-empty.bin"
#define CREATE_PATH "build/nhex-test-created.bin"
#define TEST_SIZE ((uint64_t)NHEX_PAGE_SIZE * 10u + 37u)

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static uint8_t original_value(uint64_t offset) {
    return (uint8_t)((offset * 37u + 11u) & 0xffu);
}

static bool create_fixture(const char *path, uint64_t size) {
    FILE *file = fopen(path, "wb");
    uint64_t offset;
    CHECK(file != NULL);
    for (offset = 0; offset < size; ++offset) {
        CHECK(fputc(original_value(offset), file) != EOF);
    }
    CHECK(fclose(file) == 0);
    return true;
}

static bool read_disk_byte(const char *path, uint64_t offset, uint8_t *value) {
    FILE *file = fopen(path, "rb");
    int byte;
    CHECK(file != NULL);
    CHECK(fseeko(file, (off_t)offset, SEEK_SET) == 0);
    byte = fgetc(file);
    CHECK(byte != EOF);
    *value = (uint8_t)byte;
    CHECK(fclose(file) == 0);
    return true;
}

static bool disk_size(const char *path, uint64_t *size) {
    FILE *file = fopen(path, "rb");
    off_t end;
    CHECK(file != NULL);
    CHECK(fseeko(file, 0, SEEK_END) == 0);
    end = ftello(file);
    CHECK(end >= 0);
    *size = (uint64_t)end;
    CHECK(fclose(file) == 0);
    return true;
}

static bool test_cache_editor_and_safe_save(void) {
    FileBuffer buffer;
    Editor editor;
    uint8_t value;
    uint64_t size;
    unsigned page;

    remove(INPUT_PATH);
    remove(OUTPUT_PATH);
    CHECK(create_fixture(INPUT_PATH, TEST_SIZE));
    CHECK(file_buffer_open(&buffer, INPUT_PATH));
    CHECK(file_buffer_size(&buffer) == TEST_SIZE);
    CHECK(file_buffer_read(&buffer, 0, &value) && value == original_value(0));
    CHECK(file_buffer_read(&buffer, TEST_SIZE - 1, &value) && value == original_value(TEST_SIZE - 1));

    editor_init(&editor, &buffer);
    editor.cursor_offset = 100;
    CHECK(editor_input_hex(&editor, 0xA));
    CHECK(!editor.high_nibble);
    CHECK(file_buffer_read(&buffer, 100, &value) && (value & 0xf0) == 0xa0);
    CHECK(editor_input_hex(&editor, 0x5));
    CHECK(editor.high_nibble && editor.cursor_offset == 101);
    CHECK(file_buffer_read(&buffer, 100, &value) && value == 0xa5);

    editor.cursor_offset = 200;
    editor_toggle_mode(&editor);
    CHECK(editor.mode == EDIT_MODE_ASCII && editor.cursor_offset == 200);
    CHECK(editor_input_ascii(&editor, 'Z'));
    CHECK(editor.cursor_offset == 201);
    CHECK(file_buffer_read(&buffer, 200, &value) && value == 0x5a);
    editor_toggle_mode(&editor);
    CHECK(editor.mode == EDIT_MODE_HEX && editor.cursor_offset == 201);

    CHECK(editor_go_to(&editor, "1234") && editor.cursor_offset == 1234);
    CHECK(editor_go_to(&editor, "0x100") && editor.cursor_offset == 0x100);
    CHECK(editor_go_to(&editor, "+0x20") && editor.cursor_offset == 0x120);
    CHECK(editor_go_to(&editor, "-0x10") && editor.cursor_offset == 0x110);
    CHECK(!editor_go_to(&editor, "0xFFFFFFFFFFFFFFFF"));
    CHECK(!editor_go_to(&editor, "not-an-offset"));

    editor.cursor_offset = 17;
    CHECK(editor_move(&editor, EDIT_MOVE_HOME) && editor.cursor_offset == 16);
    CHECK(editor_move(&editor, EDIT_MOVE_END) && editor.cursor_offset == 23);
    CHECK(editor_move(&editor, EDIT_MOVE_DOWN) && editor.cursor_offset == 31);
    CHECK(editor_move(&editor, EDIT_MOVE_PAGE_DOWN) && editor.cursor_offset == 135);
    CHECK(editor_move(&editor, EDIT_MOVE_PAGE_UP) && editor.cursor_offset == 31);

    for (page = 0; page < 9; ++page) {
        uint64_t offset = (uint64_t)page * NHEX_PAGE_SIZE + page;
        CHECK(file_buffer_write(&buffer, offset, (uint8_t)(0xe0u + page)));
    }
    CHECK(file_buffer_modified(&buffer));
    CHECK(buffer.dirty_count > 0);

    CHECK(read_disk_byte(INPUT_PATH, 100, &value) && value == original_value(100));
    CHECK(!file_buffer_save_as(&buffer, "build/missing-directory/output.bin"));
    CHECK(read_disk_byte(INPUT_PATH, 100, &value) && value == original_value(100));

    CHECK(file_buffer_save_as(&buffer, OUTPUT_PATH));
    CHECK(!file_buffer_modified(&buffer));
    CHECK(strcmp(file_buffer_path(&buffer), OUTPUT_PATH) == 0);
    CHECK(disk_size(OUTPUT_PATH, &size) && size == TEST_SIZE);
    CHECK(read_disk_byte(OUTPUT_PATH, 100, &value) && value == 0xa5);
    CHECK(read_disk_byte(OUTPUT_PATH, 200, &value) && value == 0x5a);
    for (page = 0; page < 9; ++page) {
        uint64_t offset = (uint64_t)page * NHEX_PAGE_SIZE + page;
        CHECK(read_disk_byte(OUTPUT_PATH, offset, &value) && value == (uint8_t)(0xe0u + page));
    }
    CHECK(read_disk_byte(INPUT_PATH, 100, &value) && value == original_value(100));

    CHECK(file_buffer_write(&buffer, 5, 0x33));
    CHECK(file_buffer_save(&buffer));
    CHECK(read_disk_byte(OUTPUT_PATH, 5, &value) && value == 0x33);
    CHECK(file_buffer_read(&buffer, 5, &value) && value == 0x33);

    {
        FILE *stale_backup = fopen(BACKUP_PATH, "wb");
        CHECK(stale_backup != NULL);
        CHECK(fputs("recovery", stale_backup) >= 0);
        CHECK(fclose(stale_backup) == 0);
    }
    CHECK(file_buffer_write(&buffer, 6, 0x44));
    CHECK(!file_buffer_save(&buffer));
    CHECK(file_buffer_modified(&buffer));
    CHECK(read_disk_byte(OUTPUT_PATH, 6, &value) && value == original_value(6));
    CHECK(remove(BACKUP_PATH) == 0);
    CHECK(file_buffer_save(&buffer));
    CHECK(read_disk_byte(OUTPUT_PATH, 6, &value) && value == 0x44);

    file_buffer_close(&buffer);
    remove(INPUT_PATH);
    remove(OUTPUT_PATH);
    return true;
}

static bool test_empty_file(void) {
    FileBuffer buffer;
    Editor editor;
    FILE *file = fopen(EMPTY_PATH, "wb");
    CHECK(file != NULL && fclose(file) == 0);
    CHECK(file_buffer_open(&buffer, EMPTY_PATH));
    editor_init(&editor, &buffer);
    CHECK(file_buffer_size(&buffer) == 0);
    CHECK(editor_go_to(&editor, "0"));
    CHECK(!editor_input_hex(&editor, 1));
    CHECK(!editor_move(&editor, EDIT_MOVE_RIGHT));
    CHECK(file_buffer_save(&buffer));
    file_buffer_close(&buffer);
    remove(EMPTY_PATH);
    return true;
}

static bool test_create_and_extend(void) {
    FileBuffer buffer;
    FileBuffer duplicate;
    uint64_t size;
    uint8_t value;
    unsigned page;
    const uint64_t extended_size = (uint64_t)NHEX_PAGE_SIZE * 10u + 7u;

    remove(CREATE_PATH);
    CHECK(file_buffer_create(&buffer, CREATE_PATH));
    CHECK(file_buffer_size(&buffer) == 0);
    CHECK(!file_buffer_create(&duplicate, CREATE_PATH));
    file_buffer_close(&duplicate);
    CHECK(file_buffer_extend(&buffer, NHEX_PAGE_SIZE + 1u));
    CHECK(file_buffer_read(&buffer, 0, &value) && value == 0);
    CHECK(file_buffer_read(&buffer, NHEX_PAGE_SIZE, &value) && value == 0);
    CHECK(file_buffer_write(&buffer, NHEX_PAGE_SIZE, 0xaa));
    CHECK(file_buffer_extend(&buffer, extended_size));
    CHECK(!file_buffer_extend(&buffer, extended_size - 1));
    for (page = 2; page < 10; ++page) {
        CHECK(file_buffer_read(&buffer, (uint64_t)page * NHEX_PAGE_SIZE, &value));
        CHECK(value == 0);
    }
    CHECK(file_buffer_read(&buffer, extended_size - 1, &value) && value == 0);
    CHECK(file_buffer_save(&buffer));
    CHECK(disk_size(CREATE_PATH, &size) && size == extended_size);
    CHECK(read_disk_byte(CREATE_PATH, NHEX_PAGE_SIZE, &value) && value == 0xaa);
    CHECK(read_disk_byte(CREATE_PATH, extended_size - 1, &value) && value == 0);
    file_buffer_close(&buffer);

    CHECK(file_buffer_open(&buffer, CREATE_PATH));
    CHECK(file_buffer_size(&buffer) == extended_size);
    CHECK(file_buffer_read(&buffer, NHEX_PAGE_SIZE, &value) && value == 0xaa);
    file_buffer_close(&buffer);
    remove(CREATE_PATH);
    return true;
}

int main(void) {
    if (!test_cache_editor_and_safe_save() || !test_empty_file() ||
        !test_create_and_extend()) {
        return 1;
    }
    puts("PASS core: paging, synchronized editing, navigation, goto, safe save");
    return 0;
}
