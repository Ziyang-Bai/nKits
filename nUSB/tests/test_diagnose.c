#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "diagnose.h"
#include "image_disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static ImageDisk disk;
static ResidentApi api;
static MscHooks active_hooks;
static DiagProfile profile;
static bool pending, fault, fail_first, quarantine;
static bool fail_state;
static bool replay_data, sector_saved;
static unsigned char first_sector[512];
static const char *fixture = "build/fixtures/fat32-sfd.img";
static char report_text[65536];

static void poll_disk(MscInfo *info) {
    memset(info, 0, sizeof(*info));
    info->connected = disk.connected;
    info->ready = disk.connected && api.session_active;
    info->generation = disk.generation;
    info->sector_size = disk.device.sector_size;
    info->sector_count = disk.device.sector_count;
}

static MscResult open_disk(MscInfo *info, const MscHooks *hooks) {
    if (api.session_active) return MSC_BUSY;
    if (hooks && hooks->pump && hooks->pump(hooks->ctx)) return MSC_CANCELLED;
    if (!disk.connected) return MSC_NO_DEVICE;
    if (hooks) active_hooks = *hooks;
    else memset(&active_hooks, 0, sizeof(active_hooks));
    api.session_active = 1;
    ++api.session_id;
    poll_disk(info);
    return MSC_OK;
}

static MscResult read_disk(uint32_t lba, uint32_t count, void *buf) {
    if (!api.session_active) return MSC_NO_DEVICE;
    bool saved_pending = pending;
    pending = true;
    bool cancel = active_hooks.pump && active_hooks.pump(active_hooks.ctx);
    pending = saved_pending;
    if (cancel) return MSC_CANCELLED;
    return disk.device.read(disk.device.ctx, lba, count, buf);
}

static bool diagnostic_data(const void *buf) {
    const unsigned char *bytes = buf;
    unsigned i;
    for (i = 0; i < 32; ++i)
        if (bytes[i] != (unsigned char)(i * 37u + 11u)) return false;
    return true;
}

static MscResult write_disk(uint32_t lba, uint32_t count, const void *buf) {
    if (!api.session_active) return MSC_NO_DEVICE;
    if (!fault && profile == DIAG_BASELINE &&
        (quarantine || (fail_first && diagnostic_data(buf)))) {
        fault = true;
        if (quarantine) {
            pending = !fail_state;
            if (fail_state) api.state = RESIDENT_FAILED;
        }
        api.last_error = MSC_TIMEOUT;
        api.session_active = 0;
        return MSC_TIMEOUT;
    }
    if (replay_data && profile == DIAG_BASELINE && count == 1) {
        const unsigned char *bytes = buf;
        bool pattern = true;
        for (unsigned i = 1; i < 32; ++i) {
            if (bytes[i] != (unsigned char)(bytes[0] + i * 37u) ||
                bytes[480 + i] != (unsigned char)(bytes[480] + i * 37u))
                pattern = false;
        }
        if (pattern) {
            if (!sector_saved) {
                memcpy(first_sector, buf, sizeof(first_sector));
                sector_saved = true;
            }
            return disk.device.write(disk.device.ctx, lba, count, first_sector);
        }
    }
    return disk.device.write(disk.device.ctx, lba, count, buf);
}

static MscResult sync_disk(void) {
    return disk.device.sync ? disk.device.sync(disk.device.ctx) : MSC_OK;
}

static void close_disk(void) { api.session_active = 0; }

bool diagnose_usb_profile(DiagProfile value) { profile = value; return true; }
bool diagnose_usb_pending(void) { return pending; }
bool diagnose_usb_information(FILE *report) {
    fprintf(report, "image-backed read-only information; sectors=%llu\n",
            (unsigned long long)disk.device.sector_count);
    return disk.connected;
}
void diagnose_usb_finish(FILE *report) { fprintf(report, "image-backed transport trace\n"); }
const char *diagnose_profile_name(DiagProfile value) {
    static const char *const names[] = {"baseline", "pace2ms", "pace20ms", "CSW100ms", "CSW500ms",
                                       "aligned", "unaligned", "wait15s", "baseline repeat"};
    return names[value];
}
void diagnose_progress(unsigned current, unsigned total, const char *label) {
    (void)current; (void)total; (void)label;
}
bool diagnose_cancelled(void) { return false; }

static void setup(bool writable) {
    CHECK(writable ? image_disk_copy(&disk, fixture, 512) : image_disk_open(&disk, fixture, 512));
    memset(&api, 0, sizeof(api));
    api.magic = NUSB_RESIDENT_MAGIC;
    api.abi = NUSB_RESIDENT_ABI;
    api.state = RESIDENT_READY;
    api.open = open_disk;
    api.close = close_disk;
    api.read = read_disk;
    api.poll = poll_disk;
    if (writable) { api.write = write_disk; api.sync = sync_disk; }
    profile = DIAG_BASELINE;
    pending = fault = fail_first = quarantine = fail_state = replay_data = sector_saved = false;
}

static void teardown(void) { fs_unmount(); image_disk_close(&disk); }

static DiagSummary run(void) {
    FILE *report = tmpfile();
    DiagSummary summary;
    size_t count;
    CHECK(report != NULL);
    summary = diagnose_run(&api, report, false);
    CHECK(!api.session_active);
    CHECK(!fflush(report));
    rewind(report);
    count = fread(report_text, 1, sizeof(report_text) - 1, report);
    CHECK(!ferror(report) && count < sizeof(report_text) - 1);
    report_text[count] = '\0';
    CHECK(!fclose(report));
    return summary;
}

static uint64_t image_hash(void) {
    unsigned char buf[32768];
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t count, i;
    CHECK(!fflush(disk.file));
    rewind(disk.file);
    while ((count = fread(buf, 1, sizeof(buf), disk.file)) != 0)
        for (i = 0; i < count; ++i) hash = (hash ^ buf[i]) * UINT64_C(1099511628211);
    CHECK(!ferror(disk.file));
    return hash;
}

static unsigned directory_count(void) {
    FsDir *dir;
    FsEntry entry;
    bool end = false;
    unsigned count = 0;
    CHECK(fs_opendir("0:/", &dir) == FS_OK);
    while (!end) {
        CHECK(fs_next(dir, &entry, &end) == FS_OK);
        if (!end) ++count;
    }
    fs_closedir(dir);
    return count;
}

static void collision_create(void) {
    static const unsigned char contents[] = "user data: do not truncate or delete";
    FsFile *file;
    uint32_t actual;
    CHECK(fs_open("0:/nUSB-diag-00-00", FS_OPEN_WRITE | FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE, &file) == FS_OK);
    CHECK(fs_write(file, contents, sizeof(contents), &actual) == FS_OK && actual == sizeof(contents));
    CHECK(fs_sync(file) == FS_OK);
    CHECK(fs_close(file) == FS_OK);
}

static void collision_check(void) {
    static const unsigned char contents[] = "user data: do not truncate or delete";
    unsigned char buf[sizeof(contents)];
    FsFile *file;
    uint32_t actual;
    CHECK(fs_open("0:/nUSB-diag-00-00", FS_OPEN_READ, &file) == FS_OK);
    CHECK(fs_size(file) == sizeof(contents));
    CHECK(fs_read(file, buf, sizeof(buf), &actual) == FS_OK && actual == sizeof(buf));
    CHECK(!memcmp(buf, contents, sizeof(buf)));
    CHECK(fs_close(file) == FS_OK);
}

static void no_scratch(unsigned start) {
    FsEntry entry;
    unsigned p, i;
    char path[128];
    for (p = start; p < DIAG_PROFILE_COUNT; ++p) {
        for (i = 0; i < 32; ++i) {
            if (!p && !i) continue;
            snprintf(path, sizeof(path), "0:/nUSB-diag-%02u-%02u", p, i);
            CHECK(fs_stat(path, &entry) == FS_NOT_FOUND);
        }
    }
}

static void test_healthy(void) {
    DiagSummary summary;
    unsigned count;
    setup(true);
    CHECK(fs_mount(&disk.device) == FS_OK);
    collision_create();
    count = directory_count();
    fs_unmount();
    summary = run();
    CHECK(summary.failed == 0 && !summary.stopped && !summary.leftovers);
    CHECK(summary.passed > 30 && summary.skipped == 1);
    CHECK(strstr(report_text, "PASS size-65536.tns"));
    CHECK(strstr(report_text, "PASS offset.tns"));
    CHECK(strstr(report_text, "PASS append.tns"));
    CHECK(strstr(report_text, "PASS rename.tns"));
    CHECK(strstr(report_text, "PASS 资料-USB-long-filename"));
    CHECK(strstr(report_text, "PASS subdirectory lifecycle"));
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(directory_count() == count);
    collision_check();
    no_scratch(0);
    teardown();
}

static void test_readonly(void) {
    DiagSummary summary;
    uint64_t before;
    setup(false);
    before = image_hash();
    summary = run();
    CHECK(!summary.failed && !summary.stopped && !summary.leftovers);
    CHECK(summary.skipped >= 30);
    CHECK(image_hash() == before);
    teardown();
}

static void test_recovery(void) {
    DiagSummary summary;
    const char *later;
    setup(true);
    fail_first = true;
    summary = run();
    CHECK(summary.failed && summary.skipped && !summary.stopped);
    CHECK(strstr(report_text, "FAIL size-230.tns"));
    CHECK(summary.leftovers);
    later = strstr(report_text, "PROFILE 1 pace2ms");
    CHECK(later && strstr(later, "PASS size-230.tns"));
    CHECK(strstr(later, "PASS size-4096.tns"));
    CHECK(fs_mount(&disk.device) == FS_OK);
    {
        FsEntry entry;
        CHECK(fs_stat("0:/nUSB-diag-00-00", &entry) == FS_OK && entry.is_directory);
        CHECK(fs_stat("0:/nUSB-diag-00-00/size-230.tns", &entry) == FS_OK);
    }
    no_scratch(1);
    teardown();
}

static void test_replayed_sector(void) {
    unsigned char contents[1024];
    FsFile *file;
    uint32_t actual;
    setup(true);
    replay_data = true;
    DiagSummary summary = run();
    CHECK(summary.failed && summary.leftovers && !summary.stopped);
    CHECK(strstr(report_text, "FAIL size-4096.tns"));
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_open("0:/nUSB-diag-00-00/size-4096.tns", FS_OPEN_READ, &file) == FS_OK);
    CHECK(fs_read(file, contents, sizeof(contents), &actual) == FS_OK && actual == sizeof(contents));
    CHECK(!memcmp(contents, contents + 512, 512));
    CHECK(fs_close(file) == FS_OK);
    no_scratch(1);
    teardown();
}

static void test_pending(bool state_failed) {
    DiagSummary summary;
    uint64_t before;
    setup(true);
    before = image_hash();
    fail_state = state_failed;
    quarantine = true;
    summary = run();
    CHECK(summary.failed && summary.stopped && summary.skipped >= 30);
    CHECK(strstr(report_text, state_failed ? "resident FAILED; restart required" :
                 "pending callback / quarantined transport"));
    CHECK(strstr(report_text, "SKIP size-4096.tns"));
    CHECK(!strstr(report_text, "PASS size-230.tns"));
    CHECK(image_hash() == before);
    CHECK(fs_mount(&disk.device) == FS_OK);
    no_scratch(1);
    teardown();
}

int main(int argc, char **argv) {
    if (argc > 1) fixture = argv[1];
    test_healthy();
    test_readonly();
    test_recovery();
    test_replayed_sector();
    test_pending(false);
    test_pending(true);
    puts("diagnose tests passed");
    return 0;
}
