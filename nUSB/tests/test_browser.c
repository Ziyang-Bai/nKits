#include "browser.h"
#include "image_disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void select_name(Browser *browser, const char *name) {
    unsigned i;
    CHECK(browser_load(browser, 0) == FS_OK);
    for (;;) {
        for (i = 0; i < browser->count; ++i) {
            if (!strcmp(browser->entries[i].name, name)) {
                browser->selected = i;
                return;
            }
        }
        CHECK(browser->has_next);
        CHECK(browser_move(browser, 1, true) == FS_OK);
    }
}

int main(void) {
    ImageDisk disk;
    Browser *browser = malloc(sizeof(*browser));
    uint64_t reads;
    unsigned i, total = 0;
    char saved[BROWSER_PATH_MAX];
    bool found_last = false, found_long = false, found_hidden = false, found_system = false;
    CHECK(browser != NULL);
    CHECK(image_disk_open(&disk, "build/fixtures/fat32-sfd.img", 512));
    CHECK(fs_mount(&disk.device) == FS_OK);
    browser_init(browser);
    CHECK(browser_load(browser, 0) == FS_OK);
    CHECK(browser->count == BROWSER_ROWS && browser->has_next);
    CHECK(!strcmp(browser->entries[0].name, "README.TXT"));
    CHECK(browser->entries[0].size == 12);
    {
        FsDir *dir;
        FsFile *file;
        FsEntry entry;
        char buf[16];
        uint32_t count;
        bool end;
        CHECK(fs_opendir("0:/", &dir) == FS_OK);
        CHECK(fs_next(dir, &entry, &end) == FS_OK && !end);
        CHECK(!strcmp(entry.name, "README.TXT"));
        CHECK(fs_open("0:/README.TXT", FS_OPEN_READ, &file) == FS_OK);
        CHECK(fs_read(file, buf, 3, &count) == FS_OK && count == 3);
        CHECK(browser_load(browser, 0) == FS_OK);
        CHECK(fs_next(dir, &entry, &end) == FS_OK && !end && !strcmp(entry.name, "docs"));
        CHECK(fs_read(file, buf, sizeof(buf), &count) == FS_OK && count == 9);
        CHECK(!memcmp(buf, "lo nUSB!\n", 9));
        fs_closedir(dir);
        fs_close(file);
    }
    reads = disk.reads;
    CHECK(browser_move(browser, -1, false) == FS_OK && browser->selected == 0);
    for (i = 0; i < BROWSER_ROWS - 1; ++i) CHECK(browser_move(browser, 1, false) == FS_OK);
    CHECK(browser->selected == BROWSER_ROWS - 1 && disk.reads == reads);
    CHECK(browser_move(browser, 1, false) == FS_OK);
    CHECK(browser->page == 1 && browser->selected == 0 && disk.reads > reads);
    CHECK(browser_move(browser, -1, false) == FS_OK);
    CHECK(browser->page == 0 && browser->selected == BROWSER_ROWS - 1);
    for (;;) {
        for (i = 0; i < browser->count; ++i) {
            const char *name = browser->entries[i].name;
            ++total;
            if (!strcmp(name, "f149.txt")) found_last = true;
            if (strlen(name) == 757) found_long = true;
            if (!strcmp(name, "hidden.txt") || !strcmp(name, "HIDDEN.TXT")) found_hidden = true;
            if (!strcmp(name, "system.txt") || !strcmp(name, "SYSTEM.TXT")) found_system = true;
        }
        if (!browser->has_next) break;
        CHECK(browser_move(browser, 1, true) == FS_OK);
    }
    CHECK(total == 159 && found_last && found_long && found_hidden && found_system);
    CHECK(browser->page == (total - 1) / BROWSER_ROWS);
    CHECK(browser->count == (total - 1) % BROWSER_ROWS + 1);
    reads = disk.reads;
    CHECK(browser_move(browser, 1, true) == FS_OK);
    CHECK(browser->selected + 1 == browser->count && disk.reads == reads);
    CHECK(browser_move(browser, -1, true) == FS_OK);
    CHECK(browser->page == (total - 1) / BROWSER_ROWS - 1 && browser->selected == 0);

    select_name(browser, "docs");
    CHECK(browser_enter(browser) == FS_OK);
    CHECK(!strcmp(browser->path, "0:/docs") && browser->count == 1);
    CHECK(!strcmp(browser->entries[0].name, "nested.txt") && browser->entries[0].size == 3);
    CHECK(browser_enter(browser) == FS_OK && !strcmp(browser->path, "0:/docs"));
    CHECK(browser_parent(browser) == FS_OK && browser_is_root(browser));
    select_name(browser, "empty");
    CHECK(browser_enter(browser) == FS_OK && browser->count == 0);
    CHECK(browser_parent(browser) == FS_OK);
    select_name(browser, "资料");
    CHECK(browser_enter(browser) == FS_OK && !strcmp(browser->path, "0:/资料"));
    CHECK(!strcmp(browser->entries[0].name, "inside.txt"));
    CHECK(browser_parent(browser) == FS_OK);
    select_name(browser, "Long filename example.txt");
    CHECK(browser_enter(browser) == FS_OK && browser_is_root(browser));
    select_name(browser, "deep");
    CHECK(browser_enter(browser) == FS_OK);
    for (i = 0; i < 16; ++i) {
        CHECK(browser->count == 1 && browser->entries[0].is_directory);
        CHECK(browser_enter(browser) == FS_OK);
    }
    strcpy(saved, browser->path);
    reads = disk.reads;
    CHECK(browser_enter(browser) == FS_PATH_TOO_LONG);
    CHECK(!strcmp(saved, browser->path) && browser->count == 1 && disk.reads == reads);
    CHECK(browser_parent(browser) == FS_OK);

    browser_storage(browser, STORAGE_USB);
    browser->directories_only = true;
    CHECK(browser_load(browser, 0) == FS_OK);
    CHECK(browser->count > 0);
    for (i = 0; i < browser->count; ++i) CHECK(browser->entries[i].is_directory);
    select_name(browser, "docs");
    {
        StorageLocation selected;
        CHECK(browser_selection(browser, &selected) == FS_OK);
        CHECK(selected.kind == STORAGE_USB && !strcmp(selected.path, "0:/docs"));
    }
    CHECK(browser_enter(browser) == FS_OK && browser->count == 0);
    CHECK(browser_parent(browser) == FS_OK && browser_is_root(browser));
    browser_storage(browser, STORAGE_INTERNAL);
    CHECK(browser->kind == STORAGE_INTERNAL && !strcmp(browser->path, "/documents"));
    CHECK(browser_is_root(browser));
    browser_storage(browser, STORAGE_USB);

    image_disk_detach(&disk);
    CHECK(browser_load(browser, 0) == FS_NO_DEVICE && browser->count == 0);
    fs_unmount();
    image_disk_close(&disk);
    free(browser);
    puts("browser tests passed");
    return 0;
}
