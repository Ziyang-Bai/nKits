#ifndef NHEX_FILE_BUFFER_H
#define NHEX_FILE_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#define NHEX_PAGE_SIZE 4096u
#define NHEX_CACHE_SLOTS 6u
#define NHEX_PATH_MAX 512u
#define NHEX_ERROR_MAX 160u

typedef struct {
    uint64_t page_index;
    uint64_t journal_offset;
    size_t valid;
} DirtyRecord;

typedef struct {
    uint64_t page_index;
    uint64_t stamp;
    size_t valid;
    bool loaded;
    bool dirty;
    uint8_t data[NHEX_PAGE_SIZE];
} FilePage;

typedef struct {
    FILE *source;
    FILE *journal;
    uint64_t size;
    uint64_t source_size;
    uint64_t clock;
    bool modified;
    char path[NHEX_PATH_MAX];
    char journal_path[NHEX_PATH_MAX];
    char error[NHEX_ERROR_MAX];
    FilePage pages[NHEX_CACHE_SLOTS];
    DirtyRecord *dirty_records;
    size_t dirty_count;
    size_t dirty_capacity;
} FileBuffer;

bool file_buffer_open(FileBuffer *buffer, const char *path);
bool file_buffer_create(FileBuffer *buffer, const char *path);
void file_buffer_close(FileBuffer *buffer);
bool file_buffer_reload(FileBuffer *buffer);

bool file_buffer_read(FileBuffer *buffer, uint64_t offset, uint8_t *value);
bool file_buffer_read_span(FileBuffer *buffer, uint64_t offset, uint8_t *data, size_t length);
bool file_buffer_write(FileBuffer *buffer, uint64_t offset, uint8_t value);
bool file_buffer_extend(FileBuffer *buffer, uint64_t new_size);

bool file_buffer_save(FileBuffer *buffer);
bool file_buffer_save_as(FileBuffer *buffer, const char *path);

uint64_t file_buffer_size(const FileBuffer *buffer);
bool file_buffer_modified(const FileBuffer *buffer);
const char *file_buffer_path(const FileBuffer *buffer);
const char *file_buffer_error(const FileBuffer *buffer);

#endif
