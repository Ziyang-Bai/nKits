#include "file_replace.h"

#include <errno.h>
#include <stdio.h>

static int write_parts(const char *path, const FilePart *parts, size_t count) {
    FILE *fp;
    size_t i;
    int ok = 1;

    fp = fopen(path, "wb");
    if (!fp) return 0;
    for (i = 0; i < count; ++i) {
        if (parts[i].size && fwrite(parts[i].data, 1, parts[i].size, fp) != parts[i].size) {
            ok = 0;
            break;
        }
    }
    if (ok && fflush(fp) != 0) ok = 0;
    if (fclose(fp) != 0) ok = 0;
    return ok;
}

static int copy_file(const char *source_path, const char *destination_path) {
    unsigned char buffer[4096];
    FILE *source;
    FILE *destination;
    size_t size;
    int ok = 1;

    source = fopen(source_path, "rb");
    if (!source) return 0;
    destination = fopen(destination_path, "wb");
    if (!destination) {
        fclose(source);
        return 0;
    }
    while ((size = fread(buffer, 1, sizeof(buffer), source)) != 0) {
        if (fwrite(buffer, 1, size, destination) != size) {
            ok = 0;
            break;
        }
    }
    if (ferror(source)) ok = 0;
    if (ok && fflush(destination) != 0) ok = 0;
    if (fclose(source) != 0) ok = 0;
    if (fclose(destination) != 0) ok = 0;
    return ok;
}

int file_replace_parts(const char *path, const FilePart *parts, size_t count) {
    char temporary[600];
    int rename_errno;
    int result;

    size_t i;
    if (!path || !parts || !count ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) {
        errno = EINVAL;
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (parts[i].size && !parts[i].data) {
            errno = EINVAL;
            return 0;
        }
    }
    if (!write_parts(temporary, parts, count)) {
        int saved_errno = errno;
        remove(temporary);
        errno = saved_errno ? saved_errno : EIO;
        return 0;
    }
    if (rename(temporary, path) == 0) return 1;
    errno = 0;
    result = copy_file(temporary, path);
    if (!result && !errno) errno = EIO;
    rename_errno = errno;
    remove(temporary);
    errno = rename_errno;
    return result;
}
