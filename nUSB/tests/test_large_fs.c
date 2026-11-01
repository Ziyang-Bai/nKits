#include "fs.h"
#include "image_disk.h"
#include "fileops.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#define LARGE_SIZE (UINT64_C(0x100000000) + 12345)
static unsigned char buf[32768], expected[32768];
static const uint64_t sentinels[] = {
    0, 65504, UINT64_C(0x80000000) - 32, UINT64_C(0x100000000) - 32,
    UINT64_C(0x100000000) + 32, LARGE_SIZE - 64
};

static void verify_file(const char *path) {
    FsFile *file;
    uint64_t offset = 0;
    uint32_t count;
    unsigned i;
    CHECK(fs_open(path, FS_OPEN_READ, &file) == FS_OK);
    CHECK(fs_size(file) == LARGE_SIZE);
    while (offset < LARGE_SIZE) {
        uint32_t wanted = LARGE_SIZE - offset < sizeof(buf) ?
                          (uint32_t)(LARGE_SIZE - offset) : sizeof(buf);
        memset(expected, 0, wanted);
        for (i = 0; i < sizeof(sentinels) / sizeof(sentinels[0]); ++i) {
            uint64_t first = sentinels[i] > offset ? sentinels[i] : offset;
            uint64_t end = sentinels[i] + 64 < offset + wanted ?
                           sentinels[i] + 64 : offset + wanted;
            uint64_t pos;
            for (pos = first; pos < end; ++pos)
                expected[pos - offset] = (unsigned char)(pos % 251 + 1);
        }
        CHECK(fs_read(file, buf, wanted, &count) == FS_OK && count == wanted);
        CHECK(!memcmp(buf, expected, wanted));
        offset += count;
        CHECK(fs_tell(file) == offset);
    }
    CHECK(fs_read(file, buf, 1, &count) == FS_OK && !count);
    CHECK(fs_close(file) == FS_OK);
}

static void run_job(FileOperation operation, const char *from, const char *to) {
    StorageLocation source, destination;
    FileJob *job;
    const FileJobStatus *status;
    uint64_t previous = 0, steps = 0;
    CHECK(storage_location(&source, STORAGE_USB, from) == FS_OK);
    if (to) CHECK(storage_location(&destination, STORAGE_USB, to) == FS_OK);
    CHECK(fileops_begin(&job, operation, &source,
                        operation == FILE_DELETE ? NULL : &destination) == FS_OK);
    do {
        fileops_step(job);
        status = fileops_status(job);
        CHECK(status && status->phase != FILE_CONFLICT);
        CHECK(status->bytes_done >= previous && status->bytes_done <= status->bytes_total);
        previous = status->bytes_done;
        CHECK(++steps < UINT64_C(10000000));
    } while (status->phase != FILE_RESULT);
    CHECK(status->result == FS_OK);
    if (operation == FILE_COPY) {
        CHECK(status->bytes_total == LARGE_SIZE && status->bytes_done == LARGE_SIZE);
        CHECK(status->copied == 1 && !status->skipped);
    } else if (operation == FILE_MOVE) {
        CHECK(status->bytes_total == LARGE_SIZE && status->bytes_total > UINT32_MAX);
        CHECK(status->bytes_done == 0 && !status->skipped);
    } else CHECK(status->deleted == 1);
    printf("job steps=%" PRIu64 " bytes=%" PRIu64 "/%" PRIu64 "\n",
           steps, status->bytes_done, status->bytes_total);
    fileops_destroy(job);
}

static void mutate(void) {
    FsFile *file;
    FsEntry entry;
    FsTimes times = {0x5d21, 0x5d22, 0x645c, 0x5b83, 0x4a21};
    uint32_t count, i;
    CHECK(fs_open("0:/copied.bin", FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_APPEND, &file) == FS_OK);
    CHECK(fs_write(file, "append", 6, &count) == FS_OK && count == 6);
    CHECK(fs_tell(file) == LARGE_SIZE + 6 && fs_size(file) == LARGE_SIZE + 6);
    CHECK(fs_seek(file, LARGE_SIZE - 32) == FS_OK);
    CHECK(fs_read(file, buf, 38, &count) == FS_OK && count == 38);
    for (i = 0; i < 32; ++i) CHECK(buf[i] == (LARGE_SIZE - 32 + i) % 251 + 1);
    CHECK(!memcmp(buf + 32, "append", 6));
    CHECK(fs_truncate(file, UINT64_C(0x100000001)) == FS_OK);
    CHECK(fs_size(file) == UINT64_C(0x100000001));
    CHECK(fs_close(file) == FS_OK);
    CHECK(fs_open("0:/copied.bin", FS_OPEN_READ | FS_OPEN_WRITE, &file) == FS_OK);
    CHECK(fs_seek(file, UINT64_C(0xffffffff)) == FS_OK);
    CHECK(fs_read(file, buf, 4, &count) == FS_OK && count == 2);
    CHECK(buf[0] == UINT64_C(0xffffffff) % 251 + 1);
    CHECK(buf[1] == UINT64_C(0x100000000) % 251 + 1);
    CHECK(fs_truncate(file, 7) == FS_OK);
    CHECK(fs_seek(file, 0) == FS_OK);
    memset(buf, 0, sizeof(buf));
    CHECK(fs_write(file, buf, 7, &count) == FS_OK && count == 7);
    CHECK(fs_truncate(file, 131073) == FS_OK);
    CHECK(fs_seek(file, 0) == FS_OK);
    do {
        CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK);
        for (i = 0; i < count; ++i) CHECK(!buf[i]);
    } while (count);
    CHECK(fs_close(file) == FS_OK);
    CHECK(fs_utime("0:/copied.bin", &times) == FS_OK);
    CHECK(fs_chmod("0:/copied.bin", 0x26) == FS_OK);
    CHECK(fs_stat("0:/copied.bin", &entry) == FS_OK && entry.size == 131073);
    CHECK(entry.attributes == 0x26 && entry.date == times.modified_date);
    CHECK(entry.created_time == times.created_time && entry.access_date == times.access_date);
}

int main(int argc, char **argv) {
    ImageDisk disk;
    FsVolumeInfo info;
    FsEntry entry;
    CHECK(argc == 3);
    CHECK(image_disk_open_writable(&disk, argv[1], 512));
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_volume_info(&info) == FS_OK && info.format == FS_FORMAT_EXFAT && info.writable);
    CHECK(info.volume_bytes == (UINT64_C(12) << 30));
    CHECK(info.max_file_size > LARGE_SIZE);
    CHECK(fs_stat("0:/source.bin", &entry) == FS_OK && entry.size == LARGE_SIZE);
    if (!strcmp(argv[2], "copy")) {
        verify_file("0:/source.bin");
        run_job(FILE_COPY, "0:/source.bin", "0:/copied.bin");
        verify_file("0:/copied.bin");
        CHECK(disk.sectors_written >= LARGE_SIZE / 512);
        CHECK(disk.sectors_read >= LARGE_SIZE / 512 * 3);
    } else if (!strcmp(argv[2], "move")) {
        CHECK(fs_stat("0:/copied.bin", &entry) == FS_OK && entry.size == LARGE_SIZE);
        CHECK(fs_stat("0:/moved.bin", &entry) == FS_NOT_FOUND);
        run_job(FILE_MOVE, "0:/copied.bin", "0:/moved.bin");
        CHECK(fs_stat("0:/copied.bin", &entry) == FS_NOT_FOUND);
        CHECK(fs_stat("0:/moved.bin", &entry) == FS_OK && entry.size == LARGE_SIZE);
        verify_file("0:/moved.bin");
        run_job(FILE_MOVE, "0:/moved.bin", "0:/copied.bin");
        CHECK(fs_stat("0:/moved.bin", &entry) == FS_NOT_FOUND);
        CHECK(fs_stat("0:/copied.bin", &entry) == FS_OK && entry.size == LARGE_SIZE);
    } else if (!strcmp(argv[2], "mutate")) {
        verify_file("0:/copied.bin");
        mutate();
    } else if (!strcmp(argv[2], "delete")) {
        CHECK(fs_stat("0:/copied.bin", &entry) == FS_OK && entry.size == 131073);
        CHECK(entry.attributes == 0x26 && entry.date == 0x5d22 && entry.created_time == 0x4a21);
        run_job(FILE_DELETE, "0:/copied.bin", NULL);
        CHECK(fs_stat("0:/copied.bin", &entry) == FS_NOT_FOUND);
        verify_file("0:/source.bin");
    } else CHECK(0);
    fs_unmount();
    CHECK(fflush(disk.file) == 0);
    printf("%s reads=%" PRIu64 " sectors_read=%" PRIu64
           " writes=%" PRIu64 " sectors_written=%" PRIu64 " syncs=%" PRIu64 "\n",
           argv[2], disk.reads, disk.sectors_read, disk.writes, disk.sectors_written, disk.syncs);
    image_disk_close(&disk);
    return 0;
}
