#ifndef NEDIT_EDITOR_H
#define NEDIT_EDITOR_H

#include "text_buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEDIT_VISIBLE_ROWS 18u
#define NEDIT_VISIBLE_COLUMNS 45u
#define NEDIT_TAB_WIDTH 4u

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
    TextBuffer *buffer;
    size_t cursor_offset;
    size_t top_offset;
    size_t left_column;
    size_t preferred_column;
} Editor;

void editor_init(Editor *editor, TextBuffer *buffer);
bool editor_move(Editor *editor, EditorMove move);
bool editor_insert_character(Editor *editor, uint8_t value);
bool editor_insert_newline(Editor *editor);
bool editor_insert_tab(Editor *editor);
bool editor_backspace(Editor *editor);
bool editor_delete(Editor *editor);
bool editor_go_to_line(Editor *editor, const char *expression);

size_t editor_line_number(const Editor *editor);
size_t editor_column(const Editor *editor);
size_t editor_line_start(const Editor *editor, size_t offset);
size_t editor_line_end(const Editor *editor, size_t offset);
size_t editor_next_line(const Editor *editor, size_t offset);
size_t editor_previous_line(const Editor *editor, size_t offset);
void editor_ensure_visible(Editor *editor);

#endif
