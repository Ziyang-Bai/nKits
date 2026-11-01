#define _POSIX_C_SOURCE 200809L
#include "app.h"
#include "image_disk.h"
#include "resident.h"
#include "language.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

/* Only MSC is replaced; browser and jobs operate on real image / POSIX files. */
static ImageDisk disk;
static MscHooks transport_hooks;
static ResidentApi api;
static bool active, busy, cancel, consume_dirty;
static unsigned closes, pump_draws;

ResidentApi *resident_api(void) { return &api; }

void msc_poll(MscInfo *info) {
    memset(info, 0, sizeof(*info));
    info->connected = disk.connected;
    info->ready = disk.connected && active;
    info->generation = disk.generation;
    info->sector_size = disk.device.sector_size;
    info->sector_count = disk.device.sector_count;
}

MscResult msc_open(MscInfo *info, const MscHooks *hooks) {
    if (busy || active) return MSC_BUSY;
    if (!disk.connected) return MSC_NO_DEVICE;
    active = true;
    api.session_active = 1;
    ++api.session_id;
    if (hooks) transport_hooks = *hooks;
    msc_poll(info);
    return MSC_OK;
}

MscResult msc_read(uint32_t lba, uint32_t count, void *buf) {
    CHECK(active);
    if (transport_hooks.pump && transport_hooks.pump(transport_hooks.ctx)) return MSC_CANCELLED;
    return disk.device.read(disk.device.ctx, lba, count, buf);
}

MscResult msc_write(uint32_t lba, uint32_t count, const void *buf) {
    CHECK(active);
    if (transport_hooks.pump && transport_hooks.pump(transport_hooks.ctx)) return MSC_CANCELLED;
    return disk.device.write(disk.device.ctx, lba, count, buf);
}

MscResult msc_sync(void) { return disk.device.sync(disk.device.ctx); }
void msc_close(void) {
    active = false;
    api.session_active = 0;
    memset(&transport_hooks, 0, sizeof(transport_hooks));
    ++closes;
}

static uint32_t fattime(void) { return (44u << 25) | (1u << 21) | (2u << 16); }
static bool pump(void *ctx) {
    App *app = ctx;
    if (consume_dirty && app->dirty) {
        app->dirty = false;
        ++pump_draws;
    }
    if (app->job && cancel) fileops_cancel(app->job);
    return cancel && !app->job && !storage_mutating();
}

static FsResult portal_path(const FsEntry *entry, const FsIdentity *parent,
                            char *path, size_t capacity) {
    (void)entry;
    (void)parent;
    snprintf(path, capacity, "/documents/USB/~nUSB-paths/test");
    return FS_OK;
}

static void attach(void) {
    uint32_t generation = disk.generation + 1;
    image_disk_close(&disk);
    CHECK(image_disk_copy(&disk, "build/fixtures/fat32-sfd.img", 512));
    disk.generation = generation;
}

static void key(App *app, InputAction action) {
    InputEvent event = {action, 0};
    app_handle(app, event);
}
static void character(App *app, uint32_t value) {
    InputEvent event = {INPUT_CHARACTER, value};
    app_handle(app, event);
}
static void menu(App *app, unsigned item) {
    unsigned i;
    key(app, INPUT_MENU);
    CHECK(app->view == APP_MENU);
    for (i = 0; i < item; ++i) key(app, INPUT_DOWN);
    key(app, INPUT_ENTER);
}
static void select_name(App *app, const char *name) {
    unsigned i;
    CHECK(app->state == APP_LIST);
    while (app->browser.page) key(app, INPUT_PAGE_UP);
    for (;;) {
        for (i = 0; i < app->browser.count; ++i) {
            if (!strcmp(app->browser.entries[i].name, name)) {
                while (app->browser.selected > i) key(app, INPUT_UP);
                while (app->browser.selected < i) key(app, INPUT_DOWN);
                return;
            }
        }
        CHECK(app->browser.has_next);
        key(app, INPUT_PAGE_DOWN);
        CHECK(app->state == APP_LIST);
    }
}
static void name(App *app, const char *text) {
    CHECK(app->view == APP_EDITOR);
    key(app, INPUT_HOME);
    while (app->editor.text[0]) key(app, INPUT_DELETE);
    while (*text) character(app, (unsigned char)*text++);
    key(app, INPUT_ENTER);
}
static void finish(App *app) {
    unsigned steps = 0;
    while (app->view != APP_RESULT && app->view != APP_CONFLICT) {
        CHECK(++steps < 100000);
        app_tick(app);
    }
}
static void content(StorageKind kind, const char *path, const char *expected) {
    StorageLocation location;
    StorageFile *file;
    char data[128];
    uint32_t count;
    CHECK(storage_location(&location, kind, path) == FS_OK);
    CHECK(storage_open(&location, FS_OPEN_READ, &file) == FS_OK);
    CHECK(storage_read(file, data, sizeof(data), &count) == FS_OK);
    CHECK(count == strlen(expected) && !memcmp(data, expected, count));
    CHECK(storage_close(file) == FS_OK);
}

int main(void) {
    App *app = malloc(sizeof(*app));
    MscHooks hooks = {pump, app};
    FsEntry entry;
    unsigned old_closes;
    char documents[] = "/tmp/nusb-app-XXXXXX";
    char settings[260];
    CHECK(app && mkdtemp(documents));
    CHECK(setenv("NUSB_TEST_DOCUMENTS", documents, 1) == 0);
    snprintf(settings, sizeof(settings), "%s/nusb.cfg", documents);
    language_init(settings);
    api.fattime = fattime;
    api.portal_path = portal_path;
    app_init(app, &hooks);
    app_tick(app);
    CHECK(app->state == APP_WAITING);
    menu(app, 6);
    CHECK(app->view == APP_HELP);
    attach();
    app_tick(app);
    CHECK(app->view == APP_HELP && app->state == APP_WAITING);
    key(app, INPUT_BACK);
    app_tick(app);
    CHECK(app->state == APP_LIST && app->browser.count == BROWSER_ROWS);
    old_closes = closes;
    menu(app, 8);
    CHECK(app->view == APP_LANGUAGE && app->menu_selected == LANGUAGE_ENGLISH);
    key(app, INPUT_DOWN);
    CHECK(language_current() == LANGUAGE_ENGLISH);
    key(app, INPUT_ENTER);
    CHECK(language_current() == LANGUAGE_CHINESE && app->language_status == 1);
    key(app, INPUT_DOWN);
    key(app, INPUT_BACK);
    CHECK(app->view == APP_MENU && language_current() == LANGUAGE_CHINESE);
    key(app, INPUT_BACK);
    CHECK(app->view == APP_BROWSER && app->state == APP_LIST && active);
    CHECK(closes == old_closes && app->browser.selected == 0);
    CHECK(!strcmp(app->browser.path, "0:/") && app->browser.count == BROWSER_ROWS);
    language_init(settings);
    CHECK(language_current() == LANGUAGE_CHINESE);
    CHECK(unlink(settings) == 0);
    language_init(settings);
    key(app, INPUT_ENTER);
    CHECK(app->view == APP_DETAILS);
    key(app, INPUT_BACK);
    select_name(app, "docs");
    key(app, INPUT_ENTER);
    CHECK(!strcmp(app->browser.path, "0:/docs"));
    CHECK(fs_stat("0:/docs/nested.txt", &entry) == FS_OK);
    consume_dirty = true;
    pump_draws = 0;
    key(app, INPUT_ENTER);
    CHECK(pump_draws > 0);
    CHECK(app->view == APP_DETAILS && app->dirty);
    consume_dirty = false;
    key(app, INPUT_BACK);
    key(app, INPUT_BACK);

    select_name(app, "README.TXT");
    menu(app, 2);
    name(app, "renamed.txt");
    CHECK(app->view == APP_JOB);
    finish(app);
    CHECK(fileops_status(app->job)->result == FS_OK);
    key(app, INPUT_ENTER);
    CHECK(fs_stat("0:/README.TXT", &entry) == FS_NOT_FOUND);
    content(STORAGE_USB, "0:/renamed.txt", "Hello nUSB!\n");

    select_name(app, "renamed.txt");
    menu(app, 0);
    CHECK(app->view == APP_PICKER && app->browser.directories_only);
    select_name(app, "empty");
    key(app, INPUT_ENTER);
    key(app, INPUT_MENU);
    key(app, INPUT_ENTER);
    name(app, "new-folder");
    CHECK(app->view == APP_PICKER);
    CHECK(fs_stat("0:/empty/new-folder", &entry) == FS_OK && entry.is_directory);
    character(app, 'u');
    finish(app);
    CHECK(fileops_status(app->job)->result == FS_OK);
    CHECK(fileops_status(app->job)->bytes_done == 12);
    key(app, INPUT_ENTER);
    content(STORAGE_USB, "0:/empty/renamed.txt", "Hello nUSB!\n");

    select_name(app, "renamed.txt");
    menu(app, 0);
    select_name(app, "empty");
    key(app, INPUT_ENTER);
    character(app, 'u');
    finish(app);
    CHECK(app->view == APP_CONFLICT && app->menu_selected == 0);
    key(app, INPUT_ENTER);
    finish(app);
    CHECK(fileops_status(app->job)->skipped == 1);
    key(app, INPUT_ENTER);

    select_name(app, "renamed.txt");
    menu(app, 0);
    select_name(app, "empty");
    key(app, INPUT_ENTER);
    character(app, 'u');
    finish(app);
    key(app, INPUT_DOWN);
    key(app, INPUT_ENTER);
    name(app, "another.txt");
    finish(app);
    CHECK(fileops_status(app->job)->result == FS_OK);
    key(app, INPUT_ENTER);
    content(STORAGE_USB, "0:/empty/another.txt", "Hello nUSB!\n");

    select_name(app, "renamed.txt");
    menu(app, 0);
    character(app, 's');
    CHECK(app->view == APP_PICKER && app->browser.kind == STORAGE_INTERNAL);
    character(app, 'u');
    finish(app);
    CHECK(fileops_status(app->job)->result == FS_OK);
    content(STORAGE_INTERNAL, "/documents/renamed.txt", "Hello nUSB!\n");
    key(app, INPUT_ENTER);
    menu(app, 4);
    CHECK(app->browser.kind == STORAGE_INTERNAL);
    select_name(app, "renamed.txt");
    menu(app, 1);
    character(app, 's');
    select_name(app, "docs");
    key(app, INPUT_ENTER);
    character(app, 'u');
    finish(app);
    CHECK(fileops_status(app->job)->result == FS_OK && fileops_status(app->job)->deleted == 1);
    content(STORAGE_USB, "0:/docs/renamed.txt", "Hello nUSB!\n");
    key(app, INPUT_ENTER);
    CHECK(app->browser.kind == STORAGE_INTERNAL && app->browser.count == 0);
    menu(app, 4);

    select_name(app, "empty");
    menu(app, 3);
    CHECK(app->view == APP_DELETE_CONFIRM && app->menu_selected == 0);
    key(app, INPUT_ENTER);
    CHECK(fs_stat("0:/empty", &entry) == FS_OK);
    menu(app, 3);
    key(app, INPUT_DOWN);
    key(app, INPUT_ENTER);
    finish(app);
    CHECK(fileops_status(app->job)->result == FS_OK);
    key(app, INPUT_ENTER);
    CHECK(fs_stat("0:/empty", &entry) == FS_NOT_FOUND);

    select_name(app, "docs");
    menu(app, 0);
    character(app, 's');
    character(app, 'u');
    key(app, INPUT_BACK);
    finish(app);
    CHECK(fileops_status(app->job)->result == FS_CANCELLED);
    key(app, INPUT_ENTER);
    CHECK(fs_stat("0:/docs", &entry) == FS_OK);

    select_name(app, "renamed.txt");
    menu(app, 0);
    character(app, 's');
    character(app, 'u');
    image_disk_detach(&disk);
    app_tick(app);
    finish(app);
    CHECK(app->view == APP_RESULT && fileops_status(app->job)->result == FS_NO_DEVICE);
    attach();
    app_tick(app);
    CHECK(app->view == APP_RESULT && fileops_status(app->job)->result == FS_NO_DEVICE);
    key(app, INPUT_ENTER);
    CHECK(app->view == APP_BROWSER && app->state == APP_LIST);

    /* A replacement observed inside a modal is reconciled on returning. */
    menu(app, 6);
    old_closes = closes;
    attach();
    app_tick(app);
    CHECK(app->view == APP_HELP);
    key(app, INPUT_BACK);
    app_tick(app);
    CHECK(app->state == APP_LIST && closes > old_closes);

    /* Detach while editing a conflict name must leave the editor and finish cleanup. */
    {
        StorageLocation target;
        StorageFile *file;
        unsigned steps;
        CHECK(storage_location(&target, STORAGE_INTERNAL, "/documents/README.TXT") == FS_OK);
        CHECK(storage_open(&target, FS_OPEN_WRITE | FS_OPEN_CREATE | FS_OPEN_EXCLUSIVE, &file) == FS_OK);
        CHECK(storage_close(file) == FS_OK);
        select_name(app, "README.TXT");
        menu(app, 0);
        character(app, 's');
        character(app, 'u');
        finish(app);
        CHECK(app->view == APP_CONFLICT);
        key(app, INPUT_DOWN);
        key(app, INPUT_ENTER);
        CHECK(app->view == APP_EDITOR && app->editor.text);
        image_disk_detach(&disk);
        for (steps = 0; steps < 16 && app->view != APP_RESULT; ++steps) app_tick(app);
        CHECK(app->view == APP_RESULT && !app->editor.text);
        CHECK(fileops_status(app->job)->result == FS_NO_DEVICE);
        attach();
        app_tick(app);
        CHECK(app->view == APP_RESULT);
        key(app, INPUT_ENTER);
        CHECK(app->state == APP_LIST && active);
        CHECK(storage_unlink(&target) == FS_OK);
    }

    /* An obsolete frontend must not unmount or close the later owner's volume. */
    old_closes = closes;
    ++api.session_id;
    /* The later owner installs its own callbacks, not the obsolete App's disk. */
    CHECK(fs_mount(&disk.device) == FS_OK);
    CHECK(fs_stat("0:/README.TXT", &entry) == FS_OK);
    app_destroy(app);
    CHECK(closes == old_closes && active);
    CHECK(fs_stat("0:/README.TXT", &entry) == FS_OK);
    fs_unmount();
    msc_close();
    app_init(app, &hooks);
    busy = true;
    app_tick(app);
    CHECK(app->state == APP_ERROR && app->last_msc == MSC_BUSY);
    menu(app, 4);
    CHECK(app->state == APP_LIST && app->browser.kind == STORAGE_INTERNAL);
    busy = false;
    menu(app, 6);
    CHECK(app->view == APP_HELP);
    key(app, INPUT_BACK);
    menu(app, 7);
    CHECK(app->view == APP_ABOUT);
    key(app, INPUT_BACK);
    menu(app, 9);
    CHECK(!app->running);
    app_destroy(app);
    image_disk_close(&disk);
    CHECK(rmdir(documents) == 0);
    free(app);
    puts("app tests passed");
    return 0;
}
