#include "capture.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int folder_fd;
static unsigned write_limit;
static int write_budget;
static int zero_write;
static int interrupt_write;
static int fail_close;
static int fail_remove;
static int fail_rename;
static unsigned rename_calls, fail_rename_at;
static int fail_restore;

static const char *local_path(const char *path) {
    assert(!strncmp(path, "/documents/", 11));
    return path + 11;
}

static int file_open(const char *path, int flags, int mode) {
    return openat(folder_fd, local_path(path), flags, mode);
}

static int file_write(int fd, const void *data, unsigned size) {
    int count;
    if (interrupt_write) {
        interrupt_write = 0;
        errno = EINTR;
        return -1;
    }
    if (write_budget == 0) {
        if (zero_write) return 0;
        errno = ENOSPC;
        return -1;
    }
    if (write_limit && size > write_limit) size = write_limit;
    if (write_budget > 0 && size > (unsigned)write_budget) size = (unsigned)write_budget;
    count = (int)write(fd, data, size);
    if (count > 0 && write_budget > 0) write_budget -= count;
    return count;
}

static int file_close(int fd) {
    int result = close(fd);
    if (fail_close) {
        errno = EIO;
        return -1;
    }
    return result;
}

static int file_remove(const char *path) {
    if (fail_remove) {
        errno = EACCES;
        return -1;
    }
    return unlinkat(folder_fd, local_path(path), 0);
}

static int file_error(void) { return errno; }

static int file_rename(const char *from, const char *to) {
    ++rename_calls;
    if (fail_rename || rename_calls == fail_rename_at ||
        (fail_restore && strstr(from, ".bak"))) {
        errno = EACCES;
        return -1;
    }
    if (faccessat(folder_fd, local_path(to), F_OK, 0) == 0) {
        errno = EEXIST;
        return -1;
    }
    return renameat(folder_fd, local_path(from), folder_fd, local_path(to));
}

static const CaptureIo io = {
    file_open, file_write, file_close, file_remove, file_error, file_rename
};

static void reset_io(void) {
    write_limit = 0;
    write_budget = -1;
    zero_write = interrupt_write = fail_close = fail_remove = 0;
    fail_rename = 0;
    rename_calls = fail_rename_at = 0;
    fail_restore = 0;
    errno = 0;
}

static size_t read_file(const char *name, unsigned char *data, size_t size) {
    int fd = openat(folder_fd, name, O_RDONLY);
    size_t used = 0;
    ssize_t count;
    assert(fd >= 0);
    while ((count = read(fd, data + used, size - used)) > 0) used += (size_t)count;
    assert(count == 0);
    assert(close(fd) == 0);
    return used;
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void test_bmp(void) {
    const uint16_t pixels[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    const unsigned char expected[] = {
        255, 0, 0, 255, 255, 255, 0, 0,
        0, 0, 255, 0, 255, 0, 0, 0
    };
    unsigned char data[128];
    const char *path = "/documents/orientation.bmp.tns";
    char temporary[CAPTURE_PATH_MAX], backup[CAPTURE_PATH_MAX];
    CaptureResult result;
    reset_io();
    write_limit = 3;
    interrupt_write = 1;
    result = capture_save_path(&io, path, 0, pixels, 2, 2,
                               temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_OK && !*temporary && !*backup);
    assert(read_file(local_path(path), data, sizeof(data)) == 70);
    assert(data[0] == 'B' && data[1] == 'M');
    assert(le32(data + 2) == 70 && le32(data + 10) == 54);
    assert(le32(data + 14) == 40 && le32(data + 18) == 2 && le32(data + 22) == 2);
    assert(data[26] == 1 && data[27] == 0 && data[28] == 24 && data[29] == 0);
    assert(le32(data + 30) == 0 && le32(data + 34) == 16);
    assert(!memcmp(data + 54, expected, sizeof(expected)));
}

static void test_channels_and_padding(void) {
    unsigned width;
    for (width = 1; width <= 4; ++width) {
        uint16_t pixels[4];
        unsigned char data[100];
        char path[CAPTURE_PATH_MAX], temporary[CAPTURE_PATH_MAX], backup[CAPTURE_PATH_MAX];
        unsigned i;
        unsigned stride = (width * 3 + 3) & ~3u;
        for (i = 0; i < width; ++i) pixels[i] = (17u << 11) | (42u << 5) | 9u;
        snprintf(path, sizeof(path), "/documents/channels-%u.bmp.tns", width);
        reset_io();
        assert(capture_save_path(&io, path, 0, pixels, width, 1,
                                 temporary, backup, sizeof(temporary)).code == CAPTURE_OK);
        assert(read_file(local_path(path), data, sizeof(data)) == 54 + stride);
        for (i = 0; i < width; ++i) {
            assert(data[54 + i * 3] == 74);
            assert(data[55 + i * 3] == 170);
            assert(data[56 + i * 3] == 140);
        }
        for (i = width * 3; i < stride; ++i) assert(data[54 + i] == 0);
    }
}

static void test_full_screen(void) {
    static uint16_t pixels[CAPTURE_WIDTH * CAPTURE_HEIGHT];
    static unsigned char data[54 + CAPTURE_WIDTH * CAPTURE_HEIGHT * 3 + 1];
    const char *path = "/documents/screen.bmp.tns";
    char temporary[CAPTURE_PATH_MAX], backup[CAPTURE_PATH_MAX];
    size_t last = 54 + (CAPTURE_HEIGHT - 1) * CAPTURE_WIDTH * 3;
    pixels[0] = 0xf800;
    pixels[CAPTURE_WIDTH - 1] = 0x07e0;
    pixels[(CAPTURE_HEIGHT - 1) * CAPTURE_WIDTH] = 0x001f;
    pixels[CAPTURE_WIDTH * CAPTURE_HEIGHT - 1] = 0xffff;
    reset_io();
    assert(capture_save_path(&io, path, 0, pixels, CAPTURE_WIDTH, CAPTURE_HEIGHT,
                             temporary, backup, sizeof(temporary)).code == CAPTURE_OK);
    assert(read_file(local_path(path), data, sizeof(data)) == 230454);
    assert(le32(data + 18) == 320 && le32(data + 22) == 240);
    assert(data[54] == 255 && data[55] == 0 && data[56] == 0);
    assert(data[last] == 0 && data[last + 1] == 0 && data[last + 2] == 255);
    assert(data[last + 319 * 3 + 1] == 255);
    assert(data[54 + 319 * 3] == 255 && data[55 + 319 * 3] == 255 && data[56 + 319 * 3] == 255);
}

static void test_temporary_collision(void) {
    const uint16_t pixel = 0;
    unsigned char data[64];
    char temporary[CAPTURE_PATH_MAX], backup[CAPTURE_PATH_MAX];
    int fd = openat(folder_fd, ".ncapture-000001.part", O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && write(fd, "keep", 4) == 4 && close(fd) == 0);
    reset_io();
    assert(capture_save_path(&io, "/documents/collision.bmp.tns", 0, &pixel, 1, 1,
                             temporary, backup, sizeof(temporary)).code == CAPTURE_OK);
    assert(!*temporary && !*backup);
    assert(read_file("collision.bmp.tns", data, sizeof(data)) == 58);
    assert(read_file(".ncapture-000001.part", data, sizeof(data)) == 4);
    assert(!memcmp(data, "keep", 4));
    assert(unlinkat(folder_fd, ".ncapture-000001.part", 0) == 0);
}

static void test_errors(void) {
    const uint16_t pixel = 0;
    const char *path = "/documents/error.bmp.tns";
    char temporary[CAPTURE_PATH_MAX], backup[CAPTURE_PATH_MAX];
    CaptureResult result;
    reset_io();
    result = capture_save_path(&io, "/documents/missing/error.bmp.tns", 0, &pixel, 1, 1,
                               temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_OPEN_ERROR && result.error == ENOENT);
    reset_io();
    write_budget = 54;
    zero_write = 1;
    errno = EEXIST;
    result = capture_save_path(&io, path, 0, &pixel, 1, 1,
                               temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_WRITE_ERROR && result.error == EIO && !*temporary);
    assert(faccessat(folder_fd, local_path(path), F_OK, 0) < 0 && errno == ENOENT);
    assert(faccessat(folder_fd, ".ncapture-000001.part", F_OK, 0) < 0 && errno == ENOENT);
    reset_io();
    assert(capture_save_path(&io, "/documents/../bad.bmp.tns", 0, &pixel, 1, 1,
                             temporary, backup, sizeof(temporary)).code == CAPTURE_BAD_PATH);
    assert(capture_save_path(&io, path, 0, &pixel, 1, 1,
                             temporary, backup, 1).code == CAPTURE_BAD_PATH);
    assert(capture_save_path(&io, path, 0, &pixel, 0, 1,
                             temporary, backup, sizeof(temporary)).code == CAPTURE_BAD_PATH);
    assert(capture_save_path(&io, path, 0, &pixel, 321, 1,
                             temporary, backup, sizeof(temporary)).code == CAPTURE_BAD_PATH);
    assert(capture_save_path(&io, path, 0, &pixel, 1, 0,
                             temporary, backup, sizeof(temporary)).code == CAPTURE_BAD_PATH);
    assert(capture_save_path(&io, path, 0, &pixel, 1, 241,
                             temporary, backup, sizeof(temporary)).code == CAPTURE_BAD_PATH);
    assert(capture_bmp(&io, -1, &pixel, 0, 1) == EINVAL);
    assert(capture_bmp(&io, -1, &pixel, 1, 0) == EINVAL);
    assert(capture_bmp(&io, -1, &pixel, 321, 1) == EINVAL);
    assert(capture_bmp(&io, -1, &pixel, 1, 241) == EINVAL);
}

static void test_hotkey(void) {
    CaptureHotkey hotkey = {0, 0};
    assert(!capture_hotkey(&hotkey, 8, '.', 42, 0));
    assert(!capture_hotkey(&hotkey, 8, 'x', 42, 4));
    assert(capture_hotkey(&hotkey, 8, '.', 42, 4));
    assert(!capture_hotkey(&hotkey, 8, '.', 42, 4));
    assert(!capture_hotkey(&hotkey, 16, 0, 43, 0));
    assert(!capture_hotkey(&hotkey, 8, '.', 42, 4));
    assert(!capture_hotkey(&hotkey, 16, 0, 42, 0));
    assert(capture_hotkey(&hotkey, 8, '.', 42, 4));
    assert(!capture_hotkey(&hotkey, 16, '.', 0, 0));
    assert(capture_hotkey(&hotkey, 8, '.', 42, 4));
    hotkey.held = 0;
    assert(capture_hotkey(&hotkey, 8, 0, CAPTURE_PERIOD_KEY, 4));
    assert(!capture_hotkey(&hotkey, 8, 0, CAPTURE_PERIOD_KEY, 4));
    assert(!capture_hotkey(&hotkey, 16, 0, CAPTURE_PERIOD_KEY, 4));
    assert(capture_hotkey(&hotkey, 8, 0, CAPTURE_PERIOD_KEY, 4));
    hotkey = (CaptureHotkey){0, 0};
    assert(capture_hotkey(&hotkey, 16, 0, CAPTURE_PERIOD_KEY, 4));
}

static void test_native_choice(void) {
    const uint16_t pixels[] = {0xf800, 0x07e0};
    unsigned char data[128], first[128];
    char path[CAPTURE_PATH_MAX], temporary[CAPTURE_PATH_MAX], backup[CAPTURE_PATH_MAX];
    CaptureResult result;
    size_t size;
    assert(capture_dialog_path("/", "capture.bmp", path, sizeof(path)));
    assert(!strcmp(path, "/documents/capture.bmp.tns"));
    assert(capture_dialog_path("/My Captures/", "Test image.bmp", path, sizeof(path)));
    assert(!strcmp(path, "/documents/My Captures/Test image.bmp.tns"));
    assert(!capture_dialog_path("/../", "capture.bmp", path, sizeof(path)));
    assert(!capture_dialog_path("/", "../capture.bmp", path, sizeof(path)));
    assert(!capture_dialog_path("/a//b", "capture.bmp", path, sizeof(path)));
    assert(!capture_dialog_path("/", "capture.bmp", path, 4));
    assert(capture_dialog_path("/", "retained.bmp", path, sizeof(path)));
    reset_io();
    result = capture_save_path(&io, path, 0, pixels, 2, 1, temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_OK && !*temporary);
    size = read_file("retained.bmp.tns", first, sizeof(first));
    assert(size == 62);
    reset_io();
    result = capture_save_path(&io, path, 0, pixels + 1, 1, 1,
                               temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_RENAME_ERROR && result.error == EEXIST);
    assert(!*temporary && !*backup);
    assert(read_file("retained.bmp.tns", data, sizeof(data)) == size);
    assert(!memcmp(data, first, size));
    reset_io();
    write_limit = 3;
    write_budget = 55;
    result = capture_save_path(&io, path, 1, pixels + 1, 1, 1, temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_WRITE_ERROR && result.error == ENOSPC && !*temporary);
    assert(read_file("retained.bmp.tns", data, sizeof(data)) == size);
    assert(!memcmp(data, first, size));
    reset_io();
    fail_close = 1;
    result = capture_save_path(&io, path, 1, pixels + 1, 1, 1, temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_CLOSE_ERROR && !*temporary);
    assert(read_file("retained.bmp.tns", data, sizeof(data)) == size);
    assert(!memcmp(data, first, size));
    reset_io();
    fail_rename = 1;
    result = capture_save_path(&io, path, 1, pixels + 1, 1, 1, temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_RENAME_ERROR && result.error == EACCES);
    assert(read_file("retained.bmp.tns", data, sizeof(data)) == size);
    assert(!memcmp(data, first, size));
    reset_io();
    fail_rename_at = 3;
    result = capture_save_path(&io, path, 1, pixels + 1, 1, 1, temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_RENAME_ERROR && result.error == EACCES);
    assert(!result.backup_error && !*backup && !*temporary);
    assert(read_file("retained.bmp.tns", data, sizeof(data)) == size);
    assert(!memcmp(data, first, size));
    reset_io();
    fail_rename_at = 3;
    fail_restore = 1;
    result = capture_save_path(&io, path, 1, pixels + 1, 1, 1, temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_RENAME_ERROR && result.backup_error == EACCES);
    assert(*backup && !*temporary);
    assert(faccessat(folder_fd, local_path(path), F_OK, 0) < 0 && errno == ENOENT);
    assert(read_file(local_path(backup), data, sizeof(data)) == size);
    assert(!memcmp(data, first, size));
    reset_io();
    assert(file_rename(backup, path) == 0);
    reset_io();
    result = capture_save_path(&io, path, 1, pixels + 1, 1, 1, temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_OK);
    assert(read_file("retained.bmp.tns", data, sizeof(data)) == 58);
    assert(data[54] == 0 && data[55] == 255 && data[56] == 0);
    reset_io();
    fail_remove = 1;
    result = capture_save_path(&io, path, 1, pixels, 2, 1, temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_BACKUP_ERROR && result.backup_error == EACCES);
    assert(!*temporary && *backup);
    assert(read_file("retained.bmp.tns", data, sizeof(data)) == size);
    assert(!memcmp(data, first, size));
    assert(read_file(local_path(backup), data, sizeof(data)) == 58);
    assert(data[54] == 0 && data[55] == 255 && data[56] == 0);
    reset_io();
    assert(file_remove(backup) == 0);
    assert(capture_dialog_path("/", "another name.bmp", path, sizeof(path)));
    assert(capture_save_path(&io, path, 0, pixels, 2, 1, temporary, backup, sizeof(temporary)).code == CAPTURE_OK);
    assert(read_file("another name.bmp.tns", data, sizeof(data)) == size);
    assert(!memcmp(data, first, size));
    reset_io();
    write_budget = 0;
    fail_remove = 1;
    result = capture_save_path(&io, path, 1, pixels, 2, 1, temporary, backup, sizeof(temporary));
    assert(result.code == CAPTURE_WRITE_ERROR && result.cleanup_error == EACCES);
    assert(*temporary && faccessat(folder_fd, local_path(temporary), F_OK, 0) == 0);
    assert(read_file("another name.bmp.tns", data, sizeof(data)) == size);
    assert(!memcmp(data, first, size));
}

static void test_preview_image(void) {
    static uint16_t shot[CAPTURE_WIDTH * CAPTURE_HEIGHT];
    static uint16_t frame[CAPTURE_WIDTH * CAPTURE_HEIGHT];
    static uint16_t screen[CAPTURE_WIDTH * CAPTURE_HEIGHT];
    unsigned x, y;
    for (y = 0; y < CAPTURE_HEIGHT; ++y)
        for (x = 0; x < CAPTURE_WIDTH; ++x)
            shot[y * CAPTURE_WIDTH + x] = (uint16_t)(y * CAPTURE_WIDTH + x);
    memset(frame, 0xab, sizeof(frame));
    capture_preview(shot, frame, screen);
    assert(screen[0] == 0xabab && screen[CAPTURE_WIDTH * CAPTURE_HEIGHT - 1] == 0xabab);
    assert(screen[37 * CAPTURE_WIDTH + 60] == shot[0]);
    assert(screen[37 * CAPTURE_WIDTH + 59] == 0xabab);
    assert(screen[37 * CAPTURE_WIDTH + 260] == 0xabab);
    for (y = 0; y < 150; ++y)
        for (x = 0; x < 200; ++x)
            assert(screen[(y + 37) * CAPTURE_WIDTH + x + 60] ==
                   shot[(y * 240 / 150) * CAPTURE_WIDTH + x * 320 / 200]);
}

int main(void) {
    char folder[] = "/tmp/ncapture-test-XXXXXX";
    struct dirent *item;
    DIR *dir;
    assert(mkdtemp(folder));
    folder_fd = open(folder, O_RDONLY | O_DIRECTORY);
    assert(folder_fd >= 0);
    test_bmp();
    test_channels_and_padding();
    test_full_screen();
    test_temporary_collision();
    test_errors();
    test_hotkey();
    test_native_choice();
    test_preview_image();
    dir = fdopendir(dup(folder_fd));
    assert(dir);
    while ((item = readdir(dir))) {
        if (strcmp(item->d_name, ".") && strcmp(item->d_name, ".."))
            assert(unlinkat(folder_fd, item->d_name, 0) == 0);
    }
    assert(closedir(dir) == 0);
    assert(close(folder_fd) == 0);
    assert(rmdir(folder) == 0);
    puts("capture tests passed");
    return 0;
}
