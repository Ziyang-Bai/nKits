#ifndef NUSB_NAME_EDITOR_H
#define NUSB_NAME_EDITOR_H

#include "input.h"
#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *text;
    size_t cursor;
    bool hex_mode;
    char hex[7];
    char error[96];
} NameEditor;

typedef enum {
    NAME_EDITING, NAME_ACCEPTED, NAME_CANCELLED
} NameEditResult;

bool name_editor_init(NameEditor *editor, const char *text);
void name_editor_destroy(NameEditor *editor);
NameEditResult name_editor_handle(NameEditor *editor, InputEvent event);
size_t name_editor_units(const NameEditor *editor);
/* The scalar after the byte cursor, or zero at the end. */
uint32_t name_editor_scalar(const NameEditor *editor);
/* Capacity includes NUL; pixels excludes the caller's cursor margin. */
void name_editor_view(const NameEditor *editor, char *out, size_t capacity,
                      unsigned pixels, TextWidth width, unsigned *cursor_x);

#endif
