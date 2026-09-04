#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE

#include "file_buffer.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static void set_error(FileBuffer *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(buffer->error, sizeof(buffer->error), format, args);
    va_end(args);
}

static bool copy_path(char destination[NHEX_PATH_MAX], const char *source) {
    size_t length;
    if (!source) {
        return false;
    }
    length = strlen(source);
    if (length == 0 || length >= NHEX_PATH_MAX) {
        return false;
    }
    memcpy(destination, source, length + 1);
    return true;
}

static bool suffix_path(char destination[NHEX_PATH_MAX], const char *path, const char *suffix) {
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    if (path_length + suffix_length >= NHEX_PATH_MAX) {
        return false;
    }
    memcpy(destination, path, path_length);
    memcpy(destination + path_length, suffix, suffix_length + 1);
    return true;
}

static bool seek_file(FileBuffer *buffer, FILE *file, uint64_t offset, const char *operation) {
    off_t position = (off_t)offset;
    if (position < 0 || (uint64_t)position != offset) {
        set_error(buffer, "%s offset is outside SDK range", operation);
        return false;
    }
    if (fseeko(file, position, SEEK_SET) != 0) {
        set_error(buffer, "%s seek failed: %s", operation, strerror(errno));
        return false;
    }
    return true;
}

static bool read_exact(FileBuffer *buffer, FILE *file, void *data, size_t length, const char *operation) {
    if (length == 0) {
        return true;
    }
    if (fread(data, 1, length, file) != length) {
        set_error(buffer, "%s read failed", operation);
        return false;
    }
    return true;
}

static bool write_exact(FileBuffer *buffer, FILE *file, const void *data, size_t length, const char *operation) {
    if (length == 0) {
        return true;
    }
    if (fwrite(data, 1, length, file) != length) {
        set_error(buffer, "%s write failed: %s", operation, strerror(errno));
        return false;
    }
    return true;
}

static size_t record_position(const FileBuffer *buffer, uint64_t page_index, bool *found) {
    size_t low = 0;
    size_t high = buffer->dirty_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (buffer->dirty_records[middle].page_index < page_index) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *found = low < buffer->dirty_count && buffer->dirty_records[low].page_index == page_index;
    return low;
}

static DirtyRecord *ensure_record(FileBuffer *buffer, uint64_t page_index) {
    bool found;
    size_t position = record_position(buffer, page_index, &found);
    DirtyRecord *records;
    size_t capacity;

    if (found) {
        return &buffer->dirty_records[position];
    }
    if (buffer->dirty_count == buffer->dirty_capacity) {
        capacity = buffer->dirty_capacity == 0 ? 8 : buffer->dirty_capacity * 2;
        if (capacity < buffer->dirty_capacity || capacity > SIZE_MAX / sizeof(*records)) {
            set_error(buffer, "dirty page index is too large");
            return NULL;
        }
        records = (DirtyRecord *)realloc(buffer->dirty_records, capacity * sizeof(*records));
        if (!records) {
            set_error(buffer, "out of memory indexing dirty pages");
            return NULL;
        }
        buffer->dirty_records = records;
        buffer->dirty_capacity = capacity;
    }
    if (position < buffer->dirty_count) {
        memmove(&buffer->dirty_records[position + 1], &buffer->dirty_records[position],
                (buffer->dirty_count - position) * sizeof(*buffer->dirty_records));
    }
    buffer->dirty_count++;
    buffer->dirty_records[position].page_index = page_index;
    buffer->dirty_records[position].journal_offset = 0;
    buffer->dirty_records[position].valid = 0;
    return &buffer->dirty_records[position];
}

static DirtyRecord *find_record(FileBuffer *buffer, uint64_t page_index) {
    bool found;
    size_t position = record_position(buffer, page_index, &found);
    return found ? &buffer->dirty_records[position] : NULL;
}

static size_t page_valid_size(const FileBuffer *buffer, uint64_t page_index) {
    uint64_t base = page_index * (uint64_t)NHEX_PAGE_SIZE;
    uint64_t remaining = buffer->size - base;
    return remaining < NHEX_PAGE_SIZE ? (size_t)remaining : NHEX_PAGE_SIZE;
}

static size_t source_page_size(const FileBuffer *buffer, uint64_t page_index) {
    uint64_t base = page_index * (uint64_t)NHEX_PAGE_SIZE;
    uint64_t remaining;
    if (base >= buffer->source_size) return 0;
    remaining = buffer->source_size - base;
    return remaining < NHEX_PAGE_SIZE ? (size_t)remaining : NHEX_PAGE_SIZE;
}

static bool store_page(FileBuffer *buffer, FilePage *page) {
    DirtyRecord *record;
    off_t journal_offset;

    if (!page->dirty) {
        return true;
    }
    if (fseeko(buffer->journal, 0, SEEK_END) != 0) {
        set_error(buffer, "journal seek failed: %s", strerror(errno));
        return false;
    }
    journal_offset = ftello(buffer->journal);
    if (journal_offset < 0) {
        set_error(buffer, "journal position failed: %s", strerror(errno));
        return false;
    }
    record = ensure_record(buffer, page->page_index);
    if (!record) {
        return false;
    }
    if (!write_exact(buffer, buffer->journal, page->data, page->valid, "journal")) {
        return false;
    }
    if (fflush(buffer->journal) != 0) {
        set_error(buffer, "journal flush failed: %s", strerror(errno));
        return false;
    }
    record->journal_offset = (uint64_t)journal_offset;
    record->valid = page->valid;
    page->dirty = false;
    return true;
}

static FilePage *load_page(FileBuffer *buffer, uint64_t page_index) {
    FilePage *page = NULL;
    DirtyRecord *record;
    size_t i;

    for (i = 0; i < NHEX_CACHE_SLOTS; ++i) {
        if (buffer->pages[i].loaded && buffer->pages[i].page_index == page_index) {
            buffer->pages[i].stamp = ++buffer->clock;
            return &buffer->pages[i];
        }
        if (!buffer->pages[i].loaded) {
            page = &buffer->pages[i];
        }
    }
    if (!page) {
        page = &buffer->pages[0];
        for (i = 1; i < NHEX_CACHE_SLOTS; ++i) {
            if (buffer->pages[i].stamp < page->stamp) {
                page = &buffer->pages[i];
            }
        }
        if (!store_page(buffer, page)) {
            return NULL;
        }
    }

    page->page_index = page_index;
    page->valid = page_valid_size(buffer, page_index);
    record = find_record(buffer, page_index);
    if (record) {
        size_t stored = record->valid < page->valid ? record->valid : page->valid;
        if (!seek_file(buffer, buffer->journal, record->journal_offset, "journal") ||
            !read_exact(buffer, buffer->journal, page->data, stored, "journal")) {
            page->loaded = false;
            return NULL;
        }
        memset(page->data + stored, 0, NHEX_PAGE_SIZE - stored);
    } else {
        uint64_t offset = page_index * (uint64_t)NHEX_PAGE_SIZE;
        size_t source_valid = source_page_size(buffer, page_index);
        if (source_valid != 0 &&
            (!seek_file(buffer, buffer->source, offset, "source") ||
             !read_exact(buffer, buffer->source, page->data, source_valid, "source"))) {
            page->loaded = false;
            return NULL;
        }
        memset(page->data + source_valid, 0, NHEX_PAGE_SIZE - source_valid);
    }
    page->loaded = true;
    page->dirty = false;
    page->stamp = ++buffer->clock;
    return page;
}

static bool open_journal(FileBuffer *buffer) {
    if (!suffix_path(buffer->journal_path, buffer->path, ".nhex.journal")) {
        set_error(buffer, "path is too long for journal");
        return false;
    }
    remove(buffer->journal_path);
    buffer->journal = fopen(buffer->journal_path, "wb+");
    if (!buffer->journal) {
        set_error(buffer, "cannot create journal: %s", strerror(errno));
        return false;
    }
    return true;
}

bool file_buffer_open(FileBuffer *buffer, const char *path) {
    off_t size;

    if (!buffer) {
        return false;
    }
    memset(buffer, 0, sizeof(*buffer));
    if (!copy_path(buffer->path, path)) {
        set_error(buffer, "file path is empty or too long");
        return false;
    }
    buffer->source = fopen(path, "rb");
    if (!buffer->source) {
        set_error(buffer, "cannot open file: %s", strerror(errno));
        return false;
    }
    if (fseeko(buffer->source, 0, SEEK_END) != 0 || (size = ftello(buffer->source)) < 0) {
        set_error(buffer, "cannot determine file size: %s", strerror(errno));
        fclose(buffer->source);
        buffer->source = NULL;
        return false;
    }
    buffer->size = (uint64_t)size;
    buffer->source_size = buffer->size;
    if (!open_journal(buffer)) {
        fclose(buffer->source);
        buffer->source = NULL;
        return false;
    }
    return true;
}

bool file_buffer_create(FileBuffer *buffer, const char *path) {
    FILE *existing;
    FILE *file;
    int flush_result;
    int close_result;
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
    flush_result = fflush(file);
    close_result = fclose(file);
    if (flush_result != 0 || close_result != 0) {
        set_error(buffer, "created file flush/close failed: %s", strerror(errno));
        remove(path);
        return false;
    }
    if (!file_buffer_open(buffer, path)) {
        remove(path);
        return false;
    }
    return true;
}

void file_buffer_close(FileBuffer *buffer) {
    if (!buffer) {
        return;
    }
    if (buffer->source) {
        fclose(buffer->source);
    }
    if (buffer->journal) {
        fclose(buffer->journal);
    }
    if (buffer->journal_path[0]) {
        remove(buffer->journal_path);
    }
    free(buffer->dirty_records);
    memset(buffer, 0, sizeof(*buffer));
}

bool file_buffer_reload(FileBuffer *buffer) {
    char path[NHEX_PATH_MAX];
    if (!buffer || !copy_path(path, buffer->path)) {
        return false;
    }
    file_buffer_close(buffer);
    return file_buffer_open(buffer, path);
}

bool file_buffer_read(FileBuffer *buffer, uint64_t offset, uint8_t *value) {
    FilePage *page;
    if (!buffer || !value || offset >= buffer->size) {
        if (buffer) {
            set_error(buffer, "read offset is outside file");
        }
        return false;
    }
    page = load_page(buffer, offset / NHEX_PAGE_SIZE);
    if (!page) {
        return false;
    }
    *value = page->data[offset % NHEX_PAGE_SIZE];
    return true;
}

bool file_buffer_read_span(FileBuffer *buffer, uint64_t offset, uint8_t *data, size_t length) {
    size_t copied = 0;
    if (!buffer || (!data && length != 0) || offset > buffer->size ||
        (uint64_t)length > buffer->size - offset) {
        if (buffer) {
            set_error(buffer, "read range is outside file");
        }
        return false;
    }
    while (copied < length) {
        uint64_t current = offset + copied;
        size_t in_page = (size_t)(current % NHEX_PAGE_SIZE);
        size_t chunk = NHEX_PAGE_SIZE - in_page;
        FilePage *page = load_page(buffer, current / NHEX_PAGE_SIZE);
        if (!page) {
            return false;
        }
        if (chunk > length - copied) {
            chunk = length - copied;
        }
        memcpy(data + copied, page->data + in_page, chunk);
        copied += chunk;
    }
    return true;
}

bool file_buffer_write(FileBuffer *buffer, uint64_t offset, uint8_t value) {
    FilePage *page;
    size_t in_page;
    if (!buffer || offset >= buffer->size) {
        if (buffer) {
            set_error(buffer, "write offset is outside file");
        }
        return false;
    }
    page = load_page(buffer, offset / NHEX_PAGE_SIZE);
    if (!page) {
        return false;
    }
    in_page = (size_t)(offset % NHEX_PAGE_SIZE);
    if (page->data[in_page] == value) {
        return true;
    }
    page->data[in_page] = value;
    page->dirty = true;
    page->stamp = ++buffer->clock;
    buffer->modified = true;
    return true;
}

bool file_buffer_extend(FileBuffer *buffer, uint64_t new_size) {
    size_t index;
    if (!buffer || !buffer->source) return false;
    if (new_size < buffer->size) {
        set_error(buffer, "new size cannot be smaller than current size");
        return false;
    }
    if (new_size == buffer->size) return true;
    buffer->size = new_size;
    for (index = 0; index < NHEX_CACHE_SLOTS; ++index) {
        FilePage *page = &buffer->pages[index];
        size_t new_valid;
        DirtyRecord *record;
        if (!page->loaded) continue;
        new_valid = page_valid_size(buffer, page->page_index);
        if (new_valid <= page->valid) continue;
        memset(page->data + page->valid, 0, new_valid - page->valid);
        page->valid = new_valid;
        record = find_record(buffer, page->page_index);
        if (record && record->valid < new_valid) page->dirty = true;
    }
    buffer->modified = true;
    buffer->error[0] = '\0';
    return true;
}

static bool persist_pages(FileBuffer *buffer) {
    size_t i;
    for (i = 0; i < NHEX_CACHE_SLOTS; ++i) {
        if (buffer->pages[i].loaded && !store_page(buffer, &buffer->pages[i])) {
            return false;
        }
    }
    return true;
}

static bool destination_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    fclose(file);
    return true;
}

static bool copy_file_contents(FileBuffer *buffer, const char *source_path,
                               const char *destination_path, const char *operation) {
    uint8_t data[NHEX_PAGE_SIZE];
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
        if (count != 0 && fwrite(data, 1, count, output) != count) {
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

static bool replace_file(FileBuffer *buffer, const char *temporary, const char *destination) {
    char backup[NHEX_PATH_MAX];
    char install_error[NHEX_ERROR_MAX];
    bool had_destination = destination_exists(destination);

    if (!suffix_path(backup, destination, ".nhex.backup")) {
        set_error(buffer, "path is too long for backup");
        return false;
    }
    if (destination_exists(backup)) {
        set_error(buffer, "backup already exists; recover or remove %s", backup);
        return false;
    }
    if (had_destination &&
        !copy_file_contents(buffer, destination, backup, "backup")) {
        return false;
    }
    if (!copy_file_contents(buffer, temporary, destination, "install")) {
        strncpy(install_error, buffer->error, sizeof(install_error) - 1);
        install_error[sizeof(install_error) - 1] = '\0';
        if (had_destination) {
            if (copy_file_contents(buffer, backup, destination, "restore")) {
                set_error(buffer, "%s; original restored", install_error);
            } else {
                set_error(buffer, "save and restore failed; backup kept at %s", backup);
            }
        } else {
            remove(destination);
            set_error(buffer, "%s", install_error);
        }
        return false;
    }
    remove(temporary);
    if (had_destination) {
        remove(backup);
    }
    return true;
}

static bool reset_after_save(FileBuffer *buffer, const char *path) {
    size_t i;

    if (buffer->source) {
        fclose(buffer->source);
        buffer->source = NULL;
    }
    if (buffer->journal) {
        fclose(buffer->journal);
        buffer->journal = NULL;
    }
    if (buffer->journal_path[0]) {
        remove(buffer->journal_path);
    }
    if (!copy_path(buffer->path, path)) {
        set_error(buffer, "saved path is too long");
        return false;
    }
    buffer->source = fopen(buffer->path, "rb");
    if (!buffer->source) {
        set_error(buffer, "saved file cannot be reopened: %s", strerror(errno));
        return false;
    }
    free(buffer->dirty_records);
    buffer->dirty_records = NULL;
    buffer->dirty_count = 0;
    buffer->dirty_capacity = 0;
    for (i = 0; i < NHEX_CACHE_SLOTS; ++i) {
        buffer->pages[i].loaded = false;
        buffer->pages[i].dirty = false;
    }
    buffer->modified = false;
    buffer->source_size = buffer->size;
    buffer->clock = 0;
    if (!open_journal(buffer)) {
        return false;
    }
    buffer->error[0] = '\0';
    return true;
}

bool file_buffer_save_as(FileBuffer *buffer, const char *path) {
    char temporary[NHEX_PATH_MAX];
    uint8_t data[NHEX_PAGE_SIZE];
    FILE *output;
    uint64_t page_count;
    uint64_t page_index;

    if (!buffer || !buffer->source || !copy_path(temporary, path)) {
        if (buffer) {
            set_error(buffer, "save path is empty or too long");
        }
        return false;
    }
    if (!persist_pages(buffer)) {
        return false;
    }
    if (!suffix_path(temporary, path, ".nhex.tmp")) {
        set_error(buffer, "path is too long for temporary file");
        return false;
    }
    remove(temporary);
    output = fopen(temporary, "wb");
    if (!output) {
        set_error(buffer, "cannot create temporary file: %s", strerror(errno));
        return false;
    }

    page_count = buffer->size / NHEX_PAGE_SIZE + (buffer->size % NHEX_PAGE_SIZE != 0);
    for (page_index = 0; page_index < page_count; ++page_index) {
        DirtyRecord *record = find_record(buffer, page_index);
        size_t valid = page_valid_size(buffer, page_index);
        size_t stored = record ? record->valid : source_page_size(buffer, page_index);
        bool ok = true;
        if (stored > valid) stored = valid;
        memset(data, 0, valid);
        if (stored != 0) {
            FILE *source = record ? buffer->journal : buffer->source;
            uint64_t offset = record ? record->journal_offset
                                     : page_index * (uint64_t)NHEX_PAGE_SIZE;
            ok = seek_file(buffer, source, offset, record ? "journal" : "source") &&
                 read_exact(buffer, source, data, stored, record ? "journal" : "source");
        }
        if (!ok || !write_exact(buffer, output, data, valid, "temporary file")) {
            fclose(output);
            remove(temporary);
            return false;
        }
    }
    if (fflush(output) != 0) {
        set_error(buffer, "temporary file flush failed: %s", strerror(errno));
        fclose(output);
        remove(temporary);
        return false;
    }
    if (fclose(output) != 0) {
        set_error(buffer, "temporary file close failed: %s", strerror(errno));
        remove(temporary);
        return false;
    }

    if (strcmp(path, buffer->path) == 0) {
        fclose(buffer->source);
        buffer->source = NULL;
    }
    if (!replace_file(buffer, temporary, path)) {
        if (!buffer->source) {
            buffer->source = fopen(buffer->path, "rb");
        }
        remove(temporary);
        return false;
    }
    return reset_after_save(buffer, path);
}

bool file_buffer_save(FileBuffer *buffer) {
    return buffer ? file_buffer_save_as(buffer, buffer->path) : false;
}

uint64_t file_buffer_size(const FileBuffer *buffer) {
    return buffer ? buffer->size : 0;
}

bool file_buffer_modified(const FileBuffer *buffer) {
    return buffer && buffer->modified;
}

const char *file_buffer_path(const FileBuffer *buffer) {
    return buffer ? buffer->path : "";
}

const char *file_buffer_error(const FileBuffer *buffer) {
    return buffer ? buffer->error : "invalid file buffer";
}
