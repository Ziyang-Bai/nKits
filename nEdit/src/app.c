#include "app.h"

#include "input.h"

#include <libndls.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_status(App *app, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(app->status, sizeof(app->status), format, args);
    va_end(args);
}

static bool save_file(App *app) {
    if (!text_buffer_modified(&app->buffer)) {
        set_status(app, "No changes");
        return true;
    }
    if (!text_buffer_save(&app->buffer)) {
        ui_message("Save failed", text_buffer_error(&app->buffer));
        set_status(app, "Save failed");
        return false;
    }
    set_status(app, "Saved");
    return true;
}

static bool resolve_dirty(App *app) {
    int choice;
    if (!app->file_open || !text_buffer_modified(&app->buffer)) return true;
    choice = ui_confirm_save(&app->ui);
    if (choice == 1) return save_file(app);
    return choice == 2;
}

static bool open_file(App *app, const char *path, bool check_dirty) {
    TextBuffer *candidate;
    if (!path || !path[0]) return false;
    if (check_dirty && !resolve_dirty(app)) return false;
    candidate = (TextBuffer *)malloc(sizeof(*candidate));
    if (!candidate) {
        ui_message("Open failed", "Not enough memory for text buffer");
        return false;
    }
    if (!text_buffer_open(candidate, path)) {
        ui_message("Open failed", text_buffer_error(candidate));
        free(candidate);
        return false;
    }
    if (app->file_open) text_buffer_close(&app->buffer);
    app->buffer = *candidate;
    free(candidate);
    app->file_open = true;
    editor_init(&app->editor, &app->buffer);
    set_status(app, "Opened %.70s", path);
    return true;
}

static bool create_file(App *app, const char *path, bool check_dirty) {
    TextBuffer *candidate;
    if (!path || !path[0]) return false;
    if (check_dirty && !resolve_dirty(app)) return false;
    candidate = (TextBuffer *)malloc(sizeof(*candidate));
    if (!candidate) {
        ui_message("Create failed", "Not enough memory for text buffer");
        return false;
    }
    if (!text_buffer_create(candidate, path)) {
        ui_message("Create failed", text_buffer_error(candidate));
        free(candidate);
        return false;
    }
    if (app->file_open) text_buffer_close(&app->buffer);
    app->buffer = *candidate;
    free(candidate);
    app->file_open = true;
    editor_init(&app->editor, &app->buffer);
    set_status(app, "Created %.69s", path);
    return true;
}

static void prompt_open(App *app) {
    const char *initial = app->file_open ? text_buffer_path(&app->buffer) : get_documents_dir();
    bool create = false;
    char *path = ui_choose_file(&app->ui, initial, &create);
    if (path) {
        if (create) create_file(app, path, true);
        else open_file(app, path, true);
        free(path);
    }
}

static void prompt_save_as(App *app) {
    char *path = ui_prompt("nEdit - Save As", "Destination path", text_buffer_path(&app->buffer));
    if (!path) return;
    if (!text_buffer_save_as(&app->buffer, path)) {
        ui_message("Save As failed", text_buffer_error(&app->buffer));
        set_status(app, "Save As failed");
    } else {
        set_status(app, "Saved as %.65s", path);
    }
    free(path);
}

static void prompt_goto(App *app) {
    char initial[24];
    char *line;
    snprintf(initial, sizeof(initial), "%lu", (unsigned long)editor_line_number(&app->editor));
    line = ui_prompt("nEdit - Go To", "Line number", initial);
    if (!line) return;
    if (!editor_go_to_line(&app->editor, line)) {
        ui_message("Invalid line", "Line is malformed or outside the file");
        set_status(app, "Go To rejected");
    } else {
        set_status(app, "Moved to line %lu", (unsigned long)editor_line_number(&app->editor));
    }
    free(line);
}

static void prompt_new(App *app) {
    char suggested[NEDIT_PATH_MAX];
    char *path;
    snprintf(suggested, sizeof(suggested), "%s/new.txt", get_documents_dir());
    path = ui_prompt("nEdit - New File", "New file path", suggested);
    if (!path) return;
    create_file(app, path, true);
    free(path);
}

static void reload_file(App *app) {
    char path[NEDIT_PATH_MAX];
    if (!resolve_dirty(app)) return;
    strncpy(path, text_buffer_path(&app->buffer), sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    if (open_file(app, path, false)) set_status(app, "Reloaded");
}

static void edit_result(App *app, bool ok) {
    if (ok) app->status[0] = '\0';
    else if (text_buffer_error(&app->buffer)[0])
        set_status(app, "%s", text_buffer_error(&app->buffer));
}

static void request_exit(App *app) {
    if (resolve_dirty(app)) app->quit = true;
}

static void handle_event(App *app, InputEvent event) {
    switch (event.action) {
        case INPUT_LEFT: editor_move(&app->editor, EDIT_MOVE_LEFT); break;
        case INPUT_RIGHT: editor_move(&app->editor, EDIT_MOVE_RIGHT); break;
        case INPUT_UP: editor_move(&app->editor, EDIT_MOVE_UP); break;
        case INPUT_DOWN: editor_move(&app->editor, EDIT_MOVE_DOWN); break;
        case INPUT_PAGE_UP: editor_move(&app->editor, EDIT_MOVE_PAGE_UP); break;
        case INPUT_PAGE_DOWN: editor_move(&app->editor, EDIT_MOVE_PAGE_DOWN); break;
        case INPUT_HOME: editor_move(&app->editor, EDIT_MOVE_HOME); break;
        case INPUT_END: editor_move(&app->editor, EDIT_MOVE_END); break;
        case INPUT_NEW: prompt_new(app); break;
        case INPUT_OPEN: prompt_open(app); break;
        case INPUT_SAVE: save_file(app); break;
        case INPUT_SAVE_AS: prompt_save_as(app); break;
        case INPUT_RELOAD: reload_file(app); break;
        case INPUT_GOTO: prompt_goto(app); break;
        case INPUT_NEWLINE: edit_result(app, editor_insert_newline(&app->editor)); break;
        case INPUT_TAB: edit_result(app, editor_insert_tab(&app->editor)); break;
        case INPUT_CAPS_LOCK:
            app->caps_lock = event.character != 0;
            set_status(app, "CAPS LOCK %s", app->caps_lock ? "ON" : "OFF");
            break;
        case INPUT_STICKY_KEYS:
            app->sticky_keys = event.character != 0;
            set_status(app, "STICKY KEYS %s", app->sticky_keys ? "ON" : "OFF");
            break;
        case INPUT_BACKSPACE: edit_result(app, editor_backspace(&app->editor)); break;
        case INPUT_MENU: ui_show_menu(&app->ui); break;
        case INPUT_EXIT: request_exit(app); break;
        case INPUT_CHARACTER: edit_result(app, editor_insert_character(&app->editor, event.character)); break;
        case INPUT_NONE: break;
    }
}

bool app_init(App *app, const char *initial_path) {
    bool create = false;
    char *path;
    memset(app, 0, sizeof(*app));
    if (!ui_init(&app->ui)) return false;
    if (initial_path && initial_path[0]) return open_file(app, initial_path, false);
    path = ui_choose_file(&app->ui, get_documents_dir(), &create);
    if (!path) return false;
    if (!(create ? create_file(app, path, false) : open_file(app, path, false))) {
        free(path);
        return false;
    }
    free(path);
    return true;
}

void app_run(App *app) {
    while (!app->quit) {
        InputEvent event;
        ui_draw(&app->ui, &app->editor, app->status,
                app->caps_lock, app->sticky_keys);
        input_wait();
        event = input_read();
        wait_no_key_pressed();
        handle_event(app, event);
    }
}

void app_destroy(App *app) {
    if (!app) return;
    ui_destroy(&app->ui);
    if (app->file_open) {
        text_buffer_close(&app->buffer);
        app->file_open = false;
    }
}
