#include "editor.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

static bool byte_at(const Editor *editor, size_t offset, uint8_t *value) {
    return text_buffer_get(editor->buffer, offset, value);
}

size_t editor_character_at(const Editor *editor, size_t offset, uint32_t *value) {
    uint8_t first;
    uint8_t next;
    uint32_t scalar;
    uint32_t minimum;
    size_t length;
    size_t index;

    if (!byte_at(editor, offset, &first)) return 0;
    *value = 0xfffd;
    if (first < 0x80) {
        *value = first;
        return 1;
    }
    if (first >= 0xc2 && first <= 0xdf) {
        scalar = first & 0x1f;
        minimum = 0x80;
        length = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
        scalar = first & 0x0f;
        minimum = 0x800;
        length = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
        scalar = first & 0x07;
        minimum = 0x10000;
        length = 4;
    } else {
        return 1;
    }
    if (text_buffer_size(editor->buffer) - offset < length) return 1;
    for (index = 1; index < length; ++index) {
        if (!byte_at(editor, offset + index, &next) || (next & 0xc0) != 0x80) return 1;
        scalar = (scalar << 6) | (next & 0x3f);
    }
    if (scalar < minimum || scalar > 0x10ffff ||
        (scalar >= 0xd800 && scalar <= 0xdfff)) return 1;
    *value = scalar;
    return length;
}

static size_t character_start(const Editor *editor, size_t offset) {
    uint8_t byte;
    uint32_t value;
    size_t distance;
    if (!byte_at(editor, offset, &byte) || (byte & 0xc0) != 0x80) return offset;
    for (distance = 1; distance <= 3 && distance <= offset; ++distance) {
        if (editor_character_at(editor, offset - distance, &value) > distance)
            return offset - distance;
    }
    return offset;
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
    size_t offset = editor_line_start(editor, editor->cursor_offset);
    size_t column = 0;
    uint32_t value;
    while (offset < editor->cursor_offset) {
        size_t length = editor_character_at(editor, offset, &value);
        if (length == 0) break;
        offset += length;
        column++;
    }
    return column;
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
    editor->visible_rows = NEDIT_VISIBLE_ROWS;
    editor->visible_columns = NEDIT_VISIBLE_COLUMNS;
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
        for (row = 0; row < editor->visible_rows; ++row) {
            if (visible_start == current_start) {
                visible = true;
                break;
            }
            if (visible_start >= text_buffer_size(editor->buffer)) break;
            visible_start = editor_next_line(editor, visible_start);
        }
        if (!visible) {
            editor->top_offset = current_start;
            for (row = 1; row < editor->visible_rows; ++row) {
                size_t previous = editor_previous_line(editor, editor->top_offset);
                if (previous == editor->top_offset) break;
                editor->top_offset = previous;
            }
        }
    }
    if (column < editor->left_column) {
        editor->left_column = column;
    } else if (column >= editor->left_column + editor->visible_columns) {
        editor->left_column = column - editor->visible_columns + 1;
    }
}

static bool move_vertical(Editor *editor, bool down) {
    size_t start = editor_line_start(editor, editor->cursor_offset);
    size_t target_start = down ? editor_next_line(editor, start)
                               : editor_previous_line(editor, start);
    size_t column = 0;
    uint32_t value;
    size_t length;
    if (target_start == start) return false;
    while (column < editor->preferred_column &&
           (length = editor_character_at(editor, target_start, &value)) != 0 &&
           value != '\n') {
        target_start += length;
        column++;
    }
    editor->cursor_offset = target_start;
    return true;
}

bool editor_move(Editor *editor, EditorMove move) {
    size_t size = text_buffer_size(editor->buffer);
    bool moved = false;
    unsigned count = 1;
    unsigned index;

    if (move == EDIT_MOVE_PAGE_UP || move == EDIT_MOVE_PAGE_DOWN) {
        count = editor->visible_rows;
    }
    switch (move) {
        case EDIT_MOVE_LEFT:
            if (editor->cursor_offset > 0) {
                editor->cursor_offset = character_start(editor, editor->cursor_offset - 1);
                moved = true;
            }
            break;
        case EDIT_MOVE_RIGHT:
            if (editor->cursor_offset < size) {
                uint32_t value;
                editor->cursor_offset += editor_character_at(editor, editor->cursor_offset, &value);
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
        case EDIT_MOVE_DOCUMENT_HOME:
            moved = editor->cursor_offset != 0;
            editor->cursor_offset = 0;
            break;
        case EDIT_MOVE_DOCUMENT_END:
            moved = editor->cursor_offset != size;
            editor->cursor_offset = size;
            break;
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

bool editor_insert_character(Editor *editor, uint32_t value) {
    uint8_t bytes[4];
    size_t length;
    if (value < 0x20 || (value >= 0x7f && value <= 0x9f) ||
        (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) return false;
    if (value < 0x80) {
        bytes[0] = (uint8_t)value;
        length = 1;
    } else if (value < 0x800) {
        bytes[0] = (uint8_t)(0xc0 | (value >> 6));
        bytes[1] = (uint8_t)(0x80 | (value & 0x3f));
        length = 2;
    } else if (value < 0x10000) {
        bytes[0] = (uint8_t)(0xe0 | (value >> 12));
        bytes[1] = (uint8_t)(0x80 | ((value >> 6) & 0x3f));
        bytes[2] = (uint8_t)(0x80 | (value & 0x3f));
        length = 3;
    } else {
        bytes[0] = (uint8_t)(0xf0 | (value >> 18));
        bytes[1] = (uint8_t)(0x80 | ((value >> 12) & 0x3f));
        bytes[2] = (uint8_t)(0x80 | ((value >> 6) & 0x3f));
        bytes[3] = (uint8_t)(0x80 | (value & 0x3f));
        length = 4;
    }
    if (!text_buffer_insert_span(editor->buffer, editor->cursor_offset, bytes, length)) return false;
    editor->cursor_offset += length;
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
    size_t start;
    if (editor->cursor_offset == 0) return false;
    start = character_start(editor, editor->cursor_offset - 1);
    if (!text_buffer_delete(editor->buffer, start, editor->cursor_offset - start)) return false;
    /* Deleting between malformed bytes can join them into a valid character. */
    editor->cursor_offset = character_start(editor, start);
    editor->preferred_column = editor_column(editor);
    editor_ensure_visible(editor);
    return true;
}

bool editor_delete(Editor *editor) {
    uint32_t value;
    size_t length = editor_character_at(editor, editor->cursor_offset, &value);
    if (length == 0 ||
        !text_buffer_delete(editor->buffer, editor->cursor_offset, length)) return false;
    editor->cursor_offset = character_start(editor, editor->cursor_offset);
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
