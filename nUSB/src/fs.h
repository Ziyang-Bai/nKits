#ifndef NUSB_FS_H
#define NUSB_FS_H

#include "usb_msc.h"

typedef enum {
    FS_OK, FS_NO_DEVICE, FS_UNSUPPORTED, FS_IO_ERROR, FS_CANCELLED,
    FS_PATH_TOO_LONG, FS_NOT_FOUND, FS_NO_MEMORY, FS_WRITE_PROTECTED,
    FS_NO_SPACE, FS_EXISTS, FS_DENIED, FS_INVALID_ARGUMENT, FS_BUSY, FS_TOO_LARGE
} FsResult;

typedef struct {
    void *ctx;
    MscResult (*read)(void *ctx, uint32_t lba, uint32_t count, void *buf);
    uint32_t sector_size;
    uint64_t sector_count;
    uint32_t (*generation)(void *ctx);
    MscResult (*write)(void *ctx, uint32_t lba, uint32_t count, const void *buf);
    MscResult (*sync)(void *ctx);
    uint32_t (*fattime)(void *ctx); /* Packed FAT local time; zero means unknown. */
} BlockDevice;

typedef struct {
    uint32_t parent_cluster, entry_offset, directory_cluster;
} FsIdentity;

typedef enum {
    FS_FORMAT_FAT32, FS_FORMAT_EXFAT, FS_FORMAT_INTERNAL
} FsFormat;

typedef struct {
    FsFormat format;
    bool writable;
    uint64_t max_file_size;
    uint32_t max_path_bytes, max_name_bytes, max_name_units;
    bool dirty, media_failure;
    uint64_t volume_bytes;
} FsVolumeInfo;

typedef struct {
    char name[768];
    uint64_t size;
    bool is_directory;
    uint16_t date, time;
    uint8_t attributes;
    uint16_t access_date, created_date, created_time;
    FsIdentity identity;
} FsEntry;

typedef struct FsDir FsDir;
typedef struct FsFile FsFile;

typedef struct {
    uint16_t access_date;
    uint16_t modified_date, modified_time;
    uint16_t created_date, created_time;
} FsTimes;

enum {
    FS_OPEN_READ = 1, FS_OPEN_WRITE = 2, FS_OPEN_CREATE = 4,
    FS_OPEN_EXCLUSIVE = 8, FS_OPEN_TRUNCATE = 16, FS_OPEN_APPEND = 32
};

FsResult fs_mount(const BlockDevice *disk);
void fs_unmount(void);
FsResult fs_volume_info(FsVolumeInfo *info);
uint64_t fs_revision(void);
/* A mount failure may provide a format-specific diagnostic. */
const char *fs_mount_error(void);
/* Handles belong to the caller, including after unmount or media removal. */
FsResult fs_opendir(const char *path, FsDir **dir);
FsResult fs_next(FsDir *dir, FsEntry *entry, bool *end);
void fs_closedir(FsDir *dir);
FsResult fs_stat(const char *path, FsEntry *entry);
FsResult fs_open(const char *path, unsigned flags, FsFile **file);
FsResult fs_read(FsFile *file, void *buf, uint32_t count, uint32_t *read);
FsResult fs_write(FsFile *file, const void *buf, uint32_t count, uint32_t *written);
FsResult fs_sync(FsFile *file);
/* Extension writes zeroes; the current file position is preserved. */
FsResult fs_truncate(FsFile *file, uint64_t size);
/* Seeks clamp to EOF; callers retain deferred offsets for sparse writes. */
FsResult fs_seek(FsFile *file, uint64_t absolute_offset);
uint64_t fs_tell(FsFile *file);
uint64_t fs_size(FsFile *file);
FsResult fs_close(FsFile *file);
FsResult fs_mkdir(const char *path);
FsResult fs_unlink(const char *path);
FsResult fs_rename(const char *from, const char *to);
FsResult fs_chmod(const char *path, uint8_t attributes);
FsResult fs_utime(const char *path, const FsTimes *times);

#endif
