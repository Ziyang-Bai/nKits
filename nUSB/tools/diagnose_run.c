#include "diagnose.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#define DIAG_CHUNK 32768u
#define DIAG_SCAN 32u

/* Retain transfer storage if the controller still owns a callback. */
static unsigned char data[DIAG_CHUNK] __attribute__((aligned(32)));
static unsigned char check[DIAG_CHUNK] __attribute__((aligned(32)));
static uint32_t route_tag[2];

typedef struct {
    ResidentApi *api;
    FILE *report;
    DiagSummary sum;
    MscInfo info;
    bool active, removed, busy, generation_set;
    uint32_t generation, session_id;
    unsigned progress;
} Diag;

static bool stopped(Diag *d) {
    if (diagnose_cancelled() || diagnose_usb_pending() ||
        d->api->state == RESIDENT_FAILED || d->removed || d->busy) {
        d->sum.stopped = true;
        return true;
    }
    return d->sum.stopped;
}

static const char *stop_reason(Diag *d) {
    if (diagnose_usb_pending()) return "pending callback / quarantined transport";
    if (d->api->state == RESIDENT_FAILED) return "resident FAILED; restart required";
    if (d->removed) return "media removed or generation changed";
    if (d->busy) return "foreign or lingering native session; no forced close";
    if (diagnose_cancelled()) return "cancelled";
    return "report flush failed";
}

static void flush_report(Diag *d) {
    if (fflush(d->report) || ferror(d->report)) {
        ++d->sum.failed;
        d->sum.stopped = true;
    }
}

static bool result(Diag *d, const char *name, bool ok, const char *reason) {
    fprintf(d->report, "%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
            reason && *reason ? ": " : "", reason ? reason : "");
    if (ok) ++d->sum.passed;
    else ++d->sum.failed;
    flush_report(d);
    diagnose_progress(++d->progress, 0, name);
    return ok;
}

static void skip(Diag *d, const char *name, const char *reason) {
    fprintf(d->report, "SKIP %s: %s\n", name, reason);
    ++d->sum.skipped;
    flush_report(d);
    diagnose_progress(++d->progress, 0, name);
}

static bool fs_result(Diag *d, const char *name, FsResult r) {
    char reason[48];
    snprintf(reason, sizeof(reason), "FsResult=%u", (unsigned)r);
    return result(d, name, r == FS_OK, r == FS_OK ? NULL : reason);
}

static void leftover(Diag *d, const char *path, bool owned) {
    d->sum.leftovers = true;
    fprintf(d->report, "LEFTOVER %s [%s; preserved, no further mutation]\n",
            path, owned ? "exclusive ownership confirmed" : "creation outcome uncertain");
}

static bool pump(void *ctx) {
    Diag *d = ctx;
    return diagnose_cancelled() || d->api->state == RESIDENT_FAILED ||
           d->removed || d->busy || d->sum.stopped;
}

static MscResult disk_read(void *ctx, uint32_t lba, uint32_t count, void *buf) {
    Diag *d = ctx;
    if (stopped(d)) return MSC_CANCELLED;
    return d->api->read(lba, count, buf);
}

static MscResult disk_write(void *ctx, uint32_t lba, uint32_t count, const void *buf) {
    Diag *d = ctx;
    if (stopped(d)) return MSC_CANCELLED;
    return d->api->write(lba, count, buf);
}

static MscResult disk_sync(void *ctx) {
    Diag *d = ctx;
    if (stopped(d)) return MSC_CANCELLED;
    return d->api->sync();
}

static uint32_t disk_generation(void *ctx) {
    Diag *d = ctx;
    MscInfo info;
    memset(&info, 0, sizeof(info));
    d->api->poll(&info);
    if (!info.connected || info.generation != d->info.generation)
        d->removed = true;
    return d->removed ? ~d->info.generation : info.generation;
}

static uint32_t disk_fattime(void *ctx) {
    Diag *d = ctx;
    return d->api->fattime ? d->api->fattime() : 0;
}

static bool open_session(Diag *d, const char *label) {
    MscHooks hooks = {pump, d};
    MscResult r;
    char reason[64];
    if (stopped(d)) { skip(d, label, stop_reason(d)); return false; }
    if (d->api->session_active) {
        d->busy = true;
        d->sum.stopped = true;
        skip(d, label, stop_reason(d));
        return false;
    }
    memset(&d->info, 0, sizeof(d->info));
    r = d->api->open(&d->info, &hooks);
    snprintf(reason, sizeof(reason), "MscResult=%u last_error=%u",
             (unsigned)r, (unsigned)d->api->last_error);
    if (r != MSC_OK) {
        if (r == MSC_NO_DEVICE) d->removed = true;
        return result(d, label, false, reason);
    }
    d->active = true;
    d->session_id = d->api->session_id;
    if (d->generation_set && d->generation != d->info.generation) {
        d->removed = true;
        return result(d, label, false, "media generation changed between sessions");
    }
    d->generation = d->info.generation;
    d->generation_set = true;
    fprintf(d->report, "session=%lu generation=%lu sector_size=%lu sectors=%llu\n",
            (unsigned long)d->api->session_id, (unsigned long)d->info.generation,
            (unsigned long)d->info.sector_size, (unsigned long long)d->info.sector_count);
    return result(d, label, true, NULL);
}

static void close_session(Diag *d) {
    if (!d->active) return;
    if (d->api->session_id == d->session_id) {
        fs_unmount();
        d->api->close();
    } else {
        d->busy = true;
        d->sum.stopped = true;
    }
    d->active = false;
}

static bool mount_session(Diag *d) {
    BlockDevice disk = {d, disk_read, d->info.sector_size, d->info.sector_count,
                        disk_generation, NULL, NULL, disk_fattime};
    FsResult r;
    if (d->api->write && d->api->sync) { disk.write = disk_write; disk.sync = disk_sync; }
    r = fs_mount(&disk);
    if (r != FS_OK && fs_mount_error())
        fprintf(d->report, "mount detail: %s\n", fs_mount_error());
    return fs_result(d, "mount", r);
}

static bool sector_reads(Diag *d, const char *label) {
    unsigned i;
    MscResult r = MSC_UNSUPPORTED;
    bool ok = d->info.sector_size && d->info.sector_size <= DIAG_CHUNK &&
              d->info.sector_count && d->info.sector_count <= UINT32_MAX;
    char reason[80];
    if (ok) {
        r = disk_read(d, 0, 1, data);
        ok = r == MSC_OK;
    }
    for (i = 0; ok && i < 3; ++i) {
        r = disk_read(d, 0, 1, check);
        ok = r == MSC_OK && !memcmp(data, check, d->info.sector_size);
    }
    if (ok) {
        r = disk_read(d, (uint32_t)(d->info.sector_count - 1), 1, check);
        ok = r == MSC_OK;
    }
    snprintf(reason, sizeof(reason), "MscResult=%u; sector 0 repeats or final sector read", (unsigned)r);
    return result(d, label, ok, ok ? NULL : reason);
}

static bool root_list(Diag *d) {
    FsDir *dir = NULL;
    FsEntry entry;
    FsResult r = fs_opendir("0:/", &dir);
    bool end = false;
    unsigned i;
    for (i = 0; r == FS_OK && !end && i < 16 && !stopped(d); ++i) {
        r = fs_next(dir, &entry, &end);
        if (r == FS_OK && !end)
            fprintf(d->report, "root %c %llu %.200s\n", entry.is_directory ? 'D' : 'F',
                    (unsigned long long)entry.size, entry.name);
    }
    if (r == FS_OK && !end) fprintf(d->report, "root listing bounded at 16 entries\n");
    fs_closedir(dir);
    if (stopped(d)) return result(d, "root list", false, stop_reason(d));
    return fs_result(d, "root list", r);
}

static unsigned char byte_at(uint32_t offset, unsigned variant) {
    return (unsigned char)(offset * 37u + (offset >> 8) + 11u + variant * 71u);
}

static bool write_bytes(Diag *d, FsFile *file, uint32_t size, unsigned variant) {
    uint32_t offset = 0;
    while (offset < size && !stopped(d)) {
        uint32_t i, actual = 0, count = size - offset;
        FsResult r;
        if (count > DIAG_CHUNK) count = DIAG_CHUNK;
        for (i = 0; i < count; ++i) data[i] = byte_at(offset + i, variant);
        r = fs_write(file, data, count, &actual);
        if (r != FS_OK || actual != count) {
            fprintf(d->report, "write offset=%lu requested=%lu actual=%lu FsResult=%u\n",
                    (unsigned long)offset, (unsigned long)count, (unsigned long)actual, (unsigned)r);
            return false;
        }
        offset += count;
    }
    return offset == size;
}

static bool close_file(Diag *d, FsFile **file) {
    FsResult r;
    if (!*file) return true;
    r = fs_close(*file);
    *file = NULL;
    if (r != FS_OK) return fs_result(d, "close", r);
    return true;
}

static bool verify_file(Diag *d, const char *path, uint32_t size, unsigned mode) {
    FsFile *file = NULL;
    FsResult r = fs_open(path, FS_OPEN_READ, &file);
    uint32_t offset = 0;
    bool ok = r == FS_OK;
    if (!ok) fprintf(d->report, "reopen FsResult=%u %s\n", (unsigned)r, path);
    if (ok && fs_size(file) != size) {
        fprintf(d->report, "size expected=%lu actual=%llu\n", (unsigned long)size,
                (unsigned long long)fs_size(file));
        ok = false;
    }
    while (ok && offset < size && !stopped(d)) {
        uint32_t i, count = size - offset, actual = 0;
        if (count > DIAG_CHUNK) count = DIAG_CHUNK;
        r = fs_read(file, check, count, &actual);
        ok = r == FS_OK && actual == count;
        if (!ok) fprintf(d->report, "read offset=%lu requested=%lu actual=%lu FsResult=%u\n",
                         (unsigned long)offset, (unsigned long)count, (unsigned long)actual, (unsigned)r);
        for (i = 0; ok && i < count; ++i) {
            uint32_t p = offset + i;
            unsigned char expected = byte_at(p, 0);
            if (mode == 1 && p >= 123 && p < 353) expected = byte_at(p - 123, 1);
            if (mode == 2 && p >= 230) expected = byte_at(p - 230, 1);
            if (check[i] != expected) {
                fprintf(d->report, "mismatch offset=%lu expected=%u actual=%u\n",
                        (unsigned long)p, expected, check[i]);
                ok = false;
            }
        }
        offset += count;
    }
    if (ok && !stopped(d)) {
        uint32_t actual = 1;
        r = fs_read(file, check, 1, &actual);
        ok = r == FS_OK && actual == 0;
        if (!ok) fprintf(d->report, "EOF FsResult=%u actual=%lu\n", (unsigned)r, (unsigned long)actual);
    }
    if (!close_file(d, &file)) ok = false;
    return ok && !stopped(d);
}

/* Modes: plain, offset overwrite, append/truncate, rename. */
static bool file_case(Diag *d, const char *dir, const char *name, uint32_t size, unsigned mode) {
    char path[256], target[256];
    FsFile *file = NULL;
    FsResult r;
    bool owned = false, renamed = false, ok;
    uint32_t expected = size;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    snprintf(target, sizeof(target), "%s/renamed.tns", dir);
    r = fs_open(path, FS_OPEN_WRITE | FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE, &file);
    owned = r == FS_OK;
    ok = owned;
    if (!ok) fprintf(d->report, "exclusive create FsResult=%u %s\n", (unsigned)r, path);
    if (ok) ok = write_bytes(d, file, size, 0);
    if (ok && mode == 1) {
        r = fs_seek(file, 123);
        ok = r == FS_OK;
        if (ok) ok = write_bytes(d, file, 230, 1);
        else fprintf(d->report, "seek FsResult=%u\n", (unsigned)r);
    }
    if (ok) {
        r = fs_sync(file);
        ok = r == FS_OK;
        if (!ok) fprintf(d->report, "sync FsResult=%u\n", (unsigned)r);
    }
    if (!close_file(d, &file)) ok = false;
    if (ok && mode == 2) {
        r = fs_open(path, FS_OPEN_WRITE | FS_OPEN_APPEND, &file);
        ok = r == FS_OK;
        if (!ok) fprintf(d->report, "append open FsResult=%u\n", (unsigned)r);
        if (ok) ok = write_bytes(d, file, 283, 1);
        if (ok) {
            r = fs_truncate(file, 511);
            ok = r == FS_OK;
            if (!ok) fprintf(d->report, "truncate FsResult=%u\n", (unsigned)r);
        }
        if (ok) {
            r = fs_sync(file);
            ok = r == FS_OK;
            if (!ok) fprintf(d->report, "append sync FsResult=%u\n", (unsigned)r);
        }
        if (!close_file(d, &file)) ok = false;
        expected = 511;
    }
    if (ok && mode == 3) {
        r = fs_rename(path, target);
        ok = r == FS_OK;
        renamed = ok;
        if (!ok) {
            fprintf(d->report, "rename FsResult=%u\n", (unsigned)r);
            if (r != FS_EXISTS) leftover(d, target, false);
        }
    }
    if (ok) ok = verify_file(d, renamed ? target : path, expected, mode);
    if (ok) {
        r = fs_unlink(renamed ? target : path);
        ok = r == FS_OK;
        if (!ok) fs_result(d, "file cleanup", r);
    }
    if (!ok && (owned || (r != FS_EXISTS && r != FS_WRITE_PROTECTED)))
        leftover(d, renamed ? target : path, owned);
    return result(d, name, ok, ok ? "sync/close/reopen/full readback/delete" : "see operation errors; scratch preserved");
}

static bool reserve_dir(Diag *d, unsigned profile, char *path, size_t capacity) {
    unsigned i;
    for (i = 0; i < DIAG_SCAN && !stopped(d); ++i) {
        FsResult r;
        snprintf(path, capacity, "0:/nUSB-diag-%02u-%02u", profile, i);
        r = fs_mkdir(path);
        if (r == FS_EXISTS) {
            fprintf(d->report, "collision preserved %s\n", path);
            continue;
        }
        if (r == FS_OK) {
            fprintf(d->report, "owned scratch directory %s\n", path);
            return true;
        }
        fprintf(d->report, "scratch reservation path %s\n", path);
        fs_result(d, "reserve scratch directory", r);
        if (r != FS_WRITE_PROTECTED && r != FS_DENIED)
            leftover(d, path, false);
        return false;
    }
    return result(d, "reserve scratch directory", false, "bounded collision scan exhausted or stopped");
}

static bool subdir_case(Diag *d, const char *dir) {
    char path[192];
    FsResult r;
    bool ok, owned;
    snprintf(path, sizeof(path), "%s/subdirectory", dir);
    r = fs_mkdir(path);
    ok = r == FS_OK;
    owned = ok;
    if (ok) ok = file_case(d, path, "child.tns", 230, 0);
    if (ok) {
        r = fs_unlink(path);
        ok = r == FS_OK;
        if (!ok) fs_result(d, "subdirectory cleanup", r);
    }
    if (!ok) {
        if (r != FS_OK) fs_result(d, "subdirectory operation", r);
        leftover(d, path, owned);
    }
    return result(d, "subdirectory lifecycle", ok, ok ? NULL : "scratch preserved");
}

static bool native_idle(Diag *d) {
    if (d->api->session_active) {
        d->busy = true;
        d->sum.stopped = true;
        fprintf(d->report, "native session remains active; no forced close or further native writes\n");
        return false;
    }
    return !stopped(d);
}

static bool native_read(Diag *d, const char *path, uint32_t size, unsigned mode) {
    FILE *file = fopen(path, "rb");
    uint32_t i;
    bool ok = file != NULL;
    if (!file) fprintf(d->report, "native reopen errno=%d %s\n", errno, path);
    if (ok) {
        size_t actual = fread(check, 1, size, file);
        ok = actual == size && !ferror(file);
        fprintf(d->report, "native read requested=%lu actual=%lu error=%d\n",
                (unsigned long)size, (unsigned long)actual, ferror(file));
        for (i = 0; ok && i < size; ++i) {
            unsigned char expected = byte_at(i, 0);
            if (mode == 1 && i >= 123 && i < 353) expected = byte_at(i - 123, 1);
            if (mode == 3) expected ^= (unsigned char)(route_tag[(i >> 2) & 1] >> ((i & 3) * 8));
            ok = check[i] == expected;
        }
        if (ok) ok = fgetc(file) == EOF && !ferror(file);
        if (fclose(file)) { fprintf(d->report, "native read close errno=%d\n", errno); ok = false; }
        if (!ok) result(d, "native readback", false, "short read, mismatch, EOF or close error");
    }
    return native_idle(d) && ok;
}

/* The marker must be read through the SDK before any native mutation. The
   directory was created exclusively on USB, never in the internal USB folder. */
static bool native_case(Diag *d, const char *dir) {
    char marker[256], native_marker[272], a[256], b[256], saved[256];
    char real_a[256], real_b[256], real_saved[256];
    FsFile *file = NULL;
    FILE *stream = NULL;
    FsResult r;
    bool ok, route, created_a = false, created_b = false, moved_a = false, moved_b = false;
    bool marker_owned;
    unsigned i;
    route_tag[0] = (uint32_t)time(NULL);
    route_tag[1] = d->session_id ^ (uint32_t)(uintptr_t)d;
    snprintf(marker, sizeof(marker), "%s/route-%08lx%08lx.tns", dir,
             (unsigned long)route_tag[0], (unsigned long)route_tag[1]);
    snprintf(native_marker, sizeof(native_marker), "/documents/USB/%s", marker + 3);
    snprintf(a, sizeof(a), "/documents/USB/%s/native.tns", dir + 3);
    snprintf(b, sizeof(b), "/documents/USB/%s/temporary.tns", dir + 3);
    snprintf(saved, sizeof(saved), "/documents/USB/%s/backup.tns", dir + 3);
    snprintf(real_a, sizeof(real_a), "%s/native.tns", dir);
    snprintf(real_b, sizeof(real_b), "%s/temporary.tns", dir);
    snprintf(real_saved, sizeof(real_saved), "%s/backup.tns", dir);
    r = fs_open(marker, FS_OPEN_WRITE | FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE, &file);
    marker_owned = r == FS_OK;
    ok = marker_owned;
    if (ok && !stopped(d)) {
        uint32_t actual = 0;
        for (i = 0; i < 230; ++i)
            data[i] = byte_at(i, 0) ^ (unsigned char)(route_tag[(i >> 2) & 1] >> ((i & 3) * 8));
        r = fs_write(file, data, 230, &actual);
        ok = r == FS_OK && actual == 230;
        fprintf(d->report, "route marker write requested=230 actual=%lu FsResult=%u\n",
                (unsigned long)actual, (unsigned)r);
    } else ok = false;
    if (!close_file(d, &file)) ok = false;
    if (!ok) {
        fs_result(d, "native route marker create", r == FS_OK ? FS_IO_ERROR : r);
        if (r != FS_EXISTS) leftover(d, marker, marker_owned);
        skip(d, "native SDK route", "route marker could not be reserved");
        return false;
    }
    close_session(d);
    route = !stopped(d) && native_read(d, native_marker, 230, 3);
    if (!route) {
        skip(d, "native SDK route", "SDK cannot read USB-only ownership marker; no native writes attempted");
        ok = true;
    } else {
        for (i = 0; i < 512; ++i) data[i] = byte_at(i, 0);
        /* No fopen("x") assumption: Nucleus SDK uses its own mode parser.
           Both children are absent inside our exclusively owned directory. */
        stream = fopen(a, "w+b");
        ok = stream != NULL;
        created_a = ok;
        if (ok) ok = !stopped(d);
        if (ok) ok = !setvbuf(stream, NULL, _IONBF, 0);
        if (!ok) fprintf(d->report, "native create errno=%d\n", errno);
        if (ok) {
            size_t actual = fwrite(data, 1, 512, stream);
            fprintf(d->report, "native write requested=512 actual=%lu error=%d\n",
                    (unsigned long)actual, ferror(stream));
            ok = actual == 512 && !ferror(stream);
        }
        if (ok) ok = !fseek(stream, 123, SEEK_SET);
        if (ok) {
            size_t actual;
            for (i = 0; i < 230; ++i) data[i] = byte_at(i, 1);
            actual = fwrite(data, 1, 230, stream);
            ok = actual == 230 && !ferror(stream);
            fprintf(d->report, "native overwrite requested=230 actual=%lu error=%d\n",
                    (unsigned long)actual, ferror(stream));
        }
        if (ok) ok = !fflush(stream);
        if (stream && fclose(stream)) { fprintf(d->report, "native close errno=%d\n", errno); ok = false; }
        stream = NULL;
        if (!native_idle(d)) ok = false;
        if (ok && !stopped(d)) ok = native_read(d, a, 512, 1);
        if (ok && !stopped(d)) {
            stream = fopen(b, "wb");
            ok = stream != NULL;
            created_b = ok;
            if (ok) ok = !setvbuf(stream, NULL, _IONBF, 0);
            if (ok) ok = !stopped(d);
            if (ok) {
                for (i = 0; i < 230; ++i) data[i] = byte_at(i, 0);
                size_t actual = fwrite(data, 1, 230, stream);
                ok = actual == 230 && !ferror(stream);
                fprintf(d->report, "native temporary requested=230 actual=%lu error=%d\n",
                        (unsigned long)actual, ferror(stream));
            }
            if (ok) ok = !fflush(stream);
            if (stream && fclose(stream)) ok = false;
            stream = NULL;
            if (!native_idle(d)) ok = false;
        }
        if (ok && !stopped(d)) {
            bool renamed;
            int rename_error;
            result(d, "native SDK write/seek/reopen", true, NULL);
            errno = 0;
            renamed = !rename(a, saved);
            rename_error = errno;
            moved_a = renamed;
            if (!native_idle(d)) ok = false;
            if (renamed && ok) {
                errno = 0;
                renamed = !rename(b, a);
                rename_error = errno;
                moved_b = renamed;
            }
            if (!native_idle(d)) ok = false;
            fprintf(d->report, "native rename errno=%d\n", rename_error);
            if (ok && !renamed && rename_error == ENOSYS)
                skip(d, "native SDK save replacement", "SDK rename ENOSYS; filesystem rename tested separately");
            else ok = result(d, "native SDK save replacement", ok && renamed,
                             renamed ? NULL : "rename failed; paths preserved");
            if (ok && moved_b && !stopped(d)) ok = native_read(d, a, 230, 0);
        } else {
            fprintf(d->report, "native operation failed errno=%d; paths preserved\n", errno);
            result(d, "native SDK write/seek/reopen", false, NULL);
        }
    }
    if (open_session(d, "native route recovery open") && sector_reads(d, "native route recovery read") && mount_session(d)) {
        if (route && ok) {
            ok = verify_file(d, moved_a ? real_saved : real_a, 512, 1) &&
                 verify_file(d, moved_b ? real_a : real_b, 230, 0);
            result(d, "native route reached USB readback", ok, NULL);
        }
        if (ok && !stopped(d)) {
            const char *paths[3] = {marker, moved_a ? real_saved : real_a,
                                   moved_b ? real_a : real_b};
            unsigned count = route ? 3 : 1;
            for (i = 0; i < count && ok; ++i) {
                r = fs_unlink(paths[i]);
                ok = fs_result(d, "native scratch cleanup", r);
                if (!ok) leftover(d, paths[i], true);
            }
        }
    } else ok = false;
    if (!ok) {
        leftover(d, marker, true);
        if (route) {
            leftover(d, real_a, created_a || moved_b);
            leftover(d, real_b, created_b);
            leftover(d, real_saved, moved_a);
        }
    }
    return ok && !stopped(d);
}

static void skip_cases(Diag *d, unsigned profile, const char *reason) {
    static const char *const baseline[] = {
        "size-230.tns", "size-511.tns", "size-512.tns", "size-513.tns",
        "size-4096.tns", "size-32768.tns", "size-65536.tns", "offset overwrite",
        "append/truncate", "rename", "Chinese long filename", "subdirectory lifecycle", "native SDK route"
    };
    unsigned i;
    if (profile == DIAG_BASELINE) {
        for (i = 0; i < sizeof(baseline) / sizeof(baseline[0]); ++i) skip(d, baseline[i], reason);
    } else {
        skip(d, "size-230.tns", reason);
        if (profile != DIAG_REPEAT) {
            skip(d, "size-512.tns", reason);
            skip(d, "size-4096.tns", reason);
        }
    }
}

DiagSummary diagnose_run(ResidentApi *api, FILE *report, bool native_paths) {
    static const uint32_t sizes[] = {230, 511, 512, 513, 4096, 32768, 65536};
    Diag d;
    unsigned profile;
    bool native_done = false;
    memset(&d, 0, sizeof(d));
    d.api = api;
    d.report = report;
    fprintf(report, "Bounded filesystem diagnostics: no raw writes; exclusive scratch only.\n");
    fprintf(report, "Native primitives only; document-editor UI, unplug/power-loss and bus electrical behavior are not tested.\n");
    for (profile = 0; profile < DIAG_PROFILE_COUNT; ++profile) {
        FsVolumeInfo volume;
        char dir[128] = "", name[48];
        bool ok = false, owned = false, writes = false;
        unsigned i;
        fprintf(report, "\nPROFILE %u %s\n", profile, diagnose_profile_name((DiagProfile)profile));
        if (stopped(&d)) {
            skip_cases(&d, profile, stop_reason(&d));
            diagnose_usb_finish(report);
            flush_report(&d);
            continue;
        }
        if (!diagnose_usb_profile((DiagProfile)profile)) {
            skip_cases(&d, profile, "profile controls unavailable; not silently run as baseline");
            diagnose_usb_finish(report);
            flush_report(&d);
            continue;
        }
        if (open_session(&d, profile ? "between-profile recovery open" : "session open")) {
            bool synchronized = diagnose_usb_information(report);
            if (!synchronized) {
                result(&d, "SCSI information probe synchronization", false, "probe failed or unavailable; reopening before filesystem I/O");
                close_session(&d);
                if (!open_session(&d, "information probe recovery open")) {
                    skip_cases(&d, profile, stopped(&d) ? stop_reason(&d) : "information probe recovery failed");
                    goto finish;
                }
            }
            else result(&d, "SCSI information probe synchronization", true, NULL);
            if (!stopped(&d) && sector_reads(&d, profile ? "between-profile recovery read" : "repeated safe sector reads") && mount_session(&d)) {
                FsResult r = fs_volume_info(&volume);
                if (fs_result(&d, "volume information", r)) {
                    fprintf(report, "format=%s writable=%u dirty=%u media_failure=%u bytes=%llu max_file=%llu\n",
                            volume.format == FS_FORMAT_FAT32 ? "FAT32" :
                            volume.format == FS_FORMAT_EXFAT ? "exFAT" : "internal",
                            volume.writable, volume.dirty, volume.media_failure,
                            (unsigned long long)volume.volume_bytes, (unsigned long long)volume.max_file_size);
                    ok = root_list(&d);
                    writes = volume.writable && !volume.media_failure;
                }
            }
        }
        if (!ok || !writes || stopped(&d)) {
            skip_cases(&d, profile, stopped(&d) ? stop_reason(&d) :
                       !ok ? "session/read/mount prerequisite failed" : "volume is read-only or reports media failure");
            goto finish;
        }
        owned = reserve_dir(&d, profile, dir, sizeof(dir));
        if (!owned) {
            skip_cases(&d, profile, "scratch reservation failed; no files opened");
            goto finish;
        }
        ok = file_case(&d, dir, "size-230.tns", 230, 0);
        if (profile == DIAG_BASELINE) {
            for (i = 1; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
                snprintf(name, sizeof(name), "size-%lu.tns", (unsigned long)sizes[i]);
                if (ok && !stopped(&d)) ok = file_case(&d, dir, name, sizes[i], 0);
                else skip(&d, name, stopped(&d) ? stop_reason(&d) : "earlier write case failed; dependent writes suppressed");
            }
            if (ok && !stopped(&d)) ok = file_case(&d, dir, "offset.tns", 512, 1);
            else skip(&d, "offset overwrite", "earlier write case failed");
            if (ok && !stopped(&d)) ok = file_case(&d, dir, "append.tns", 230, 2);
            else skip(&d, "append/truncate", "earlier write case failed");
            if (ok && !stopped(&d)) ok = file_case(&d, dir, "rename.tns", 513, 3);
            else skip(&d, "rename", "earlier write case failed");
            if (ok && !stopped(&d)) ok = file_case(&d, dir, "资料-USB-long-filename-readback-0123456789-abcdefghijklmnopqrstuvwxyz.tns", 513, 0);
            else skip(&d, "Chinese long filename", "earlier write case failed");
            if (ok && !stopped(&d)) ok = subdir_case(&d, dir);
            else skip(&d, "subdirectory lifecycle", "earlier write case failed");
        } else if (profile != DIAG_REPEAT) {
            if (ok && !stopped(&d)) ok = file_case(&d, dir, "size-512.tns", 512, 0);
            else skip(&d, "size-512.tns", "smoke failed; dependent writes suppressed");
            if (ok && !stopped(&d)) ok = file_case(&d, dir, "size-4096.tns", 4096, 0);
            else skip(&d, "size-4096.tns", "earlier write case failed; dependent writes suppressed");
        }
        if (!native_done) {
            if (!native_paths) {
                skip(&d, "native SDK route", "resident native folder route unavailable");
                native_done = true;
            } else if (ok && !stopped(&d)) {
                native_done = true;
                ok = native_case(&d, dir);
            } else skip(&d, "native SDK route", "earlier write case failed; may try after a later profile recovers");
        }
        if (ok && !stopped(&d) && d.active) {
            FsResult r = fs_unlink(dir);
            if (!fs_result(&d, "scratch directory cleanup", r)) leftover(&d, dir, true);
        } else leftover(&d, dir, true);
finish:
        close_session(&d);
        if (!stopped(&d) && !ok && owned) {
            if (open_session(&d, "failed-write recovery open")) {
                if (sector_reads(&d, "failed-write recovery read") && !stopped(&d))
                    result(&d, "recovery SCSI probe synchronization", diagnose_usb_information(report), NULL);
                else skip(&d, "recovery SCSI probe synchronization", "recovery read failed or transport stopped");
                close_session(&d);
            }
        }
        diagnose_usb_finish(report);
        flush_report(&d);
    }
    if (native_paths && !native_done)
        skip(&d, "native SDK route", "no safe successful write profile; native writes not attempted");
    fprintf(report, "\nSUMMARY passed=%u failed=%u skipped=%u stopped=%u leftovers=%u\n",
            d.sum.passed, d.sum.failed, d.sum.skipped, d.sum.stopped, d.sum.leftovers);
    flush_report(&d);
    return d.sum;
}
