/* SPDX-License-Identifier: GPL-3.0-or-later
 * Viewing behavior derived from mViewer CX by Xavier Andréani, JayTe, Excale.
 * Copyright (c) 2026 Ziyang-Bai
 */
#include "image.h"
#include "common/ui.h"
#include "version.h"

#include <dirent.h>
#include <errno.h>
#include <libndls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define L(en, zh, fr) ui_text(en, zh, fr)
#define PATH_SIZE 1024
#define VISIBLE_ROWS 8
#define VIEW_TOP 30
#define VIEW_HEIGHT 178

typedef struct {
    char name[256];
    bool directory;
} Entry;

typedef struct {
    UiCanvas canvas;
    char documents[PATH_SIZE];
    char directory[PATH_SIZE];
    char last[PATH_SIZE];
    char config[PATH_SIZE];
    char program[256];
    Entry *entries;
    size_t count, selected, top;
    const char *folder_error;
} App;

typedef struct {
    /* Like mViewer, zoom is source pixels per screen pixel. */
    float zoom, x, y;
    bool fullscreen;
} View;

static const UiTheme *const theme = &ui_themes[0];
static const char title[] = "nSee";

static bool copy_path(char *out, const char *path) {
    size_t length = strlen(path);
    if (length >= PATH_SIZE) return false;
    memcpy(out, path, length + 1);
    return true;
}

static bool join_path(char *out, const char *directory, const char *name) {
    size_t a = strlen(directory), b = strlen(name);
    bool separator = a && directory[a - 1] != '/';
    if (a + separator + b >= PATH_SIZE) return false;
    memcpy(out, directory, a);
    if (separator) out[a++] = '/';
    memcpy(out + a, name, b + 1);
    return true;
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void normalize_path(char *path) {
    size_t read = 0, write = 1;
    while (path[read]) {
        size_t start, length;
        while (path[read] == '/') ++read;
        start = read;
        while (path[read] && path[read] != '/') ++read;
        length = read - start;
        if (!length || (length == 1 && path[start] == '.')) continue;
        if (length == 2 && path[start] == '.' && path[start + 1] == '.') {
            while (write > 1 && path[write - 1] != '/') --write;
            if (write > 1) --write;
        } else {
            if (write > 1) path[write++] = '/';
            memmove(path + write, path + start, length);
            write += length;
        }
    }
    path[0] = '/';
    path[write] = 0;
}

static bool image_name(const char *name) {
    static const char *const extensions[] = {"jpg", "jpeg", "png", "bmp", "dib"};
    size_t length = strlen(name), start;
    if (length >= 4 && !strcasecmp(name + length - 4, ".tns")) length -= 4;
    start = length;
    while (start && name[start - 1] != '.') --start;
    if (!start) return false;
    for (unsigned i = 0; i < sizeof(extensions) / sizeof(*extensions); ++i)
        if (strlen(extensions[i]) == length - start &&
            !strncasecmp(name + start, extensions[i], length - start)) return true;
    return false;
}

static int compare_entries(const void *a, const void *b) {
    const Entry *left = a, *right = b;
    int order;
    if (left->directory != right->directory) return right->directory - left->directory;
    order = strcasecmp(left->name, right->name);
    return order ? order : strcmp(left->name, right->name);
}

static bool has_parent(const App *app) {
    return strcmp(app->directory, app->documents) && strcmp(app->directory, "/");
}

static void load_directory(App *app) {
    DIR *directory;
    size_t capacity = 0;
    free(app->entries);
    app->entries = NULL;
    app->count = app->selected = app->top = 0;
    app->folder_error = NULL;
    directory = opendir(app->directory);
    if (!directory) {
        app->folder_error = L("Cannot open folder.", "无法打开文件夹。", "Impossible d'ouvrir le dossier.");
        return;
    }
    for (;;) {
        struct dirent *item;
        struct stat st;
        char path[PATH_SIZE];
        errno = 0;
        item = readdir(directory);
        if (!item) {
            if (errno) app->folder_error = L("Cannot read folder.", "无法读取文件夹。", "Impossible de lire le dossier.");
            break;
        }
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
        if (strlen(item->d_name) >= sizeof(app->entries[0].name) ||
            !join_path(path, app->directory, item->d_name)) {
            app->folder_error = L("Some paths are too long.", "部分路径过长。", "Certains chemins sont trop longs.");
            continue;
        }
        if (stat(path, &st)) continue;
        if (!S_ISDIR(st.st_mode) && !(S_ISREG(st.st_mode) && image_name(item->d_name))) continue;
        if (app->count == capacity) {
            size_t next_capacity = capacity ? capacity * 2 : 32;
            Entry *next = next_capacity > capacity && next_capacity <= SIZE_MAX / sizeof(*next) ?
                realloc(app->entries, next_capacity * sizeof(*next)) : NULL;
            if (!next) {
                app->folder_error = L("Not enough memory to list folder.", "内存不足，无法列出文件夹。", "Mémoire insuffisante pour le dossier.");
                break;
            }
            app->entries = next;
            capacity = next_capacity;
        }
        strcpy(app->entries[app->count].name, item->d_name);
        app->entries[app->count++].directory = S_ISDIR(st.st_mode);
    }
    closedir(directory);
    if (app->count > 1) qsort(app->entries, app->count, sizeof(*app->entries), compare_entries);
}

static void present(App *app) {
    lcd_blit(ui_canvas_pixels(&app->canvas), SCR_320x240_565);
}

static void page(App *app, const char *heading, const char *const *lines, unsigned count) {
    unsigned first = 0;
    wait_no_key_pressed();
    for (;;) {
        int y = 30;
        unsigned end = first;
        ui_canvas_begin(&app->canvas);
        ui_draw_header(app->canvas.gc, theme, heading);
        while (end < count && y < 195) {
            int height = ui_line_height(app->canvas.gc, Regular10);
            uint16_t text[UI_LINE_UNITS];
            int units = ui_utf8_to_u16(lines[end], text, UI_LINE_UNITS), width = 0, rows = 1;
            for (int i = 0; i < units; ++i) {
                int advance = gui_gc_getCharWidth(app->canvas.gc, Regular10, text[i]);
                if (width + advance > 304) { ++rows; width = 0; }
                width += advance;
            }
            if (end > first && y + rows * height > 208) break;
            y = ui_draw_wrapped(app->canvas.gc, lines[end++], 8, y, 304, Regular10, theme->fg) + 6;
        }
        ui_draw_footer(app->canvas.gc, theme,
            L("Up/Down: scroll  Esc/Enter: back", "上下滚动  Esc/Enter 返回", "Haut/Bas : défiler  Échap/Entrée : retour"));
        ui_canvas_end(&app->canvas);
        present(app);
        for (;;) {
            if (isKeyPressed(KEY_NSPIRE_ESC) || isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET)) {
                wait_no_key_pressed();
                return;
            }
            if (isKeyPressed(KEY_NSPIRE_DOWN) && end < count) {
                ++first;
                wait_no_key_pressed();
                break;
            }
            if (isKeyPressed(KEY_NSPIRE_UP) && first) {
                --first;
                wait_no_key_pressed();
                break;
            }
            msleep(25);
        }
    }
}

static void notice(App *app, const char *message) {
    const char *lines[] = {message};
    page(app, title, lines, 1);
}

static void help(App *app) {
    const char *lines[] = {
        L("Browser: arrows or 8/2 select; Enter/click opens; Esc/Left goes up. R reloads.",
          "浏览：方向键或 8/2 选择；Enter/点击打开；Esc/左键返回上级；R 刷新。",
          "Dossiers : flèches ou 8/2 ; Entrée/clic ouvre ; Échap/Gauche remonte ; R actualise."),
        L("View: arrows or 1-9 pan; hold 5 or click for a page step. Slide the touchpad to drag.",
          "看图：方向键或 1-9 平移；按住 5 或点击键整页移动；滑动触摸板拖动。",
          "Image : flèches ou 1-9 déplacent ; maintenir 5 ou clic pour une page ; glisser sur le pavé."),
        L("+/- or multiply/divide: zoom about the center. 0: fit; Enter: 1:1; F: fullscreen.",
          "+/- 或乘除键：以中心缩放；0 适合屏幕；Enter 原始大小；F 全屏。",
          "+/- ou multiplier/diviser : zoom centré ; 0 : ajuster ; Entrée : 1:1 ; F : plein écran."),
        L("( / ): previous / next image in this folder. Esc: browser. Menu: actions, help and about.",
          "( / )：本文件夹上一张/下一张；Esc 返回浏览；Menu 打开操作、帮助和关于。",
          "( / ) : image précédente / suivante du dossier ; Échap : dossiers ; Menu : actions, aide et crédits."),
        L("JPEG, PNG and BMP/DIB; normal names and .tns-wrapped names are supported. Last opened image is saved in nsee.cfg.tns.",
          "支持 JPEG、PNG、BMP/DIB 及附加 .tns 的文件名。上次打开的图片保存在 nsee.cfg.tns。",
          "JPEG, PNG et BMP/DIB, avec ou sans suffixe .tns. La dernière image est mémorisée dans nsee.cfg.tns.")
    };
    page(app, L("Help", "帮助", "Aide"), lines, sizeof(lines) / sizeof(*lines));
}

static void about(App *app) {
    const char *lines[] = {
        "nSee " NKITS_VERSION " (" NKITS_COMMIT ")",
        NKITS_STAGE,
        "Original mViewer_CX 4.5.1",
        "Copyright (c) 2013-2019 Xavier Andréani, JayTe, Excale",
        "nKits port: Copyright (c) 2026 Ziyang-Bai",
        "GPL v3",
        L("This is free software, without any warranty. See LICENSE for the GNU General Public License version 3.",
          "本程序是自由软件，不提供任何担保。GNU 通用公共许可证第 3 版详见 LICENSE。",
          "Logiciel libre, sans aucune garantie. Voir LICENSE pour la licence publique générale GNU version 3.")
    };
    page(app, L("About nSee", "关于 nSee", "À propos de nSee"), lines, sizeof(lines) / sizeof(*lines));
}

static void register_extensions(App *app) {
    static const char *const extensions[] = {
        "jpg", "jpeg", "png", "bmp", "dib", "JPG", "JPEG", "PNG", "BMP", "DIB"
    };
    unsigned registered = 0;
    if (!app->program[0]) {
        notice(app, L("Cannot determine the program filename.", "无法确定程序文件名。", "Nom du programme indisponible."));
        return;
    }
    const char *items[] = {
        L("Register unassigned image types", "注册未关联的图片类型", "Associer les types non attribués"),
        L("Cancel", "取消", "Annuler")
    };
    if (ui_menu(&app->canvas, L("File associations", "文件关联", "Associations"), items, 2, 1) != 0) return;
    for (unsigned i = 0; i < sizeof(extensions) / sizeof(*extensions); ++i)
        cfg_register_fileext(extensions[i], app->program);
    cfg_open();
    for (unsigned i = 0; i < sizeof(extensions) / sizeof(*extensions); ++i) {
        char key[16];
        snprintf(key, sizeof(key), "ext.%s", extensions[i]);
        const char *value = cfg_get(key);
        if (value && !strcmp(value, app->program)) ++registered;
    }
    cfg_close();
    notice(app, registered == sizeof(extensions) / sizeof(*extensions) ?
        L("Image types are associated with nSee.", "图片类型已关联到 nSee。", "Les types d'images sont associés à nSee.") :
        L("Existing associations were kept. Some types could not be registered; check ndless.cfg.tns.",
          "已保留原有关联。部分类型未注册，请检查 ndless.cfg.tns。",
          "Associations existantes conservées. Certains types n'ont pas été associés ; vérifier ndless.cfg.tns."));
}

static void save_last(App *app, const char *path) {
    FILE *file;
    copy_path(app->last, path);
    if (!app->config[0]) return;
    file = fopen(app->config, "wb");
    if (!file) return;
    fwrite(path, 1, strlen(path), file);
    fclose(file);
}

static float fit_zoom(const Image *image, const View *view) {
    float x = image->width / (float)UI_WIDTH;
    float y = image->height / (float)(view->fullscreen ? UI_HEIGHT : VIEW_HEIGHT);
    return x > y ? x : y;
}

static void clamp_view(const Image *image, View *view) {
    float half_width = UI_WIDTH * view->zoom * 0.5f;
    float half_height = (view->fullscreen ? UI_HEIGHT : VIEW_HEIGHT) * view->zoom * 0.5f;
    if (half_width * 2 >= image->width) view->x = image->width * 0.5f;
    else {
        if (view->x < half_width) view->x = half_width;
        if (view->x > image->width - half_width) view->x = image->width - half_width;
    }
    if (half_height * 2 >= image->height) view->y = image->height * 0.5f;
    else {
        if (view->y < half_height) view->y = half_height;
        if (view->y > image->height - half_height) view->y = image->height - half_height;
    }
}

static void zoom_view(const Image *image, View *view, float zoom) {
    float limit = fit_zoom(image, view) * 8;
    if (limit < 32) limit = 32;
    if (zoom < 0.03125f) zoom = 0.03125f;
    if (zoom > limit) zoom = limit;
    view->zoom = zoom;
    clamp_view(image, view);
}

static void draw_image(App *app, const Image *image, View *view, const char *path) {
    int columns[UI_WIDTH];
    int top = view->fullscreen ? 0 : VIEW_TOP;
    int height = view->fullscreen ? UI_HEIGHT : VIEW_HEIGHT;
    float left = view->x - UI_WIDTH * view->zoom * 0.5f;
    float upper = view->y - height * view->zoom * 0.5f;
    uint16_t *pixels;
    ui_canvas_begin(&app->canvas);
    if (!view->fullscreen) {
        char status[128];
        ui_draw_header(app->canvas.gc, theme, title);
        ui_fill(app->canvas.gc, 0, 0, UI_WIDTH, VIEW_TOP, theme->bg);
        ui_canvas_text(&app->canvas, 8, 10, 304, base_name(path), theme->fg);
        snprintf(status, sizeof(status), "%u%%  %d x %d  %s", (unsigned)(100.0f / view->zoom + 0.5f),
            image->width, image->height, L("Menu: actions", "Menu：操作", "Menu : actions"));
        ui_draw_footer(app->canvas.gc, theme, status);
    }
    ui_fill(app->canvas.gc, 0, top, UI_WIDTH, height, 0);
    ui_canvas_end(&app->canvas);
    /* Finish the GC's drawing before sampling the image into its RGB565 surface. */
    pixels = ui_canvas_pixels(&app->canvas);
    for (int x = 0; x < UI_WIDTH; ++x) {
        float source = left + (x + 0.5f) * view->zoom;
        columns[x] = source >= 0 && source < image->width ? (int)source : -1;
    }
    for (int y = 0; y < height; ++y) {
        float source = upper + (y + 0.5f) * view->zoom;
        if (source < 0 || source >= image->height) continue;
        const uint16_t *row = image->pixels + (size_t)(int)source * image->width;
        uint16_t *out = pixels + (top + y) * UI_WIDTH;
        for (int x = 0; x < UI_WIDTH; ++x)
            if (columns[x] >= 0) out[x] = row[columns[x]];
    }
    present(app);
}

static int view_menu(App *app, const Image *image, View *view) {
    const char *items[] = {
        L("Fit to screen", "适合屏幕", "Ajuster à l'écran"),
        L("Actual size (1:1)", "原始大小 (1:1)", "Taille réelle (1:1)"),
        L("Toggle fullscreen", "切换全屏", "Basculer plein écran"),
        L("Previous image", "上一张图片", "Image précédente"),
        L("Next image", "下一张图片", "Image suivante"),
        L("Help", "帮助", "Aide"),
        L("About", "关于", "À propos"),
        L("Back to browser", "返回浏览", "Retour aux dossiers"),
        L("Resume image", "继续看图", "Reprendre l'image")
    };
    int choice = ui_menu(&app->canvas, title, items, sizeof(items) / sizeof(*items), 8);
    switch (choice) {
        case 0: view->zoom = fit_zoom(image, view); break;
        case 1: view->zoom = 1; break;
        case 2: view->fullscreen = !view->fullscreen; break;
        case 3: return -1;
        case 4: return 1;
        case 5: help(app); break;
        case 6: about(app); break;
        case 7: return 2;
    }
    clamp_view(image, view);
    return 0;
}

static int view_image(App *app, const Image *image, const char *path, bool previous, bool next) {
    View view = {1, image->width * 0.5f, image->height * 0.5f, false};
    unsigned frames = 0, page_keys = 0;
    bool dirty = true;
    view.zoom = fit_zoom(image, &view);
    wait_no_key_pressed();
    for (;;) {
        if (dirty) { draw_image(app, image, &view, path); dirty = false; }
        if (isKeyPressed(KEY_NSPIRE_ESC)) { wait_no_key_pressed(); return 0; }
        int direction = 0;
        if (isKeyPressed(KEY_NSPIRE_LP) || isKeyPressed(KEY_NSPIRE_RP)) {
            direction = isKeyPressed(KEY_NSPIRE_LP) ? -1 : 1;
            wait_no_key_pressed();
        }
        if (isKeyPressed(KEY_NSPIRE_MENU)) {
            int result = view_menu(app, image, &view);
            if (result == 2) return 0;
            direction = result;
            dirty = true;
        } else if (isKeyPressed(KEY_NSPIRE_F)) {
            view.fullscreen = !view.fullscreen;
            clamp_view(image, &view);
            wait_no_key_pressed();
            dirty = true;
        } else if (isKeyPressed(KEY_NSPIRE_0) || isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET)) {
            view.zoom = isKeyPressed(KEY_NSPIRE_0) ? fit_zoom(image, &view) : 1;
            clamp_view(image, &view);
            wait_no_key_pressed();
            dirty = true;
        } else {
            float old_x = view.x, old_y = view.y, old_zoom = view.zoom;
            unsigned directions = 0;
            bool page_step = isKeyPressed(KEY_NSPIRE_5) || isKeyPressed(KEY_NSPIRE_CLICK);
            if (isKeyPressed(KEY_NSPIRE_UP) || isKeyPressed(KEY_NSPIRE_UPRIGHT) || isKeyPressed(KEY_NSPIRE_LEFTUP) ||
                isKeyPressed(KEY_NSPIRE_7) || isKeyPressed(KEY_NSPIRE_8) || isKeyPressed(KEY_NSPIRE_9)) directions |= 1;
            if (isKeyPressed(KEY_NSPIRE_DOWN) || isKeyPressed(KEY_NSPIRE_RIGHTDOWN) || isKeyPressed(KEY_NSPIRE_DOWNLEFT) ||
                isKeyPressed(KEY_NSPIRE_1) || isKeyPressed(KEY_NSPIRE_2) || isKeyPressed(KEY_NSPIRE_3)) directions |= 2;
            if (isKeyPressed(KEY_NSPIRE_LEFT) || isKeyPressed(KEY_NSPIRE_LEFTUP) || isKeyPressed(KEY_NSPIRE_DOWNLEFT) ||
                isKeyPressed(KEY_NSPIRE_1) || isKeyPressed(KEY_NSPIRE_4) || isKeyPressed(KEY_NSPIRE_7)) directions |= 4;
            if (isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_UPRIGHT) || isKeyPressed(KEY_NSPIRE_RIGHTDOWN) ||
                isKeyPressed(KEY_NSPIRE_3) || isKeyPressed(KEY_NSPIRE_6) || isKeyPressed(KEY_NSPIRE_9)) directions |= 8;
            if (directions) { if (frames < 32) ++frames; } else frames = 0;
            unsigned move = page_step ? directions & ~page_keys : directions;
            float dx = (page_step ? UI_WIDTH : 3 + frames / 4) * view.zoom;
            float dy = (page_step ? (view.fullscreen ? UI_HEIGHT : VIEW_HEIGHT) : 3 + frames / 4) * view.zoom;
            if (move & 1) view.y -= dy;
            if (move & 2) view.y += dy;
            if (move & 4) view.x -= dx;
            if (move & 8) view.x += dx;
            page_keys = page_step ? directions : 0;
            touchpad_report_t report = {0};
            if (!touchpad_scan(&report) && report.contact && !report.pressed) {
                view.x -= (int8_t)report.x_velocity * view.zoom * 3;
                view.y += (int8_t)report.y_velocity * view.zoom * 3;
            }
            if (isKeyPressed(KEY_NSPIRE_PLUS) || isKeyPressed(KEY_NSPIRE_MULTIPLY)) zoom_view(image, &view, view.zoom / 1.08f);
            else if (isKeyPressed(KEY_NSPIRE_MINUS) || isKeyPressed(KEY_NSPIRE_DIVIDE)) zoom_view(image, &view, view.zoom * 1.08f);
            clamp_view(image, &view);
            dirty = old_x != view.x || old_y != view.y || old_zoom != view.zoom;
        }
        if (direction) {
            if ((direction < 0 && previous) || (direction > 0 && next)) return direction;
            notice(app, direction < 0 ?
                L("First image in this folder.", "已是本文件夹第一张。", "Première image de ce dossier.") :
                L("Last image in this folder.", "已是本文件夹最后一张。", "Dernière image de ce dossier."));
            dirty = true;
        }
        msleep(25);
    }
}

static bool open_image(App *app, const char *path, bool previous, bool next, int *direction) {
    Image image = {0};
    const char *error = NULL;
    ui_canvas_begin(&app->canvas);
    ui_draw_header(app->canvas.gc, theme, title);
    ui_draw_wrapped(app->canvas.gc, L("Loading image...", "正在载入图片…", "Chargement de l'image…"), 8, 40, 304, Regular10, theme->fg);
    ui_canvas_end(&app->canvas);
    present(app);
    if (!image_load(&image, path, &error)) {
        notice(app, error ? ui_error(error) : L("Cannot open image.", "无法打开图片。", "Impossible d'ouvrir l'image."));
        return false;
    }
    save_last(app, path);
    *direction = view_image(app, &image, path, previous, next);
    image_free(&image);
    return true;
}

static void select_name(App *app, const char *name) {
    for (size_t i = 0; i < app->count; ++i)
        if (!strcmp(app->entries[i].name, name)) {
            app->selected = i + has_parent(app);
            app->top = app->selected >= VISIBLE_ROWS ? app->selected - VISIBLE_ROWS + 1 : 0;
            return;
        }
}

static void browse_images(App *app, size_t index) {
    int direction = 0;
    for (;;) {
        char path[PATH_SIZE];
        if (!join_path(path, app->directory, app->entries[index].name)) {
            notice(app, L("Path is too long.", "路径过长。", "Chemin trop long."));
            return;
        }
        app->selected = index + has_parent(app);
        bool previous = index && !app->entries[index - 1].directory;
        bool next = index + 1 < app->count;
        if (!open_image(app, path, previous, next, &direction) || !direction) return;
        if (direction < 0) --index;
        else ++index;
    }
}

static void go_parent(App *app) {
    char child[256];
    char *slash;
    if (!has_parent(app)) return;
    snprintf(child, sizeof(child), "%s", base_name(app->directory));
    slash = strrchr(app->directory, '/');
    if (slash == app->directory) app->directory[1] = 0;
    else if (slash) *slash = 0;
    load_directory(app);
    select_name(app, child);
}

static bool browser_menu(App *app) {
    const char *items[] = {
        L("Reload folder", "刷新文件夹", "Actualiser le dossier"),
        L("Help", "帮助", "Aide"),
        L("About", "关于", "À propos"),
        L("File associations", "文件关联", "Associations"),
        L("Home", "主页", "Accueil"),
        L("Back", "返回", "Retour")
    };
    switch (ui_menu(&app->canvas, title, items, 6, 5)) {
        case 0: load_directory(app); break;
        case 1: help(app); break;
        case 2: about(app); break;
        case 3: register_extensions(app); break;
        case 4: return false;
    }
    return true;
}

static void browser(App *app) {
    wait_no_key_pressed();
    for (;;) {
        size_t parent = has_parent(app), count = app->count + parent;
        if (app->selected < app->top) app->top = app->selected;
        if (app->selected >= app->top + VISIBLE_ROWS) app->top = app->selected - VISIBLE_ROWS + 1;
        ui_canvas_begin(&app->canvas);
        ui_draw_header(app->canvas.gc, theme, title);
        ui_canvas_text(&app->canvas, 70, 11, 242, app->directory, theme->muted);
        for (size_t row = 0; row < VISIBLE_ROWS && app->top + row < count; ++row) {
            size_t item = app->top + row;
            char label[280];
            if (parent && !item) snprintf(label, sizeof(label), "[..] %s", L("Parent folder", "上级文件夹", "Dossier parent"));
            else {
                Entry *entry = &app->entries[item - parent];
                snprintf(label, sizeof(label), "%s%s", entry->directory ? "[+] " : "", entry->name);
            }
            gui_gc_clipRect(app->canvas.gc, 0, 0, 312, 240, GC_CRO_SET);
            ui_draw_row(app->canvas.gc, theme, 30 + row * 20, label, item == app->selected);
            gui_gc_clipRect(app->canvas.gc, 0, 0, UI_WIDTH, UI_HEIGHT, GC_CRO_SET);
        }
        if (!app->count && !app->folder_error)
            ui_draw_wrapped(app->canvas.gc, L("No images in this folder.", "此文件夹没有图片。", "Aucune image dans ce dossier."), 10, parent ? 55 : 35, 300, Regular10, theme->muted);
        if (app->folder_error)
            ui_draw_text(app->canvas.gc, app->folder_error, 8, 194, Regular9, theme->accent);
        ui_draw_footer(app->canvas.gc, theme,
            L("Enter: open  Esc: up/back  Menu: actions", "Enter 打开  Esc 上级/返回  Menu 操作", "Entrée : ouvrir  Échap : retour  Menu : actions"));
        ui_canvas_end(&app->canvas);
        present(app);
        for (;;) {
            if (isKeyPressed(KEY_NSPIRE_ESC) || isKeyPressed(KEY_NSPIRE_LEFT)) {
                wait_no_key_pressed();
                if (!parent) return;
                go_parent(app);
                break;
            }
            if (isKeyPressed(KEY_NSPIRE_MENU)) {
                if (!browser_menu(app)) return;
                break;
            }
            if (isKeyPressed(KEY_NSPIRE_R)) {
                wait_no_key_pressed(); load_directory(app); break;
            }
            bool up = isKeyPressed(KEY_NSPIRE_UP) || isKeyPressed(KEY_NSPIRE_8);
            bool down = isKeyPressed(KEY_NSPIRE_DOWN) || isKeyPressed(KEY_NSPIRE_2);
            if ((up || down) && count) {
                app->selected = up ? (app->selected ? app->selected - 1 : count - 1) : (app->selected + 1) % count;
                wait_no_key_pressed(); break;
            }
            if ((isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_CLICK) ||
                 isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_5)) && count) {
                wait_no_key_pressed();
                if (parent && !app->selected) go_parent(app);
                else {
                    size_t index = app->selected - parent;
                    Entry *entry = &app->entries[index];
                    if (entry->directory) {
                        char path[PATH_SIZE];
                        if (join_path(path, app->directory, entry->name)) {
                            copy_path(app->directory, path);
                            load_directory(app);
                        } else notice(app, L("Path is too long.", "路径过长。", "Chemin trop long."));
                    } else browse_images(app, index);
                }
                break;
            }
            msleep(25);
        }
    }
}

static void open_path(App *app, const char *path) {
    char absolute[PATH_SIZE], name[256];
    char *slash;
    if (!(path[0] == '/' ? copy_path(absolute, path) : join_path(absolute, app->documents, path))) {
        notice(app, L("Path is too long.", "路径过长。", "Chemin trop long."));
        return;
    }
    normalize_path(absolute);
    if (!image_name(absolute) || strlen(base_name(absolute)) >= sizeof(name)) {
        notice(app, L("Choose a JPEG, PNG or BMP/DIB image.", "请选择 JPEG、PNG 或 BMP/DIB 图片。", "Choisir une image JPEG, PNG ou BMP/DIB."));
        return;
    }
    strcpy(name, base_name(absolute));
    copy_path(app->directory, absolute);
    slash = strrchr(app->directory, '/');
    if (slash == app->directory) app->directory[1] = 0;
    else *slash = 0;
    load_directory(app);
    select_name(app, name);
    if (app->selected >= (size_t)has_parent(app) &&
        app->selected - has_parent(app) < app->count &&
        !strcmp(app->entries[app->selected - has_parent(app)].name, name))
        browse_images(app, app->selected - has_parent(app));
    else {
        int direction = 0;
        open_image(app, absolute, false, false, &direction);
    }
    browser(app);
}

int main(int argc, char *argv[]) {
    App app = {0};
    const char *documents;
    ui_language_init();
    if (nl_isstartup()) return 0;
    documents = get_documents_dir();
    if (!documents || !*documents || !copy_path(app.documents, documents)) return 1;
    normalize_path(app.documents);
    if (!join_path(app.config, app.documents, "nsee.cfg.tns")) app.config[0] = 0;
    if (argc > 0 && argv[0]) {
        const char *name = base_name(argv[0]);
        size_t length = strlen(name);
        if (length > 4 && !strcasecmp(name + length - 4, ".tns")) length -= 4;
        if (length && length < sizeof(app.program)) {
            memcpy(app.program, name, length);
            app.program[length] = 0;
        }
    }
    if (app.config[0]) {
        FILE *file = fopen(app.config, "rb");
        if (file) {
            size_t length = fread(app.last, 1, sizeof(app.last) - 1, file);
            int extra = fgetc(file);
            if (ferror(file) || extra != EOF) app.last[0] = 0;
            else app.last[length] = 0;
            fclose(file);
        }
    }
    if (!lcd_init(SCR_320x240_565)) return 1;
    if (!ui_canvas_open(&app.canvas)) {
        lcd_init(SCR_TYPE_INVALID);
        show_msgbox(title, L("Cannot open display.", "无法打开显示界面。", "Affichage indisponible."));
        return 1;
    }
    if (argc > 1 && argv[1] && *argv[1]) open_path(&app, argv[1]);
    unsigned selected = 0;
    for (;;) {
        const char *items[] = {
            L("Browse images", "浏览图片", "Parcourir les images"),
            L("Open last image", "打开上次图片", "Dernière image"),
            L("Help", "帮助", "Aide"),
            L("File associations", "文件关联", "Associations"),
            L("About", "关于", "À propos"),
            L("Exit", "退出", "Quitter")
        };
        int choice = ui_menu(&app.canvas, "nSee / nKits", items, 6, selected);
        if (choice < 0 || choice == 5) break;
        selected = choice;
        switch (choice) {
            case 0:
                copy_path(app.directory, app.documents);
                load_directory(&app);
                browser(&app);
                break;
            case 1:
                if (app.last[0]) {
                    char path[PATH_SIZE];
                    copy_path(path, app.last);
                    open_path(&app, path);
                } else notice(&app, L("No image has been opened yet.", "尚未打开过图片。", "Aucune image ouverte auparavant."));
                break;
            case 2: help(&app); break;
            case 3: register_extensions(&app); break;
            case 4: about(&app); break;
        }
    }
    free(app.entries);
    ui_canvas_close(&app.canvas);
    wait_no_key_pressed();
    lcd_init(SCR_TYPE_INVALID);
    refresh_osscr();
    return 0;
}
