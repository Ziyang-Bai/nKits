#include "editor.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

static uint64_t file_last_offset(const Editor *editor) {
    uint64_t size = file_buffer_size(editor->buffer);
    return size == 0 ? 0 : size - 1;
}

void editor_init(Editor *editor, FileBuffer *buffer) {
    editor->buffer = buffer;
    editor->cursor_offset = 0;
    editor->top_offset = 0;
    editor->mode = EDIT_MODE_HEX;
    editor->high_nibble = true;
}

void editor_toggle_mode(Editor *editor) {
    editor->mode = editor->mode == EDIT_MODE_HEX ? EDIT_MODE_ASCII : EDIT_MODE_HEX;
    editor->high_nibble = true;
}

void editor_ensure_visible(Editor *editor) {
    const uint64_t visible = (uint64_t)NHEX_BYTES_PER_ROW * NHEX_VISIBLE_ROWS;
    uint64_t cursor_row;
    uint64_t top_row;

    if (editor->cursor_offset < editor->top_offset) {
        editor->top_offset = (editor->cursor_offset / NHEX_BYTES_PER_ROW) * NHEX_BYTES_PER_ROW;
        return;
    }
    if (editor->cursor_offset < editor->top_offset + visible) {
        return;
    }
    cursor_row = editor->cursor_offset / NHEX_BYTES_PER_ROW;
    top_row = cursor_row >= NHEX_VISIBLE_ROWS - 1 ? cursor_row - (NHEX_VISIBLE_ROWS - 1) : 0;
    editor->top_offset = top_row * NHEX_BYTES_PER_ROW;
}

bool editor_move(Editor *editor, EditorMove move) {
    uint64_t size = file_buffer_size(editor->buffer);
    uint64_t target;
    uint64_t amount = NHEX_BYTES_PER_ROW;

    if (size == 0) {
        return false;
    }
    target = editor->cursor_offset;
    if (move == EDIT_MOVE_PAGE_UP || move == EDIT_MOVE_PAGE_DOWN) {
        amount *= NHEX_VISIBLE_ROWS;
    }

    switch (move) {
        case EDIT_MOVE_LEFT:
            if (target == 0) return false;
            target--;
            break;
        case EDIT_MOVE_RIGHT:
            if (target >= size - 1) return false;
            target++;
            break;
        case EDIT_MOVE_UP:
        case EDIT_MOVE_PAGE_UP:
            target = target > amount ? target - amount : 0;
            break;
        case EDIT_MOVE_DOWN:
        case EDIT_MOVE_PAGE_DOWN:
            target = amount <= (size - 1) - target ? target + amount : size - 1;
            break;
        case EDIT_MOVE_HOME:
            target = (target / NHEX_BYTES_PER_ROW) * NHEX_BYTES_PER_ROW;
            break;
        case EDIT_MOVE_END:
            target = (target / NHEX_BYTES_PER_ROW) * NHEX_BYTES_PER_ROW + (NHEX_BYTES_PER_ROW - 1);
            if (target >= size) target = size - 1;
            break;
    }
    if (target == editor->cursor_offset) {
        return false;
    }
    editor->cursor_offset = target;
    editor->high_nibble = true;
    editor_ensure_visible(editor);
    return true;
}

bool editor_go_to(Editor *editor, const char *expression) {
    const char *cursor = expression;
    char *end;
    bool relative = false;
    bool negative = false;
    int base = 10;
    unsigned long long parsed;
    uint64_t target;
    uint64_t size = file_buffer_size(editor->buffer);

    if (!cursor) {
        return false;
    }
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '+' || *cursor == '-') {
        relative = true;
        negative = *cursor == '-';
        cursor++;
    }
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16;
        cursor += 2;
    }
    if (*cursor == '\0') {
        return false;
    }
    errno = 0;
    parsed = strtoull(cursor, &end, base);
    if (errno == ERANGE || end == cursor) {
        return false;
    }
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0' || parsed > UINT64_MAX) {
        return false;
    }

    if (!relative) {
        target = (uint64_t)parsed;
    } else if (negative) {
        if ((uint64_t)parsed > editor->cursor_offset) {
            return false;
        }
        target = editor->cursor_offset - (uint64_t)parsed;
    } else {
        if ((uint64_t)parsed > UINT64_MAX - editor->cursor_offset) {
            return false;
        }
        target = editor->cursor_offset + (uint64_t)parsed;
    }
    if ((size == 0 && target != 0) || (size != 0 && target >= size)) {
        return false;
    }
    editor->cursor_offset = target;
    editor->high_nibble = true;
    editor_ensure_visible(editor);
    return true;
}

bool editor_write_byte(Editor *editor, uint64_t offset, uint8_t value) {
    return file_buffer_write(editor->buffer, offset, value);
}

bool editor_input_hex(Editor *editor, int nibble) {
    uint8_t old_value;
    uint8_t new_value;

    if (editor->mode != EDIT_MODE_HEX || nibble < 0 || nibble > 15 ||
        !file_buffer_read(editor->buffer, editor->cursor_offset, &old_value)) {
        return false;
    }
    if (editor->high_nibble) {
        new_value = (uint8_t)((old_value & 0x0f) | ((uint8_t)nibble << 4));
        if (!editor_write_byte(editor, editor->cursor_offset, new_value)) {
            return false;
        }
        editor->high_nibble = false;
    } else {
        new_value = (uint8_t)((old_value & 0xf0) | (uint8_t)nibble);
        if (!editor_write_byte(editor, editor->cursor_offset, new_value)) {
            return false;
        }
        editor->high_nibble = true;
        if (editor->cursor_offset < file_last_offset(editor)) {
            editor->cursor_offset++;
            editor_ensure_visible(editor);
        }
    }
    return true;
}

bool editor_input_ascii(Editor *editor, uint8_t value) {
    if (editor->mode != EDIT_MODE_ASCII || value < 0x20 || value > 0x7e ||
        !editor_write_byte(editor, editor->cursor_offset, value)) {
        return false;
    }
    if (editor->cursor_offset < file_last_offset(editor)) {
        editor->cursor_offset++;
        editor_ensure_visible(editor);
    }
    return true;
}

bool editor_current_byte(Editor *editor, uint8_t *value) {
    return file_buffer_read(editor->buffer, editor->cursor_offset, value);
}
