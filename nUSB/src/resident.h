#ifndef NUSB_RESIDENT_H
#define NUSB_RESIDENT_H

#include "fs.h"
#include "usb_msc.h"
#include <stddef.h>

#define NUSB_RESIDENT_MAGIC 0x6e555342u
#define NUSB_RESIDENT_ABI 4u

typedef enum { RESIDENT_INSTALLING, RESIDENT_READY, RESIDENT_FAILED } ResidentState;

typedef struct {
    uint32_t magic;
    uint32_t abi;
    uint32_t state;
    uint32_t session_active;
    MscResult last_error;
    char build_id[65];
    MscResult (*open)(MscInfo *info, const MscHooks *hooks);
    MscResult (*read)(uint32_t lba, uint32_t count, void *buf);
    void (*close)(void);
    void (*poll)(MscInfo *info);
    bool (*folder_ready)(void);
    uint32_t session_id;
    MscResult (*write)(uint32_t lba, uint32_t count, const void *buf);
    MscResult (*sync)(void);
    FsResult (*internal_stat)(const char *path, FsEntry *entry);
    FsResult (*internal_rename)(const char *from, const char *to);
    FsResult (*internal_rmdir)(const char *path);
    FsResult (*internal_set_metadata)(const char *path, uint8_t attributes,
                                      const FsTimes *times);
    uint32_t (*fattime)(void);
    FsResult (*portal_path)(const FsEntry *entry, const FsIdentity *parent,
                            char *path, size_t capacity);
    void (*namespace_changed)(void);
    FsResult (*internal_open)(const char *path, unsigned flags, int *handle, uint64_t *size);
    FsResult (*internal_read)(int handle, void *buf, uint32_t count, uint32_t *actual);
    FsResult (*internal_write)(int handle, const void *buf, uint32_t count, uint32_t *actual);
    FsResult (*internal_seek)(int handle, uint32_t offset, int whence, uint32_t *position);
    FsResult (*internal_sync)(int handle);
    FsResult (*internal_close)(int handle);
} ResidentApi;

/* The publisher keeps its image until reboot; each frontend owns its UI. */
const char *resident_init(bool *publisher);
ResidentApi *resident_api(void);

#endif
