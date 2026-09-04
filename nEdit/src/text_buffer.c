#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE

#include "text_buffer.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define COPY_BLOCK_SIZE 4096u

static void set_error(TextBuffer *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(buffer->error, sizeof(buffer->error), format, args);
    va_end(args);
}

static bool copy_path(char destination[NEDIT_PATH_MAX], const char *source) {
    size_t length;
    if (!source) return false;
    length = strlen(source);
    if (length == 0 || length >= NEDIT_PATH_MAX) return false;
    memcpy(destination, source, length + 1);
    return true;
}

static bool suffix_path(char destination[NEDIT_PATH_MAX], const char *path, const char *suffix) {
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    if (path_length + suffix_length >= NEDIT_PATH_MAX) return false;
    memcpy(destination, path, path_length);
    memcpy(destination + path_length, suffix, suffix_length + 1);
    return true;
}

size_t text_buffer_size(const TextBuffer *buffer) {
    return buffer ? buffer->capacity - (buffer->gap_end - buffer->gap_start) : 0;
}

static size_t gap_size(const TextBuffer *buffer) {
    return buffer->gap_end - buffer->gap_start;
}

static bool move_gap(TextBuffer *buffer, size_t offset) {
    size_t size = text_buffer_size(buffer);
    if (offset > size) {
        set_error(buffer, "text offset is outside buffer");
        return false;
    }
    if (offset < buffer->gap_start) {
        size_t amount = buffer->gap_start - offset;
        memmove(buffer->data + buffer->gap_end - amount, buffer->data + offset, amount);
        buffer->gap_start = offset;
        buffer->gap_end -= amount;
    } else if (offset > buffer->gap_start) {
        size_t amount = offset - buffer->gap_start;
        memmove(buffer->data + buffer->gap_start, buffer->data + buffer->gap_end, amount);
        buffer->gap_start += amount;
        buffer->gap_end += amount;
    }
    return true;
}

static bool ensure_gap(TextBuffer *buffer, size_t required) {
    size_t size;
    size_t new_capacity;
    uint8_t *data;
    size_t tail;

    if (gap_size(buffer) >= required) return true;
    size = text_buffer_size(buffer);
    if (required > SIZE_MAX - size - NEDIT_INITIAL_GAP) {
        set_error(buffer, "text buffer size overflow");
        return false;
    }
    new_capacity = size + required + NEDIT_INITIAL_GAP;
    if (buffer->capacity <= SIZE_MAX / 2 && new_capacity < buffer->capacity * 2) {
        new_capacity = buffer->capacity * 2;
    }
    data = (uint8_t *)malloc(new_capacity);
    if (!data) {
        set_error(buffer, "not enough memory to grow text");
        return false;
    }
    tail = buffer->capacity - buffer->gap_end;
    memcpy(data, buffer->data, buffer->gap_start);
    memcpy(data + new_capacity - tail, buffer->data + buffer->gap_end, tail);
    free(buffer->data);
    buffer->data = data;
    buffer->capacity = new_capacity;
    buffer->gap_end = new_capacity - tail;
    return true;
}

bool text_buffer_open(TextBuffer *buffer, const char *path) {
    FILE *file;
    off_t end;
    size_t size;
    size_t capacity;

    if (!buffer) return false;
    memset(buffer, 0, sizeof(*buffer));
    if (!copy_path(buffer->path, path)) {
        set_error(buffer, "file path is empty or too long");
        return false;
    }
    file = fopen(path, "rb");
    if (!file) {
        set_error(buffer, "cannot open file: %s", strerror(errno));
        return false;
    }
    if (fseeko(file, 0, SEEK_END) != 0 || (end = ftello(file)) < 0 ||
        (uint64_t)end > SIZE_MAX - NEDIT_INITIAL_GAP) {
        set_error(buffer, "cannot determine supported file size");
        fclose(file);
        return false;
    }
    size = (size_t)end;
    capacity = size + NEDIT_INITIAL_GAP;
    if (capacity < 4096) capacity = 4096;
    buffer->data = (uint8_t *)malloc(capacity);
    if (!buffer->data) {
        set_error(buffer, "not enough memory to open file");
        fclose(file);
        return false;
    }
    rewind(file);
    if (size != 0 && fread(buffer->data, 1, size, file) != size) {
        set_error(buffer, "file read failed");
        fclose(file);
        free(buffer->data);
        buffer->data = NULL;
        return false;
    }
    if (fclose(file) != 0) {
        set_error(buffer, "file close failed: %s", strerror(errno));
        free(buffer->data);
        buffer->data = NULL;
        return false;
    }
    buffer->capacity = capacity;
    buffer->gap_start = size;
    buffer->gap_end = capacity;
    return true;
}

bool text_buffer_create(TextBuffer *buffer, const char *path) {
    FILE *file;
    FILE *existing;
    if (!buffer) return false;
    memset(buffer, 0, sizeof(*buffer));
    if (!copy_path(buffer->path, path)) {
        set_error(buffer, "file path is empty or too long");
        return false;
    }
    existing = fopen(path, "rb");
    if (existing) {
        fclose(existing);
        set_error(buffer, "file already exists");
        return false;
    }
    file = fopen(path, "wb");
    if (!file) {
        set_error(buffer, "cannot create file: %s", strerror(errno));
        return false;
    }
    {
        int flush_result = fflush(file);
        int close_result = fclose(file);
        if (flush_result != 0 || close_result != 0) {
            set_error(buffer, "created file flush/close failed: %s", strerror(errno));
            remove(path);
            return false;
        }
    }
    if (!text_buffer_open(buffer, path)) {
        remove(path);
        return false;
    }
    return true;
}

void text_buffer_close(TextBuffer *buffer) {
    if (!buffer) return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

bool text_buffer_reload(TextBuffer *buffer) {
    TextBuffer replacement;
    if (!buffer) return false;
    if (!text_buffer_open(&replacement, buffer->path)) {
        strncpy(buffer->error, replacement.error, sizeof(buffer->error) - 1);
        buffer->error[sizeof(buffer->error) - 1] = '\0';
        return false;
    }
    text_buffer_close(buffer);
    *buffer = replacement;
    return true;
}

bool text_buffer_get(const TextBuffer *buffer, size_t offset, uint8_t *value) {
    size_t physical;
    if (!buffer || !value || offset >= text_buffer_size(buffer)) return false;
    physical = offset < buffer->gap_start ? offset : offset + gap_size(buffer);
    *value = buffer->data[physical];
    return true;
}

bool text_buffer_insert_span(TextBuffer *buffer, size_t offset, const uint8_t *data, size_t length) {
    if (!buffer || (!data && length != 0) || !move_gap(buffer, offset) ||
        !ensure_gap(buffer, length)) return false;
    if (length != 0) {
        memcpy(buffer->data + buffer->gap_start, data, length);
        buffer->gap_start += length;
        buffer->modified = true;
    }
    return true;
}

bool text_buffer_insert(TextBuffer *buffer, size_t offset, uint8_t value) {
    return text_buffer_insert_span(buffer, offset, &value, 1);
}

bool text_buffer_delete(TextBuffer *buffer, size_t offset, size_t length) {
    size_t size;
    if (!buffer || !move_gap(buffer, offset)) return false;
    size = text_buffer_size(buffer);
    if (length > size - offset) {
        set_error(buffer, "delete range is outside text");
        return false;
    }
    if (length != 0) {
        buffer->gap_end += length;
        buffer->modified = true;
    }
    return true;
}

static bool destination_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

static bool copy_file(TextBuffer *buffer, const char *source_path,
                      const char *destination_path, const char *operation) {
    uint8_t data[COPY_BLOCK_SIZE];
    FILE *input = fopen(source_path, "rb");
    FILE *output;
    bool ok = true;
    if (!input) {
        set_error(buffer, "%s source open failed: %s", operation, strerror(errno));
        return false;
    }
    output = fopen(destination_path, "wb");
    if (!output) {
        set_error(buffer, "%s destination open failed: %s", operation, strerror(errno));
        fclose(input);
        return false;
    }
    for (;;) {
        size_t count = fread(data, 1, sizeof(data), input);
        if (count && fwrite(data, 1, count, output) != count) {
            set_error(buffer, "%s write failed: %s", operation, strerror(errno));
            ok = false;
            break;
        }
        if (count < sizeof(data)) {
            if (ferror(input)) {
                set_error(buffer, "%s read failed: %s", operation, strerror(errno));
                ok = false;
            }
            break;
        }
    }
    if (ok && fflush(output) != 0) {
        set_error(buffer, "%s flush failed: %s", operation, strerror(errno));
        ok = false;
    }
    if (fclose(output) != 0 && ok) {
        set_error(buffer, "%s close failed: %s", operation, strerror(errno));
        ok = false;
    }
    if (fclose(input) != 0 && ok) {
        set_error(buffer, "%s source close failed: %s", operation, strerror(errno));
        ok = false;
    }
    return ok;
}

static bool install_temporary(TextBuffer *buffer, const char *temporary, const char *destination) {
    char backup[NEDIT_PATH_MAX];
    char install_error[NEDIT_ERROR_MAX];
    bool existed = destination_exists(destination);
    if (!suffix_path(backup, destination, ".nedit.backup")) {
        set_error(buffer, "path is too long for backup");
        return false;
    }
    if (destination_exists(backup)) {
        set_error(buffer, "backup already exists: %s", backup);
        return false;
    }
    if (existed && !copy_file(buffer, destination, backup, "backup")) return false;
    if (!copy_file(buffer, temporary, destination, "install")) {
        strncpy(install_error, buffer->error, sizeof(install_error) - 1);
        install_error[sizeof(install_error) - 1] = '\0';
        if (existed) {
            if (copy_file(buffer, backup, destination, "restore"))
                set_error(buffer, "%s; original restored", install_error);
            else
                set_error(buffer, "save and restore failed; backup kept at %s", backup);
        } else {
            remove(destination);
            set_error(buffer, "%s", install_error);
        }
        return false;
    }
    remove(temporary);
    if (existed) remove(backup);
    return true;
}

bool text_buffer_save_as(TextBuffer *buffer, const char *path) {
    char temporary[NEDIT_PATH_MAX];
    FILE *output;
    size_t tail;
    if (!buffer || !buffer->data || !path || !path[0]) return false;
    if (!suffix_path(temporary, path, ".nedit.tmp")) {
        set_error(buffer, "path is too long for temporary file");
        return false;
    }
    remove(temporary);
    output = fopen(temporary, "wb");
    if (!output) {
        set_error(buffer, "cannot create temporary file: %s", strerror(errno));
        return false;
    }
    tail = buffer->capacity - buffer->gap_end;
    if ((buffer->gap_start && fwrite(buffer->data, 1, buffer->gap_start, output) != buffer->gap_start) ||
        (tail && fwrite(buffer->data + buffer->gap_end, 1, tail, output) != tail)) {
        set_error(buffer, "temporary file write failed: %s", strerror(errno));
        fclose(output);
        remove(temporary);
        return false;
    }
    {
        int flush_result = fflush(output);
        int close_result = fclose(output);
        if (flush_result != 0 || close_result != 0) {
            set_error(buffer, "temporary file flush/close failed: %s", strerror(errno));
            remove(temporary);
            return false;
        }
    }
    if (!install_temporary(buffer, temporary, path)) {
        remove(temporary);
        return false;
    }
    if (!copy_path(buffer->path, path)) {
        set_error(buffer, "saved path is too long");
        return false;
    }
    buffer->modified = false;
    buffer->error[0] = '\0';
    return true;
}

bool text_buffer_save(TextBuffer *buffer) {
    return buffer ? text_buffer_save_as(buffer, buffer->path) : false;
}

bool text_buffer_modified(const TextBuffer *buffer) {
    return buffer && buffer->modified;
}

const char *text_buffer_path(const TextBuffer *buffer) {
    return buffer ? buffer->path : "";
}

const char *text_buffer_error(const TextBuffer *buffer) {
    return buffer ? buffer->error : "invalid text buffer";
}
