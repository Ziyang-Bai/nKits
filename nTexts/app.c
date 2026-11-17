#include "app.h"
#include "app_log.h"
#include "app_strings.h"
#include "interface.h"
#include "library.h"
#include "reader.h"
#include "settings.h"
#include <libndls.h>
#include <stdio.h>
#include <string.h>

int ntexts_app_main(int argc, char **argv) {
    Gc gc;
    scr_type_t screen_type;
    AppState app;
    char data_dir[512], selected[512];
    int sel = 0;
    int home_top = 0;
    shortcut_hints_reset();
    app_log("startup", "enter argc=%d", argc);
    enable_relative_paths(argv);
    storage_app_defaults(&app);
    debug_install_handlers();
    app_log("startup", "handlers installed");
    screen_type = lcd_type() == SCR_320x240_4 ? SCR_320x240_4 : SCR_320x240_565;
    app_log("startup", "lcd_type selected=%d", (int)screen_type);
    lcd_init(screen_type);
    gc = gui_gc_global_GC();
    debug_set_runtime(gc, &app, screen_type);
    app_log("startup", "graphics ready gc=%p", gc);
    if (!storage_init(data_dir, sizeof(data_dir))) {
        debug_fail_errno("main:storage_init", data_dir[0] ? data_dir : "Documents/nTexts");
        lcd_init(SCR_TYPE_INVALID);
        return 1;
    }
    app_log("startup", "storage dir=%s", data_dir);
    storage_load_app(&app, data_dir);
    app_locale = app.locale;
    if (storage_prune_recents(&app)) {
        app_log("startup", "pruned invalid recents");
        if (!storage_save_app(&app, data_dir))
            debug_fail_errno("main:prune_recents", data_dir);
    }
    app_log("startup", "app loaded recents=%lu history=%lu theme=%d",
            (unsigned long)app.recent_count, (unsigned long)app.history_count, app.theme);
    if (app.theme < 0 || app.theme > 2) app.theme = 0;
    if (argc > 1) {
        app_log("startup", "open argv path=%s", argv[1]);
        open_reader(argv[1], gc, &app, data_dir);
        lcd_init(SCR_TYPE_INVALID);
        app_log("shutdown", "exit after argv path");
        return 0;
    }
    app_log("startup", "home loop begin");
    for (;;) {
        int key;
        int recent_count;
        int total;
        draw_home(gc, &app, sel, home_top);
        if (shortcut_hints(gc, &app, data_dir, SHORTCUT_HOME, 0, 0)) continue;
        key = wait_key();
        wait_release();
        if (key == 14) {
            shortcut_hints(gc, &app, data_dir, SHORTCUT_HOME, 0, 1);
            continue;
        }
        recent_count = app.recent_count > MAX_RECENTS ? MAX_RECENTS : (int)app.recent_count;
        total = recent_count + 2;
        if (key == 1) sel = sel > 0 ? sel - 1 : total - 1;
        else if (key == 2) sel = sel + 1 < total ? sel + 1 : 0;
        else if ((key == 3 || key == 4) && sel == recent_count + 1)
            settings_menu(gc, &app, data_dir, NULL);
        else if (key == 12) {
            memset(selected, 0, sizeof(selected));
            if (browse_files(gc, &app, data_dir, selected, sizeof(selected))) {
                open_reader(selected, gc, &app, data_dir);
                sel = home_top = 0;
            }
        } else if (key == 13) {
            settings_menu(gc, &app, data_dir, NULL);
        } else if (key == 5) {
            if (sel < recent_count) {
                char recent_path[512];
                snprintf(recent_path, sizeof(recent_path), "%s", app.recents[sel].path);
                app_log("home", "open recent[%d]=%s", sel, recent_path);
                if (!open_reader(recent_path, gc, &app, data_dir)) {
                    storage_remove_recent(&app, recent_path);
                    if (!storage_save_app(&app, data_dir))
                        debug_fail_errno("main:remove_recent", recent_path);
                }
                sel = home_top = 0;
            } else if (sel == recent_count) {
                memset(selected, 0, sizeof(selected));
                if (browse_files(gc, &app, data_dir, selected, sizeof(selected))) {
                    open_reader(selected, gc, &app, data_dir);
                    sel = home_top = 0;
                }
            } else {
                settings_menu(gc, &app, data_dir, NULL);
            }
        } else if (key == 7) {
            memset(selected, 0, sizeof(selected));
            if (browse_files(gc, &app, data_dir, selected, sizeof(selected))) {
                open_reader(selected, gc, &app, data_dir);
                sel = home_top = 0;
            }
        } else if (key == 6) break;
        recent_count = app.recent_count > MAX_RECENTS ? MAX_RECENTS : (int)app.recent_count;
        total = recent_count + 2;
        if (sel >= total) sel = total - 1;
        if (sel < 0) sel = 0;
        if (home_top > sel) home_top = sel;
        if (home_top + HOME_VISIBLE_ROWS <= sel) home_top = sel - HOME_VISIBLE_ROWS + 1;
    }
    if (!storage_save_app(&app, data_dir))
        debug_fail_errno("main:save_app", data_dir);
    lcd_init(SCR_TYPE_INVALID);
    app_log("shutdown", "normal exit");
    return 0;
}
