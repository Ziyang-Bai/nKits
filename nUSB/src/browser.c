#include "browser.h"

#include <string.h>

void browser_clear(Browser *browser) {
    browser->count = 0;
    browser->selected = 0;
    browser->page = 0;
    browser->has_next = false;
}

void browser_init(Browser *browser) {
    browser->directories_only = false;
    browser_storage(browser, STORAGE_USB);
}

void browser_storage(Browser *browser, StorageKind kind) {
    browser->kind = kind;
    strcpy(browser->path, kind == STORAGE_USB ? "0:/" : "/documents");
    browser_clear(browser);
}

void browser_location(const Browser *browser, StorageLocation *location) {
    location->kind = browser->kind;
    strcpy(location->path, browser->path);
}

FsResult browser_selection(const Browser *browser, StorageLocation *location) {
    StorageLocation parent;
    if (!browser->count) return FS_NOT_FOUND;
    browser_location(browser, &parent);
    return storage_join(location, &parent, browser->entries[browser->selected].name);
}

bool browser_is_root(const Browser *browser) {
    return strcmp(browser->path, browser->kind == STORAGE_USB ? "0:/" : "/documents") == 0;
}

FsResult browser_load(Browser *browser, uint32_t page) {
    FsResult result;
    StorageDir *dir;
    StorageLocation location;
    uint64_t skip = (uint64_t)page * BROWSER_ROWS;
    bool end = false;
    unsigned count = 0;

    browser_clear(browser);
    browser_location(browser, &location);
    result = storage_opendir(&location, &dir);
    if (result != FS_OK) return result;
    while (count <= BROWSER_ROWS && result == FS_OK && !end) {
        FsEntry *entry = &browser->entries[count];
        result = storage_next(dir, entry, &end);
        if (result != FS_OK || end) break;
        if (browser->directories_only && !entry->is_directory) continue;
        if (skip) --skip;
        else ++count;
    }
    storage_closedir(dir);
    if (result != FS_OK) return result;
    browser->page = page;
    browser->has_next = count > BROWSER_ROWS;
    browser->count = count > BROWSER_ROWS ? BROWSER_ROWS : count;
    return FS_OK;
}

FsResult browser_move(Browser *browser, int direction, bool whole_page) {
    FsResult result;
    if (!browser->count) return FS_OK;
    if (direction < 0) {
        if (!whole_page && browser->selected) {
            --browser->selected;
        } else if (browser->page) {
            result = browser_load(browser, browser->page - 1);
            if (result != FS_OK) return result;
            if (!whole_page && browser->count) browser->selected = browser->count - 1;
        } else {
            browser->selected = 0;
        }
    } else {
        if (!whole_page && browser->selected + 1 < browser->count) {
            ++browser->selected;
        } else if (browser->has_next) {
            return browser_load(browser, browser->page + 1);
        } else {
            browser->selected = browser->count - 1;
        }
    }
    return FS_OK;
}

FsResult browser_enter(Browser *browser) {
    size_t length, name_length;
    bool slash;
    FsResult result;
    const FsEntry *entry;
    if (!browser->count) return FS_OK;
    entry = &browser->entries[browser->selected];
    if (!entry->is_directory) return FS_OK;
    length = strlen(browser->path);
    name_length = strlen(entry->name);
    slash = browser->path[length - 1] != '/';
    if (length + slash + name_length >= sizeof(browser->path) ||
        (browser->kind == STORAGE_INTERNAL && length + slash + name_length >= 260))
        return FS_PATH_TOO_LONG;
    if (slash) browser->path[length] = '/';
    memcpy(browser->path + length + slash, entry->name, name_length + 1);
    result = browser_load(browser, 0);
    if (result != FS_OK) browser->path[length] = '\0';
    return result;
}

FsResult browser_parent(Browser *browser) {
    char *slash;
    if (browser_is_root(browser)) return FS_OK;
    slash = strrchr(browser->path, '/');
    if (slash == browser->path + 2) slash[1] = '\0';
    else *slash = '\0';
    return browser_load(browser, 0);
}
