#include "fileops.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum {
    JOB_CANON_SOURCE, JOB_CANON_DESTINATION, JOB_CHECK_ROOT, JOB_CHECK_PARENT, JOB_CHECK_SOURCE_PARENT,
    JOB_VISIT, JOB_OPEN_DIR, JOB_NEXT, JOB_LEAVE, JOB_TARGET, JOB_MKDIR, JOB_UNLINK,
    JOB_TEMP, JOB_OPEN_SOURCE, JOB_DATA, JOB_SYNC, JOB_CLOSE_OUTPUT,
    JOB_CLOSE_INPUT, JOB_TEMP_METADATA, JOB_PUBLISH, JOB_ATTRIBUTES,
    JOB_RENAME, JOB_CASE_TEMP, JOB_CASE_FINISH, JOB_CASE_RESTORE, JOB_SKIP_TEMP,
    JOB_CLEAN_SYNC, JOB_CLEAN_OUTPUT, JOB_CLEAN_INPUT, JOB_CLEAN_TEMP
} JobStep;
typedef struct {
    uint16_t source_length, destination_length;
    uint32_t cluster;
    uint64_t ordinal;
    FsTimes times;
    uint8_t attributes;
} JobFrame;
struct FileJob {
    FileOperation operation;
    FileJobStatus status;
    StorageLocation root_source, root_destination, temporary, canonical;
    StorageLimits limits;
    StorageDir *dir;
    StorageFile *input, *output;
    JobFrame *frames;
    size_t depth, capacity, canonical_offset, scan_base;
    uint64_t revision[2], resume, file_done;
    uint32_t temp_counter;
    bool temp_exhausted, temp_exists, cleanup_healthy, scanning, removing;
    bool case_only, direct, choosing;
    JobStep step, resume_step;
    FsEntry entry, candidate;
    char last_name[768];
    bool candidate_found;
    unsigned char *buffer;
};

static FsTimes entry_times(const FsEntry *entry) {
    FsTimes times;
    times.access_date = entry->access_date;
    times.modified_date = entry->date; times.modified_time = entry->time;
    times.created_date = entry->created_date; times.created_time = entry->created_time;
    return times;
}
static void revisions(FileJob *job) {
    job->revision[STORAGE_USB] = storage_revision(STORAGE_USB);
    job->revision[STORAGE_INTERNAL] = storage_revision(STORAGE_INTERNAL);
}
static bool same_identity(const FsEntry *a, const FsEntry *b) {
    if (a->is_directory != b->is_directory) return false;
    if (a->is_directory) return a->identity.directory_cluster == b->identity.directory_cluster;
    return a->identity.parent_cluster == b->identity.parent_cluster &&
        a->identity.entry_offset == b->identity.entry_offset;
}
static bool ancestor(const char *parent, const char *path) {
    size_t n = strlen(parent);
    return !strncmp(parent, path, n) && (!path[n] || parent[n - 1] == '/' || path[n] == '/');
}
static void finish(FileJob *job, FsResult result) {
    job->status.result = result;
    job->status.phase = FILE_RESULT;
}
static void fail_at(FileJob *job, FsResult result, const char *path) {
    if (job->status.phase == FILE_RESULT) return;
    if (job->status.result == FS_OK || (job->status.result == FS_CANCELLED && result != FS_CANCELLED)) {
        job->status.result = result;
        if (result != FS_CANCELLED && path) {
            strncpy(job->status.failure_path, path, sizeof(job->status.failure_path) - 1);
            job->status.failure_path[sizeof(job->status.failure_path) - 1] = 0;
        }
    }
    if ((result == FS_NO_DEVICE || result == FS_IO_ERROR || result == FS_WRITE_PROTECTED) &&
        (!path || (job->temporary.kind == STORAGE_USB) == !strncmp(path, "0:/", 3)))
        job->cleanup_healthy = false;
    storage_closedir(job->dir); job->dir = NULL;
    if (job->step < JOB_CLEAN_SYNC) job->step = JOB_CLEAN_SYNC;
    if (result == FS_CANCELLED) job->status.phase = FILE_CANCELLING;
}
void fileops_fail(FileJob *job, FsResult result) {
    if (job && result != FS_OK) fail_at(job, result, job->status.source.path);
}
static FsResult push(FileJob *job) {
    JobFrame *frame;
    size_t i;
    if (job->status.source.kind == STORAGE_USB) {
        for (i = 0; i < job->depth; ++i)
            if (job->frames[i].cluster == job->entry.identity.directory_cluster) return FS_IO_ERROR;
    }
    if (job->depth == job->capacity) {
        size_t capacity = job->capacity ? job->capacity * 2 : 16;
        JobFrame *frames;
        if (capacity > STORAGE_PATH_MAX / 2) capacity = STORAGE_PATH_MAX / 2;
        if (capacity <= job->capacity) return FS_PATH_TOO_LONG;
        frames = realloc(job->frames, capacity * sizeof(*frames));
        if (!frames) return FS_NO_MEMORY;
        job->frames = frames; job->capacity = capacity;
    }
    frame = &job->frames[job->depth++];
    frame->source_length = (uint16_t)strlen(job->status.source.path);
    frame->destination_length = (uint16_t)strlen(job->status.destination.path);
    frame->cluster = job->entry.identity.directory_cluster;
    frame->ordinal = 0; frame->times = entry_times(&job->entry);
    frame->attributes = job->entry.attributes;
    job->last_name[0] = 0;
    job->step = JOB_OPEN_DIR;
    return FS_OK;
}
static void pass_complete(FileJob *job) {
    if (job->choosing) {
        job->choosing = false; job->scanning = false;
        job->status.phase = FILE_COPYING; job->step = JOB_VISIT;
        return;
    }
    if (job->scanning) {
        job->scanning = false;
        job->status.source = job->root_source;
        job->status.destination = job->root_destination;
        if (job->operation == FILE_DELETE) {
            job->removing = true; job->status.phase = FILE_DELETING; job->step = JOB_VISIT;
        } else {
            job->status.phase = FILE_COPYING; job->step = JOB_VISIT;
        }
    } else if (!job->removing && job->operation == FILE_MOVE && !job->status.skipped) {
        job->removing = true;
        job->status.phase = FILE_REMOVING_SOURCE;
        job->status.source = job->root_source;
        job->status.destination = job->root_destination;
        job->step = JOB_VISIT;
    } else finish(job, FS_OK);
}
static void next_item(FileJob *job) {
    if (!job->depth || (job->choosing && job->depth == job->scan_base)) {
        pass_complete(job); return;
    }
    if (job->removing) {
        const char *name = strrchr(job->status.source.path, '/');
        strcpy(job->last_name, name ? name + 1 : job->status.source.path);
    }
    job->status.source.path[job->frames[job->depth - 1].source_length] = 0;
    job->status.destination.path[job->frames[job->depth - 1].destination_length] = 0;
    job->step = JOB_OPEN_DIR;
}
static FsResult destination_check(FileJob *job, const FsEntry *entry) {
    const char *name = strrchr(job->status.destination.path, '/');
    FsResult result;
    if (job->operation == FILE_DELETE) return FS_OK;
    if (strlen(job->status.destination.path) > job->limits.max_path_bytes) return FS_PATH_TOO_LONG;
    if (!name || strlen(++name) > job->limits.max_name_bytes) return FS_PATH_TOO_LONG;
    result = storage_validate_name(name);
    if (result != FS_OK) return result;
    if (!entry->is_directory && entry->size > job->limits.max_file_size) return FS_TOO_LARGE;
    /* The temporary leaf must fit too, even for an empty source file. */
    if (!job->direct && !entry->is_directory && (size_t)(name - job->status.destination.path) + 19 > job->limits.max_path_bytes)
        return FS_PATH_TOO_LONG;
    return FS_OK;
}
static FsResult temporary_name(FileJob *job) {
    char name[32];
    StorageLocation parent = job->status.destination;
    if (job->temp_exhausted) return FS_EXISTS;
    storage_parent(&parent);
    snprintf(name, sizeof(name), ".nusb-%08lx.part", (unsigned long)job->temp_counter);
    if (job->temp_counter == UINT32_MAX) job->temp_exhausted = true;
    else ++job->temp_counter;
    return storage_join(&job->temporary, &parent, name);
}
static void conflict(FileJob *job) {
    job->status.phase = FILE_CONFLICT;
    job->resume_step = JOB_TARGET;
}
static void canonical_start(FileJob *job, StorageKind kind) {
    job->canonical.kind = kind;
    strcpy(job->canonical.path, kind == STORAGE_USB ? "0:/" : "/documents");
    job->canonical_offset = strlen(job->canonical.path);
}
/* Resolve one existing path component per step, retaining the on-disk spelling. */
static FsResult canonical_step(FileJob *job, StorageLocation *location, bool destination, bool *done) {
    const char *p = location->path + job->canonical_offset, *end;
    char name[768];
    StorageLocation child;
    FsEntry entry;
    FsResult result;
    *done = false;
    while (*p == '/') ++p;
    if (!*p) { *location = job->canonical; *done = true; return FS_OK; }
    end = strchr(p, '/');
    if (!end) end = p + strlen(p);
    if ((size_t)(end - p) >= sizeof(name)) return FS_PATH_TOO_LONG;
    memcpy(name, p, (size_t)(end - p)); name[end - p] = 0;
    result = storage_join(&child, &job->canonical, name);
    if (result != FS_OK) return result;
    if (destination && !*end) {
        *location = child; *done = true; return FS_OK;
    }
    result = storage_stat(&child, &entry);
    if (result != FS_OK) return result;
    if (*end && !entry.is_directory) return FS_NOT_FOUND;
    result = storage_join(&job->canonical, &job->canonical, entry.name);
    if (result != FS_OK) return result;
    job->canonical_offset = (size_t)(end - location->path);
    if (!*end) { *location = job->canonical; *done = true; }
    return FS_OK;
}
FsResult fileops_begin(FileJob **out, FileOperation operation,
                       const StorageLocation *source, const StorageLocation *destination) {
    FileJob *job;
    FsResult result;
    if (!out) return FS_INVALID_ARGUMENT;
    *out = NULL;
    if (!source || (unsigned)operation > FILE_DELETE ||
        (operation != FILE_DELETE && !destination)) return FS_INVALID_ARGUMENT;
    job = calloc(1, sizeof(*job));
    if (!job) return FS_NO_MEMORY;
    result = storage_location(&job->root_source, source->kind, source->path);
    if (result == FS_OK && operation != FILE_DELETE)
        result = storage_location(&job->root_destination, destination->kind, destination->path);
    if (result == FS_OK && (storage_is_root(&job->root_source) ||
        (operation != FILE_DELETE && storage_is_root(&job->root_destination)))) result = FS_DENIED;
    if (result == FS_OK && operation == FILE_RENAME && source->kind != destination->kind) result = FS_INVALID_ARGUMENT;
    if (result != FS_OK) { free(job); return result; }
    job->operation = operation; job->status.phase = FILE_SCANNING;
    job->status.source = job->root_source; job->status.destination = job->root_destination;
    job->scanning = true; job->cleanup_healthy = true;
    job->direct = (operation == FILE_MOVE || operation == FILE_RENAME) && source->kind == destination->kind;
    job->step = JOB_CANON_SOURCE;
    canonical_start(job, source->kind); revisions(job);
    *out = job; return FS_OK;
}
void fileops_cancel(FileJob *job) {
    if (job && job->status.phase != FILE_RESULT) job->status.cancel_requested = true;
}
const FileJobStatus *fileops_status(const FileJob *job) { return job ? &job->status : NULL; }
FsResult fileops_decide(FileJob *job, FileDecision decision, const char *new_name) {
    FsResult result;
    StorageLocation parent, previous;
    if (!job || job->status.phase != FILE_CONFLICT) return FS_INVALID_ARGUMENT;
    if (decision == FILE_CANCEL) { fileops_cancel(job); return FS_OK; }
    if (decision == FILE_SKIP) {
        ++job->status.skipped; job->status.phase = FILE_COPYING;
        if (job->temp_exists) job->step = JOB_SKIP_TEMP;
        else next_item(job);
        return FS_OK;
    }
    if (decision != FILE_RENAME_ITEM) return FS_INVALID_ARGUMENT;
    previous = job->status.destination;
    parent = job->status.destination; storage_parent(&parent);
    result = storage_join(&job->status.destination, &parent, new_name);
    if (result != FS_OK) return result;
    /* A longer replacement can invalidate descendants: repeat the read-only scan. */
    if (job->entry.is_directory) {
        if (!job->depth) job->root_destination = job->status.destination;
        job->scan_base = job->depth; job->choosing = true;
        job->scanning = true; job->status.phase = FILE_SCANNING;
        job->step = JOB_VISIT;
    } else {
        result = destination_check(job, &job->entry);
        if (result != FS_OK) { job->status.destination = previous; return result; }
        if (!job->depth) job->root_destination = job->status.destination;
        job->status.phase = FILE_COPYING; job->step = job->resume_step;
    }
    return FS_OK;
}
void fileops_step(FileJob *job) {
    FsResult result = FS_OK;
    bool end = false;
    const char *failure;
    if (!job || job->status.phase == FILE_RESULT) return;
    failure = job->status.source.path;
    if (job->step < JOB_CLEAN_SYNC) {
        if (storage_revision(job->root_source.kind) != job->revision[job->root_source.kind] ||
            (job->operation != FILE_DELETE && storage_revision(job->root_destination.kind) != job->revision[job->root_destination.kind])) {
            fail_at(job, FS_IO_ERROR, failure); return;
        }
        if (job->status.cancel_requested && job->step != JOB_CASE_RESTORE) {
            if (job->step == JOB_CASE_FINISH) {
                job->status.result = FS_CANCELLED; job->step = JOB_CASE_RESTORE;
            } else { fail_at(job, FS_CANCELLED, NULL); return; }
        }
        if (job->status.phase == FILE_CONFLICT) return;
    }
    switch (job->step) {
        case JOB_CANON_SOURCE:
            result = canonical_step(job, &job->root_source, false, &end);
            if (result == FS_OK && end) {
                job->status.source = job->root_source;
                if (job->operation == FILE_DELETE) job->step = JOB_CHECK_ROOT;
                else { canonical_start(job, job->root_destination.kind); job->step = JOB_CANON_DESTINATION; }
            }
            break;
        case JOB_CANON_DESTINATION:
            result = canonical_step(job, &job->root_destination, true, &end);
            if (result == FS_OK && end) { job->status.destination = job->root_destination; job->step = JOB_CHECK_ROOT; }
            break;
        case JOB_CHECK_ROOT:
            result = storage_stat(&job->status.source, &job->entry);
            if (result != FS_OK) break;
            if (job->operation != FILE_DELETE && job->root_source.kind == job->root_destination.kind &&
                ancestor(job->root_source.path, job->root_destination.path)) {
                if (job->direct && !strcmp(job->root_source.path, job->root_destination.path)) { finish(job, FS_OK); return; }
                result = FS_INVALID_ARGUMENT; break;
            }
            if (job->operation != FILE_COPY) {
                StorageLimits limits;
                result = storage_limits(&job->root_source, &limits);
                if (result == FS_OK && !limits.writable) result = FS_WRITE_PROTECTED;
                if (result != FS_OK) break;
            }
            if (job->operation == FILE_DELETE) job->step = JOB_CHECK_SOURCE_PARENT;
            else {
                result = storage_limits(&job->root_destination, &job->limits);
                if (result == FS_OK && !job->limits.writable) result = FS_WRITE_PROTECTED;
                job->step = JOB_CHECK_PARENT;
            }
            break;
        case JOB_CHECK_PARENT: {
            StorageLocation parent = job->root_destination;
            FsEntry entry;
            storage_parent(&parent); failure = job->status.destination.path;
            result = storage_stat(&parent, &entry);
            if (result == FS_OK && (!entry.is_directory || (entry.attributes & 1))) result = FS_DENIED;
            if (result == FS_OK) job->step = job->operation == FILE_COPY ? JOB_VISIT : JOB_CHECK_SOURCE_PARENT;
            break;
        }
        case JOB_CHECK_SOURCE_PARENT: {
            StorageLocation parent = job->root_source;
            FsEntry entry;
            storage_parent(&parent);
            result = storage_stat(&parent, &entry);
            if (result == FS_OK && (!entry.is_directory || (entry.attributes & 1))) result = FS_DENIED;
            if (result == FS_OK) job->step = JOB_VISIT;
            break;
        }
        case JOB_VISIT:
            result = storage_stat(&job->status.source, &job->entry);
            if (result != FS_OK) break;
            if (!job->entry.is_directory && job->status.source.kind == STORAGE_INTERNAL &&
                job->entry.size > INT32_MAX) { result = FS_TOO_LARGE; break; }
            if (job->operation != FILE_COPY && (job->entry.attributes & 1)) { result = FS_DENIED; break; }
            if (job->scanning) {
                result = destination_check(job, &job->entry);
                if (result != FS_OK) { failure = job->status.destination.path; break; }
                if (job->status.items_total == UINT64_MAX ||
                    (!job->entry.is_directory && UINT64_MAX - job->status.bytes_total < job->entry.size)) {
                    result = FS_INVALID_ARGUMENT; break;
                }
                if (!job->choosing) {
                    ++job->status.items_total;
                    if (!job->entry.is_directory) job->status.bytes_total += job->entry.size;
                }
                if (job->entry.is_directory) result = push(job);
                else next_item(job);
            } else if (job->removing) {
                if (job->entry.is_directory) result = push(job);
                else job->step = JOB_UNLINK;
            } else job->step = JOB_TARGET;
            break;
        case JOB_UNLINK:
            result = storage_unlink(&job->status.source); revisions(job);
            if (result == FS_OK) { ++job->status.deleted; next_item(job); }
            break;
        case JOB_OPEN_DIR:
            result = storage_opendir(&job->status.source, &job->dir);
            if (result == FS_OK) {
                job->resume = 0; job->candidate_found = false; job->step = JOB_NEXT;
            }
            break;
        case JOB_NEXT: {
            FsEntry entry;
            JobFrame *frame = &job->frames[job->depth - 1];
            result = storage_next(job->dir, &entry, &end);
            if (result != FS_OK) break;
            if (!end && (!strcmp(entry.name, ".") || !strcmp(entry.name, ".."))) break;
            if (job->removing) {
                if (!end) {
                    if (strcmp(entry.name, job->last_name) > 0 &&
                        (!job->candidate_found || strcmp(entry.name, job->candidate.name) < 0)) {
                        job->candidate = entry; job->candidate_found = true;
                    }
                    break;
                }
                if (job->candidate_found) { entry = job->candidate; end = false; }
            } else if (!end && job->resume++ < frame->ordinal) break;
            storage_closedir(job->dir); job->dir = NULL;
            if (end) { job->step = JOB_LEAVE; break; }
            ++frame->ordinal;
            job->entry = entry;
            result = storage_join(&job->status.source, &job->status.source, entry.name);
            if (result == FS_OK && job->operation != FILE_DELETE && !job->removing)
                result = storage_join(&job->status.destination, &job->status.destination, entry.name);
            if (result == FS_OK) job->step = JOB_VISIT;
            break;
        }
        case JOB_LEAVE: {
            JobFrame *frame = &job->frames[job->depth - 1];
            if (!job->scanning) {
                if (job->removing) {
                    result = storage_unlink(&job->status.source); revisions(job);
                    if (result == FS_OK) ++job->status.deleted;
                } else {
                    failure = job->status.destination.path;
                    result = storage_set_metadata(&job->status.destination, frame->attributes, &frame->times);
                    if (result == FS_OK) ++job->status.copied;
                }
            }
            if (result == FS_OK) { --job->depth; next_item(job); }
            break;
        }
        case JOB_TARGET: {
            FsEntry target;
            failure = job->status.destination.path;
            result = storage_stat(&job->status.destination, &target);
            if (result == FS_OK) {
                job->case_only = job->direct && job->status.source.kind == STORAGE_USB && same_identity(&job->entry, &target);
                if (job->direct && job->status.source.kind == STORAGE_INTERNAL) {
                    const char *name = strrchr(job->status.source.path, '/') + 1;
                    job->case_only = !strcasecmp(job->status.source.path, job->status.destination.path) &&
                        !strcmp(name, target.name);
                }
                if (job->case_only) job->step = JOB_CASE_TEMP;
                else conflict(job);
                result = FS_OK; break;
            }
            if (result != FS_NOT_FOUND) break;
            result = FS_OK;
            if (job->direct) { job->step = JOB_RENAME; break; }
            if (job->entry.is_directory) job->step = JOB_MKDIR;
            else {
                job->file_done = 0;
                if (!job->buffer) job->buffer = malloc(STORAGE_COPY_CHUNK);
                if (!job->buffer) result = FS_NO_MEMORY;
                else job->step = JOB_TEMP;
            }
            break;
        }
        case JOB_MKDIR:
            failure = job->status.destination.path;
            result = storage_mkdir(&job->status.destination); revisions(job);
            if (result == FS_EXISTS) { conflict(job); result = FS_OK; }
            else if (result == FS_OK) result = push(job);
            break;
        case JOB_TEMP:
            result = temporary_name(job);
            if (result == FS_OK) result = storage_open(&job->temporary, FS_OPEN_WRITE | FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE, &job->output);
            revisions(job); failure = job->temporary.path;
            if (result == FS_EXISTS && !job->temp_exhausted) { result = FS_OK; break; }
            if (result == FS_OK) {
                job->temp_exists = true; strcpy(job->status.temporary_path, job->temporary.path);
                job->step = JOB_OPEN_SOURCE;
            } else if (result == FS_IO_ERROR || result == FS_NO_DEVICE) {
                strcpy(job->status.temporary_path, job->temporary.path);
            }
            break;
        case JOB_OPEN_SOURCE:
            result = storage_open(&job->status.source, FS_OPEN_READ, &job->input);
            if (result == FS_OK && storage_size(job->input) != job->entry.size) result = FS_IO_ERROR;
            if (result == FS_OK) job->step = JOB_DATA;
            break;
        case JOB_DATA: {
            uint32_t count, actual = 0, written = 0;
            uint64_t remaining = job->entry.size - job->file_done;
            if (!remaining) { job->status.phase = FILE_FINISHING; job->step = JOB_SYNC; break; }
            count = remaining > STORAGE_COPY_CHUNK ? STORAGE_COPY_CHUNK : (uint32_t)remaining;
            result = storage_read(job->input, job->buffer, count, &actual);
            if (result != FS_OK) break;
            if (actual != count) { result = FS_IO_ERROR; break; }
            failure = job->temporary.path;
            result = storage_write(job->output, job->buffer, count, &written);
            job->file_done += written; job->status.bytes_done += written;
            if (result == FS_OK && written != count) result = FS_NO_SPACE;
            break;
        }
        case JOB_SYNC:
            failure = job->temporary.path; result = storage_sync(job->output);
            if (result == FS_OK) job->step = JOB_CLOSE_OUTPUT;
            break;
        case JOB_CLOSE_OUTPUT:
            failure = job->temporary.path; result = storage_close(job->output); job->output = NULL;
            if (result == FS_OK) job->step = JOB_CLOSE_INPUT;
            break;
        case JOB_CLOSE_INPUT:
            result = storage_close(job->input); job->input = NULL;
            if (result == FS_OK) job->step = JOB_TEMP_METADATA;
            break;
        case JOB_TEMP_METADATA: {
            FsTimes times = entry_times(&job->entry);
            failure = job->temporary.path;
            result = storage_set_metadata(&job->temporary, job->entry.attributes & ~1u, &times);
            if (result == FS_OK) job->step = JOB_PUBLISH;
            break;
        }
        case JOB_PUBLISH:
            failure = job->status.destination.path;
            result = storage_rename(&job->temporary, &job->status.destination); revisions(job);
            if (result == FS_OK) {
                job->temp_exists = false; job->status.temporary_path[0] = 0;
                ++job->status.copied; job->step = JOB_ATTRIBUTES;
            } else if (result == FS_EXISTS) {
                conflict(job); job->resume_step = JOB_PUBLISH; result = FS_OK;
            }
            break;
        case JOB_ATTRIBUTES: {
            FsTimes times = entry_times(&job->entry);
            failure = job->status.destination.path;
            result = storage_set_metadata(&job->status.destination, job->entry.attributes, &times);
            if (result == FS_OK) { job->status.phase = FILE_COPYING; next_item(job); }
            break;
        }
        case JOB_SKIP_TEMP:
            failure = job->temporary.path;
            result = storage_unlink(&job->temporary); revisions(job);
            if (result == FS_OK) {
                job->temp_exists = false; job->status.temporary_path[0] = 0;
                next_item(job);
            }
            break;
        case JOB_RENAME:
            failure = job->status.destination.path;
            result = storage_rename(&job->status.source, &job->status.destination); revisions(job);
            if (result == FS_EXISTS) { conflict(job); result = FS_OK; }
            else if (result == FS_OK) {
                job->status.copied = job->status.items_total; job->status.deleted = job->status.items_total;
                finish(job, FS_OK);
            }
            break;
        case JOB_CASE_TEMP: {
            result = temporary_name(job);
            if (result != FS_OK) break;
            result = storage_rename(&job->status.source, &job->temporary); revisions(job);
            if (result == FS_EXISTS && !job->temp_exhausted) { result = FS_OK; break; }
            if (result == FS_OK) {
                strcpy(job->status.temporary_path, job->temporary.path);
                job->step = JOB_CASE_FINISH;
            }
            break;
        }
        case JOB_CASE_FINISH:
            result = storage_rename(&job->temporary, &job->status.destination); revisions(job);
            if (result == FS_OK) {
                job->status.temporary_path[0] = 0;
                job->status.copied = job->status.items_total; job->status.deleted = job->status.items_total;
                finish(job, FS_OK);
            } else {
                job->status.result = result; strcpy(job->status.failure_path, job->status.destination.path);
                job->step = JOB_CASE_RESTORE; result = FS_OK;
            }
            break;
        case JOB_CASE_RESTORE:
            result = storage_rename(&job->temporary, &job->status.source); revisions(job);
            if (result == FS_OK) job->status.temporary_path[0] = 0;
            else {
                if (job->status.result == FS_CANCELLED || job->status.result == FS_OK ||
                    result == FS_NO_DEVICE || (result == FS_IO_ERROR && job->status.result != FS_NO_DEVICE))
                    job->status.result = result;
                strcpy(job->status.failure_path, job->temporary.path);
            }
            finish(job, job->status.result); return;
        case JOB_CLEAN_SYNC:
            if (job->output && job->cleanup_healthy) {
                result = storage_sync(job->output);
                if (result != FS_OK) { job->cleanup_healthy = false; fail_at(job, result, job->temporary.path); }
            }
            job->step = JOB_CLEAN_OUTPUT; return;
        case JOB_CLEAN_OUTPUT:
            if (job->output) {
                result = storage_close(job->output); job->output = NULL;
                if (result != FS_OK) { job->cleanup_healthy = false; fail_at(job, result, job->temporary.path); }
            }
            job->step = JOB_CLEAN_INPUT; return;
        case JOB_CLEAN_INPUT:
            if (job->input) {
                result = storage_close(job->input); job->input = NULL;
                if (result != FS_OK) fail_at(job, result, job->status.source.path);
            }
            job->step = JOB_CLEAN_TEMP; return;
        case JOB_CLEAN_TEMP:
            if (job->temp_exists && job->cleanup_healthy) {
                result = storage_unlink(&job->temporary); revisions(job);
                if (result == FS_OK || result == FS_NOT_FOUND) {
                    job->temp_exists = false; job->status.temporary_path[0] = 0;
                } else fail_at(job, result, job->temporary.path);
            } else if (!job->temp_exists && job->status.temporary_path[0] &&
                       job->temporary.kind == STORAGE_INTERNAL) {
                FsEntry entry;
                /* A failed stream setup may already have rolled back its exclusive leaf. */
                if (storage_stat(&job->temporary, &entry) == FS_NOT_FOUND)
                    job->status.temporary_path[0] = 0;
            }
            finish(job, job->status.result); return;
    }
    if (result != FS_OK) fail_at(job, result, failure);
}
void fileops_destroy(FileJob *job) {
    if (!job) return;
    if (job->status.phase != FILE_RESULT) {
        fileops_cancel(job);
        while (job->status.phase != FILE_RESULT) fileops_step(job);
    }
    storage_closedir(job->dir);
    free(job->buffer); free(job->frames); free(job);
}
