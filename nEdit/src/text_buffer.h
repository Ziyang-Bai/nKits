#ifndef NEDIT_TEXT_BUFFER_H
#define NEDIT_TEXT_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEDIT_PATH_MAX 512u
#define NEDIT_ERROR_MAX 160u
#define NEDIT_INITIAL_GAP 1024u

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t gap_start;
    size_t gap_end;
    bool modified;
    char path[NEDIT_PATH_MAX];
    char error[NEDIT_ERROR_MAX];
} TextBuffer;

bool text_buffer_open(TextBuffer *buffer, const char *path);
bool text_buffer_create(TextBuffer *buffer, const char *path);
void text_buffer_close(TextBuffer *buffer);
bool text_buffer_reload(TextBuffer *buffer);

size_t text_buffer_size(const TextBuffer *buffer);
bool text_buffer_get(const TextBuffer *buffer, size_t offset, uint8_t *value);
bool text_buffer_insert(TextBuffer *buffer, size_t offset, uint8_t value);
bool text_buffer_insert_span(TextBuffer *buffer, size_t offset, const uint8_t *data, size_t length);
bool text_buffer_delete(TextBuffer *buffer, size_t offset, size_t length);

bool text_buffer_save(TextBuffer *buffer);
bool text_buffer_save_as(TextBuffer *buffer, const char *path);
bool text_buffer_modified(const TextBuffer *buffer);
const char *text_buffer_path(const TextBuffer *buffer);
const char *text_buffer_error(const TextBuffer *buffer);

#endif
