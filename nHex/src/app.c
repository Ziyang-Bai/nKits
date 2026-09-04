#include "app.h"

#include "input.h"

#include <libndls.h>
#include <ctype.h>
#include <errno.h>

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
    if (!file_buffer_modified(&app->buffer)) {
        set_status(app, "No changes");
        return true;
    }
    if (!file_buffer_save(&app->buffer)) {
        ui_message("Save failed", file_buffer_error(&app->buffer));
        set_status(app, "Save failed");
        return false;
    }
    set_status(app, "Saved");
    return true;
}

static bool resolve_dirty(App *app) {
    int choice;
    if (!app->file_open || !file_buffer_modified(&app->buffer)) {
        return true;
    }
    choice = ui_confirm_save(&app->ui);
    if (choice == 1) {
        return save_file(app);
    }
    return choice == 2;
}

static bool open_file(App *app, const char *path, bool check_dirty) {
    FileBuffer *candidate;

    if (!path || !path[0]) {
        return false;
    }
    if (check_dirty && !resolve_dirty(app)) {
        return false;
    }
    candidate = (FileBuffer *)malloc(sizeof(*candidate));
    if (!candidate) {
        ui_message("Open failed", "Not enough memory for page cache");
        return false;
    }
    if (!file_buffer_open(candidate, path)) {
        ui_message("Open failed", file_buffer_error(candidate));
        free(candidate);
        return false;
    }
    if (app->file_open) {
        file_buffer_close(&app->buffer);
    }
    app->buffer = *candidate;
    free(candidate);
    app->file_open = true;
    editor_init(&app->editor, &app->buffer);
    set_status(app, "Opened %.70s", path);
    return true;
}

static bool parse_file_size(const char *text, uint64_t *value) {
    const char *cursor = text;
    char *end;
    int base = 10;
    unsigned long long parsed;
    if (!cursor || !value) return false;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16;
        cursor += 2;
    }
    if (!*cursor) return false;
    errno = 0;
    parsed = strtoull(cursor, &end, base);
    if (errno == ERANGE || end == cursor || parsed > UINT64_MAX) return false;
    while (isspace((unsigned char)*end)) end++;
    if (*end) return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool create_file(App *app, const char *path, bool check_dirty) {
    FileBuffer *candidate;
    char *size_text;
    uint64_t initial_size;
    if (!path || !path[0]) return false;
    if (check_dirty && !resolve_dirty(app)) return false;
    size_text = ui_prompt("nHex - New File", "Initial size (decimal or 0x)", "0");
    if (!size_text) return false;
    if (!parse_file_size(size_text, &initial_size)) {
        free(size_text);
        ui_message("Create failed", "Initial size is invalid");
        return false;
    }
    free(size_text);
    candidate = (FileBuffer *)malloc(sizeof(*candidate));
    if (!candidate) {
        ui_message("Create failed", "Not enough memory for page cache");
        return false;
    }
    if (!file_buffer_create(candidate, path)) {
        ui_message("Create failed", file_buffer_error(candidate));
        file_buffer_close(candidate);
        free(candidate);
        return false;
    }
    if (initial_size && (!file_buffer_extend(candidate, initial_size) ||
                         !file_buffer_save(candidate))) {
        ui_message("Create failed", file_buffer_error(candidate));
        file_buffer_close(candidate);
        remove(path);
        free(candidate);
        return false;
    }
    if (app->file_open) file_buffer_close(&app->buffer);
    app->buffer = *candidate;
    free(candidate);
    app->file_open = true;
    editor_init(&app->editor, &app->buffer);
    set_status(app, "Created %.69s", path);
    return true;
}

static void prompt_open(App *app) {
    const char *initial = app->file_open ? file_buffer_path(&app->buffer) : get_documents_dir();
    bool create = false;
    char *path = ui_choose_file(&app->ui, initial, &create);
    if (path) {
        if (create) create_file(app, path, true);
        else open_file(app, path, true);
        free(path);
    }
}

static void prompt_save_as(App *app) {
    char *path = ui_prompt("nHex - Save As", "Destination path", file_buffer_path(&app->buffer));
    if (!path) {
        return;
    }
    if (!file_buffer_save_as(&app->buffer, path)) {
        ui_message("Save As failed", file_buffer_error(&app->buffer));
        set_status(app, "Save As failed");
    } else {
        set_status(app, "Saved as %.65s", path);
    }
    free(path);
}

static void prompt_new(App *app) {
    char suggested[NHEX_PATH_MAX];
    char *path;
    snprintf(suggested, sizeof(suggested), "%s/new.bin", get_documents_dir());
    path = ui_prompt("nHex - New File", "New file path", suggested);
    if (!path) return;
    create_file(app, path, true);
    free(path);
}

static void prompt_extend(App *app) {
    char initial[32];
    char *text;
    uint64_t new_size;
    uint64_t old_size = file_buffer_size(&app->buffer);
    snprintf(initial, sizeof(initial), "%llu", (unsigned long long)old_size);
    text = ui_prompt("nHex - Extend", "New size (decimal or 0x)", initial);
    if (!text) return;
    if (!parse_file_size(text, &new_size) || new_size < old_size) {
        ui_message("Extend failed", "New size must be at least the current size");
        set_status(app, "Extend rejected");
    } else if (!file_buffer_extend(&app->buffer, new_size)) {
        ui_message("Extend failed", file_buffer_error(&app->buffer));
        set_status(app, "Extend failed");
    } else {
        if (new_size > old_size) {
            app->editor.cursor_offset = old_size;
            app->editor.high_nibble = true;
            editor_ensure_visible(&app->editor);
        }
        set_status(app, "Size extended to %llu", (unsigned long long)new_size);
    }
    free(text);
}

static void prompt_goto(App *app) {
    char initial[32];
    char *expression;
    snprintf(initial, sizeof(initial), "0x%llX", (unsigned long long)app->editor.cursor_offset);
    expression = ui_prompt("nHex - Go To", "Offset (absolute or +/-relative)", initial);
    if (!expression) {
        return;
    }
    if (!editor_go_to(&app->editor, expression)) {
        ui_message("Invalid offset", "Offset is malformed or outside the file");
        set_status(app, "Go To rejected");
    } else {
        set_status(app, "Moved to 0x%llX", (unsigned long long)app->editor.cursor_offset);
    }
    free(expression);
}

static void reload_file(App *app) {
    char path[NHEX_PATH_MAX];
    if (!resolve_dirty(app)) {
        return;
    }
    strncpy(path, file_buffer_path(&app->buffer), sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    if (open_file(app, path, false)) {
        set_status(app, "Reloaded");
    }
}

static int hex_nibble(uint8_t character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static void handle_character(App *app, uint8_t character) {
    bool ok;
    if (app->editor.mode == EDIT_MODE_HEX) {
        int nibble = hex_nibble(character);
        if (nibble < 0) {
            set_status(app, "HEX accepts 0-9 and A-F");
            return;
        }
        ok = editor_input_hex(&app->editor, nibble);
    } else {
        ok = editor_input_ascii(&app->editor, character);
    }
    if (!ok) {
        set_status(app, "%s", file_buffer_error(&app->buffer));
    } else {
        app->status[0] = '\0';
    }
}

static void request_exit(App *app) {
    if (resolve_dirty(app)) {
        app->quit = true;
    }
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
        case INPUT_TOGGLE_MODE:
            editor_toggle_mode(&app->editor);
            set_status(app, "%s mode", app->editor.mode == EDIT_MODE_HEX ? "HEX" : "ASCII");
            break;
        case INPUT_GOTO: prompt_goto(app); break;
        case INPUT_NEW: prompt_new(app); break;
        case INPUT_EXTEND: prompt_extend(app); break;
        case INPUT_OPEN: prompt_open(app); break;
        case INPUT_SAVE: save_file(app); break;
        case INPUT_SAVE_AS: prompt_save_as(app); break;
        case INPUT_RELOAD: reload_file(app); break;
        case INPUT_MENU: ui_show_menu(&app->ui); break;
        case INPUT_EXIT: request_exit(app); break;
        case INPUT_CHARACTER: handle_character(app, event.character); break;
        case INPUT_NONE: break;
    }
}

bool app_init(App *app, const char *initial_path) {
    bool create = false;
    char *path = NULL;
    memset(app, 0, sizeof(*app));
    if (!ui_init(&app->ui)) {
        return false;
    }
    if (initial_path && initial_path[0]) {
        return open_file(app, initial_path, false);
    }
    path = ui_choose_file(&app->ui, get_documents_dir(), &create);
    if (!path) {
        return false;
    }
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
        ui_draw(&app->ui, &app->editor, app->status);
        input_wait();
        event = input_read();
        wait_no_key_pressed();
        handle_event(app, event);
    }
}

void app_destroy(App *app) {
    if (app) {
        ui_destroy(&app->ui);
    }
    if (app && app->file_open) {
        file_buffer_close(&app->buffer);
        app->file_open = false;
    }
}
