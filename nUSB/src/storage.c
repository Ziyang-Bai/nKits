#ifdef NUSB_HOST
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "storage.h"
#ifndef NUSB_HOST
#include "resident.h"
#include <libndls.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <utime.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

struct StorageFile {
    StorageKind kind;
    FsFile *usb;
#ifdef NUSB_HOST
    FILE *internal;
#else
    int internal;
#endif
    uint64_t size, position;
    unsigned flags;
};
struct StorageDir {
    StorageLocation location;
    FsDir *usb;
#ifdef NUSB_HOST
    DIR *internal;
#else
    NUC_DIR *internal;
#endif
};
static unsigned mutations;
static uint64_t internal_revision;

bool storage_mutating(void) { return mutations != 0; }
uint64_t storage_revision(StorageKind kind) {
    return kind == STORAGE_USB ? fs_revision() : internal_revision;
}
static void namespace_changed(StorageKind kind, FsResult result) {
#ifndef NUSB_HOST
    if (kind == STORAGE_USB && (result == FS_OK || result == FS_IO_ERROR ||
        result == FS_NO_DEVICE || result == FS_CANCELLED))
        resident_api()->namespace_changed();
#else
    (void)kind; (void)result;
#endif
}
static FsResult io_error(void) {
    switch (errno) {
        case ENOENT: case ENOTDIR: return FS_NOT_FOUND;
        case EEXIST: return FS_EXISTS;
        case ENOMEM: return FS_NO_MEMORY;
        case ENOSPC: return FS_NO_SPACE;
        case EACCES: case EPERM: return FS_DENIED;
        case EROFS: return FS_WRITE_PROTECTED;
        case ENAMETOOLONG: return FS_PATH_TOO_LONG;
        case EFBIG: case EOVERFLOW: return FS_TOO_LARGE;
        default: return FS_IO_ERROR;
    }
}
FsResult storage_validate_name(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    unsigned units = 0;
    size_t length;
    if (!name || !(length = strlen(name))) return FS_INVALID_ARGUMENT;
    if (length > 765) return FS_PATH_TOO_LONG;
    if (!strcmp(name, ".") || !strcmp(name, "..") || name[length - 1] == '.' ||
        name[length - 1] == ' ') return FS_INVALID_ARGUMENT;
    while (*p) {
        uint32_t c = *p++;
        unsigned n = 0;
        uint32_t minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { c &= 31; n = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { c &= 15; n = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { c &= 7; n = 3; minimum = 0x10000; }
        else if (c >= 0x80) return FS_INVALID_ARGUMENT;
        while (n--) {
            if ((*p & 0xc0) != 0x80) return FS_INVALID_ARGUMENT;
            c = (c << 6) | (*p++ & 63);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) ||
            c < 32 || c == 127 || (c < 128 && strchr("\"*/:<>?\\|", (int)c)))
            return FS_INVALID_ARGUMENT;
        units += c >= 0x10000 ? 2 : 1;
        if (units > 255) return FS_PATH_TOO_LONG;
    }
    return FS_OK;
}
FsResult storage_location(StorageLocation *out, StorageKind kind, const char *path) {
    StorageLocation value;
    const char *p;
    size_t root, length;
    if (!out || !path || (kind != STORAGE_USB && kind != STORAGE_INTERNAL))
        return FS_INVALID_ARGUMENT;
    value.kind = kind;
    if (kind == STORAGE_USB) {
        if (strncmp(path, "0:/", 3)) return FS_INVALID_ARGUMENT;
        strcpy(value.path, "0:/"); root = 3; p = path + 3;
    } else {
        if (strncmp(path, "/documents", 10) || (path[10] && path[10] != '/'))
            return FS_INVALID_ARGUMENT;
        strcpy(value.path, "/documents"); root = 10; p = path + 10;
    }
    length = root;
    while (*p) {
        const char *start;
        size_t n;
        while (*p == '/') ++p;
        start = p;
        while (*p && *p != '/') ++p;
        n = (size_t)(p - start);
        if (!n || (n == 1 && *start == '.')) continue;
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            if (length == root) return FS_DENIED;
            while (length > root && value.path[length - 1] != '/') --length;
            if (length > root) --length;
            value.path[length] = 0;
            continue;
        }
        if (n > (kind == STORAGE_USB ? 765u : 255u)) return FS_PATH_TOO_LONG;
        if (length + (value.path[length - 1] != '/') + n >=
            (kind == STORAGE_USB ? sizeof(value.path) : 260u)) return FS_PATH_TOO_LONG;
        if (value.path[length - 1] != '/') value.path[length++] = '/';
        memcpy(value.path + length, start, n); length += n; value.path[length] = 0;
    }
    if (kind == STORAGE_INTERNAL && !strncasecmp(value.path, "/documents/USB", 14) &&
        (!value.path[14] || value.path[14] == '/')) return FS_DENIED;
    *out = value;
    return FS_OK;
}
bool storage_is_root(const StorageLocation *location) {
    return location && !strcmp(location->path, location->kind == STORAGE_USB ? "0:/" : "/documents");
}
bool storage_parent(StorageLocation *location) {
    char *slash;
    if (!location || storage_is_root(location)) return false;
    slash = strrchr(location->path, '/');
    if (!slash) return false;
    if (location->kind == STORAGE_USB && slash == location->path + 2) slash[1] = 0;
    else *slash = 0;
    return true;
}
FsResult storage_join(StorageLocation *out, const StorageLocation *parent, const char *name) {
    StorageLocation value;
    FsResult result = storage_validate_name(name);
    size_t n;
    if (result != FS_OK || !out || !parent) return result != FS_OK ? result : FS_INVALID_ARGUMENT;
    value = *parent; n = strlen(value.path);
    if (n + (n && value.path[n - 1] != '/') + strlen(name) >= sizeof(value.path))
        return FS_PATH_TOO_LONG;
    if (n && value.path[n - 1] != '/') value.path[n++] = '/';
    strcpy(value.path + n, name);
    return storage_location(out, value.kind, value.path);
}
static FsResult checked(const StorageLocation *location) {
    StorageLocation normalized;
    FsResult result;
    if (!location) return FS_INVALID_ARGUMENT;
    result = storage_location(&normalized, location->kind, location->path);
    if (result == FS_OK && strcmp(location->path, normalized.path)) return FS_INVALID_ARGUMENT;
    return result;
}
#ifdef NUSB_HOST
/* Host tests use a private real Documents directory, never the host root. */
static FsResult host_path(const StorageLocation *location, char *path, size_t capacity) {
    const char *root = getenv("NUSB_TEST_DOCUMENTS");
    int n;
    if (!root || !*root) return FS_INVALID_ARGUMENT;
    n = snprintf(path, capacity, "%s%s", root, location->path + 10);
    return n < 0 || (size_t)n >= capacity ? FS_PATH_TOO_LONG : FS_OK;
}
static void packed_time(time_t value, uint16_t *date, uint16_t *clock) {
    struct tm t;
    *date = *clock = 0;
    if (!localtime_r(&value, &t) || t.tm_year < 80 || t.tm_year > 207) return;
    *date = (uint16_t)(((t.tm_year - 80) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
    *clock = (uint16_t)((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
}
static time_t unpacked_time(uint16_t date, uint16_t clock) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = 80 + (date >> 9); t.tm_mon = ((date >> 5) & 15) - 1;
    t.tm_mday = date & 31; t.tm_hour = clock >> 11;
    t.tm_min = (clock >> 5) & 63; t.tm_sec = (clock & 31) * 2; t.tm_isdst = -1;
    return mktime(&t);
}
#endif
FsResult storage_stat(const StorageLocation *location, FsEntry *entry) {
    FsResult result = checked(location);
    if (result != FS_OK || !entry) return result != FS_OK ? result : FS_INVALID_ARGUMENT;
    if (location->kind == STORAGE_USB) return fs_stat(location->path, entry);
#ifdef NUSB_HOST
    {
        char path[STORAGE_PATH_MAX]; struct stat st; uint16_t unused;
        result = host_path(location, path, sizeof(path));
        if (result != FS_OK) return result;
        if (lstat(path, &st)) return io_error();
        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) return FS_UNSUPPORTED;
        memset(entry, 0, sizeof(*entry));
        strcpy(entry->name, strrchr(location->path, '/') + 1);
        entry->is_directory = S_ISDIR(st.st_mode);
        entry->size = entry->is_directory ? 0 : (uint64_t)st.st_size;
        entry->attributes = (entry->is_directory ? 0x10 : 0x20) | ((st.st_mode & 0222) ? 0 : 1);
        packed_time(st.st_mtime, &entry->date, &entry->time);
        packed_time(st.st_atime, &entry->access_date, &unused);
        packed_time(st.st_ctime, &entry->created_date, &entry->created_time);
        return FS_OK;
    }
#else
    return resident_api()->internal_stat(location->path, entry);
#endif
}
FsResult storage_opendir(const StorageLocation *location, StorageDir **out) {
    StorageDir *dir;
    FsResult result = checked(location);
    if (!out) return FS_INVALID_ARGUMENT;
    *out = NULL;
    if (result != FS_OK) return result;
    dir = calloc(1, sizeof(*dir));
    if (!dir) return FS_NO_MEMORY;
    dir->location = *location;
    if (location->kind == STORAGE_USB) result = fs_opendir(location->path, &dir->usb);
    else {
#ifdef NUSB_HOST
        char path[STORAGE_PATH_MAX];
        result = host_path(location, path, sizeof(path));
        if (result == FS_OK) dir->internal = opendir(path);
#else
        dir->internal = nuc_opendir(location->path);
#endif
        if (result == FS_OK && !dir->internal) result = io_error();
    }
    if (result != FS_OK) { free(dir); return result; }
    *out = dir; return FS_OK;
}
FsResult storage_next(StorageDir *dir, FsEntry *entry, bool *end) {
    if (!dir || !entry || !end) return FS_INVALID_ARGUMENT;
    *end = false;
    if (dir->location.kind == STORAGE_USB) return fs_next(dir->usb, entry, end);
    for (;;) {
        StorageLocation child;
        FsResult result;
#ifdef NUSB_HOST
        struct dirent *item;
        errno = 0; item = readdir(dir->internal);
#else
        struct nuc_dirent *item;
        errno = 0; item = nuc_readdir(dir->internal);
#endif
        if (!item) { if (errno) return io_error(); *end = true; return FS_OK; }
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
        if (storage_is_root(&dir->location) && !strcasecmp(item->d_name, "USB")) continue;
        result = storage_join(&child, &dir->location, item->d_name);
        if (result != FS_OK) return result;
        return storage_stat(&child, entry);
    }
}
void storage_closedir(StorageDir *dir) {
    if (!dir) return;
    if (dir->location.kind == STORAGE_USB) fs_closedir(dir->usb);
#ifdef NUSB_HOST
    else closedir(dir->internal);
#else
    else nuc_closedir(dir->internal);
#endif
    free(dir);
}
FsResult storage_open(const StorageLocation *location, unsigned flags, StorageFile **out) {
    StorageFile *file;
    FsResult result = checked(location);
#ifdef NUSB_HOST
    FsEntry entry;
    const char *path;
    char host[STORAGE_PATH_MAX];
    int fd, mode;
#endif
    if (!out) return FS_INVALID_ARGUMENT;
    *out = NULL;
    if (result != FS_OK) return result;
    if (!(flags & (FS_OPEN_READ | FS_OPEN_WRITE)) ||
        (flags & ~(FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE | FS_OPEN_TRUNCATE | FS_OPEN_APPEND)) ||
        ((flags & (FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE | FS_OPEN_TRUNCATE | FS_OPEN_APPEND)) && !(flags & FS_OPEN_WRITE)))
        return FS_INVALID_ARGUMENT;
    file = calloc(1, sizeof(*file));
    if (!file) return FS_NO_MEMORY;
    file->kind = location->kind; file->flags = flags;
    ++mutations;
    if (file->kind == STORAGE_USB) result = fs_open(location->path, flags, &file->usb);
    else {
#ifdef NUSB_HOST
        result = storage_stat(location, &entry);
        if (result == FS_OK && entry.is_directory) result = FS_DENIED;
        if (result == FS_OK && entry.size > INT32_MAX) result = FS_TOO_LARGE;
        if (result == FS_NOT_FOUND && (flags & FS_OPEN_CREATE)) result = FS_OK;
        if (result == FS_OK) {
            path = location->path;
            result = host_path(location, host, sizeof(host)); path = host;
            mode = (flags & FS_OPEN_WRITE) ? ((flags & FS_OPEN_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
            if (flags & FS_OPEN_CREATE) mode |= O_CREAT;
            if (flags & FS_OPEN_EXCLUSIVE) mode |= O_EXCL;
            if (flags & FS_OPEN_TRUNCATE) mode |= O_TRUNC;
            if (flags & FS_OPEN_APPEND) mode |= O_APPEND;
            if (result == FS_OK) {
                fd = open(path, mode, 0666);
                if (fd < 0) result = io_error();
                else {
                    file->internal = fdopen(fd, (flags & FS_OPEN_WRITE) ? ((flags & FS_OPEN_READ) ? "r+b" : "wb") : "rb");
                    if (!file->internal) {
                        result = io_error();
                        if (close(fd)) result = FS_IO_ERROR;
                    }
                    else {
                        if (fseeko(file->internal, 0, SEEK_END)) result = io_error();
                        else {
                            off_t n = ftello(file->internal);
                            if (n < 0) result = io_error();
                            else file->size = (uint64_t)n;
                        }
                        if (result == FS_OK && !(flags & FS_OPEN_APPEND) && fseeko(file->internal, 0, SEEK_SET)) result = io_error();
                        file->position = flags & FS_OPEN_APPEND ? file->size : 0;
                        if (result != FS_OK) {
                            if (fclose(file->internal)) result = FS_IO_ERROR;
                            file->internal = NULL;
                        }
                    }
                    if (flags & FS_OPEN_CREATE) ++internal_revision;
                    if (result != FS_OK &&
                        (flags & (FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE)) == (FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE)) {
                        /* Only this successful exclusive open owns the new leaf. */
                        if (unlink(path)) result = FS_IO_ERROR;
                        else ++internal_revision;
                    }
                }
            }
        }
#else
        /* SDK exclusive open reports EEXIST for missing files; use the publisher's REL operations. */
        result = resident_api()->internal_open(location->path, flags, &file->internal, &file->size);
        if (result == FS_OK) {
            file->position = flags & FS_OPEN_APPEND ? file->size : 0;
            if (flags & FS_OPEN_CREATE) ++internal_revision;
        }
#endif
    }
    if (flags & FS_OPEN_CREATE) namespace_changed(location->kind, result);
    --mutations;
    if (result != FS_OK) { free(file); return result; }
    *out = file; return FS_OK;
}
FsResult storage_read(StorageFile *file, void *buf, uint32_t count, uint32_t *actual) {
#ifdef NUSB_HOST
    size_t n;
#else
    FsResult result;
#endif
    if (actual) *actual = 0;
    if (!file || !actual || (!buf && count)) return FS_INVALID_ARGUMENT;
    if (file->kind == STORAGE_USB) return fs_read(file->usb, buf, count, actual);
    if (!(file->flags & FS_OPEN_READ)) return FS_DENIED;
#ifdef NUSB_HOST
    n = fread(buf, 1, count, file->internal); *actual = (uint32_t)n; file->position += n;
    return ferror(file->internal) ? io_error() : FS_OK;
#else
    result = resident_api()->internal_read(file->internal, buf, count, actual);
    if (*actual > count || file->position > INT32_MAX ||
        *actual > (uint64_t)INT32_MAX - file->position) return FS_IO_ERROR;
    file->position += *actual;
    return result;
#endif
}
FsResult storage_write(StorageFile *file, const void *buf, uint32_t count, uint32_t *actual) {
    FsResult result;
#ifdef NUSB_HOST
    size_t n;
#endif
    if (actual) *actual = 0;
    if (!file || !actual || (!buf && count)) return FS_INVALID_ARGUMENT;
    ++mutations;
    if (file->kind == STORAGE_USB) result = fs_write(file->usb, buf, count, actual);
    else if (!(file->flags & FS_OPEN_WRITE)) result = FS_DENIED;
    else {
        if (file->flags & FS_OPEN_APPEND) {
#ifdef NUSB_HOST
            off_t position;
            if (fseeko(file->internal, 0, SEEK_END) || (position = ftello(file->internal)) < 0) {
                result = io_error(); --mutations; return result;
            }
            file->position = file->size = (uint64_t)position;
#else
            uint32_t position;
            result = resident_api()->internal_seek(file->internal, 0, SEEK_END, &position);
            if (result != FS_OK) { --mutations; return result; }
            if (position > INT32_MAX) { --mutations; return FS_TOO_LARGE; }
            file->position = file->size = position;
#endif
        }
        if (file->position > INT32_MAX || count > (uint64_t)INT32_MAX - file->position) result = FS_TOO_LARGE;
        else {
#ifdef NUSB_HOST
            n = fwrite(buf, 1, count, file->internal); *actual = (uint32_t)n;
            result = n == count ? FS_OK : io_error();
#else
            result = resident_api()->internal_write(file->internal, buf, count, actual);
#endif
            if (*actual > count) { --mutations; return FS_IO_ERROR; }
            file->position += *actual;
            if (file->position > file->size) file->size = file->position;
        }
    }
    --mutations; return result;
}
FsResult storage_seek(StorageFile *file, uint64_t offset) {
#ifdef NUSB_HOST
    off_t position;
#else
    uint32_t position;
    FsResult result;
#endif
    if (!file) return FS_INVALID_ARGUMENT;
    if (file->kind == STORAGE_USB) return fs_seek(file->usb, offset);
    if (offset > file->size) offset = file->size;
    if (offset > INT32_MAX) return FS_TOO_LARGE;
#ifdef NUSB_HOST
    position = (off_t)offset;
    if (position < 0 || (uint64_t)position != offset) return FS_TOO_LARGE;
    if (fseeko(file->internal, position, SEEK_SET)) return io_error();
#else
    result = resident_api()->internal_seek(file->internal, (uint32_t)offset, SEEK_SET, &position);
    if (result != FS_OK) return result;
    if (position != offset) return FS_IO_ERROR;
#endif
    file->position = offset; return FS_OK;
}
uint64_t storage_size(StorageFile *file) {
    return !file ? 0 : file->kind == STORAGE_USB ? fs_size(file->usb) : file->size;
}
FsResult storage_sync(StorageFile *file) {
    FsResult result;
    if (!file) return FS_INVALID_ARGUMENT;
    ++mutations;
    if (file->kind == STORAGE_USB) result = fs_sync(file->usb);
#ifdef NUSB_HOST
    else result = fflush(file->internal) ? io_error() : FS_OK;
#else
    else result = resident_api()->internal_sync(file->internal);
#endif
    --mutations; return result;
}
FsResult storage_close(StorageFile *file) {
    FsResult result;
    if (!file) return FS_INVALID_ARGUMENT;
    ++mutations;
    if (file->kind == STORAGE_USB) result = fs_close(file->usb);
#ifdef NUSB_HOST
    else result = fclose(file->internal) ? io_error() : FS_OK;
#else
    else result = resident_api()->internal_close(file->internal);
#endif
    --mutations; free(file); return result;
}
FsResult storage_mkdir(const StorageLocation *location) {
    FsResult result = checked(location);
    if (result != FS_OK) return result;
    if (storage_is_root(location)) return FS_DENIED;
    ++mutations;
    if (location->kind == STORAGE_USB) result = fs_mkdir(location->path);
    else {
        const char *path = location->path;
#ifdef NUSB_HOST
        char host[STORAGE_PATH_MAX]; result = host_path(location, host, sizeof(host)); path = host;
#endif
        if (result == FS_OK && mkdir(path, 0777)) result = io_error();
        if (result == FS_OK) ++internal_revision;
    }
    namespace_changed(location->kind, result);
    --mutations; return result;
}
FsResult storage_unlink(const StorageLocation *location) {
    FsResult result = checked(location);
    FsEntry entry;
    if (result != FS_OK) return result;
    if (storage_is_root(location)) return FS_DENIED;
    result = storage_stat(location, &entry);
    if (result != FS_OK) return result;
    if (entry.attributes & 1) return FS_DENIED;
    ++mutations;
    if (location->kind == STORAGE_USB) result = fs_unlink(location->path);
    else {
#ifdef NUSB_HOST
        char path[STORAGE_PATH_MAX]; result = host_path(location, path, sizeof(path));
        if (result == FS_OK && (entry.is_directory ? rmdir(path) : unlink(path))) result = io_error();
#else
        if (entry.is_directory) result = resident_api()->internal_rmdir(location->path);
        else if (remove(location->path)) result = io_error();
#endif
        if (result == FS_OK) ++internal_revision;
    }
    namespace_changed(location->kind, result);
    --mutations; return result;
}
FsResult storage_rename(const StorageLocation *from, const StorageLocation *to) {
    FsEntry entry;
    FsResult result = checked(from);
    if (result == FS_OK) result = checked(to);
    if (result != FS_OK) return result;
    if (from->kind != to->kind || storage_is_root(from) || storage_is_root(to)) return FS_DENIED;
    result = storage_stat(to, &entry);
    if (result == FS_OK) return FS_EXISTS;
    if (result != FS_NOT_FOUND) return result;
    ++mutations;
    if (from->kind == STORAGE_USB) result = fs_rename(from->path, to->path);
    else {
#ifdef NUSB_HOST
        char oldpath[STORAGE_PATH_MAX], newpath[STORAGE_PATH_MAX];
        result = host_path(from, oldpath, sizeof(oldpath));
        if (result == FS_OK) result = host_path(to, newpath, sizeof(newpath));
        if (result == FS_OK && renameat2(AT_FDCWD, oldpath, AT_FDCWD, newpath, RENAME_NOREPLACE)) result = io_error();
#else
        result = resident_api()->internal_rename(from->path, to->path);
#endif
        if (result == FS_OK) ++internal_revision;
    }
    namespace_changed(from->kind, result);
    --mutations; return result;
}
FsResult storage_set_metadata(const StorageLocation *location, uint8_t attributes, const FsTimes *times) {
    FsResult result = checked(location);
    if (result != FS_OK || !times) return result != FS_OK ? result : FS_INVALID_ARGUMENT;
    ++mutations;
    if (location->kind == STORAGE_USB) {
        result = fs_utime(location->path, times);
        if (result == FS_OK) result = fs_chmod(location->path, attributes);
    } else {
#ifdef NUSB_HOST
        char path[STORAGE_PATH_MAX]; struct utimbuf stamp; struct stat st;
        result = host_path(location, path, sizeof(path));
        stamp.actime = unpacked_time(times->access_date, 0);
        stamp.modtime = unpacked_time(times->modified_date, times->modified_time);
        if (result == FS_OK && utime(path, &stamp)) result = io_error();
        if (result == FS_OK && stat(path, &st)) result = io_error();
        if (result == FS_OK && chmod(path, attributes & 1 ? st.st_mode & ~0222 : st.st_mode | 0200)) result = io_error();
#else
        result = resident_api()->internal_set_metadata(location->path, attributes, times);
#endif
    }
    --mutations; return result;
}
FsResult storage_limits(const StorageLocation *location, StorageLimits *limits) {
    FsResult result = checked(location);
    if (result != FS_OK || !limits) return result != FS_OK ? result : FS_INVALID_ARGUMENT;
    if (location->kind == STORAGE_USB) return fs_volume_info(limits);
    memset(limits, 0, sizeof(*limits));
    limits->format = FS_FORMAT_INTERNAL; limits->writable = true;
    limits->max_file_size = INT32_MAX; limits->max_path_bytes = 259;
    limits->max_name_bytes = 255; limits->max_name_units = 255;
    return FS_OK;
}
