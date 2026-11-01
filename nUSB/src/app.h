#ifndef NUSB_APP_H
#define NUSB_APP_H

#include "browser.h"
#include "fileops.h"
#include "input.h"
#include "name_editor.h"

typedef enum { APP_WAITING, APP_READING, APP_LIST, APP_ERROR } AppState;
typedef enum {
    APP_BROWSER, APP_MENU, APP_HELP, APP_ABOUT, APP_PICKER, APP_PICKER_MENU,
    APP_EDITOR, APP_DELETE_CONFIRM, APP_JOB, APP_CONFLICT, APP_RESULT, APP_DETAILS,
    APP_LANGUAGE
} AppView;
typedef enum { EDIT_RENAME, EDIT_FOLDER, EDIT_CONFLICT } AppEdit;

typedef struct {
    Browser browser;
    BlockDevice disk;
    MscInfo info;
    MscHooks hooks;
    MscResult last_msc;
    AppState state;
    AppView view;
    unsigned menu_selected;
    int language_status;
    bool session;
    uint32_t acquired_session_id, acquired_generation;
    bool running;
    bool dirty;
    char message[128];
    FileJob *job;
    bool job_media_failed;
    FileOperation operation;
    StorageLocation source, source_parent;
    unsigned source_selected;
    uint32_t source_page;
    NameEditor editor;
    AppEdit edit;
    unsigned detail_scroll;
    StorageLimits detail_limits;
    char portal_path[260];
    FsResult portal_result;
    bool source_directory;
} App;

void app_init(App *app, const MscHooks *hooks);
void app_destroy(App *app);
void app_tick(App *app);
void app_handle(App *app, InputEvent event);
void app_refresh(App *app);
const char *app_result_message(FsResult result);

#endif
