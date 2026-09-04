#ifndef NHEX_EDITOR_H
#define NHEX_EDITOR_H

#include "file_buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NHEX_BYTES_PER_ROW 8u
#define NHEX_VISIBLE_ROWS 13u

typedef enum {
    EDIT_MODE_HEX,
    EDIT_MODE_ASCII
} EditMode;

typedef enum {
    EDIT_MOVE_LEFT,
    EDIT_MOVE_RIGHT,
    EDIT_MOVE_UP,
    EDIT_MOVE_DOWN,
    EDIT_MOVE_PAGE_UP,
    EDIT_MOVE_PAGE_DOWN,
    EDIT_MOVE_HOME,
    EDIT_MOVE_END
} EditorMove;

typedef struct {
    FileBuffer *buffer;
    uint64_t cursor_offset;
    uint64_t top_offset;
    EditMode mode;
    bool high_nibble;
} Editor;

void editor_init(Editor *editor, FileBuffer *buffer);
void editor_toggle_mode(Editor *editor);
bool editor_move(Editor *editor, EditorMove move);
bool editor_go_to(Editor *editor, const char *expression);
bool editor_write_byte(Editor *editor, uint64_t offset, uint8_t value);
bool editor_input_hex(Editor *editor, int nibble);
bool editor_input_ascii(Editor *editor, uint8_t value);
bool editor_current_byte(Editor *editor, uint8_t *value);
void editor_ensure_visible(Editor *editor);

#endif
