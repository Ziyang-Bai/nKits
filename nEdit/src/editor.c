#include "editor.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

static bool byte_at(const Editor *editor, size_t offset, uint8_t *value) {
    return text_buffer_get(editor->buffer, offset, value);
}

size_t editor_line_start(const Editor *editor, size_t offset) {
    uint8_t value;
    size_t size = text_buffer_size(editor->buffer);
    if (offset > size) offset = size;
    while (offset > 0) {
        if (byte_at(editor, offset - 1, &value) && value == '\n') break;
        offset--;
    }
    return offset;
}

size_t editor_line_end(const Editor *editor, size_t offset) {
    uint8_t value;
    size_t size = text_buffer_size(editor->buffer);
    if (offset > size) offset = size;
    while (offset < size) {
        if (!byte_at(editor, offset, &value) || value == '\n') break;
        offset++;
    }
    return offset;
}

size_t editor_next_line(const Editor *editor, size_t offset) {
    size_t start = editor_line_start(editor, offset);
    size_t end = editor_line_end(editor, start);
    size_t size = text_buffer_size(editor->buffer);
    return end < size ? end + 1 : start;
}

size_t editor_previous_line(const Editor *editor, size_t offset) {
    size_t start = editor_line_start(editor, offset);
    return start == 0 ? 0 : editor_line_start(editor, start - 1);
}

size_t editor_column(const Editor *editor) {
    return editor->cursor_offset - editor_line_start(editor, editor->cursor_offset);
}

size_t editor_line_number(const Editor *editor) {
    size_t line = 1;
    size_t offset;
    uint8_t value;
    for (offset = 0; offset < editor->cursor_offset; ++offset) {
        if (byte_at(editor, offset, &value) && value == '\n') line++;
    }
    return line;
}

void editor_init(Editor *editor, TextBuffer *buffer) {
    editor->buffer = buffer;
    editor->cursor_offset = 0;
    editor->top_offset = 0;
    editor->left_column = 0;
    editor->preferred_column = 0;
}

void editor_ensure_visible(Editor *editor) {
    size_t current_start = editor_line_start(editor, editor->cursor_offset);
    size_t visible_start = editor->top_offset;
    size_t row;
    size_t column = editor_column(editor);
    bool visible = false;

    if (current_start < editor->top_offset) {
        editor->top_offset = current_start;
    } else {
        for (row = 0; row < NEDIT_VISIBLE_ROWS; ++row) {
            if (visible_start == current_start) {
                visible = true;
                break;
            }
            if (visible_start >= text_buffer_size(editor->buffer)) break;
            visible_start = editor_next_line(editor, visible_start);
        }
        if (!visible) {
            editor->top_offset = current_start;
            for (row = 1; row < NEDIT_VISIBLE_ROWS; ++row) {
                size_t previous = editor_previous_line(editor, editor->top_offset);
                if (previous == editor->top_offset) break;
                editor->top_offset = previous;
            }
        }
    }
    if (column < editor->left_column) {
        editor->left_column = column;
    } else if (column >= editor->left_column + NEDIT_VISIBLE_COLUMNS) {
        editor->left_column = column - NEDIT_VISIBLE_COLUMNS + 1;
    }
}

static bool move_vertical(Editor *editor, bool down) {
    size_t start = editor_line_start(editor, editor->cursor_offset);
    size_t target_start = down ? editor_next_line(editor, start)
                               : editor_previous_line(editor, start);
    size_t target_end;
    if (target_start == start) return false;
    target_end = editor_line_end(editor, target_start);
    editor->cursor_offset = target_start +
        (editor->preferred_column < target_end - target_start
             ? editor->preferred_column
             : target_end - target_start);
    return true;
}

bool editor_move(Editor *editor, EditorMove move) {
    size_t size = text_buffer_size(editor->buffer);
    bool moved = false;
    unsigned count = 1;
    unsigned index;

    if (move == EDIT_MOVE_PAGE_UP || move == EDIT_MOVE_PAGE_DOWN) {
        count = NEDIT_VISIBLE_ROWS;
    }
    switch (move) {
        case EDIT_MOVE_LEFT:
            if (editor->cursor_offset > 0) {
                editor->cursor_offset--;
                moved = true;
            }
            break;
        case EDIT_MOVE_RIGHT:
            if (editor->cursor_offset < size) {
                editor->cursor_offset++;
                moved = true;
            }
            break;
        case EDIT_MOVE_HOME: {
            size_t target = editor_line_start(editor, editor->cursor_offset);
            moved = target != editor->cursor_offset;
            editor->cursor_offset = target;
            break;
        }
        case EDIT_MOVE_END: {
            size_t target = editor_line_end(editor, editor->cursor_offset);
            moved = target != editor->cursor_offset;
            editor->cursor_offset = target;
            break;
        }
        case EDIT_MOVE_UP:
        case EDIT_MOVE_PAGE_UP:
            for (index = 0; index < count; ++index) {
                if (!move_vertical(editor, false)) break;
                moved = true;
            }
            break;
        case EDIT_MOVE_DOWN:
        case EDIT_MOVE_PAGE_DOWN:
            for (index = 0; index < count; ++index) {
                if (!move_vertical(editor, true)) break;
                moved = true;
            }
            break;
    }
    if (move != EDIT_MOVE_UP && move != EDIT_MOVE_DOWN &&
        move != EDIT_MOVE_PAGE_UP && move != EDIT_MOVE_PAGE_DOWN) {
        editor->preferred_column = editor_column(editor);
    }
    editor_ensure_visible(editor);
    return moved;
}

bool editor_insert_character(Editor *editor, uint8_t value) {
    if (value < 0x20 || value > 0x7e ||
        !text_buffer_insert(editor->buffer, editor->cursor_offset, value)) return false;
    editor->cursor_offset++;
    editor->preferred_column = editor_column(editor);
    editor_ensure_visible(editor);
    return true;
}

bool editor_insert_newline(Editor *editor) {
    if (!text_buffer_insert(editor->buffer, editor->cursor_offset, '\n')) return false;
    editor->cursor_offset++;
    editor->preferred_column = 0;
    editor_ensure_visible(editor);
    return true;
}

bool editor_insert_tab(Editor *editor) {
    static const uint8_t spaces[NEDIT_TAB_WIDTH] = {' ', ' ', ' ', ' '};
    if (!text_buffer_insert_span(editor->buffer, editor->cursor_offset,
                                 spaces, sizeof(spaces))) return false;
    editor->cursor_offset += sizeof(spaces);
    editor->preferred_column = editor_column(editor);
    editor_ensure_visible(editor);
    return true;
}

bool editor_backspace(Editor *editor) {
    if (editor->cursor_offset == 0 ||
        !text_buffer_delete(editor->buffer, editor->cursor_offset - 1, 1)) return false;
    editor->cursor_offset--;
    editor->preferred_column = editor_column(editor);
    editor_ensure_visible(editor);
    return true;
}

bool editor_delete(Editor *editor) {
    if (editor->cursor_offset >= text_buffer_size(editor->buffer) ||
        !text_buffer_delete(editor->buffer, editor->cursor_offset, 1)) return false;
    editor->preferred_column = editor_column(editor);
    editor_ensure_visible(editor);
    return true;
}

bool editor_go_to_line(Editor *editor, const char *expression) {
    const char *cursor = expression;
    char *end;
    unsigned long long requested;
    size_t line = 1;
    size_t offset = 0;
    size_t size = text_buffer_size(editor->buffer);
    uint8_t value;

    if (!cursor) return false;
    while (isspace((unsigned char)*cursor)) cursor++;
    errno = 0;
    requested = strtoull(cursor, &end, 10);
    if (errno == ERANGE || end == cursor || requested == 0 || requested > SIZE_MAX) return false;
    while (isspace((unsigned char)*end)) end++;
    if (*end) return false;
    while (line < (size_t)requested && offset < size) {
        if (byte_at(editor, offset++, &value) && value == '\n') line++;
    }
    if (line != (size_t)requested) return false;
    editor->cursor_offset = offset;
    editor->preferred_column = 0;
    editor_ensure_visible(editor);
    return true;
}
