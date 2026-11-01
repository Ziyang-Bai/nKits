#ifndef NUSB_STORAGE_H
#define NUSB_STORAGE_H

#include "fs.h"

#define STORAGE_PATH_MAX 4096
#define STORAGE_COPY_CHUNK 32768

typedef enum { STORAGE_USB, STORAGE_INTERNAL } StorageKind;
typedef struct { StorageKind kind; char path[STORAGE_PATH_MAX]; } StorageLocation;
typedef struct StorageFile StorageFile;
typedef struct StorageDir StorageDir;
typedef FsVolumeInfo StorageLimits;

FsResult storage_location(StorageLocation *location, StorageKind kind, const char *path);
FsResult storage_join(StorageLocation *out, const StorageLocation *parent, const char *name);
bool storage_parent(StorageLocation *location);
bool storage_is_root(const StorageLocation *location);
FsResult storage_validate_name(const char *name);
FsResult storage_stat(const StorageLocation *location, FsEntry *entry);
FsResult storage_opendir(const StorageLocation *location, StorageDir **dir);
FsResult storage_next(StorageDir *dir, FsEntry *entry, bool *end);
void storage_closedir(StorageDir *dir);
FsResult storage_open(const StorageLocation *location, unsigned flags, StorageFile **file);
FsResult storage_read(StorageFile *file, void *buf, uint32_t count, uint32_t *actual);
FsResult storage_write(StorageFile *file, const void *buf, uint32_t count, uint32_t *actual);
FsResult storage_seek(StorageFile *file, uint64_t offset);
uint64_t storage_size(StorageFile *file);
FsResult storage_sync(StorageFile *file);
FsResult storage_close(StorageFile *file);
FsResult storage_mkdir(const StorageLocation *location);
FsResult storage_unlink(const StorageLocation *location);
FsResult storage_rename(const StorageLocation *from, const StorageLocation *to);
FsResult storage_set_metadata(const StorageLocation *location, uint8_t attributes, const FsTimes *times);
FsResult storage_limits(const StorageLocation *location, StorageLimits *limits);
uint64_t storage_revision(StorageKind kind);
bool storage_mutating(void);

#endif
