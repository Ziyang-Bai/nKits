#ifndef NTEXTS_FILE_REPLACE_H
#define NTEXTS_FILE_REPLACE_H

#include <stddef.h>

typedef struct {
    const void *data;
    size_t size;
} FilePart;

int file_replace_parts(const char *path, const FilePart *parts, size_t count);

#endif
