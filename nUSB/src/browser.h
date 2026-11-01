#ifndef NUSB_BROWSER_H
#define NUSB_BROWSER_H

#include "storage.h"
#include <stddef.h>

#define BROWSER_ROWS 9
#define BROWSER_PATH_MAX 4096

typedef struct {
    char path[BROWSER_PATH_MAX];
    StorageKind kind;
    bool directories_only;
    FsEntry entries[BROWSER_ROWS + 1];
    uint32_t page;
    unsigned count;
    unsigned selected;
    bool has_next;
} Browser;

void browser_init(Browser *browser);
void browser_clear(Browser *browser);
void browser_storage(Browser *browser, StorageKind kind);
void browser_location(const Browser *browser, StorageLocation *location);
FsResult browser_selection(const Browser *browser, StorageLocation *location);
FsResult browser_load(Browser *browser, uint32_t page);
FsResult browser_move(Browser *browser, int direction, bool whole_page);
FsResult browser_enter(Browser *browser);
FsResult browser_parent(Browser *browser);
bool browser_is_root(const Browser *browser);

#endif
