#include "name_editor.h"

#include <stdlib.h>
#include <string.h>

#define NAME_BYTES 765
#define NAME_UNITS 255
#define NAME_CAPACITY 768

static bool decode(const char *text, size_t *length, uint32_t *scalar) {
    *length = text_next(text, scalar);
    return *length != 1 || (unsigned char)text[0] < 0x80;
}

static bool measure(const char *text, size_t *units) {
    size_t length;
    uint32_t scalar;
    *units = 0;
    while (*text) {
        if (!decode(text, &length, &scalar)) return false;
        *units += scalar > 0xffff ? 2 : 1;
        text += length;
    }
    return true;
}

static bool forbidden(uint32_t scalar) {
    return scalar < 0x20 || scalar == 0x7f ||
           (scalar < 0x80 && strchr("\"*/:<>?\\|", (int)scalar));
}

bool name_editor_init(NameEditor *editor, const char *text) {
    size_t length, units;
    memset(editor, 0, sizeof(*editor));
    length = strlen(text);
    if (length > NAME_BYTES || !measure(text, &units) || units > NAME_UNITS) {
        strcpy(editor->error, "Invalid UTF-8 or name too long");
        return false;
    }
    editor->text = malloc(NAME_CAPACITY);
    if (!editor->text) {
        strcpy(editor->error, "Out of memory");
        return false;
    }
    memcpy(editor->text, text, length + 1);
    editor->cursor = length;
    return true;
}

void name_editor_destroy(NameEditor *editor) {
    free(editor->text);
    memset(editor, 0, sizeof(*editor));
}

size_t name_editor_units(const NameEditor *editor) {
    size_t units = 0;
    if (editor->text) measure(editor->text, &units);
    return units;
}

uint32_t name_editor_scalar(const NameEditor *editor) {
    size_t length;
    uint32_t scalar = 0;
    if (editor->text) decode(editor->text + editor->cursor, &length, &scalar);
    return scalar;
}

static size_t previous(const char *text, size_t cursor) {
    if (cursor) {
        --cursor;
        while (cursor && ((unsigned char)text[cursor] & 0xc0) == 0x80) --cursor;
    }
    return cursor;
}

static bool insert(NameEditor *editor, uint32_t scalar) {
    unsigned char bytes[4];
    size_t count, length = strlen(editor->text);
    if (!scalar || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
        strcpy(editor->error, "Invalid Unicode scalar");
        return false;
    }
    if (forbidden(scalar)) {
        strcpy(editor->error, "Character not allowed in a filename");
        return false;
    }
    if (scalar < 0x80) {
        bytes[0] = (unsigned char)scalar;
        count = 1;
    } else if (scalar < 0x800) {
        bytes[0] = 0xc0 | (scalar >> 6);
        bytes[1] = 0x80 | (scalar & 0x3f);
        count = 2;
    } else if (scalar < 0x10000) {
        bytes[0] = 0xe0 | (scalar >> 12);
        bytes[1] = 0x80 | ((scalar >> 6) & 0x3f);
        bytes[2] = 0x80 | (scalar & 0x3f);
        count = 3;
    } else {
        bytes[0] = 0xf0 | (scalar >> 18);
        bytes[1] = 0x80 | ((scalar >> 12) & 0x3f);
        bytes[2] = 0x80 | ((scalar >> 6) & 0x3f);
        bytes[3] = 0x80 | (scalar & 0x3f);
        count = 4;
    }
    if (length + count > NAME_BYTES ||
        name_editor_units(editor) + (scalar > 0xffff ? 2 : 1) > NAME_UNITS) {
        strcpy(editor->error, "Name exceeds 255 UTF-16 units or 765 bytes");
        return false;
    }
    memmove(editor->text + editor->cursor + count, editor->text + editor->cursor,
            length - editor->cursor + 1);
    memcpy(editor->text + editor->cursor, bytes, count);
    editor->cursor += count;
    return true;
}

static bool valid_name(NameEditor *editor) {
    size_t length = strlen(editor->text), units, offset = 0, count;
    uint32_t scalar;
    if (!length) {
        strcpy(editor->error, "Name is empty");
        return false;
    }
    if (length > NAME_BYTES || !measure(editor->text, &units) || units > NAME_UNITS) {
        strcpy(editor->error, "Invalid UTF-8 or name too long");
        return false;
    }
    if (!strcmp(editor->text, ".") || !strcmp(editor->text, "..")) {
        strcpy(editor->error, "Dot names are not allowed");
        return false;
    }
    if (editor->text[length - 1] == '.' || editor->text[length - 1] == ' ') {
        strcpy(editor->error, "Name cannot end in a dot or space");
        return false;
    }
    while (offset < length) {
        decode(editor->text + offset, &count, &scalar);
        if (forbidden(scalar)) {
            strcpy(editor->error, "Character not allowed in a filename");
            return false;
        }
        offset += count;
    }
    return true;
}

static int hex_digit(uint32_t scalar) {
    if (scalar >= '0' && scalar <= '9') return (int)(scalar - '0');
    if (scalar >= 'a' && scalar <= 'f') return (int)(scalar - 'a') + 10;
    if (scalar >= 'A' && scalar <= 'F') return (int)(scalar - 'A') + 10;
    return -1;
}

NameEditResult name_editor_handle(NameEditor *editor, InputEvent event) {
    size_t length, count, start;
    uint32_t scalar;
    if (event.action == INPUT_NONE) return NAME_EDITING;
    editor->error[0] = '\0';
    if (event.action == INPUT_CATALOG) {
        editor->hex_mode = !editor->hex_mode;
        editor->hex[0] = '\0';
        return NAME_EDITING;
    }
    if (editor->hex_mode) {
        length = strlen(editor->hex);
        switch (event.action) {
        case INPUT_BACK:
            editor->hex_mode = false;
            editor->hex[0] = '\0';
            break;
        case INPUT_BACKSPACE:
            if (length) editor->hex[length - 1] = '\0';
            break;
        case INPUT_CHARACTER:
            if (hex_digit(event.character) < 0) {
                strcpy(editor->error, "Enter 1-6 hexadecimal digits");
            } else if (length == 6) {
                strcpy(editor->error, "At most 6 hexadecimal digits");
            } else {
                editor->hex[length] = "0123456789ABCDEF"[hex_digit(event.character)];
                editor->hex[length + 1] = '\0';
            }
            break;
        case INPUT_ENTER:
            if (!length) {
                strcpy(editor->error, "Enter 1-6 hexadecimal digits");
                break;
            }
            scalar = 0;
            for (count = 0; count < length; ++count)
                scalar = (scalar << 4) | (uint32_t)hex_digit(editor->hex[count]);
            if (insert(editor, scalar)) {
                editor->hex_mode = false;
                editor->hex[0] = '\0';
            }
            break;
        default:
            break;
        }
        return NAME_EDITING;
    }
    switch (event.action) {
    case INPUT_BACK:
        return NAME_CANCELLED;
    case INPUT_ENTER:
        return valid_name(editor) ? NAME_ACCEPTED : NAME_EDITING;
    case INPUT_LEFT:
        editor->cursor = previous(editor->text, editor->cursor);
        break;
    case INPUT_RIGHT:
        decode(editor->text + editor->cursor, &count, &scalar);
        editor->cursor += count;
        break;
    case INPUT_HOME:
        editor->cursor = 0;
        break;
    case INPUT_END:
        editor->cursor = strlen(editor->text);
        break;
    case INPUT_BACKSPACE:
        start = previous(editor->text, editor->cursor);
        memmove(editor->text + start, editor->text + editor->cursor,
                strlen(editor->text + editor->cursor) + 1);
        editor->cursor = start;
        break;
    case INPUT_DELETE:
        decode(editor->text + editor->cursor, &count, &scalar);
        memmove(editor->text + editor->cursor, editor->text + editor->cursor + count,
                strlen(editor->text + editor->cursor + count) + 1);
        break;
    case INPUT_CHARACTER:
        insert(editor, event.character);
        break;
    default:
        break;
    }
    return NAME_EDITING;
}

void name_editor_view(const NameEditor *editor, char *out, size_t capacity,
                      unsigned pixels, TextWidth width, unsigned *cursor_x) {
    size_t start = editor->cursor, offset, count, used = 0, reserve_bytes = 0;
    unsigned x = 0, glyph, reserve = 0;
    uint32_t scalar;
    *cursor_x = 0;
    if (!capacity) return;
    out[0] = '\0';
    if (capacity == 1 || !editor->text) return;
    count = text_next(editor->text + editor->cursor, &scalar);
    if (count) {
        glyph = width(scalar);
        if (glyph <= pixels && count < capacity) {
            reserve = glyph;
            reserve_bytes = count;
        }
    }
    while (start) {
        offset = previous(editor->text, start);
        count = text_next(editor->text + offset, &scalar);
        glyph = width(scalar);
        if (glyph > pixels - reserve - x ||
            count > capacity - 1 - reserve_bytes - used) break;
        x += glyph;
        used += count;
        start = offset;
    }
    memcpy(out, editor->text + start, used);
    *cursor_x = x;
    offset = editor->cursor;
    while ((count = text_next(editor->text + offset, &scalar)) != 0) {
        glyph = width(scalar);
        if (glyph > pixels - x || count > capacity - 1 - used) break;
        memcpy(out + used, editor->text + offset, count);
        x += glyph;
        used += count;
        offset += count;
    }
    out[used] = '\0';
}
