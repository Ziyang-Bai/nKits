#ifndef NUSB_NATIVE_FS_H
#define NUSB_NATIVE_FS_H

#include "resident.h"

const char *native_fs_install(ResidentApi *api);
bool native_fs_ready(void);
void native_fs_session_changed(uint32_t session_id, uint32_t generation);
void native_fs_namespace_changed(void);
FsResult native_fs_internal_stat(const char *path, FsEntry *entry);
FsResult native_fs_internal_rename(const char *from, const char *to);
FsResult native_fs_internal_rmdir(const char *path);
FsResult native_fs_internal_set_metadata(const char *path, uint8_t attributes,
                                       const FsTimes *times);
uint32_t native_fs_fattime(void);
FsResult native_fs_portal_path(const FsEntry *entry, const FsIdentity *parent,
                             char *path, size_t capacity);
FsResult native_fs_internal_open(const char *path, unsigned flags, int *handle, uint64_t *size);
FsResult native_fs_internal_read(int handle, void *buf, uint32_t count, uint32_t *actual);
FsResult native_fs_internal_write(int handle, const void *buf, uint32_t count, uint32_t *actual);
FsResult native_fs_internal_seek(int handle, uint32_t offset, int whence, uint32_t *position);
FsResult native_fs_internal_sync(int handle);
FsResult native_fs_internal_close(int handle);

#endif
