#include "native_namespace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_PREFIX "A:\\documents\\USB"
#define INCARNATION_COUNT 2048
#define PORTAL_DEPTH 2048

typedef struct {
    uint64_t incarnation;
    uint32_t parent, offset, cluster;
    bool directory;
} Incarnation;

typedef struct {
    uint32_t cluster, ordinal;
    uint16_t length;
} PortalFrame;

struct NativePortal {
    uint64_t epoch, revision;
    unsigned depth;
    bool done;
    FsResult error;
    char path[NATIVE_REAL_PATH_MAX];
    PortalFrame stack[PORTAL_DEPTH];
};

static Incarnation incarnations[INCARNATION_COUNT];
static unsigned incarnation_count;
static uint64_t epoch = 1;
typedef char incarnation_budget[sizeof(incarnations) <= 65536 ? 1 : -1];

typedef struct {
    uint32_t cluster;
    uint16_t path;
    bool duplicate;
} PortalLocation;

static PortalLocation locations[128];
static char location_paths[32768];
static unsigned location_count, location_bytes;
static bool locations_ready;

static unsigned char lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static bool equal(const char *a, const char *b) {
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool prefix(const char *name, const char *start) {
    while (*start) {
        if (lower((unsigned char)*name++) != lower((unsigned char)*start++)) return false;
    }
    return true;
}

static bool token_equal(const char *alias, const char *token) {
    size_t i, length;
    if (alias[1] != 'D') return equal(alias, token);
    if (!prefix(token, alias)) return false;
    token += strlen(alias);
    if (!*token) return true;
    if (*token++ != '-') return false;
    length = strlen(token);
    if (!length || length > 12) return false;
    for (i = 0; i < length; ++i) {
        if ((unsigned char)token[i] < 0x20 || (unsigned char)token[i] > 0x7e)
            return false;
    }
    return true;
}

bool native_namespace_reserved(const char *name) {
    return equal(name, NATIVE_PORTAL_NAME) || prefix(name, "~D_") || prefix(name, "~F_");
}

uint64_t native_namespace_epoch(void) {
    return epoch;
}

void native_namespace_invalidate(void) {
    /* Zero is terminal: an exhausted epoch must never make old tokens valid again. */
    if (epoch) ++epoch;
    incarnation_count = 0;
    locations_ready = false;
}

void native_namespace_reset(void) {
    native_namespace_invalidate();
}

static bool same_identity(const Incarnation *slot, const FsEntry *entry) {
    if (slot->directory != entry->is_directory) return false;
    if (slot->directory) return slot->cluster == entry->identity.directory_cluster;
    return slot->parent == entry->identity.parent_cluster &&
           slot->offset == entry->identity.entry_offset;
}

static unsigned identity_slot(const FsEntry *entry) {
    unsigned i;
    for (i = 0; i < incarnation_count; ++i) {
        if (same_identity(&incarnations[i], entry)) break;
    }
    return i;
}

static uint64_t incarnation(const FsEntry *entry) {
    unsigned i = identity_slot(entry);
    return i == incarnation_count ? 1 : incarnations[i].incarnation;
}

FsResult native_namespace_prepare(const FsEntry *entry, NativeRetirement *retirement) {
    unsigned i;
    Incarnation *slot;
    if (!entry || !retirement) return FS_INVALID_ARGUMENT;
    if (!epoch) return FS_BUSY;
    i = identity_slot(entry);
    retirement->created = i == incarnation_count;
    if (i == incarnation_count) {
        if (i == INCARNATION_COUNT) return FS_NO_MEMORY;
        slot = &incarnations[incarnation_count++];
        slot->incarnation = 1;
        slot->parent = entry->identity.parent_cluster;
        slot->offset = entry->identity.entry_offset;
        slot->cluster = entry->identity.directory_cluster;
        slot->directory = entry->is_directory;
    }
    if (incarnations[i].incarnation == UINT64_MAX) {
        epoch = 0;
        return FS_BUSY;
    }
    retirement->epoch = epoch;
    retirement->slot = i;
    return FS_OK;
}

void native_namespace_finish(const NativeRetirement *retirement, FsResult result) {
    if (result == FS_IO_ERROR || result == FS_NO_DEVICE || result == FS_CANCELLED) {
        native_namespace_invalidate();
    } else if (result == FS_OK && retirement && retirement->epoch == epoch &&
               retirement->slot < incarnation_count) {
        ++incarnations[retirement->slot].incarnation;
        locations_ready = false;
    } else if (retirement && retirement->epoch == epoch && retirement->created &&
               retirement->slot + 1 == incarnation_count &&
               incarnations[retirement->slot].incarnation == 1) {
        --incarnation_count;
    }
}

FsResult native_namespace_project(const FsEntry *entry, size_t parent_length,
                                  bool force_alias, char name[NATIVE_NAME_MAX]) {
    size_t length;
    uint64_t inc;
    uint32_t key;
    const char *suffix;
    if (!entry || !name) return FS_INVALID_ARGUMENT;
    if (!epoch) return FS_BUSY;
    length = strlen(entry->name);
    if (!force_alias && !native_namespace_reserved(entry->name) &&
        length < NATIVE_NAME_MAX && parent_length < 260 &&
        length + parent_length + 1 < 260) {
        memcpy(name, entry->name, length + 1);
        return FS_OK;
    }
    inc = incarnation(entry);
    key = entry->is_directory ? entry->identity.directory_cluster : entry->identity.entry_offset;
    suffix = !entry->is_directory && length >= 4 && equal(entry->name + length - 4, ".tns")
        ? ".tns" : "";
    snprintf(name, NATIVE_NAME_MAX, "~%c_%08lx%08lx_%08lx_%08lx%08lx%s",
             entry->is_directory ? 'D' : 'F',
             (unsigned long)(epoch >> 32), (unsigned long)(uint32_t)epoch,
             (unsigned long)key, (unsigned long)(inc >> 32),
             (unsigned long)(uint32_t)inc, suffix);
    if (parent_length >= 260 || parent_length + strlen(name) + 1 >= 260)
        return FS_PATH_TOO_LONG;
    return FS_OK;
}

static FsResult append(char *path, const char *name) {
    size_t length = strlen(path), count = strlen(name);
    bool slash = length && path[length - 1] != '/';
    if (length + slash + count >= NATIVE_REAL_PATH_MAX) return FS_PATH_TOO_LONG;
    if (slash) path[length++] = '/';
    memcpy(path + length, name, count + 1);
    return FS_OK;
}

FsResult native_portal_open(NativePortal **result) {
    NativePortal *portal;
    FsEntry entry;
    FsResult status;
    if (!result) return FS_INVALID_ARGUMENT;
    *result = NULL;
    if (!epoch) return FS_BUSY;
    status = fs_stat("0:/", &entry);
    if (status != FS_OK) return status;
    portal = calloc(1, sizeof(*portal));
    if (!portal) return FS_NO_MEMORY;
    portal->epoch = epoch;
    portal->revision = fs_revision();
    portal->depth = 1;
    memcpy(portal->path, "0:/", 4);
    portal->stack[0].cluster = entry.identity.directory_cluster;
    portal->stack[0].length = 3;
    *result = portal;
    return FS_OK;
}

static FsResult portal_next(NativePortal *portal, FsEntry *entry, bool *end) {
    FsResult status;
    FsDir *dir;
    unsigned i;
    bool finished;
    if (!portal || !entry || !end) return FS_INVALID_ARGUMENT;
    *end = false;
    if (portal->error != FS_OK) return portal->error;
    if (portal->epoch != epoch || portal->revision != fs_revision())
        return portal->error = FS_IO_ERROR;
    if (portal->done) {
        *end = true;
        return FS_OK;
    }
    while (portal->depth) {
        PortalFrame *frame = &portal->stack[portal->depth - 1];
        portal->path[frame->length] = '\0';
        if (frame->ordinal == UINT32_MAX) return portal->error = FS_IO_ERROR;
        status = fs_opendir(portal->path, &dir);
        if (status != FS_OK) return portal->error = status;
        /* Only ordinals survive a descent; no open ancestor DIRs or path copies. */
        for (i = 0; i <= frame->ordinal; ++i) {
            status = fs_next(dir, entry, &finished);
            if (status != FS_OK || finished) break;
        }
        fs_closedir(dir);
        if (status != FS_OK) return portal->error = status;
        if (portal->revision != fs_revision()) return portal->error = FS_IO_ERROR;
        if (finished) {
            --portal->depth;
            continue;
        }
        ++frame->ordinal;
        if (!entry->is_directory) continue;
        for (i = 0; i < portal->depth; ++i) {
            if (portal->stack[i].cluster == entry->identity.directory_cluster)
                return portal->error = FS_IO_ERROR;
        }
        if (portal->depth == PORTAL_DEPTH) return portal->error = FS_PATH_TOO_LONG;
        status = append(portal->path, entry->name);
        if (status != FS_OK) return portal->error = status;
        frame = &portal->stack[portal->depth++];
        frame->cluster = entry->identity.directory_cluster;
        frame->ordinal = 0;
        frame->length = (uint16_t)strlen(portal->path);
        goto found;
    }
    portal->done = true;
    *end = true;
    return FS_OK;
found:
    if (portal->epoch != epoch || portal->revision != fs_revision())
        return portal->error = FS_IO_ERROR;
    return FS_OK;
}

FsResult native_portal_next(NativePortal *portal, NativeResolved *result,
                            char name[NATIVE_NAME_MAX], bool *end) {
    FsResult status;
    if (!portal || !result || !name || !end) return FS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    status = portal_next(portal, &result->entry, end);
    if (status != FS_OK || *end) return status;
    memcpy(result->path, portal->path, strlen(portal->path) + 1);
    result->portal = true;
    status = native_namespace_project(&result->entry,
        sizeof(USB_PREFIX) + sizeof(NATIVE_PORTAL_NAME), true, name);
    if (status != FS_OK) portal->error = status;
    return status;
}

void native_portal_close(NativePortal *portal) {
    free(portal);
}

static void remember_directory(const FsEntry *entry, const char *path) {
    unsigned i;
    size_t length = strlen(path) + 1;
    for (i = 0; i < location_count; ++i) {
        if (locations[i].cluster == entry->identity.directory_cluster) {
            locations[i].duplicate = true;
            return;
        }
    }
    if (location_count == sizeof(locations) / sizeof(locations[0])) return;
    if (length > sizeof(location_paths) - location_bytes) {
        /* Keep a scan prefix so every occurrence of a cached cluster is checked. */
        location_bytes = sizeof(location_paths);
        return;
    }
    PortalLocation *location = &locations[location_count++];
    location->cluster = entry->identity.directory_cluster;
    location->path = (uint16_t)location_bytes;
    location->duplicate = false;
    memcpy(location_paths + location_bytes, path, length);
    location_bytes += length;
}

static FsResult cached_directory(const char *token, NativeResolved *result) {
    FsEntry entry = {0};
    char name[NATIVE_NAME_MAX];
    unsigned i;
    if (!locations_ready) return FS_NOT_FOUND;
    entry.is_directory = true;
    for (i = 0; i < location_count; ++i) {
        PortalLocation *location = &locations[i];
        entry.identity.directory_cluster = location->cluster;
        FsResult status = native_namespace_project(&entry, 0, true, name);
        if (status != FS_OK) return status;
        if (!token_equal(name, token)) continue;
        if (location->duplicate) return FS_IO_ERROR;
        const char *path = location_paths + location->path;
        status = fs_stat(path, &result->entry);
        if (status == FS_OK && result->entry.is_directory &&
            result->entry.identity.directory_cluster == location->cluster) {
            strcpy(result->path, path);
            result->portal = true;
            return FS_OK;
        }
        locations_ready = false;
        return status == FS_OK ? FS_NOT_FOUND : status;
    }
    return FS_NOT_FOUND;
}

static FsResult portal_resolve(const char *token, NativeResolved *result) {
    NativePortal *portal;
    FsResult status;
    FsEntry entry;
    char name[NATIVE_NAME_MAX];
    bool end, found = false;
    status = cached_directory(token, result);
    if (status != FS_NOT_FOUND) return status;
    locations_ready = false;
    location_count = location_bytes = 0;
    status = native_portal_open(&portal);
    if (status != FS_OK) return status;
    while ((status = portal_next(portal, &entry, &end)) == FS_OK && !end) {
        status = native_namespace_project(&entry, 0, true, name);
        if (status != FS_OK) break;
        remember_directory(&entry, portal->path);
        if (!token_equal(name, token)) continue;
        if (found) {
            status = FS_IO_ERROR;
            break;
        }
        found = true;
        result->entry = entry;
        memcpy(result->path, portal->path, strlen(portal->path) + 1);
        result->portal = true;
    }
    native_portal_close(portal);
    if (status != FS_OK) return status;
    /* A complete scan establishes uniqueness; idle remounts revalidate each path. */
    locations_ready = true;
    return found ? FS_OK : FS_NOT_FOUND;
}

static FsResult alias_resolve(const char *parent, const char *name, FsEntry *entry) {
    FsDir *dir;
    FsResult status;
    char alias[NATIVE_NAME_MAX];
    bool end;
    status = fs_opendir(parent, &dir);
    if (status != FS_OK) return status;
    while ((status = fs_next(dir, entry, &end)) == FS_OK && !end) {
        status = native_namespace_project(entry, 0, true, alias);
        if (status != FS_OK || token_equal(alias, name)) break;
    }
    fs_closedir(dir);
    return status == FS_OK && end ? FS_NOT_FOUND : status;
}

FsResult native_namespace_resolve(const char *path, bool create_leaf, NativeResolved *result) {
    const char *p;
    char component[NATIVE_NAME_MAX];
    FsResult status;
    size_t count, length;
    bool first = true;
    if (!path || !result) return FS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (!epoch) return FS_BUSY;
    p = path;
    if (prefix(p, USB_PREFIX) && (!p[sizeof(USB_PREFIX) - 1] ||
        p[sizeof(USB_PREFIX) - 1] == '\\' || p[sizeof(USB_PREFIX) - 1] == '/'))
        p += sizeof(USB_PREFIX) - 1;
    else if (prefix(p, "0:/")) p += 2;
    memcpy(result->path, "0:/", 4);
    status = fs_stat(result->path, &result->entry);
    if (status != FS_OK) return status;
    for (;;) {
        while (*p == '/' || *p == '\\') ++p;
        if (!*p) return FS_OK;
        count = strcspn(p, "/\\");
        if (count >= sizeof(component)) return FS_PATH_TOO_LONG;
        memcpy(component, p, count);
        component[count] = '\0';
        p += count;
        while (*p == '/' || *p == '\\') ++p;
        if (equal(component, ".") || equal(component, "..")) return FS_INVALID_ARGUMENT;
        if (first && equal(component, NATIVE_PORTAL_NAME)) {
            result->synthetic = true;
            result->portal = true;
            memcpy(result->entry.name, NATIVE_PORTAL_NAME, sizeof(NATIVE_PORTAL_NAME));
            if (!*p) return FS_OK;
            first = false;
            continue;
        }
        first = false;
        if (result->synthetic) {
            status = portal_resolve(component, result);
            if (status != FS_OK) return status;
            result->synthetic = false;
            continue;
        }
        if (!result->entry.is_directory) return FS_NOT_FOUND;
        length = strlen(result->path);
        if (native_namespace_reserved(component)) {
            status = alias_resolve(result->path, component, &result->entry);
            if (status != FS_OK) return create_leaf && status == FS_NOT_FOUND ? FS_DENIED : status;
            status = append(result->path, result->entry.name);
        } else {
            status = append(result->path, component);
            if (status != FS_OK) return status;
            status = fs_stat(result->path, &result->entry);
            if (status == FS_NOT_FOUND && create_leaf && !*p) {
                memset(&result->entry, 0, sizeof(result->entry));
                memcpy(result->entry.name, component, count + 1);
                return FS_OK;
            }
            if (status == FS_OK) {
                result->path[length] = '\0';
                status = append(result->path, result->entry.name);
            }
        }
        if (status != FS_OK) return status;
        /* A shortcut is deliberately flat, including for explicitly typed paths. */
        if (result->portal && result->entry.is_directory) return FS_NOT_FOUND;
    }
}

FsResult native_namespace_portal_path(const FsEntry *entry, const FsIdentity *parent,
                                      char *path, size_t capacity) {
    FsEntry directory;
    char name[NATIVE_NAME_MAX];
    size_t length;
    FsResult status;
    int count;
    if (!entry || !path || (!entry->is_directory && !parent)) return FS_INVALID_ARGUMENT;
    if (!epoch) return FS_BUSY;
    if (entry->is_directory) directory = *entry;
    else {
        memset(&directory, 0, sizeof(directory));
        directory.is_directory = true;
        directory.identity = *parent;
    }
    if (!directory.identity.directory_cluster) {
        count = snprintf(path, capacity, "%s", USB_PREFIX);
    } else {
        status = native_namespace_project(&directory, 0, true, name);
        if (status != FS_OK) return status;
        count = snprintf(path, capacity, "%s\\%s\\%s", USB_PREFIX, NATIVE_PORTAL_NAME, name);
    }
    if (count < 0 || (size_t)count >= capacity) return FS_PATH_TOO_LONG;
    if (entry->is_directory) return FS_OK;
    length = (size_t)count;
    status = native_namespace_project(entry, length, false, name);
    if (status != FS_OK) return status;
    count = snprintf(path + length, capacity - length, "\\%s", name);
    return count < 0 || (size_t)count >= capacity - length ? FS_PATH_TOO_LONG : FS_OK;
}

FsResult native_namespace_rename_target(const NativeResolved *source, const char *path,
                                        NativeResolved *result) {
    const char *p, *leaf, *slash;
    size_t length, count;
    FsResult status;
    if (!source || !path || !result) return FS_INVALID_ARGUMENT;
    if (!source->portal || source->synthetic || !source->entry.is_directory)
        return native_namespace_resolve(path, true, result);
    p = path;
    if (prefix(p, USB_PREFIX) && (p[sizeof(USB_PREFIX) - 1] == '\\' ||
                                 p[sizeof(USB_PREFIX) - 1] == '/'))
        p += sizeof(USB_PREFIX) - 1;
    while (*p == '/' || *p == '\\') ++p;
    if (!prefix(p, NATIVE_PORTAL_NAME) ||
        (p[sizeof(NATIVE_PORTAL_NAME) - 1] != '/' &&
         p[sizeof(NATIVE_PORTAL_NAME) - 1] != '\\'))
        return native_namespace_resolve(path, true, result);
    leaf = p + sizeof(NATIVE_PORTAL_NAME);
    count = strlen(leaf);
    if (!count || strpbrk(leaf, "/\\"))
        return native_namespace_resolve(path, true, result);
    if (native_namespace_reserved(leaf)) return native_namespace_resolve(path, true, result);
    if (equal(leaf, ".") || equal(leaf, "..")) return FS_INVALID_ARGUMENT;
    if (count >= NATIVE_NAME_MAX) return FS_PATH_TOO_LONG;
    slash = strrchr(source->path, '/');
    if (!slash || !slash[1]) return FS_DENIED;
    length = (size_t)(slash - source->path) + 1;
    memset(result, 0, sizeof(*result));
    memcpy(result->path, source->path, length);
    status = append(result->path, leaf);
    if (status != FS_OK) return status;
    status = fs_stat(result->path, &result->entry);
    if (status == FS_NOT_FOUND) {
        memset(&result->entry, 0, sizeof(result->entry));
        memcpy(result->entry.name, leaf, count + 1);
        status = FS_OK;
    }
    result->portal = true;
    return status;
}
