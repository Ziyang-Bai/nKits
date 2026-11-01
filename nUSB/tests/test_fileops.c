#define _POSIX_C_SOURCE 200809L
#include "fileops.h"
#include "image_disk.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static ImageDisk disk;
static char documents[] = "/tmp/nusb-fileops-XXXXXX";
static const char *fixtures = "build/fixtures";
static unsigned char data[STORAGE_COPY_CHUNK];

enum { SETUP_OK, SETUP_FDOPEN, SETUP_SEEK, SETUP_TELL };
static unsigned setup_fault;
static bool fail_unlink;
static FILE *setup_stream;
FILE *__real_fdopen(int fd, const char *mode);
int __real_fseeko64(FILE *file, off_t offset, int whence);
off_t __real_ftello64(FILE *file);
int __real_unlink(const char *path);

FILE *__wrap_fdopen(int fd, const char *mode) {
    FILE *file;
    if (setup_fault == SETUP_FDOPEN) {
        setup_fault = SETUP_OK; errno = ENOMEM; return NULL;
    }
    file = __real_fdopen(fd, mode);
    if (setup_fault != SETUP_OK) setup_stream = file;
    return file;
}
int __wrap_fseeko64(FILE *file, off_t offset, int whence) {
    if (setup_fault == SETUP_SEEK && file == setup_stream) {
        setup_fault = SETUP_OK; setup_stream = NULL; errno = EIO; return -1;
    }
    return __real_fseeko64(file, offset, whence);
}
off_t __wrap_ftello64(FILE *file) {
    if (setup_fault == SETUP_TELL && file == setup_stream) {
        setup_fault = SETUP_OK; setup_stream = NULL; errno = EIO; return (off_t)-1;
    }
    return __real_ftello64(file);
}
int __wrap_unlink(const char *path) {
    if (fail_unlink) { fail_unlink = false; errno = EACCES; return -1; }
    return __real_unlink(path);
}

static StorageLocation location(StorageKind kind, const char *path) {
    StorageLocation value;
    CHECK(storage_location(&value, kind, path) == FS_OK);
    return value;
}
static bool exists(const StorageLocation *path) {
    FsEntry entry;
    FsResult result = storage_stat(path, &entry);
    CHECK(result == FS_OK || result == FS_NOT_FOUND);
    return result == FS_OK;
}
static void create(const StorageLocation *path, uint64_t size) {
    StorageFile *file;
    uint64_t offset = 0;
    CHECK(storage_open(path, FS_OPEN_WRITE | FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE, &file) == FS_OK);
    while (offset < size) {
        uint32_t i, count = size - offset > sizeof(data) ? sizeof(data) : (uint32_t)(size - offset), actual;
        for (i = 0; i < count; ++i) data[i] = (unsigned char)((offset + i) * 37 + 11);
        CHECK(storage_write(file, data, count, &actual) == FS_OK && actual == count);
        offset += count;
    }
    CHECK(storage_sync(file) == FS_OK);
    CHECK(storage_close(file) == FS_OK);
}
static void contents(const StorageLocation *path, uint64_t size) {
    StorageFile *file;
    uint64_t offset = 0;
    CHECK(storage_open(path, FS_OPEN_READ, &file) == FS_OK);
    CHECK(storage_size(file) == size);
    while (offset < size) {
        uint32_t i, actual, count = size - offset > sizeof(data) ? sizeof(data) : (uint32_t)(size - offset);
        CHECK(storage_read(file, data, count, &actual) == FS_OK && actual == count);
        for (i = 0; i < count; ++i) CHECK(data[i] == (unsigned char)((offset + i) * 37 + 11));
        offset += count;
    }
    CHECK(storage_close(file) == FS_OK);
}
static FileJob *begin(FileOperation op, const StorageLocation *source, const StorageLocation *destination) {
    FileJob *job;
    CHECK(fileops_begin(&job, op, source, destination) == FS_OK);
    return job;
}
static void until(FileJob *job, FileJobPhase phase) {
    unsigned count = 0;
    while (fileops_status(job)->phase != phase) {
        CHECK(fileops_status(job)->phase != FILE_RESULT);
        CHECK(++count < 1000000);
        fileops_step(job);
    }
}
static FileJobStatus run(FileOperation op, const StorageLocation *source, const StorageLocation *destination) {
    FileJob *job = begin(op, source, destination);
    FileJobStatus status;
    until(job, FILE_RESULT);
    status = *fileops_status(job);
    fileops_destroy(job);
    return status;
}
static void erase(const StorageLocation *path) {
    FileJobStatus status = run(FILE_DELETE, path, NULL);
    CHECK(status.result == FS_OK);
    CHECK(!exists(path));
}
static void mount_image(void) {
    char path[1024];
    CHECK(snprintf(path, sizeof(path), "%s/fat32-sfd.img", fixtures) < (int)sizeof(path));
    CHECK(image_disk_copy(&disk, path, 512));
    CHECK(fs_mount(&disk.device) == FS_OK);
}
static void unmount_image(void) { fs_unmount(); image_disk_close(&disk); }

static void test_paths(void) {
    StorageLocation value = location(STORAGE_INTERNAL, "/documents/a/../b");
    CHECK(!strcmp(value.path, "/documents/b"));
    CHECK(storage_location(&value, STORAGE_INTERNAL, "/documents/USB/a") == FS_DENIED);
    CHECK(storage_location(&value, STORAGE_INTERNAL, "/documents/uSb") == FS_DENIED);
    CHECK(storage_location(&value, STORAGE_INTERNAL, "/documents/../escape") == FS_DENIED);
    CHECK(storage_location(&value, STORAGE_INTERNAL, "/documents-not/a") == FS_INVALID_ARGUMENT);
    CHECK(storage_validate_name("资料-\xf0\x9f\x98\x80.tns") == FS_OK);
    CHECK(storage_validate_name("\xc0\x80") == FS_INVALID_ARGUMENT);
    CHECK(storage_validate_name("\xed\xa0\x80") == FS_INVALID_ARGUMENT);
    CHECK(storage_validate_name("bad.") == FS_INVALID_ARGUMENT);
    CHECK(storage_validate_name("../x") == FS_INVALID_ARGUMENT);
}
static void test_sizes(void) {
    static const unsigned sizes[] = {0, 1, 32767, 32768, 32769};
    unsigned i;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        StorageLocation source = location(STORAGE_USB, "0:/job-source.bin");
        StorageLocation target = location(STORAGE_INTERNAL, "/documents/job-target.bin");
        StorageLocation back = location(STORAGE_USB, "0:/job-back.bin");
        FileJobStatus status;
        create(&source, sizes[i]);
        status = run(FILE_COPY, &source, &target);
        CHECK(status.result == FS_OK && status.bytes_done == sizes[i]);
        CHECK(status.bytes_total == sizes[i] && status.copied == 1 && !status.deleted);
        contents(&source, sizes[i]); contents(&target, sizes[i]);
        status = run(FILE_MOVE, &target, &back);
        CHECK(status.result == FS_OK && status.copied == 1 && status.deleted == 1);
        CHECK(!exists(&target)); contents(&back, sizes[i]);
        erase(&source); erase(&back);
    }
}
static void make_tree(StorageLocation *root) {
    StorageLocation child, nested;
    unsigned i;
    CHECK(storage_mkdir(root) == FS_OK);
    child = *root;
    for (i = 0; i < 36; ++i) {
        CHECK(storage_join(&nested, &child, "d") == FS_OK);
        CHECK(storage_mkdir(&nested) == FS_OK); child = nested;
    }
    CHECK(storage_join(&nested, &child, "资料.txt") == FS_OK); create(&nested, 32769);
    CHECK(storage_join(&nested, root, "z.txt") == FS_OK); create(&nested, 1);
    CHECK(storage_join(&nested, root, "a.txt") == FS_OK); create(&nested, 0);
}
static void test_tree(void) {
    StorageLocation source = location(STORAGE_USB, "0:/job-tree");
    StorageLocation target = location(STORAGE_INTERNAL, "/documents/job-tree");
    StorageLocation back = location(STORAGE_USB, "0:/job-return");
    StorageLocation child;
    FileJobStatus status;
    make_tree(&source);
    status = run(FILE_COPY, &source, &target);
    CHECK(status.result == FS_OK && status.items_total == 40 && status.copied == 40);
    CHECK(status.bytes_total == 32770 && status.bytes_done == 32770);
    CHECK(storage_join(&child, &target, "z.txt") == FS_OK); contents(&child, 1);
    status = run(FILE_MOVE, &target, &back);
    CHECK(status.result == FS_OK && status.copied == 40 && status.deleted == 40);
    CHECK(!exists(&target));
    erase(&source); erase(&back);
}
static void test_conflicts(void) {
    StorageLocation source = location(STORAGE_USB, "0:/job-conflict");
    StorageLocation target = location(STORAGE_INTERNAL, "/documents/job-conflict");
    StorageLocation alternate = location(STORAGE_INTERNAL, "/documents/job-alternate");
    FileJob *job;
    create(&source, 32769); create(&target, 1);
    job = begin(FILE_MOVE, &source, &target); until(job, FILE_CONFLICT);
    CHECK(fileops_decide(job, FILE_SKIP, NULL) == FS_OK); until(job, FILE_RESULT);
    CHECK(fileops_status(job)->result == FS_OK && fileops_status(job)->skipped == 1);
    CHECK(!fileops_status(job)->deleted); fileops_destroy(job);
    contents(&source, 32769); contents(&target, 1);
    job = begin(FILE_COPY, &source, &target); until(job, FILE_CONFLICT);
    CHECK(fileops_decide(job, FILE_RENAME_ITEM, "job-alternate") == FS_OK); until(job, FILE_RESULT);
    CHECK(fileops_status(job)->result == FS_OK); fileops_destroy(job);
    contents(&alternate, 32769); contents(&target, 1);
    erase(&source); erase(&target); erase(&alternate);

    source = location(STORAGE_USB, "0:/job-conflict-tree");
    target = location(STORAGE_INTERNAL, "/documents/job-conflict-tree");
    alternate = location(STORAGE_INTERNAL, "/documents/job-alternate-tree");
    make_tree(&source); CHECK(storage_mkdir(&target) == FS_OK);
    job = begin(FILE_COPY, &source, &target); until(job, FILE_CONFLICT);
    CHECK(fileops_decide(job, FILE_RENAME_ITEM, "job-alternate-tree") == FS_OK); until(job, FILE_RESULT);
    CHECK(fileops_status(job)->result == FS_OK && fileops_status(job)->items_total == 40);
    CHECK(fileops_status(job)->copied == 40); fileops_destroy(job);
    erase(&source); erase(&target); erase(&alternate);
}
static void test_cancel(void) {
    StorageLocation source = location(STORAGE_USB, "0:/job-cancel");
    StorageLocation target = location(STORAGE_INTERNAL, "/documents/job-cancel");
    FileJob *job;
    unsigned mode;
    create(&source, 3 * STORAGE_COPY_CHUNK);
    for (mode = 0; mode < 3; ++mode) {
        job = begin(FILE_MOVE, &source, &target);
        if (mode == 1) {
            while (!fileops_status(job)->bytes_done) {
                CHECK(fileops_status(job)->phase != FILE_RESULT); fileops_step(job);
            }
        } else if (mode == 2) until(job, FILE_FINISHING);
        fileops_cancel(job); until(job, FILE_RESULT);
        CHECK(fileops_status(job)->result == FS_CANCELLED);
        CHECK(!fileops_status(job)->temporary_path[0]);
        CHECK(!fileops_status(job)->deleted); fileops_destroy(job);
        contents(&source, 3 * STORAGE_COPY_CHUNK); CHECK(!exists(&target));
    }
    job = begin(FILE_MOVE, &source, &target); until(job, FILE_REMOVING_SOURCE);
    fileops_cancel(job); until(job, FILE_RESULT);
    CHECK(fileops_status(job)->result == FS_CANCELLED && fileops_status(job)->copied == 1);
    CHECK(!fileops_status(job)->deleted); fileops_destroy(job);
    contents(&source, 3 * STORAGE_COPY_CHUNK); contents(&target, 3 * STORAGE_COPY_CHUNK);
    erase(&source); erase(&target);
}
static void test_preflight(void) {
    StorageLocation source = location(STORAGE_USB, "0:/job-preflight");
    StorageLocation target = location(STORAGE_INTERNAL, "/documents/job-preflight");
    StorageLocation child, nested;
    FileJobStatus status;
    char longname[257];
    CHECK(storage_mkdir(&source) == FS_OK);
    CHECK(storage_join(&child, &source, "first") == FS_OK); create(&child, 1);
    CHECK(storage_join(&nested, &source, "last") == FS_OK); CHECK(storage_mkdir(&nested) == FS_OK);
    memset(longname, 'x', 254); longname[254] = 0;
    CHECK(storage_join(&child, &nested, longname) == FS_OK); create(&child, 1);
    status = run(FILE_COPY, &source, &target);
    CHECK(status.result == FS_PATH_TOO_LONG && !status.copied);
    CHECK(!exists(&target));
    child = source; CHECK(storage_join(&child, &source, "inside") == FS_OK);
    status = run(FILE_COPY, &source, &child);
    CHECK(status.result == FS_INVALID_ARGUMENT && !exists(&child));
    erase(&source);

    source = location(STORAGE_INTERNAL, "/documents/job-huge");
    target = location(STORAGE_USB, "0:/job-huge");
    {
        char path[1024]; int fd;
        CHECK(snprintf(path, sizeof(path), "%s/job-huge", documents) < (int)sizeof(path));
        fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600); CHECK(fd >= 0);
        CHECK(ftruncate(fd, (off_t)INT32_MAX + 1) == 0); CHECK(close(fd) == 0);
        status = run(FILE_COPY, &source, &target);
        CHECK(status.result == FS_TOO_LARGE && !exists(&target));
        CHECK(unlink(path) == 0);
    }
}
static void test_rename_and_delete(void) {
    StorageLocation source = location(STORAGE_USB, "0:/job-case.txt");
    StorageLocation target = location(STORAGE_USB, "0:/JOB-CASE.TXT");
    FileJobStatus status;
    FileJob *job;
    FsEntry entry;
    create(&source, 32769);
    job = begin(FILE_RENAME, &source, &target);
    while (!fileops_status(job)->temporary_path[0]) {
        CHECK(fileops_status(job)->phase != FILE_RESULT); fileops_step(job);
    }
    fileops_cancel(job); until(job, FILE_RESULT);
    CHECK(fileops_status(job)->result == FS_CANCELLED);
    CHECK(!fileops_status(job)->temporary_path[0]);
    CHECK(storage_stat(&source, &entry) == FS_OK && !strcmp(entry.name, "job-case.txt"));
    fileops_destroy(job); contents(&source, 32769);
    status = run(FILE_RENAME, &source, &target);
    CHECK(status.result == FS_OK && status.copied == 1 && status.deleted == 1);
    CHECK(storage_stat(&target, &entry) == FS_OK && !strcmp(entry.name, "JOB-CASE.TXT"));
    contents(&target, 32769); erase(&target);
    source = location(STORAGE_USB, "0:/job-delete-tree"); make_tree(&source);
    job = begin(FILE_DELETE, &source, NULL); until(job, FILE_DELETING);
    while (!fileops_status(job)->deleted) {
        CHECK(fileops_status(job)->phase != FILE_RESULT); fileops_step(job);
    }
    fileops_cancel(job); until(job, FILE_RESULT);
    CHECK(fileops_status(job)->result == FS_CANCELLED && fileops_status(job)->deleted > 0);
    CHECK(exists(&source)); fileops_destroy(job); erase(&source);
}
static void test_faults(void) {
    StorageLocation source = location(STORAGE_INTERNAL, "/documents/job-fault");
    StorageLocation target = location(STORAGE_USB, "0:/job-fault");
    FileJob *job;
    create(&source, 3 * STORAGE_COPY_CHUNK);
    job = begin(FILE_MOVE, &source, &target);
    while (!fileops_status(job)->temporary_path[0]) {
        CHECK(fileops_status(job)->phase != FILE_RESULT); fileops_step(job);
    }
    disk.fail_write_at = disk.writes + 1; disk.failure = MSC_IO_ERROR;
    until(job, FILE_RESULT);
    CHECK(fileops_status(job)->result == FS_IO_ERROR && !fileops_status(job)->deleted);
    CHECK(fileops_status(job)->temporary_path[0]);
    fileops_destroy(job); contents(&source, 3 * STORAGE_COPY_CHUNK);
    disk.fail_write_at = 0;
    erase(&source);
    unmount_image(); mount_image();

    source = location(STORAGE_USB, "0:/job-detach");
    target = location(STORAGE_INTERNAL, "/documents/job-detach");
    create(&source, 3 * STORAGE_COPY_CHUNK);
    job = begin(FILE_MOVE, &source, &target);
    while (!fileops_status(job)->bytes_done) {
        CHECK(fileops_status(job)->phase != FILE_RESULT); fileops_step(job);
    }
    image_disk_detach(&disk); fileops_fail(job, FS_NO_DEVICE);
    {
        unsigned steps = 0;
        while (fileops_status(job)->phase != FILE_RESULT) {
            CHECK(++steps <= 4);
            fileops_fail(job, FS_NO_DEVICE);
            fileops_step(job);
        }
    }
    CHECK(fileops_status(job)->result == FS_NO_DEVICE && !fileops_status(job)->deleted);
    CHECK(!exists(&target));
    if (fileops_status(job)->temporary_path[0]) {
        StorageLocation temporary = location(STORAGE_INTERNAL, fileops_status(job)->temporary_path);
        CHECK(storage_unlink(&temporary) == FS_OK);
    }
    fileops_destroy(job);
    unmount_image(); mount_image();
}
static void test_metadata_and_temps(void) {
    StorageLocation source = location(STORAGE_USB, "0:/job-metadata");
    StorageLocation target = location(STORAGE_INTERNAL, "/documents/job-metadata");
    StorageLocation collision = location(STORAGE_INTERNAL, "/documents/.nusb-00000000.part");
    FsTimes times = {0x5821, 0x5822, 0x1234, 0x5821, 0x2345};
    FsEntry entry;
    FileJobStatus status;
    create(&source, 32769); create(&collision, 1);
    CHECK(storage_set_metadata(&source, 0x21, &times) == FS_OK);
    status = run(FILE_MOVE, &source, &target);
    CHECK(status.result == FS_DENIED && !exists(&target));
    status = run(FILE_COPY, &source, &target);
    CHECK(status.result == FS_OK && status.copied == 1);
    CHECK(storage_stat(&target, &entry) == FS_OK);
    CHECK((entry.attributes & 1) && entry.date == times.modified_date && entry.time == times.modified_time);
    contents(&target, 32769); contents(&collision, 1);
    CHECK(storage_set_metadata(&source, 0x20, &times) == FS_OK);
    CHECK(storage_set_metadata(&target, 0x20, &times) == FS_OK);
    erase(&source); erase(&target); erase(&collision);
}
static void test_sync_failure(void) {
    StorageLocation source = location(STORAGE_INTERNAL, "/documents/job-sync");
    StorageLocation target = location(STORAGE_USB, "0:/job-sync");
    FileJob *job;
    create(&source, 32769);
    job = begin(FILE_MOVE, &source, &target);
    until(job, FILE_FINISHING);
    disk.fail_sync_at = disk.syncs + 1; disk.failure = MSC_IO_ERROR;
    until(job, FILE_RESULT);
    CHECK(fileops_status(job)->result == FS_IO_ERROR);
    CHECK(!fileops_status(job)->copied && !fileops_status(job)->deleted);
    CHECK(fileops_status(job)->temporary_path[0]);
    fileops_destroy(job); contents(&source, 32769);
    erase(&source);
    unmount_image(); mount_image();
}
static void test_setup_failures(void) {
    StorageLocation source = location(STORAGE_USB, "0:/job-setup");
    StorageLocation target = location(STORAGE_INTERNAL, "/documents/job-setup");
    StorageLocation temporary = location(STORAGE_INTERNAL, "/documents/.nusb-00000000.part");
    FileJobStatus status;
    StorageFile *file = NULL;
    unsigned fault;
    create(&source, 32769);
    for (fault = SETUP_FDOPEN; fault <= SETUP_TELL; ++fault) {
        setup_fault = fault;
        status = run(FILE_MOVE, &source, &target);
        CHECK(setup_fault == SETUP_OK);
        CHECK(status.result == (fault == SETUP_FDOPEN ? FS_NO_MEMORY : FS_IO_ERROR));
        CHECK(!status.copied && !status.deleted && !status.temporary_path[0]);
        CHECK(!exists(&temporary) && !exists(&target));
        contents(&source, 32769);
    }
    setup_fault = SETUP_FDOPEN; fail_unlink = true;
    status = run(FILE_MOVE, &source, &target);
    CHECK(setup_fault == SETUP_OK && !fail_unlink);
    CHECK(status.result == FS_IO_ERROR);
    CHECK(!strcmp(status.temporary_path, temporary.path));
    CHECK(exists(&temporary) && !exists(&target));
    contents(&source, 32769); erase(&temporary); erase(&source);

    create(&target, 32769);
    setup_fault = SETUP_FDOPEN;
    CHECK(storage_open(&target, FS_OPEN_WRITE, &file) == FS_NO_MEMORY && !file);
    contents(&target, 32769); erase(&target);
}
static void test_restore_failures(void) {
    unsigned detached;
    for (detached = 0; detached < 2; ++detached) {
        StorageLocation source = location(STORAGE_USB, "0:/job-restore.txt");
        StorageLocation target = location(STORAGE_USB, "0:/JOB-RESTORE.TXT");
        FileJob *job;
        create(&source, 32769);
        job = begin(FILE_RENAME, &source, &target);
        while (!fileops_status(job)->temporary_path[0]) {
            CHECK(fileops_status(job)->phase != FILE_RESULT); fileops_step(job);
        }
        if (detached) image_disk_detach(&disk);
        else { disk.fail_write_at = disk.writes + 1; disk.failure = MSC_IO_ERROR; }
        fileops_cancel(job); until(job, FILE_RESULT);
        CHECK(fileops_status(job)->result == (detached ? FS_NO_DEVICE : FS_IO_ERROR));
        CHECK(fileops_status(job)->temporary_path[0]);
        CHECK(!strcmp(fileops_status(job)->failure_path, fileops_status(job)->temporary_path));
        CHECK(!fileops_status(job)->copied && !fileops_status(job)->deleted);
        fileops_destroy(job);
        unmount_image(); mount_image();
    }
}
int main(int argc, char **argv) {
    if (argc > 1) fixtures = argv[1];
    CHECK(mkdtemp(documents) != NULL);
    CHECK(setenv("NUSB_TEST_DOCUMENTS", documents, 1) == 0);
    mount_image();
    test_paths(); test_sizes(); test_tree(); test_conflicts(); test_cancel();
    test_preflight(); test_rename_and_delete(); test_metadata_and_temps();
    test_sync_failure(); test_faults(); test_setup_failures(); test_restore_failures();
    unmount_image();
    CHECK(rmdir(documents) == 0);
    puts("fileops tests passed");
    return 0;
}
