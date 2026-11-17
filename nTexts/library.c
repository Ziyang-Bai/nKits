#include "library.h"
#include "app_language.h"
#include "app_log.h"
#include "app_strings.h"
#include "interface.h"
#include "text_engine.h"
#include <dirent.h>
#include <errno.h>
#include <libndls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DIRECTORY_INITIAL_CAPACITY 32
#define MAX_DIRECTORY_ITEMS 4096

typedef struct {
    char path[512];
    char name[128];
    int is_dir;
} FileItem;

static int has_txt_ext(const char *name) {
    size_t n = strlen(name);
    return (n >= 4 && !strcmp(name + n - 4, ".txt")) ||
           (n >= 8 && !strcmp(name + n - 8, ".txt.tns"));
}

static int join_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t a = strlen(dir), b = strlen(name);
    if (a + 1 + b >= cap) return 0;
    memcpy(out, dir, a);
    out[a] = '/';
    memcpy(out + a + 1, name, b + 1);
    return 1;
}

static int cmp_items(const void *a, const void *b) {
    const FileItem *ia = (const FileItem *)a, *ib = (const FileItem *)b;
    if (ia->is_dir != ib->is_dir) return ib->is_dir - ia->is_dir;
    return strcmp(ia->name, ib->name);
}

static int filename_matches(const char *name, const uint16_t *filter) {
    uint16_t decoded[512];
    int length = ui_utf8_to_u16(name, decoded, (int)(sizeof(decoded) / sizeof(decoded[0])));
    int start;
    for (start = 0; start < length; ++start) {
        int i = 0;
        while (filter[i] && decoded[start + i] &&
               text_fold_latin(decoded[start + i]) == text_fold_latin(filter[i])) ++i;
        if (!filter[i]) return 1;
    }
    return 0;
}

static int load_dir(const char *dir, const uint16_t *filter, FileItem **out_items, int *count) {
    DIR *dp = opendir(dir);
    FileItem *items = NULL;
    struct dirent *de;
    int capacity = 0;
    *out_items = NULL;
    *count = 0;
    if (!dp) {
        debug_fail_errno("load_dir:opendir", dir);
        return 0;
    }
    while ((de = readdir(dp))) {
        char path[512];
        struct stat st;
        FileItem *next;
        int next_capacity;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (!join_path(path, sizeof(path), dir, de->d_name)) continue;
        if (stat(path, &st)) continue;
        if (!S_ISDIR(st.st_mode) && !has_txt_ext(de->d_name)) continue;
        if (!S_ISDIR(st.st_mode) && filter[0] && !filename_matches(de->d_name, filter)) continue;
        if (*count >= MAX_DIRECTORY_ITEMS) continue;
        if (*count == capacity) {
            next_capacity = capacity ? capacity * 2 : DIRECTORY_INITIAL_CAPACITY;
            if (next_capacity > MAX_DIRECTORY_ITEMS) next_capacity = MAX_DIRECTORY_ITEMS;
            next = realloc(items, (size_t)next_capacity * sizeof(*next));
            if (!next) {
                int saved_errno = errno ? errno : ENOMEM;
                free(items);
                closedir(dp);
                errno = saved_errno;
                debug_fail_errno("load_dir:realloc", dir);
                return 0;
            }
            items = next;
            capacity = next_capacity;
        }
        snprintf(items[*count].path, sizeof(items[*count].path), "%s", path);
        snprintf(items[*count].name, sizeof(items[*count].name), "%s", de->d_name);
        items[*count].is_dir = S_ISDIR(st.st_mode);
        (*count)++;
    }
    closedir(dp);
    if (*count > 1) qsort(items, (size_t)*count, sizeof(items[0]), cmp_items);
    *out_items = items;
    return 1;
}

void draw_home(Gc gc, AppState *app, int sel, int top) {
    const UiTheme *t = &ui_themes[app->theme];
    char line[160];
    int recent_count = app->recent_count > MAX_RECENTS ? MAX_RECENTS : (int)app->recent_count;
    int total = recent_count + 2;
    static int logged_once = 0;
    if (sel >= total) sel = total - 1;
    if (sel < 0) sel = 0;
    if (top > sel) top = sel;
    if (top + HOME_VISIBLE_ROWS <= sel) top = sel - HOME_VISIBLE_ROWS + 1;
    if (!logged_once) app_log("home", "draw begin recents=%lu sel=%d theme=%d",
                              (unsigned long)app->recent_count, sel, app->theme);
    gui_gc_begin(gc);
    ui_draw_header(gc, t, L("nTexts阅读器", "nTexts Text Reader", "nTexts Lecteur de textes"));
    ui_draw_text(gc, recent_count ? L("最近阅读", "Recent books", "Lectures récentes") : L("暂无最近阅读", "No recent books", "Aucune lecture récente"), 8, 30, Bold10,
              recent_count ? t->fg : t->muted);
    for (int i = 0; i < HOME_VISIBLE_ROWS && top + i < total; ++i) {
        int row = top + i;
        int y = 52 + i * 20;
        if (row < recent_count) {
            unsigned long percent = app->recents[row].file_size ?
                (unsigned long)(((uint64_t)app->recents[row].offset * 100u) /
                                app->recents[row].file_size) : 0ul;
            snprintf(line, sizeof(line), "%d. %s  %lu%%", row + 1,
                     base_name(app->recents[row].path), percent);
        } else {
            snprintf(line, sizeof(line), "%s",
                     row == recent_count ? TXT_HOME_BROWSE_DOCS : TXT_SETTINGS);
        }
        ui_draw_row(gc, t, y, line, row == sel);
    }
    ui_draw_footer(gc, t, TXT_FOOTER_HOME);
    gui_gc_finish(gc);
    gui_gc_blit_to_screen(gc);
    if (!logged_once) {
        app_log("home", "draw ok");
        logged_once = 1;
    }
}

int browse_files(Gc gc, AppState *app, const char *data_dir, char *out, size_t outcap) {
    const char *documents = get_documents_dir();
    const UiTheme *t = &ui_themes[app->theme];
    char cwd[512];
    uint16_t filter[QUERY_LEN] = {0};
    FileItem *items = NULL;
    int count = 0, file_count = 0, sel = 0, top = 0, visible = 8;
    int reload = 1;
    if (!documents || !*documents || !out || !outcap) {
        errno = EINVAL;
        return 0;
    }
    snprintf(cwd, sizeof(cwd), "%s", documents);
    for (;;) {
        int key;
        if (reload) {
            free(items);
            items = NULL;
            if (!load_dir(cwd, filter, &items, &count)) return 0;
            file_count = 0;
            for (int i = 0; i < count; ++i) if (!items[i].is_dir) ++file_count;
            if (sel >= count) sel = count ? count - 1 : 0;
            if (top > sel) top = sel;
            reload = 0;
        }
        gui_gc_begin(gc);
        ui_draw_header(gc, t, strcmp(cwd, documents) ? base_name(cwd) : "Documents");
        if (filter[0]) {
            char label[48];
            uint16_t preview[18];
            snprintf(label, sizeof(label), "%s (%d):", L("筛选", "Filter", "Filtre"), file_count);
            ui_draw_text(gc, label, 8, 23, Regular9, t->muted);
            copy_u16(preview, (int)(sizeof(preview) / sizeof(preview[0])), filter);
            ui_draw_u16(gc, preview, 100, 23, Regular9, t->fg);
        }
        for (int i = 0; i < visible && top + i < count; ++i) {
            char label[150];
            int y = (filter[0] ? 42 : 30) + i * 21;
            snprintf(label, sizeof(label), "%s%s",
                     items[top + i].is_dir ? L("[目录] ", "[Folder] ", "[Dossier] ") : "", items[top + i].name);
            ui_draw_row(gc, t, y, label, top + i == sel);
        }
        ui_draw_text(gc, filter[0] ?
                  L("F筛选 0清除 H提示 Esc上级/返回", "F filter 0 clear H:tips Esc up/back",
                    "F filtrer 0 effacer H:aide Esc retour") : TXT_FOOTER_BROWSER,
                  8, UI_FOOTER_Y, Regular9, t->muted);
        gui_gc_finish(gc);
        gui_gc_blit_to_screen(gc);
        if (shortcut_hints(gc, app, data_dir, SHORTCUT_BROWSER, 0, 0)) continue;
        key = wait_key();
        wait_release();
        if (key == 14) {
            shortcut_hints(gc, app, data_dir, SHORTCUT_BROWSER, 0, 1);
            continue;
        }
        if (key == 9) {
            uint16_t input[QUERY_LEN];
            if (request_u16_input(L("文件名筛选", "Filter filename", "Filtrer le nom"),
                                  L("输入部分文件名", "Part of filename", "Partie du nom"),
                                  filter, input, QUERY_LEN)) {
                copy_u16(filter, QUERY_LEN, input);
                sel = top = 0;
                reload = 1;
            }
            continue;
        }
        if (key == 12 && filter[0]) {
            filter[0] = 0;
            sel = top = 0;
            reload = 1;
            continue;
        }
        if (key == 1) {
            sel = count ? (sel > 0 ? sel - 1 : count - 1) : 0;
            if (sel < top) top = sel;
            if (sel >= top + visible) top = sel - visible + 1;
        } else if (key == 2) {
            sel = count ? (sel + 1 < count ? sel + 1 : 0) : 0;
            if (sel >= top + visible) top = sel - visible + 1;
            if (sel < top) top = sel;
        } else if (key == 5 && count) {
            if (items[sel].is_dir) {
                snprintf(cwd, sizeof(cwd), "%s", items[sel].path);
                sel = top = 0;
                reload = 1;
            } else {
                snprintf(out, outcap, "%s", items[sel].path);
                free(items);
                return 1;
            }
        } else if (key == 6) {
            char *slash = strrchr(cwd, '/');
            if (slash && strcmp(cwd, documents)) {
                *slash = 0;
                sel = top = 0;
                reload = 1;
            } else {
                free(items);
                return 0;
            }
        }
    }
}
