#ifndef NUSB_FILEOPS_H
#define NUSB_FILEOPS_H

#include "storage.h"

typedef enum { FILE_COPY, FILE_MOVE, FILE_RENAME, FILE_DELETE } FileOperation;
typedef enum { FILE_SKIP, FILE_RENAME_ITEM, FILE_CANCEL } FileDecision;
typedef enum {
    FILE_SCANNING, FILE_COPYING, FILE_FINISHING, FILE_REMOVING_SOURCE,
    FILE_DELETING, FILE_CONFLICT, FILE_CANCELLING, FILE_RESULT
} FileJobPhase;
typedef struct {
    FileJobPhase phase;
    FsResult result;
    bool cancel_requested;
    uint64_t bytes_total, bytes_done, items_total, copied, skipped, deleted;
    StorageLocation source, destination;
    char failure_path[STORAGE_PATH_MAX];
    char temporary_path[STORAGE_PATH_MAX];
} FileJobStatus;
typedef struct FileJob FileJob;

FsResult fileops_begin(FileJob **job, FileOperation operation,
                       const StorageLocation *source, const StorageLocation *destination);
void fileops_step(FileJob *job);
void fileops_cancel(FileJob *job);
void fileops_fail(FileJob *job, FsResult result);
FsResult fileops_decide(FileJob *job, FileDecision decision, const char *new_name);
const FileJobStatus *fileops_status(const FileJob *job);
void fileops_destroy(FileJob *job);

#endif
