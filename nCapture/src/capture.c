#include "capture.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#ifdef NCAPTURE_RESIDENT
#include "resident.h"
#include <syscall-list.h>
/* Event callbacks must not enter Ndless's SWI dispatch. */
#define strlen ((size_t (*)(const char *))resident_native(e_strlen))
#define strchr ((char *(*)(const char *, int))resident_native(e_strchr))
#define strrchr ((char *(*)(const char *, int))resident_native(e_strrchr))
#define strncmp ((int (*)(const char *, const char *, size_t))resident_native(e_strncmp))
#define memset ((void *(*)(void *, int, size_t))resident_native(e_memset))
#define snprintf ((int (*)(char *, size_t, const char *, ...))resident_native(e_snprintf))
#endif

int capture_hotkey(CaptureHotkey *hotkey, unsigned type, unsigned ascii,
                   unsigned key, unsigned modifiers) {
    if (type == 16 && (ascii == '.' || key == CAPTURE_PERIOD_KEY || key == hotkey->key)) {
        int pressed = !hotkey->held && (modifiers & 4) &&
            (ascii == '.' || key == CAPTURE_PERIOD_KEY);
        hotkey->held = 0;
        return pressed;
    }
    if (type != 8 || (ascii != '.' && key != CAPTURE_PERIOD_KEY) || !(modifiers & 4)) return 0;
    if (hotkey->held) return 0;
    hotkey->held = 1;
    hotkey->key = key;
    return 1;
}

static void put16(unsigned char *p, unsigned value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void put32(unsigned char *p, uint32_t value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

static int write_all(const CaptureIo *io, int fd, const void *data, unsigned size) {
    const unsigned char *p = data;
    while (size) {
        int count = io->write(fd, p, size);
        if (count < 0) {
            int error = io->error();
            if (error == EINTR) continue;
            return error ? error : EIO;
        }
        if (!count || (unsigned)count > size) return EIO;
        p += count;
        size -= (unsigned)count;
    }
    return 0;
}

int capture_bmp(const CaptureIo *io, int fd, const uint16_t *pixels,
                unsigned width, unsigned height) {
    unsigned char header[54];
    unsigned char row[CAPTURE_WIDTH * 3];
    unsigned stride, y;
    if (!width || width > CAPTURE_WIDTH || !height || height > CAPTURE_HEIGHT)
        return EINVAL;
    memset(header, 0, sizeof(header));
    stride = (width * 3 + 3) & ~3u;
    header[0] = 'B';
    header[1] = 'M';
    put32(header + 2, 54 + stride * height);
    put32(header + 10, 54);
    put32(header + 14, 40);
    put32(header + 18, width);
    put32(header + 22, height);
    put16(header + 26, 1);
    put16(header + 28, 24);
    put32(header + 34, stride * height);
    {
        int error = write_all(io, fd, header, sizeof(header));
        if (error) return error;
    }
    for (y = height; y; --y) {
        unsigned x;
        memset(row, 0, stride);
        for (x = 0; x < width; ++x) {
            uint16_t pixel = pixels[(y - 1) * width + x];
            unsigned r = (pixel >> 11) & 31;
            unsigned g = (pixel >> 5) & 63;
            unsigned b = pixel & 31;
            row[x * 3] = (unsigned char)((b << 3) | (b >> 2));
            row[x * 3 + 1] = (unsigned char)((g << 2) | (g >> 4));
            row[x * 3 + 2] = (unsigned char)((r << 3) | (r >> 2));
        }
        {
            int error = write_all(io, fd, row, stride);
            if (error) return error;
        }
    }
    return 0;
}

static int chosen_path_valid(const char *path) {
    const char *part;
    if (strncmp(path, "/documents/", 11) || !path[11] ||
        strlen(path) >= CAPTURE_PATH_MAX) return 0;
    for (part = path + 11; *part;) {
        const char *end = strchr(part, '/');
        size_t len = end ? (size_t)(end - part) : strlen(part);
        size_t i;
        if (!len || (len == 1 && *part == '.') ||
            (len == 2 && part[0] == '.' && part[1] == '.')) return 0;
        for (i = 0; i < len; ++i) {
            unsigned char c = (unsigned char)part[i];
            if (c < 32 || c == 127 || c == '\\' || c == ':') return 0;
        }
        if (!end) return 1;
        part = end + 1;
    }
    return 0;
}

int capture_dialog_path(const char *folder, const char *name,
                        char *path, size_t size) {
    size_t len = strlen(folder);
    int count;
    if (size) path[0] = '\0';
    if (!*name || strchr(name, '/') || strchr(name, '\\') ||
        strlen(name) >= CAPTURE_DIALOG_MAX || len >= CAPTURE_DIALOG_MAX) return 0;
    while (*folder == '/') { ++folder; --len; }
    while (len && folder[len - 1] == '/') --len;
    count = snprintf(path, size, "/documents/%.*s%s%s.tns",
                     (int)len, folder, len ? "/" : "", name);
    return count >= 0 && (size_t)count < size && chosen_path_valid(path);
}

static CaptureResult publish(const CaptureIo *io, const char *path, int replace,
                              char *temporary, char *backup, size_t size) {
    CaptureResult result = {0};
    unsigned number;
    int folder_len = (int)(strrchr(path, '/') - path);
    if (io->rename(temporary, path) == 0) {
        temporary[0] = '\0';
        return result;
    }
    result.code = CAPTURE_RENAME_ERROR;
    result.error = io->error();
    if (result.error != EEXIST || !replace) return result;
    /* Confirmed replacement retains the prior file in a separate backup. */
    for (number = 1; number <= CAPTURE_LAST_NUMBER; ++number) {
        int count = snprintf(backup, size, "%.*s/.ncapture-%06u.bak", folder_len, path, number);
        if (count < 0 || (size_t)count >= size) {
            backup[0] = '\0';
            result.code = CAPTURE_BAD_PATH;
            result.error = 0;
            return result;
        }
        if (io->rename(path, backup) == 0) break;
        result.error = io->error();
        if (result.error != EEXIST) {
            backup[0] = '\0';
            return result;
        }
    }
    if (number > CAPTURE_LAST_NUMBER) {
        backup[0] = '\0';
        result.code = CAPTURE_NO_NAMES;
        result.error = 0;
        return result;
    }
    if (io->rename(temporary, path) < 0) {
        result.error = io->error();
        if (io->rename(backup, path) < 0) result.backup_error = io->error();
        else backup[0] = '\0';
        return result;
    }
    temporary[0] = '\0';
    result.code = CAPTURE_OK;
    result.error = 0;
    if (io->remove(backup) < 0) {
        result.code = CAPTURE_BACKUP_ERROR;
        result.backup_error = io->error();
    } else backup[0] = '\0';
    return result;
}

CaptureResult capture_save_path(const CaptureIo *io, const char *path, int replace,
                                const uint16_t *pixels, unsigned width, unsigned height,
                                char *temporary, char *backup, size_t path_size) {
    CaptureResult result = {0};
    unsigned number;
    int fd = -1;
    if (path_size) temporary[0] = backup[0] = '\0';
    if (!chosen_path_valid(path) || !io->rename || !path_size ||
        !width || width > CAPTURE_WIDTH || !height || height > CAPTURE_HEIGHT) {
        result.code = CAPTURE_BAD_PATH;
        return result;
    }
    int folder_len = (int)(strrchr(path, '/') - path);
    /* A complete temporary image preserves the target through write failures. */
    for (number = 1; number <= CAPTURE_LAST_NUMBER; ++number) {
        int count = snprintf(temporary, path_size, "%.*s/.ncapture-%06u.part",
                             folder_len, path, number);
        if (count < 0 || (size_t)count >= path_size) {
            result.code = CAPTURE_BAD_PATH;
            return result;
        }
        fd = io->open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd >= 0) break;
        result.error = io->error();
        if (result.error != EEXIST) {
            result.code = CAPTURE_OPEN_ERROR;
            return result;
        }
    }
    if (fd < 0) {
        result.code = CAPTURE_NO_NAMES;
        result.error = 0;
        return result;
    }
    result.error = capture_bmp(io, fd, pixels, width, height);
    if (result.error) result.code = CAPTURE_WRITE_ERROR;
    if (io->close(fd) < 0 && result.code == CAPTURE_OK) {
        result.code = CAPTURE_CLOSE_ERROR;
        result.error = io->error();
    }
    if (result.code == CAPTURE_OK) result = publish(io, path, replace, temporary, backup, path_size);
    if (*temporary) {
        if (io->remove(temporary) < 0) result.cleanup_error = io->error();
        else temporary[0] = '\0';
    }
    return result;
}

void capture_preview(const uint16_t *shot, const uint16_t *frame,
                     volatile uint16_t *screen) {
    unsigned i, x, y;
    for (i = 0; i < CAPTURE_WIDTH * CAPTURE_HEIGHT; ++i) screen[i] = frame[i];
    for (y = 0; y < CAPTURE_PREVIEW_HEIGHT; ++y)
        for (x = 0; x < CAPTURE_PREVIEW_WIDTH; ++x)
            screen[(y + 37) * CAPTURE_WIDTH + x + 60] =
                shot[(y * CAPTURE_HEIGHT / CAPTURE_PREVIEW_HEIGHT) * CAPTURE_WIDTH +
                     x * CAPTURE_WIDTH / CAPTURE_PREVIEW_WIDTH];
}
