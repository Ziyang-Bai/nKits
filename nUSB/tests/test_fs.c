#include "fs.h"
#include "image_disk.h"
#include "ff.h"
#include "diskio.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static const char *fixtures = "build/fixtures";
static ImageDisk disk;
static FsEntry entry;

static void open_image(const char *name, uint32_t sector_size) {
    char path[1024];
    CHECK(snprintf(path, sizeof(path), "%s/%s", fixtures, name) < (int)sizeof(path));
    CHECK(image_disk_open(&disk, path, sector_size));
}

static void close_image(void) {
    fs_unmount();
    image_disk_close(&disk);
}

static unsigned compare_directory(const char *path, const char *manifest) {
    char filename[1024], line[1024];
    FILE *file;
    FsDir *dir;
    unsigned count = 0;
    bool end = false;
    CHECK(snprintf(filename, sizeof(filename), "%s/%s.tsv", fixtures, manifest) < (int)sizeof(filename));
    file = fopen(filename, "rb");
    CHECK(file != NULL);
    CHECK(fs_opendir(path, &dir) == FS_OK);
    while (fgets(line, sizeof(line), file)) {
        char *size = strchr(line, '\t'), *name, *newline;
        CHECK(size != NULL);
        name = strchr(++size, '\t');
        CHECK(name != NULL);
        *name++ = '\0';
        newline = strchr(name, '\n');
        CHECK(newline != NULL);
        *newline = '\0';
        CHECK(fs_next(dir, &entry, &end) == FS_OK && !end);
        CHECK(strcmp(entry.name, name) == 0);
        {
            char *tail;
            uint64_t expected;
            CHECK(size[0] >= '0' && size[0] <= '9');
            errno = 0;
            expected = strtoull(size, &tail, 10);
            CHECK(!errno && tail != size && !*tail);
            CHECK(entry.size == expected);
        }
        CHECK(entry.is_directory == (line[0] == 'D'));
        ++count;
    }
    CHECK(!ferror(file));
    CHECK(fclose(file) == 0);
    CHECK(fs_next(dir, &entry, &end) == FS_OK && end);
    CHECK(fs_next(dir, &entry, &end) == FS_OK && end);
    fs_closedir(dir);
    return count;
}

static void test_files(void) {
    FsFile *file, *other, *failed;
    FsDir *root, *docs;
    unsigned char buf[8192];
    uint32_t count, offset;
    unsigned i;
    bool end;

    CHECK(fs_stat("0:/", &entry) == FS_OK);
    CHECK(entry.is_directory && entry.size == 0 && !strcmp(entry.name, "/"));
    CHECK(fs_stat("0:/docs", &entry) == FS_OK && entry.is_directory);
    CHECK(fs_stat("0:/README.TXT", &entry) == FS_OK);
    CHECK(!entry.is_directory && entry.size == 12 && !strcmp(entry.name, "README.TXT"));
    CHECK(fs_stat("0:/missing", &entry) == FS_NOT_FOUND);
    CHECK(!entry.name[0] && !entry.size && !entry.is_directory);
    CHECK(fs_stat("0:/资料/inside.txt", &entry) == FS_OK && entry.size == 7);

    CHECK(fs_opendir("0:/", &root) == FS_OK);
    CHECK(fs_opendir("0:/docs", &docs) == FS_OK);
    CHECK(fs_next(root, &entry, &end) == FS_OK && !end && !strcmp(entry.name, "README.TXT"));
    CHECK(fs_next(docs, &entry, &end) == FS_OK && !end && !strcmp(entry.name, "nested.txt"));
    CHECK(fs_next(root, &entry, &end) == FS_OK && !end && !strcmp(entry.name, "docs"));
    fs_closedir(root);
    CHECK(fs_next(docs, &entry, &end) == FS_OK && end);
    fs_closedir(docs);

    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &file) == FS_OK);
    CHECK(fs_open("0:/docs/nested.txt", FS_OPEN_READ, &other) == FS_OK);
    CHECK(fs_read(file, buf, 5, &count) == FS_OK && count == 5);
    CHECK(!memcmp(buf, "Hello", 5) && fs_tell(file) == 5);
    CHECK(fs_read(other, buf, sizeof(buf), &count) == FS_OK && count == 3);
    CHECK(!memcmp(buf, "abc", 3) && fs_tell(other) == 3);
    CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 7);
    CHECK(!memcmp(buf, " nUSB!\n", 7) && fs_tell(file) == 12);
    CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 0);
    CHECK(fs_read(other, buf, 1, &count) == FS_OK && count == 0);
    CHECK(fs_seek(other, 1) == FS_OK && fs_tell(other) == 1);
    CHECK(fs_read(other, buf, 1, &count) == FS_OK && count == 1 && buf[0] == 'b');
    failed = file;
    CHECK(fs_open("0:/missing", FS_OPEN_READ, &failed) == FS_NOT_FOUND && !failed);
    failed = file;
    CHECK(fs_open("0:/docs", FS_OPEN_READ, &failed) == FS_NOT_FOUND && !failed);
    CHECK(fs_open("0:/", FS_OPEN_READ, &failed) == FS_NOT_FOUND && !failed);
    CHECK(fs_seek(file, 0) == FS_OK);
    CHECK(fs_read(file, NULL, 0, &count) == FS_OK && count == 0);
    CHECK(fs_tell(file) == 0);
    fs_close(other);
    fs_close(file);

    CHECK(fs_open("0:/资料/inside.txt", FS_OPEN_READ, &file) == FS_OK);
    CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 7);
    CHECK(!memcmp(buf, "inside\n", 7));
    fs_close(file);

    CHECK(fs_stat("0:/f149.txt", &entry) == FS_OK && entry.size == 9000);
    CHECK(fs_open("0:/f149.txt", FS_OPEN_READ, &file) == FS_OK);
    for (offset = disk.device.sector_size; offset < 9000; offset += disk.device.sector_size) {
        CHECK(fs_seek(file, offset - 3) == FS_OK && fs_tell(file) == offset - 3);
        memset(buf, 0, 10);
        CHECK(fs_read(file, buf, 10, &count) == FS_OK && count == 10);
        for (i = 0; i < count; ++i) CHECK(buf[i] == 149);
        CHECK(fs_tell(file) == offset + 7);
    }
    CHECK(fs_seek(file, 0) == FS_OK);
    CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == sizeof(buf));
    for (i = 0; i < count; ++i) CHECK(buf[i] == 149);
    CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 808);
    for (i = 0; i < count; ++i) CHECK(buf[i] == 149);
    CHECK(fs_tell(file) == 9000);
    CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 0);
    CHECK(fs_seek(file, UINT32_MAX) == FS_OK && fs_tell(file) == 9000);
    CHECK(fs_read(file, buf, 1, &count) == FS_OK && count == 0);
    CHECK(fs_seek(file, 1) == FS_OK && fs_tell(file) == 1);
    CHECK(fs_read(file, buf, 1, &count) == FS_OK && count == 1 && buf[0] == 149);
    fs_close(file);
}

static void test_volumes(void) {
    static const struct { const char *name; uint32_t size; } cases[] = {
        {"fat32-sfd.img", 512}, {"fat32-mbr.img", 512},
        {"fat32-mbr-skip.img", 512},
        {"fat32-mbr-first.img", 512}, {"fat32-1024.img", 1024},
        {"fat32-2048.img", 2048}, {"fat32-4096.img", 4096},
        {"exfat-sfd.img", 512}, {"exfat-mbr.img", 512},
        {"exfat-4096.img", 4096}, {"exfat-mbr-4096.img", 4096}
    };
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        WORD size = 0;
        LBA_t count = 0;
        FsDir *dir;
        open_image(cases[i].name, cases[i].size);
        CHECK(fs_mount(&disk.device) == FS_OK);
        CHECK(disk_initialize(0) == STA_PROTECT);
        CHECK(disk_ioctl(0, GET_SECTOR_SIZE, &size) == RES_OK && size == cases[i].size);
        CHECK(disk_ioctl(0, GET_SECTOR_COUNT, &count) == RES_OK && count == disk.device.sector_count);
        CHECK(disk_ioctl(0, GET_BLOCK_SIZE, &count) == RES_PARERR);
        CHECK(disk_ioctl(1, GET_SECTOR_SIZE, &size) == RES_PARERR);
        CHECK(compare_directory("0:/", "root") == 159);
        CHECK(compare_directory("0:/docs", "docs") == 1);
        CHECK(compare_directory("0:/empty", "empty") == 0);
        CHECK(compare_directory("0:/资料", "chinese") == 1);
        CHECK(fs_opendir("0:/missing", &dir) == FS_NOT_FOUND && !dir);
        CHECK(fs_opendir("0:/README.TXT", &dir) == FS_NOT_FOUND && !dir);
        CHECK(compare_directory("0:/", "root") == 159);
        test_files();
        CHECK(disk.reads > 0 && disk.sectors_read >= disk.reads);
        close_image();
    }
}

static void test_bad_media(void) {
    static const struct { const char *name; FsResult result; } cases[] = {
        {"fat16.img", FS_UNSUPPORTED},
        {"gpt.img", FS_UNSUPPORTED}, {"extended.img", FS_UNSUPPORTED},
        {"bad-signature.img", FS_UNSUPPORTED}, {"bad-range.img", FS_UNSUPPORTED},
        {"overflow-range.img", FS_UNSUPPORTED}, {"bad-root.img", FS_IO_ERROR},
        {"short-partition.img", FS_IO_ERROR}, {"truncated.img", FS_IO_ERROR}
    };
    unsigned i;
    bool end = true;
    FsDir *dir;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        open_image(cases[i].name, 512);
        CHECK(fs_mount(&disk.device) == cases[i].result);
        CHECK(fs_opendir("0:/", &dir) == FS_NO_DEVICE && !dir);
        CHECK(fs_next(dir, &entry, &end) == FS_NO_DEVICE && !end);
        close_image();
    }
    CHECK(fs_mount(NULL) == FS_NO_DEVICE);
    open_image("fat32-sfd.img", 512);
    disk.device.sector_size = 768;
    CHECK(fs_mount(&disk.device) == FS_UNSUPPORTED);
    disk.device.sector_size = 512;
    disk.device.sector_count = (uint64_t)UINT32_MAX + 2;
    CHECK(fs_mount(&disk.device) == FS_UNSUPPORTED);
    close_image();
}

static void test_bad_directories(void) {
    static const struct { const char *name; FsResult result; } cases[] = {
        {"bad-chain.img", FS_IO_ERROR}, {"out-of-range-chain.img", FS_IO_ERROR},
        {"reserved-chain.img", FS_IO_ERROR}, {"bad-cluster-chain.img", FS_IO_ERROR},
        {"eoc-low-chain.img", FS_OK}, {"eoc-high-chain.img", FS_OK}
    };
    unsigned i;
    FsDir *dir;
    open_image("bad-directory.img", 512);
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_opendir("0:/docs", &dir) == FS_IO_ERROR && !dir);
    CHECK(compare_directory("0:/empty", "empty") == 0);
    close_image();

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        bool end = false;
        FsResult result = FS_OK;
        unsigned count = 0;
        open_image(cases[i].name, 512);
        CHECK(fs_mount(&disk.device) == FS_OK);
        CHECK(fs_opendir("0:/", &dir) == FS_OK);
        while (count < 200) {
            result = fs_next(dir, &entry, &end);
            if (result != FS_OK || end) break;
            ++count;
        }
        CHECK(result == cases[i].result);
        CHECK(end == (result == FS_OK));
        CHECK(count == 3);
        fs_closedir(dir);
        close_image();
    }
}

static void test_io_errors(void) {
    static const struct { MscResult msc; FsResult fs; } cases[] = {
        {MSC_CANCELLED, FS_CANCELLED}, {MSC_TIMEOUT, FS_IO_ERROR},
        {MSC_IO_ERROR, FS_IO_ERROR}, {MSC_NO_DEVICE, FS_NO_DEVICE},
        {MSC_PROTOCOL_ERROR, FS_IO_ERROR}, {MSC_UNSUPPORTED, FS_UNSUPPORTED}
    };
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        bool end = false;
        FsResult result = FS_OK;
        unsigned entries = 0;
        FsDir *dir;
        open_image("fat32-sfd.img", 512);
        disk.fail_at = 1;
        disk.failure = cases[i].msc;
        CHECK(fs_mount(&disk.device) == cases[i].fs);
        disk.fail_at = 0;
        CHECK(fs_mount(&disk.device) == FS_OK);
        CHECK(fs_opendir("0:/", &dir) == FS_OK);
        CHECK(fs_next(dir, &entry, &end) == FS_OK && !end);
        disk.fail_at = disk.reads + 1;
        while (result == FS_OK && entries++ < 200) {
            result = fs_next(dir, &entry, &end);
            CHECK(!end);
        }
        CHECK(result == cases[i].fs);
        disk.fail_at = 0;
        CHECK(fs_mount(&disk.device) == FS_OK);
        fs_closedir(dir);
        CHECK(compare_directory("0:/docs", "docs") == 1);
        close_image();
    }
}

static void test_generation(void) {
    bool end = true;
    uint64_t reads;
    FsDir *dir, *failed;
    open_image("fat32-sfd.img", 512);
    disk.detach_at = 1;
    CHECK(fs_mount(&disk.device) == FS_NO_DEVICE);
    close_image();

    open_image("fat32-sfd.img", 512);
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_opendir("0:/", &dir) == FS_OK);
    CHECK(fs_next(dir, &entry, &end) == FS_OK && !end);
    reads = disk.reads;
    image_disk_detach(&disk);
    CHECK(fs_next(dir, &entry, &end) == FS_NO_DEVICE && !end);
    CHECK(fs_opendir("0:/", &failed) == FS_NO_DEVICE && !failed);
    CHECK(disk.reads == reads);
    CHECK(disk_status(0) & STA_NODISK);
    disk.connected = true;
    CHECK(fs_opendir("0:/", &failed) == FS_NO_DEVICE && !failed);
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_next(dir, &entry, &end) == FS_NO_DEVICE && !end);
    fs_closedir(dir);
    CHECK(compare_directory("0:/", "root") == 159);
    close_image();

    open_image("fat32-mbr.img", 512);
    CHECK(fs_mount(&disk.device) == FS_OK);
    disk.detach_at = disk.reads + 1;
    CHECK(fs_opendir("0:/docs", &dir) == FS_NO_DEVICE && !dir);
    CHECK(fs_next(dir, &entry, &end) == FS_NO_DEVICE && !end);
    close_image();
}

static void test_file_generation(void) {
    unsigned unplug;
    for (unplug = 0; unplug < 2; ++unplug) {
        FsFile *file, *fresh;
        FsDir *dir;
        unsigned char buf[16];
        uint32_t count = 1;
        uint64_t reads;
        bool end = true;
        open_image("fat32-sfd.img", 512);
        CHECK(fs_mount(&disk.device) == FS_OK);
        CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &file) == FS_OK);
        CHECK(fs_opendir("0:/", &dir) == FS_OK);
        CHECK(fs_read(file, buf, 1, &count) == FS_OK && count == 1);
        if (unplug) image_disk_detach(&disk);
        else fs_unmount();
        reads = disk.reads;
        CHECK(fs_read(file, buf, 1, &count) == FS_NO_DEVICE && count == 0);
        CHECK(fs_seek(file, 0) == FS_NO_DEVICE);
        CHECK(fs_tell(file) == 0);
        CHECK(fs_next(dir, &entry, &end) == FS_NO_DEVICE && !end);
        CHECK(fs_stat("0:/", &entry) == FS_NO_DEVICE);
        CHECK(disk.reads == reads);
        disk.connected = true;
        CHECK(fs_mount(&disk.device) == FS_OK);
        CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &fresh) == FS_OK);
        reads = disk.reads;
        CHECK(fs_read(file, buf, 1, &count) == FS_NO_DEVICE && count == 0);
        CHECK(fs_seek(file, 0) == FS_NO_DEVICE);
        CHECK(fs_next(dir, &entry, &end) == FS_NO_DEVICE && !end);
        fs_close(file);
        fs_closedir(dir);
        CHECK(disk.reads == reads);
        CHECK(fs_read(fresh, buf, sizeof(buf), &count) == FS_OK && count == 12);
        CHECK(!memcmp(buf, "Hello nUSB!\n", 12));
        fs_unmount();
        fs_close(fresh);
        close_image();
    }

    {
        FsFile *file;
        unsigned char buf[1024];
        uint32_t count = 1;
        open_image("fat32-sfd.img", 512);
        CHECK(fs_mount(&disk.device) == FS_OK);
        CHECK(fs_open("0:/f149.txt", FS_OPEN_READ, &file) == FS_OK);
        disk.detach_at = disk.reads + 1;
        CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_NO_DEVICE && count == 0);
        fs_close(file);
        close_image();
    }
}

static void test_paths(void) {
    char path[4352];
    unsigned i;
    bool end;
    FsDir *dir;
    open_image("fat32-sfd.img", 512);
    CHECK(fs_mount(&disk.device) == FS_OK);
    strcpy(path, "0:/deep");
    for (i = 0; i < 17; ++i) {
        size_t length;
        CHECK(fs_opendir(path, &dir) == FS_OK);
        CHECK(fs_next(dir, &entry, &end) == FS_OK && !end && entry.is_directory);
        fs_closedir(dir);
        CHECK(strlen(entry.name) == 240);
        length = strlen(path);
        path[length++] = '/';
        memcpy(path + length, entry.name, strlen(entry.name) + 1);
    }
    CHECK(strlen(path) == 4104);
    CHECK(fs_opendir(path, &dir) == FS_PATH_TOO_LONG && !dir);
    CHECK(compare_directory("0:/资料", "chinese") == 1);
    close_image();

    open_image("fat32-missing-docs.img", 512);
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_opendir("0:/docs", &dir) == FS_NOT_FOUND && !dir);
    CHECK(compare_directory("0:/empty", "empty") == 0);
    close_image();
}

static void copy_image(const char *name, uint32_t sector_size) {
    char path[1024];
    CHECK(snprintf(path, sizeof(path), "%s/%s", fixtures, name) < (int)sizeof(path));
    CHECK(image_disk_copy(&disk, path, sector_size));
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(disk_initialize(0) == 0);
}

static void test_writes(void) {
    static const struct { const char *name; uint32_t size; } cases[] = {
        {"fat32-sfd.img", 512}, {"fat32-mbr.img", 512},
        {"fat32-mbr-skip.img", 512}, {"fat32-1024.img", 1024},
        {"fat32-2048.img", 2048}, {"fat32-4096.img", 4096},
        {"exfat-sfd.img", 512}, {"exfat-mbr.img", 512},
        {"exfat-4096.img", 4096}, {"exfat-mbr-4096.img", 4096}
    };
    unsigned i, j;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        FsFile *file, *failed;
        unsigned char buf[8192];
        uint32_t count;
        copy_image(cases[i].name, cases[i].size);
        CHECK(fs_open("0:/created.txt", FS_OPEN_READ | FS_OPEN_WRITE |
                         FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE, &file) == FS_OK);
        CHECK(fs_write(file, "abcdef", 6, &count) == FS_OK && count == 6);
        CHECK(fs_seek(file, 2) == FS_OK);
        CHECK(fs_write(file, "XY", 2, &count) == FS_OK && count == 2);
        CHECK(fs_seek(file, UINT32_MAX) == FS_OK && fs_tell(file) == 6);
        CHECK(fs_truncate(file, 5000) == FS_OK);
        CHECK(fs_tell(file) == 6 && fs_size(file) == 5000);
        memset(buf, 0xff, sizeof(buf));
        CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 4994);
        for (j = 0; j < count; ++j) CHECK(buf[j] == 0);
        CHECK(fs_truncate(file, 4) == FS_OK);
        CHECK(fs_tell(file) == 5000 && fs_size(file) == 4);
        CHECK(fs_read(file, buf, 1, &count) == FS_OK && count == 0);
        CHECK(fs_close(file) == FS_OK);
        CHECK(disk.writes && disk.syncs);
        fs_unmount();
        CHECK(fs_mount(&disk.device) == FS_OK);
        CHECK(fs_stat("0:/created.txt", &entry) == FS_OK && entry.size == 4);
        CHECK(entry.date == 0x5d21 && entry.time == 0x645c);
        CHECK(entry.created_date == 0x5d21 && entry.created_time == 0x645c);
        CHECK(fs_open("0:/created.txt", FS_OPEN_WRITE | FS_OPEN_CREATE, &file) == FS_OK);
        CHECK(fs_size(file) == 4);
        CHECK(fs_close(file) == FS_OK);
        CHECK(fs_open("0:/created.txt", FS_OPEN_WRITE | FS_OPEN_CREATE |
                         FS_OPEN_EXCLUSIVE, &failed) == FS_EXISTS && !failed);
        CHECK(fs_open("0:/created.txt", FS_OPEN_READ | FS_OPEN_WRITE |
                         FS_OPEN_APPEND, &file) == FS_OK);
        CHECK(fs_seek(file, 0) == FS_OK);
        CHECK(fs_write(file, "!", 1, &count) == FS_OK && count == 1);
        CHECK(fs_seek(file, 1) == FS_OK);
        CHECK(fs_write(file, "?", 1, &count) == FS_OK && count == 1);
        CHECK(fs_seek(file, 0) == FS_OK);
        CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 6);
        CHECK(!memcmp(buf, "abXY!?", 6));
        CHECK(fs_sync(file) == FS_OK);
        CHECK(fs_close(file) == FS_OK);
        CHECK(fs_open("0:/created.txt", FS_OPEN_WRITE | FS_OPEN_TRUNCATE, &file) == FS_OK);
        CHECK(fs_size(file) == 0);
        CHECK(fs_close(file) == FS_OK);
        CHECK(fs_open("0:/missing", FS_OPEN_WRITE | FS_OPEN_TRUNCATE, &failed) == FS_NOT_FOUND);
        CHECK(fs_open("0:/created.txt", FS_OPEN_READ | FS_OPEN_TRUNCATE, &failed) == FS_INVALID_ARGUMENT);
        CHECK(fs_open("0:/created.txt", FS_OPEN_WRITE | FS_OPEN_EXCLUSIVE, &failed) == FS_INVALID_ARGUMENT);
        close_image();
    }
}

static void test_metadata(const char *name) {
    FsFile *file;
    FsTimes times = {0x5d21, 0x5d22, 0x645c, 0x5b83, 0x4a21};
    uint32_t count;
    copy_image(name, 512);
    CHECK(fs_mkdir("0:/new") == FS_OK);
    CHECK(fs_mkdir("0:/new/nested") == FS_OK);
    CHECK(fs_mkdir("0:/new") == FS_EXISTS);
    CHECK(fs_open("0:/new/nested/资料.txt", FS_OPEN_WRITE | FS_OPEN_CREATE, &file) == FS_OK);
    CHECK(fs_write(file, "payload", 7, &count) == FS_OK && count == 7);
    CHECK(fs_close(file) == FS_OK);
    CHECK(fs_unlink("0:/new/nested") == FS_DENIED);
    CHECK(fs_rename("0:/new/nested/资料.txt", "0:/new/renamed.txt") == FS_OK);
    CHECK(fs_rename("0:/new/renamed.txt", "0:/README.TXT") == FS_EXISTS);
    CHECK(fs_rename("0:/new/renamed.txt", "1:/wrong.txt") == FS_INVALID_ARGUMENT);
    CHECK(fs_utime("0:/new/renamed.txt", &times) == FS_OK);
    CHECK(fs_chmod("0:/new/renamed.txt", AM_RDO | AM_HID | AM_SYS | AM_ARC) == FS_OK);
    CHECK(fs_chmod("0:/new/nested", AM_HID) == FS_OK);
    CHECK(fs_utime("0:/new/nested", &times) == FS_OK);
    fs_unmount();
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_stat("0:/new/renamed.txt", &entry) == FS_OK && entry.size == 7);
    CHECK(entry.attributes == (AM_RDO | AM_HID | AM_SYS | AM_ARC));
    CHECK(entry.date == times.modified_date && entry.time == times.modified_time);
    CHECK(entry.access_date == times.access_date);
    CHECK(entry.created_date == times.created_date && entry.created_time == times.created_time);
    CHECK(fs_stat("0:/new/nested", &entry) == FS_OK);
    CHECK(entry.is_directory && entry.attributes == (AM_DIR | AM_HID));
    CHECK(entry.access_date == times.access_date && entry.created_time == times.created_time);
    CHECK(fs_unlink("0:/new/renamed.txt") == FS_DENIED);
    CHECK(fs_open("0:/new/renamed.txt", FS_OPEN_WRITE, &file) == FS_DENIED);
    CHECK(fs_chmod("0:/new/renamed.txt", AM_ARC) == FS_OK);
    memset(&times, 0, sizeof(times));
    CHECK(fs_utime("0:/new/renamed.txt", &times) == FS_OK);
    CHECK(fs_stat("0:/new/renamed.txt", &entry) == FS_OK);
    CHECK(!entry.date && !entry.time && !entry.access_date &&
          !entry.created_date && !entry.created_time);
    CHECK(fs_rename("0:/new/nested", "0:/moved") == FS_OK);
    CHECK(fs_stat("0:/new/nested", &entry) == FS_NOT_FOUND);
    CHECK(fs_unlink("0:/moved") == FS_OK);
    CHECK(fs_unlink("0:/new/renamed.txt") == FS_OK);
    CHECK(fs_unlink("0:/new") == FS_OK);
    fs_unmount();
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_stat("0:/new", &entry) == FS_NOT_FOUND);
    CHECK(fs_stat("0:/moved", &entry) == FS_NOT_FOUND);
    CHECK(fs_stat("0:/README.TXT", &entry) == FS_OK && entry.size == 12);
    close_image();
}

static void test_locks(void) {
    FsFile *file, *other, *failed;
    FsDir *dir;
    uint32_t count;
    FsFile *handles[32];
    char path[32];
    unsigned i;
    copy_image("fat32-sfd.img", 512);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &file) == FS_OK);
    CHECK(fs_open("0:/readme.txt", FS_OPEN_READ, &other) == FS_OK);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_WRITE, &failed) == FS_BUSY && !failed);
    CHECK(fs_unlink("0:/README.TXT") == FS_BUSY);
    CHECK(fs_rename("0:/README.TXT", "0:/renamed") == FS_BUSY);
    CHECK(fs_write(file, "x", 1, &count) == FS_DENIED && !count);
    CHECK(fs_truncate(file, 0) == FS_DENIED);
    CHECK(fs_close(other) == FS_OK);
    CHECK(fs_close(file) == FS_OK);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_WRITE, &file) == FS_OK);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &failed) == FS_BUSY && !failed);
    CHECK(fs_read(file, &count, 1, &count) == FS_DENIED && !count);
    CHECK(fs_close(file) == FS_OK);
    CHECK(fs_opendir("0:/empty", &dir) == FS_OK);
    CHECK(fs_unlink("0:/empty") == FS_BUSY);
    CHECK(fs_rename("0:/empty", "0:/renamed") == FS_BUSY);
    fs_closedir(dir);
    CHECK(fs_unlink("0:/empty") == FS_OK);
    for (i = 0; i < 32; ++i) {
        snprintf(path, sizeof(path), "0:/f%03u.txt", i);
        CHECK(fs_open(path, FS_OPEN_READ, &handles[i]) == FS_OK);
    }
    CHECK(fs_open("0:/f032.txt", FS_OPEN_READ, &failed) == FS_BUSY && !failed);
    for (i = 0; i < 32; ++i) CHECK(fs_close(handles[i]) == FS_OK);
    CHECK(fs_open("0:/f032.txt", FS_OPEN_WRITE, &file) == FS_OK);
    CHECK(fs_close(file) == FS_OK);
    close_image();
}

static void test_write_failures(void) {
    static const struct { MscResult msc; FsResult fs; } cases[] = {
        {MSC_IO_ERROR, FS_IO_ERROR}, {MSC_CANCELLED, FS_CANCELLED},
        {MSC_TIMEOUT, FS_IO_ERROR}, {MSC_NO_DEVICE, FS_NO_DEVICE},
        {MSC_WRITE_PROTECTED, FS_WRITE_PROTECTED}, {MSC_UNSUPPORTED, FS_IO_ERROR}
    };
    FsFile *file;
    FsTimes times = {0};
    unsigned i, sync;
    unsigned char buf[8192];
    uint32_t count;
    memset(buf, 0x5a, sizeof(buf));
    open_image("fat32-sfd.img", 512);
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_open("0:/readonly", FS_OPEN_WRITE | FS_OPEN_CREATE, &file) == FS_WRITE_PROTECTED);
    CHECK(fs_mkdir("0:/readonly") == FS_WRITE_PROTECTED);
    CHECK(fs_unlink("0:/README.TXT") == FS_WRITE_PROTECTED);
    CHECK(fs_rename("0:/README.TXT", "0:/renamed") == FS_WRITE_PROTECTED);
    CHECK(fs_chmod("0:/README.TXT", 0) == FS_WRITE_PROTECTED);
    CHECK(fs_utime("0:/README.TXT", &times) == FS_WRITE_PROTECTED);
    CHECK(disk_ioctl(0, CTRL_SYNC, NULL) == RES_WRPRT);
    CHECK(disk_write(0, buf, 0, 1) == RES_WRPRT);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &file) == FS_OK);
    CHECK(fs_close(file) == FS_OK);
    close_image();
    for (sync = 0; sync < 2; ++sync) {
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            uint64_t writes, syncs;
            copy_image("fat32-sfd.img", 512);
            CHECK(fs_open("0:/README.TXT", FS_OPEN_WRITE, &file) == FS_OK);
            disk.failure = cases[i].msc;
            disk.detach_on_failure = (i & 1) == 0;
            if (sync) {
                CHECK(fs_write(file, "Z", 1, &count) == FS_OK && count == 1);
                disk.fail_sync_at = disk.syncs + 1;
                CHECK(fs_close(file) == cases[i].fs);
            } else {
                disk.fail_write_at = disk.writes + 1;
                CHECK(fs_write(file, buf, sizeof(buf), &count) == cases[i].fs);
            }
            writes = disk.writes;
            syncs = disk.syncs;
            disk.fail_write_at = disk.fail_sync_at = 0;
            if (!sync) {
                CHECK(fs_write(file, "x", 1, &count) == FS_NO_DEVICE && !count);
                CHECK(fs_sync(file) == FS_NO_DEVICE);
                CHECK(fs_close(file) == FS_NO_DEVICE);
            }
            CHECK(fs_mkdir("0:/poisoned") == FS_NO_DEVICE);
            CHECK(disk.writes == writes && disk.syncs == syncs);
            close_image();
        }
    }
    copy_image("fat32-sfd.img", 512);
    disk.failure = MSC_UNSUPPORTED;
    disk.fail_sync_at = disk.syncs + 1;
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &file) == FS_OK);
    CHECK(fs_read(file, buf, 5, &count) == FS_OK && count == 5);
    CHECK(!memcmp(buf, "Hello", 5));
    CHECK(fs_close(file) == FS_OK);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_WRITE, &file) == FS_OK);
    CHECK(fs_sync(file) == FS_IO_ERROR);
    CHECK(fs_close(file) == FS_NO_DEVICE);
    close_image();
    copy_image("fat32-sfd.img", 512);
    disk.fail_sync_at = disk.syncs + 1;
    CHECK(fs_mkdir("0:/unsynced") == FS_IO_ERROR);
    CHECK(fs_stat("0:/", &entry) == FS_NO_DEVICE);
    close_image();
    copy_image("fat32-mbr.img", 512);
    CHECK(disk_write(0, buf, 0, 1) == RES_ERROR && disk.writes == 0);
    CHECK(fs_mkdir("0:/outside") == FS_NO_DEVICE);
    close_image();
    copy_image("fat32-mbr.img", 512);
    CHECK(disk_write(0, buf, (LBA_t)disk.device.sector_count, 1) == RES_ERROR);
    CHECK(disk.writes == 0);
    close_image();
}

static void test_write_generation(void) {
    FsFile *old, *fresh;
    unsigned char buf[8192];
    uint32_t count;
    uint64_t writes, syncs;
    unsigned during;
    memset(buf, 0x5a, sizeof(buf));
    for (during = 0; during < 2; ++during) {
        copy_image("fat32-sfd.img", 512);
        CHECK(fs_open("0:/README.TXT", FS_OPEN_WRITE, &old) == FS_OK);
        if (during) {
            disk.detach_write_at = disk.writes + 1;
            CHECK(fs_write(old, buf, sizeof(buf), &count) == FS_NO_DEVICE);
        } else {
            CHECK(fs_write(old, "Z", 1, &count) == FS_OK);
            image_disk_detach(&disk);
        }
        writes = disk.writes;
        CHECK(fs_sync(old) == FS_NO_DEVICE);
        CHECK(fs_write(old, "x", 1, &count) == FS_NO_DEVICE && !count);
        CHECK(disk.writes == writes);
        close_image();
        copy_image("fat32-mbr.img", 512);
        CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &fresh) == FS_OK);
        writes = disk.writes;
        syncs = disk.syncs;
        CHECK(fs_write(old, "x", 1, &count) == FS_NO_DEVICE && !count);
        CHECK(fs_truncate(old, 0) == FS_NO_DEVICE);
        CHECK(fs_close(old) == FS_NO_DEVICE);
        CHECK(disk.writes == writes && disk.syncs == syncs);
        CHECK(fs_read(fresh, buf, sizeof(buf), &count) == FS_OK && count == 12);
        CHECK(!memcmp(buf, "Hello nUSB!\n", 12));
        CHECK(fs_close(fresh) == FS_OK);
        close_image();
    }
}

static void test_disk_full(const char *name) {
    static unsigned char buf[65536];
    FsFile *file;
    FsResult result;
    uint32_t count;
    uint64_t total = 0;
    copy_image(name, 512);
    CHECK(fs_open("0:/full.bin", FS_OPEN_WRITE | FS_OPEN_CREATE, &file) == FS_OK);
    do {
        result = fs_write(file, buf, sizeof(buf), &count);
        total += count;
        CHECK(total <= disk.device.sector_count * disk.device.sector_size);
    } while (result == FS_OK);
    CHECK(result == FS_NO_SPACE && count < sizeof(buf) && total > 0);
    CHECK(fs_size(file) == total);
    CHECK(fs_close(file) == FS_OK);
    fs_unmount();
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_stat("0:/full.bin", &entry) == FS_OK && entry.size == total);
    CHECK(fs_open("0:/full.bin", FS_OPEN_WRITE, &file) == FS_OK);
    CHECK(fs_truncate(file, total + 1) == FS_NO_SPACE);
    CHECK(fs_tell(file) == 0 && fs_size(file) == total);
    CHECK(fs_truncate(file, 0) == FS_OK);
    CHECK(fs_write(file, "space", 5, &count) == FS_OK && count == 5);
    CHECK(fs_close(file) == FS_OK);
    close_image();
}

static void test_exfat_media(void) {
    static const char *bad_geometry[] = {
        "bad-checksum", "bad-geometry", "bad-fat", "bad-heap", "bad-root",
        "bad-volume", "bad-clusters", "bad-shift"
    };
    static const char *bad_bitmap[] = {
        "bitmap-missing", "bitmap-id", "bitmap-bounds", "bitmap-short",
        "bitmap-loop", "bitmap-fragmented"
    };
    unsigned i;
    char name[80];
    FsFile *file;
    FsVolumeInfo info;
    uint64_t writes;
    for (i = 0; i < sizeof(bad_geometry) / sizeof(bad_geometry[0]); ++i) {
        snprintf(name, sizeof(name), "exfat-%s.img", bad_geometry[i]);
        open_image(name, 512);
        CHECK(fs_mount(&disk.device) == FS_IO_ERROR);
        close_image();
    }
    for (i = 0; i < sizeof(bad_bitmap) / sizeof(bad_bitmap[0]); ++i) {
        snprintf(name, sizeof(name), "exfat-%s.img", bad_bitmap[i]);
        open_image(name, 512);
        CHECK(fs_mount(&disk.device) == FS_UNSUPPORTED);
        CHECK(fs_mount_error() && *fs_mount_error());
        close_image();
    }
    for (i = 0; i < 2; ++i) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/exfat-%s.img", fixtures,
                 i ? "media-failure" : "dirty");
        CHECK(image_disk_copy(&disk, path, 512));
        CHECK(fs_mount(&disk.device) == FS_OK);
        CHECK(fs_volume_info(&info) == FS_OK && info.format == FS_FORMAT_EXFAT);
        CHECK(!info.writable && info.dirty == !i && info.media_failure == !!i);
        writes = disk.writes;
        CHECK(fs_stat("0:/README.TXT", &entry) == FS_OK && entry.size == 12);
        CHECK(fs_open("0:/README.TXT", FS_OPEN_WRITE, &file) == FS_WRITE_PROTECTED);
        CHECK(fs_mkdir("0:/denied") == FS_WRITE_PROTECTED);
        CHECK(disk.writes == writes);
        close_image();
    }
    open_image("exfat-bad-vdl.img", 512);
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &file) == FS_IO_ERROR);
    close_image();
    open_image("exfat.img", 512);
    CHECK(fs_mount(&disk.device) == FS_IO_ERROR);
    close_image();
    open_image("exfat-bad-set-checksum.img", 512);
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &file) == FS_IO_ERROR);
    close_image();
}

static void test_exfat_vdl(void) {
    unsigned char buf[32];
    uint32_t count, i;
    FsFile *file;
    copy_image("exfat-short-vdl.img", 512);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ | FS_OPEN_WRITE, &file) == FS_OK);
    CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 12);
    CHECK(!memcmp(buf, "Hello", 5));
    for (i = 5; i < count; ++i) CHECK(!buf[i]);
    CHECK(fs_seek(file, 10) == FS_OK);
    CHECK(fs_write(file, "Z", 1, &count) == FS_OK && count == 1);
    CHECK(fs_close(file) == FS_OK);
    fs_unmount();
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &file) == FS_OK);
    memset(buf, 0xff, sizeof(buf));
    CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 12);
    CHECK(!memcmp(buf, "Hello", 5) && buf[10] == 'Z');
    for (i = 5; i < 12; ++i) if (i != 10) CHECK(!buf[i]);
    CHECK(fs_close(file) == FS_OK);
    close_image();
}

static void test_limits(void) {
    FsFile *file;
    FsVolumeInfo info;
    uint64_t writes, revision;
    copy_image("fat32-sfd.img", 512);
    CHECK(fs_volume_info(&info) == FS_OK && info.format == FS_FORMAT_FAT32);
    CHECK(info.writable && info.max_file_size == UINT32_MAX);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ | FS_OPEN_WRITE, &file) == FS_OK);
    writes = disk.writes;
    CHECK(fs_truncate(file, UINT64_C(0x100000000)) == FS_TOO_LARGE);
    CHECK(fs_truncate(file, UINT64_MAX) == FS_TOO_LARGE);
    CHECK(fs_size(file) == 12 && fs_tell(file) == 0 && disk.writes == writes);
    CHECK(fs_seek(file, UINT64_MAX) == FS_OK && fs_tell(file) == 12);
    CHECK(fs_close(file) == FS_OK);
    close_image();
    copy_image("exfat-sfd.img", 512);
    CHECK(fs_volume_info(&info) == FS_OK && info.format == FS_FORMAT_EXFAT);
    CHECK(info.writable && info.max_file_size > 0 &&
          info.max_file_size <= info.volume_bytes && info.max_name_units == 255);
    CHECK(fs_open("0:/README.TXT", FS_OPEN_READ | FS_OPEN_WRITE, &file) == FS_OK);
    writes = disk.writes;
    CHECK(fs_truncate(file, info.max_file_size + 1) == FS_TOO_LARGE);
    CHECK(fs_truncate(file, UINT64_MAX) == FS_TOO_LARGE);
    CHECK(fs_size(file) == 12 && fs_tell(file) == 0 && disk.writes == writes);
    CHECK(fs_close(file) == FS_OK);
    revision = fs_revision();
    CHECK(fs_mkdir("0:/revision") == FS_OK && fs_revision() != revision);
    revision = fs_revision();
    CHECK(fs_stat("0:/revision", &entry) == FS_OK && fs_revision() == revision);
    CHECK(entry.identity.directory_cluster >= 2 && entry.identity.parent_cluster == 0);
    CHECK(fs_unlink("0:/revision") == FS_OK && fs_revision() != revision);
    close_image();
}

static void test_exfat_names(void) {
    char path[1024], mixed[512];
    FsEntry listed;
    FsDir *dir;
    FsFile *file;
    uint32_t count;
    unsigned i;
    bool end;
    unsigned char buf[16];
    copy_image("exfat-semantics.img", 512);
    strcpy(path, "0:");
    for (i = 0; i < 40; ++i) {
        strcat(path, "/d");
        CHECK(fs_stat(path, &entry) == FS_OK && entry.is_directory);
    }
    strcat(path, "/leaf.txt");
    CHECK(fs_open(path, FS_OPEN_READ, &file) == FS_OK);
    CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 4);
    CHECK(!memcmp(buf, "deep", 4));
    CHECK(fs_close(file) == FS_OK);
    memset(mixed, 'a', 125);
    for (i = 0; i < 63; ++i) memcpy(mixed + 125 + i * 4, "\xf0\x90\x90\xb7", 4);
    strcpy(mixed + 377, ".txt");
    snprintf(path, sizeof(path), "0:/%s", mixed);
    CHECK(fs_stat(path, &entry) == FS_OK && entry.size == 7);
    CHECK(fs_opendir("0:/", &dir) == FS_OK);
    do {
        CHECK(fs_next(dir, &listed, &end) == FS_OK);
    } while (!end && strcmp(listed.name, mixed));
    CHECK(!end && listed.identity.parent_cluster == entry.identity.parent_cluster &&
          listed.identity.entry_offset == entry.identity.entry_offset);
    fs_closedir(dir);
    strcpy(path, "0:/");
    memset(path + 3, 'l', 255);
    strcpy(path + 258, "/leaf.txt");
    CHECK(fs_stat(path, &entry) == FS_OK && entry.size == 4);
    CHECK(fs_rename(path, "0:/moved-leaf.txt") == FS_OK);
    CHECK(fs_unlink("0:/moved-leaf.txt") == FS_OK);
    close_image();
}

int main(int argc, char **argv) {
    if (argc > 1) fixtures = argv[1];
    test_volumes();
    test_bad_media();
    test_bad_directories();
    test_io_errors();
    test_generation();
    test_file_generation();
    test_paths();
    test_writes();
    test_metadata("fat32-mbr.img");
    test_metadata("exfat-mbr.img");
    test_locks();
    test_write_failures();
    test_write_generation();
    test_disk_full("fat32-sfd.img");
    test_disk_full("exfat-sfd.img");
    test_exfat_media();
    test_exfat_vdl();
    test_limits();
    test_exfat_names();
    puts("Filesystem tests passed");
    return 0;
}
