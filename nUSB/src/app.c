#include "app.h"
#include "resident.h"
#include "language.h"

#include <stdio.h>
#include <string.h>

static void set_message(App *app, const char *text) {
    snprintf(app->message, sizeof(app->message), "%s", text);
    app->dirty = true;
}

const char *app_result_message(FsResult result) {
    switch (result) {
    case FS_OK: return "Completed";
    case FS_NO_DEVICE: return "Drive disconnected - refresh to reconnect";
    case FS_UNSUPPORTED: return "Unsupported filesystem";
    case FS_CANCELLED: return "Cancelled; completed items kept";
    case FS_PATH_TOO_LONG: return "Path too long";
    case FS_NOT_FOUND: return "Directory or file not found";
    case FS_NO_MEMORY: return "Out of memory";
    case FS_WRITE_PROTECTED: return "Read-only storage or file";
    case FS_NO_SPACE: return "Storage full";
    case FS_EXISTS: return "Destination already exists";
    case FS_DENIED: return "Operation denied";
    case FS_INVALID_ARGUMENT: return "Invalid name or destination";
    case FS_BUSY: return "Close USB documents and retry";
    case FS_TOO_LARGE: return "File exceeds destination size limit";
    default: return "Filesystem I/O error";
    }
}

static const char *msc_message(MscResult result) {
    switch (result) {
    case MSC_NO_DEVICE: return "Connect an OTG drive";
    case MSC_UNSUPPORTED: return "Unsupported USB device";
    case MSC_TIMEOUT: return "USB timeout";
    case MSC_PROTOCOL_ERROR: return "USB protocol error";
    case MSC_CANCELLED: return "Cancelled - Ctrl+R to refresh";
    case MSC_BUSY: return "Close USB documents and retry";
    case MSC_STALLED: return "USB endpoint stalled";
    case MSC_COMMAND_FAILED: return "USB command failed";
    case MSC_WRITE_PROTECTED: return "USB drive is write-protected";
    default: return "USB I/O error";
    }
}

static bool owns_session(const App *app) {
    ResidentApi *api = resident_api();
    return app->session && api && api->session_active &&
           api->session_id == app->acquired_session_id;
}

static void close_session(App *app) {
    if (owns_session(app)) {
        fs_unmount();
        msc_close();
    }
    app->session = false;
}

static void disconnected(App *app) {
    close_session(app);
    browser_clear(&app->browser);
    app->state = APP_WAITING;
    set_message(app, "Connect an OTG drive; Menu: Storage");
}

static void fs_result(App *app, FsResult result) {
    if (result == FS_OK) {
        app->state = APP_LIST;
        set_message(app, app->browser.count ? "" : "Empty directory");
    } else if (result == FS_NO_DEVICE && app->browser.kind == STORAGE_USB) {
        disconnected(app);
    } else {
        app->state = APP_ERROR;
        set_message(app, app->last_msc != MSC_OK ? msc_message(app->last_msc) :
                    app_result_message(result));
    }
}

static uint32_t disk_generation(void *ctx) {
    App *app = ctx;
    MscInfo info;
    msc_poll(&info);
    return owns_session(app) ? info.generation : ~app->acquired_generation;
}

static MscResult disk_read(void *ctx, uint32_t lba, uint32_t count, void *buf) {
    App *app = ctx;
    app->last_msc = owns_session(app) ? msc_read(lba, count, buf) : MSC_NO_DEVICE;
    return app->last_msc;
}

static MscResult disk_write(void *ctx, uint32_t lba, uint32_t count, const void *buf) {
    App *app = ctx;
    app->last_msc = owns_session(app) ? msc_write(lba, count, buf) : MSC_NO_DEVICE;
    return app->last_msc;
}

static MscResult disk_sync(void *ctx) {
    App *app = ctx;
    app->last_msc = owns_session(app) ? msc_sync() : MSC_NO_DEVICE;
    return app->last_msc;
}

static uint32_t disk_fattime(void *ctx) {
    ResidentApi *api = resident_api();
    (void)ctx;
    return api && api->fattime ? api->fattime() : 0;
}

static FsResult acquire(App *app) {
    MscResult result;
    FsResult fs;
    if (owns_session(app) && disk_generation(app) == app->acquired_generation) return FS_OK;
    close_session(app);
    result = msc_open(&app->info, &app->hooks);
    app->last_msc = result;
    if (result != MSC_OK) return result == MSC_BUSY ? FS_BUSY :
        result == MSC_NO_DEVICE ? FS_NO_DEVICE : result == MSC_CANCELLED ? FS_CANCELLED : FS_IO_ERROR;
    app->session = true;
    app->acquired_session_id = resident_api()->session_id;
    app->acquired_generation = app->info.generation;
    app->disk.ctx = app;
    app->disk.read = disk_read;
    app->disk.write = disk_write;
    app->disk.sync = disk_sync;
    app->disk.fattime = disk_fattime;
    app->disk.generation = disk_generation;
    app->disk.sector_size = app->info.sector_size;
    app->disk.sector_count = app->info.sector_count;
    fs = fs_mount(&app->disk);
    if (fs != FS_OK) close_session(app);
    return fs;
}

void app_init(App *app, const MscHooks *hooks) {
    memset(app, 0, sizeof(*app));
    browser_init(&app->browser);
    if (hooks) app->hooks = *hooks;
    app->running = true;
    app->state = APP_WAITING;
    set_message(app, "Connect an OTG drive; Menu: Storage");
}

void app_destroy(App *app) {
    if (app->job) fileops_destroy(app->job);
    app->job = NULL;
    name_editor_destroy(&app->editor);
    close_session(app);
    memset(&app->hooks, 0, sizeof(app->hooks));
    app->running = false;
}

void app_refresh(App *app) {
    FsResult result = FS_OK;
    if (app->job) return;
    browser_clear(&app->browser);
    app->last_msc = MSC_OK;
    app->state = APP_READING;
    set_message(app, "Reading directory...");
    if (app->browser.kind == STORAGE_USB) {
        close_session(app);
        result = acquire(app);
    }
    if (result == FS_OK) result = browser_load(&app->browser, 0);
    fs_result(app, result);
    if (result != FS_OK && app->browser.kind == STORAGE_USB &&
        app->last_msc == MSC_OK && !app->session && fs_mount_error())
        set_message(app, fs_mount_error());
    if (result == FS_OK && app->browser.kind == STORAGE_USB) {
        FsVolumeInfo volume;
        if (fs_volume_info(&volume) == FS_OK && (volume.dirty || volume.media_failure))
            set_message(app, "Read-only exFAT: check volume on a computer");
    }
}

static void switch_storage(App *app) {
    StorageKind kind = app->browser.kind == STORAGE_USB ? STORAGE_INTERNAL : STORAGE_USB;
    if (app->view != APP_PICKER && app->view != APP_PICKER_MENU) close_session(app);
    browser_storage(&app->browser, kind);
    app_refresh(app);
}

static void restore_source(App *app) {
    FsResult result;
    app->browser.kind = app->source_parent.kind;
    strcpy(app->browser.path, app->source_parent.path);
    app->browser.directories_only = false;
    app->view = APP_BROWSER;
    app->last_msc = MSC_OK;
    result = app->browser.kind == STORAGE_USB ? acquire(app) : FS_OK;
    while (result == FS_OK) {
        result = browser_load(&app->browser, app->source_page);
        if (result == FS_OK && !app->browser.count && app->source_page) {
            app->source_page = 0;
            continue;
        }
        if (result != FS_NOT_FOUND || browser_is_root(&app->browser)) break;
        StorageLocation parent;
        browser_location(&app->browser, &parent);
        storage_parent(&parent);
        strcpy(app->browser.path, parent.path);
        app->source_page = 0;
        result = FS_OK;
    }
    if (app->browser.count) app->browser.selected = app->source_selected < app->browser.count ?
        app->source_selected : app->browser.count - 1;
    fs_result(app, result);
}

static FsResult begin_job(App *app, const StorageLocation *destination) {
    FsResult result = fileops_begin(&app->job, app->operation, &app->source, destination);
    if (result != FS_OK) {
        set_message(app, app_result_message(result));
        return result;
    }
    app->view = APP_JOB;
    app->job_media_failed = false;
    app->menu_selected = 0;
    app->detail_scroll = 0;
    app->dirty = true;
    return FS_OK;
}

void app_tick(App *app) {
    MscInfo info;
    bool changed;
    msc_poll(&info);
    changed = info.generation != app->info.generation;
    if (app->job) {
        const FileJobStatus *status = fileops_status(app->job);
        uint64_t bytes = status->bytes_done, copied = status->copied;
        uint64_t skipped = status->skipped, deleted = status->deleted;
        uint64_t total = status->items_total, bytes_total = status->bytes_total;
        int phase = status->phase;
        bool usb = app->source.kind == STORAGE_USB ||
            (app->operation != FILE_DELETE && status->destination.kind == STORAGE_USB);
        if (status->phase != FILE_RESULT && usb && !app->job_media_failed &&
            (!info.connected || info.generation != app->acquired_generation || !owns_session(app))) {
            app->job_media_failed = true;
            fileops_fail(app->job, FS_NO_DEVICE);
            if (app->view == APP_EDITOR) name_editor_destroy(&app->editor);
            app->view = APP_JOB;
            app->dirty = true;
        }
        if (app->view != APP_EDITOR) fileops_step(app->job);
        status = fileops_status(app->job);
        if (phase != (int)status->phase || bytes != status->bytes_done || copied != status->copied ||
            skipped != status->skipped || deleted != status->deleted || total != status->items_total ||
            bytes_total != status->bytes_total) app->dirty = true;
        if (status->phase == FILE_RESULT) {
            if (app->view == APP_EDITOR) name_editor_destroy(&app->editor);
            app->view = APP_RESULT;
        } else if (status->phase == FILE_CONFLICT && app->view != APP_EDITOR) {
            if (app->view != APP_CONFLICT) app->menu_selected = 0;
            app->view = APP_CONFLICT;
        }
        app->info = info;
        return;
    }
    if (app->browser.kind == STORAGE_USB && app->view == APP_BROWSER) {
        if (!info.connected) {
            if (app->info.connected || app->state != APP_WAITING) disconnected(app);
        } else if (!app->info.connected || changed) {
            close_session(app);
            browser_storage(&app->browser, STORAGE_USB);
            app->info = info;
            app_refresh(app);
            return;
        }
    }
    if (app->view == APP_BROWSER || app->browser.kind != STORAGE_USB) app->info = info;
}

static bool remember_source(App *app) {
    FsResult result = browser_selection(&app->browser, &app->source);
    if (result != FS_OK) {
        set_message(app, "Select a file or directory first");
        return false;
    }
    browser_location(&app->browser, &app->source_parent);
    app->source_page = app->browser.page;
    app->source_selected = app->browser.selected;
    app->source_directory = app->browser.entries[app->browser.selected].is_directory;
    return true;
}

static void edit_name(App *app, AppEdit edit, const char *name) {
    name_editor_destroy(&app->editor);
    if (!name_editor_init(&app->editor, name)) {
        set_message(app, "Cannot edit name (memory or encoding)");
        return;
    }
    app->edit = edit;
    app->view = APP_EDITOR;
}

static void editor_event(App *app, InputEvent event) {
    NameEditResult result = name_editor_handle(&app->editor, event);
    FsResult fs = FS_OK;
    StorageLocation parent, target;
    if (result == NAME_EDITING) return;
    if (result == NAME_CANCELLED) {
        name_editor_destroy(&app->editor);
        app->view = app->edit == EDIT_FOLDER ? APP_PICKER :
                    app->edit == EDIT_CONFLICT ? APP_CONFLICT : APP_BROWSER;
        return;
    }
    if (app->edit == EDIT_CONFLICT) {
        fs = fileops_decide(app->job, FILE_RENAME_ITEM, app->editor.text);
        if (fs == FS_OK) app->view = APP_JOB;
    } else {
        if (app->edit == EDIT_FOLDER) browser_location(&app->browser, &parent);
        else parent = app->source_parent;
        fs = storage_join(&target, &parent, app->editor.text);
        if (fs == FS_OK && app->edit == EDIT_FOLDER) fs = storage_mkdir(&target);
        if (fs == FS_OK && app->edit == EDIT_RENAME) {
            if (!strcmp(target.path, app->source.path)) app->view = APP_BROWSER;
            else fs = begin_job(app, &target);
        } else if (fs == FS_OK) {
            app->view = APP_PICKER;
            fs_result(app, browser_load(&app->browser, 0));
        }
    }
    if (fs != FS_OK) {
        snprintf(app->editor.error, sizeof(app->editor.error), "%s", app_result_message(fs));
    } else if (app->view != APP_EDITOR) name_editor_destroy(&app->editor);
}

static void details(App *app) {
    StorageLocation location;
    FsEntry parent;
    ResidentApi *api = resident_api();
    if (!app->browser.count) return;
    browser_location(&app->browser, &location);
    memset(&app->detail_limits, 0, sizeof(app->detail_limits));
    storage_limits(&location, &app->detail_limits);
    app->portal_path[0] = '\0';
    app->portal_result = FS_UNSUPPORTED;
    if (location.kind == STORAGE_USB && api && api->portal_path &&
        storage_stat(&location, &parent) == FS_OK)
        app->portal_result = api->portal_path(&app->browser.entries[app->browser.selected],
            &parent.identity, app->portal_path, sizeof(app->portal_path));
    app->detail_scroll = 0;
    app->view = APP_DETAILS;
    app->dirty = true;
}

static void menu_action(App *app) {
    app->view = APP_BROWSER;
    switch (app->menu_selected) {
    case 0: case 1:
        if (!remember_source(app)) break;
        app->operation = app->menu_selected == 0 ? FILE_COPY : FILE_MOVE;
        app->browser.directories_only = true;
        app->view = APP_PICKER;
        fs_result(app, browser_load(&app->browser, 0));
        break;
    case 2:
        if (!remember_source(app)) break;
        app->operation = FILE_RENAME;
        edit_name(app, EDIT_RENAME, app->browser.entries[app->browser.selected].name);
        break;
    case 3:
        if (!remember_source(app)) break;
        app->operation = FILE_DELETE;
        app->menu_selected = 0;
        app->view = APP_DELETE_CONFIRM;
        break;
    case 4: switch_storage(app); break;
    case 5: app_refresh(app); break;
    case 6: app->detail_scroll = 0; app->view = APP_HELP; break;
    case 7: app->detail_scroll = 0; app->view = APP_ABOUT; break;
    case 8:
        app->view = APP_LANGUAGE;
        app->menu_selected = language_current();
        app->language_status = 0;
        break;
    case 9: app->running = false; break;
    }
}

void app_handle(App *app, InputEvent event) {
    InputAction action = event.action;
    FsResult result = FS_OK;
    if (action == INPUT_NONE) return;
    app->dirty = true;
    if (app->view == APP_EDITOR) { editor_event(app, event); return; }
    if (app->view == APP_LANGUAGE) {
        if (action == INPUT_BACK || action == INPUT_MENU) {
            app->view = APP_MENU;
            app->menu_selected = 8;
        } else if (action == INPUT_UP && app->menu_selected) --app->menu_selected;
        else if (action == INPUT_DOWN && app->menu_selected + 1 < LANGUAGE_COUNT)
            ++app->menu_selected;
        else if (action == INPUT_ENTER)
            app->language_status = language_select((Language)app->menu_selected) ? 1 : -1;
        return;
    }
    if (app->view == APP_RESULT) {
        if (action == INPUT_UP && app->detail_scroll) --app->detail_scroll;
        else if (action == INPUT_DOWN && app->detail_scroll < 1024) ++app->detail_scroll;
        else if (action == INPUT_ENTER) {
            fileops_destroy(app->job);
            app->job = NULL;
            restore_source(app);
        }
        return;
    }
    if (app->view == APP_JOB || app->view == APP_CONFLICT) {
        if (action == INPUT_BACK) { fileops_cancel(app->job); app->view = APP_JOB; }
        else if (app->view == APP_CONFLICT) {
            if (action == INPUT_UP && app->menu_selected) --app->menu_selected;
            else if (action == INPUT_DOWN && app->menu_selected < 2) ++app->menu_selected;
            else if (action == INPUT_ENTER) {
                if (app->menu_selected == 1) {
                    const char *path = fileops_status(app->job)->destination.path;
                    const char *name = strrchr(path, '/');
                    edit_name(app, EDIT_CONFLICT, name ? name + 1 : path);
                } else {
                    fileops_decide(app->job, app->menu_selected == 0 ? FILE_SKIP : FILE_CANCEL, NULL);
                    app->view = APP_JOB;
                }
            }
        }
        return;
    }
    if (app->view == APP_HELP || app->view == APP_ABOUT || app->view == APP_DETAILS) {
        if (action == INPUT_BACK || action == INPUT_ENTER || action == INPUT_MENU) app->view = APP_BROWSER;
        else if (action == INPUT_UP && app->detail_scroll) --app->detail_scroll;
        else if (action == INPUT_DOWN && app->detail_scroll < 1024) ++app->detail_scroll;
        return;
    }
    if (app->view == APP_DELETE_CONFIRM) {
        if (action == INPUT_BACK) app->view = APP_BROWSER;
        else if (action == INPUT_UP || action == INPUT_DOWN) app->menu_selected ^= 1;
        else if (action == INPUT_ENTER) {
            if (app->menu_selected) begin_job(app, NULL);
            else app->view = APP_BROWSER;
        }
        return;
    }
    if (app->view == APP_MENU || app->view == APP_PICKER_MENU) {
        bool picker = app->view == APP_PICKER_MENU;
        if (action == INPUT_BACK || action == INPUT_MENU) app->view = picker ? APP_PICKER : APP_BROWSER;
        else if (action == INPUT_UP && app->menu_selected) --app->menu_selected;
        else if (action == INPUT_DOWN && app->menu_selected < (picker ? 1u : 9u)) ++app->menu_selected;
        else if (action == INPUT_ENTER) {
            if (!picker) menu_action(app);
            else if (!app->menu_selected) edit_name(app, EDIT_FOLDER, "");
            else restore_source(app);
        }
        return;
    }
    if (action == INPUT_MENU) {
        app->view = app->view == APP_PICKER ? APP_PICKER_MENU : APP_MENU;
        app->menu_selected = 0;
        return;
    }
    if (app->view == APP_PICKER && action == INPUT_CHARACTER &&
        (event.character == 's' || event.character == 'S')) { switch_storage(app); return; }
    if (app->view == APP_PICKER && action == INPUT_CHARACTER &&
        (event.character == 'u' || event.character == 'U')) {
        StorageLocation parent, destination;
        const char *name = strrchr(app->source.path, '/');
        browser_location(&app->browser, &parent);
        result = storage_join(&destination, &parent, name ? name + 1 : app->source.path);
        if (result == FS_OK) begin_job(app, &destination);
        else set_message(app, app_result_message(result));
        return;
    }
    if (action == INPUT_REFRESH) { app_refresh(app); return; }
    if (action == INPUT_BACK) {
        if (browser_is_root(&app->browser)) {
            if (app->view == APP_PICKER) restore_source(app);
            else app->running = false;
        } else {
            StorageLocation parent;
            browser_location(&app->browser, &parent);
            storage_parent(&parent);
            strcpy(app->browser.path, parent.path);
            if (app->browser.kind == STORAGE_USB) result = acquire(app);
            if (result == FS_OK) result = browser_load(&app->browser, 0);
            fs_result(app, result);
        }
        return;
    }
    if (app->state != APP_LIST) return;
    if (action == INPUT_RIGHT && app->view == APP_BROWSER) { details(app); return; }
    if (action == INPUT_ENTER) {
        if (!app->browser.count) return;
        if (!app->browser.entries[app->browser.selected].is_directory) { details(app); return; }
        result = browser_enter(&app->browser);
    } else if (action == INPUT_UP || action == INPUT_DOWN ||
               action == INPUT_PAGE_UP || action == INPUT_PAGE_DOWN) {
        bool up = action == INPUT_UP || action == INPUT_PAGE_UP;
        bool page = action == INPUT_PAGE_UP || action == INPUT_PAGE_DOWN;
        result = browser_move(&app->browser, up ? -1 : 1, page);
    }
    fs_result(app, result);
}
