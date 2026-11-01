#ifndef NUSB_NATIVE_NAMESPACE_H
#define NUSB_NATIVE_NAMESPACE_H

#include "fs.h"
#include <stddef.h>

#define NATIVE_REAL_PATH_MAX 4096
#define NATIVE_NAME_MAX 256
#define NATIVE_PORTAL_NAME "~nUSB-paths"

typedef struct {
    char path[NATIVE_REAL_PATH_MAX];
    FsEntry entry;
    bool synthetic, portal;
} NativeResolved;

typedef struct NativePortal NativePortal;
typedef struct {
    uint64_t epoch;
    unsigned slot;
    bool created;
} NativeRetirement;

FsResult native_namespace_rename_target(const NativeResolved *source, const char *path,
                                        NativeResolved *result);
/* Callers serialize namespace operations with filesystem session changes. */
uint64_t native_namespace_epoch(void);
void native_namespace_reset(void);
void native_namespace_invalidate(void);
bool native_namespace_reserved(const char *name);
/* Memory-only projection; wildcard filtering happens after this step. */
FsResult native_namespace_project(const FsEntry *entry, size_t parent_length,
                                  bool force_alias, char name[NATIVE_NAME_MAX]);
/* create_leaf permits a missing final ordinary component, never a reserved one.
   Directory aliases may have a non-identifying '-' plus 1..12 printable ASCII
   hint bytes. File aliases, including their .tns classification, match exactly. */
FsResult native_namespace_resolve(const char *path, bool create_leaf, NativeResolved *result);
/* Reserve before unlink/file rename; finish only after the backend returns.
   Directory rename does not retire its cluster identity. */
FsResult native_namespace_prepare(const FsEntry *entry, NativeRetirement *retirement);
void native_namespace_finish(const NativeRetirement *retirement, FsResult result);
/* Memory-only formatting for foreground details. parent identifies a file's directory. */
FsResult native_namespace_portal_path(const FsEntry *entry, const FsIdentity *parent,
                                      char *path, size_t capacity);
/* Streams non-root directories with one path and a bounded ancestor stack.
   Revision/session changes fail with FS_IO_ERROR; overlong actual paths fail
   with FS_PATH_TOO_LONG. Shortcuts expose files only, never subdirectories. */
FsResult native_portal_open(NativePortal **portal);
FsResult native_portal_next(NativePortal *portal, NativeResolved *result,
                            char name[NATIVE_NAME_MAX], bool *end);
void native_portal_close(NativePortal *portal);

#endif
