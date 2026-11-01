#include "platform.h"
#include "ui.h"
#include "resident.h"
#include "resident_build.h"
#include "version.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_PATH "/documents/themeeditor.cfg"
#define ORIGINAL_PATH "/documents/themeeditor.original"
#define FILE_LIMIT 65536

static struct {
    Theme theme;
    ThemePlatform platform;
    uint32_t original[THEME_MAX_ITEMS][2];
    char path[256], program[128];
    unsigned index, mode, column;
    uint32_t seed;
    void (*menu_action)(void *, unsigned, unsigned);
    uint16_t *screen;
    bool patch, foreground, request, ready, dirty, removed;
} storage, *app;
static bool native, quiet, native_shifted;
static unsigned native_held, native_pressed, native_hex_held;
static int native_hex_pressed = -1;
static char file_data[FILE_LIMIT];
static const char title[] = "ThemeEditor";

static void clear_native_input(void) {
    native_held = native_pressed = native_hex_held = 0;
    native_hex_pressed = -1;
    native_shifted = false;
}

static void wide(uint16_t *out, size_t cap, const char *text) {
    size_t i = 0;
    while (i + 1 < cap && text[i]) { out[i] = (unsigned char)text[i]; ++i; }
    out[i] = 0;
}

static bool numeric(const char *subtitle, const char *label, int *value, int low, int high) {
    if (!native) return show_1numeric_input(title, subtitle, label, value, low, high) != 0;
    uint16_t t[32], s[192], l[256];
    wide(t, 32, title); wide(s, 192, subtitle); wide(l, 256, label);
    int (*dialog)(int, const uint16_t *, const uint16_t *, const uint16_t *, int *, int, int, int) =
        resident_native(e__show_1NumericInput);
    unsigned mask = resident_lock();
    resident_unlock(mask & ~0xc0u);
    int result = dialog(0, t, s, l, value, 1, low <= 0 ? -2 : low, high);
    resident_unlock(mask);
    clear_native_input();
    if (*value < low) *value = low;
    return result == 5103;
}

static bool question(const char *message) {
    if (!native) return show_msgbox_2b(title, message, "Yes", "No") == 1;
    uint16_t t[32], m[256];
    static const uint16_t yes[] = {'Y','e','s',0}, no[] = {'N','o',0};
    unsigned actions;
    wide(t, 32, title); wide(m, 256, message);
    int result = resident_question(t, m, yes, no, &actions);
    clear_native_input();
    return result == 1;
}

static void message(const char *text) {
    if (quiet) return;
    if (!native) { show_msgbox(title, text); return; }
    uint16_t t[32], m[384];
    static const uint16_t ok[] = {'O','K',0}, back[] = {'B','a','c','k',0};
    unsigned actions;
    wide(t, 32, title); wide(m, 384, text);
    resident_question(t, m, ok, back, &actions);
    clear_native_input();
}

static int menu(const char *subtitle, const char *const *items, unsigned count) {
    char text[256];
    size_t used = 0;
    int choice = 1;
    for (unsigned i = 0; i < count; ++i) {
        int len = snprintf(text + used, sizeof(text) - used, "%u %s%s", i + 1, items[i], i + 1 == count ? "" : "  ");
        if (len < 0 || (size_t)len >= sizeof(text) - used) return -1;
        used += (size_t)len;
    }
    return numeric(subtitle, text, &choice, 1, (int)count) ? choice - 1 : -1;
}

static int file_open(const char *path, int flags) {
    return native ? resident_open(path, flags, 0600) : open(path, flags, 0600);
}
static int file_read(int fd, void *data, unsigned size) {
    return native ? resident_read(fd, data, size) : read(fd, data, size);
}
static int file_write(int fd, const void *data, unsigned size) {
    return native ? resident_write(fd, data, size) : write(fd, data, size);
}
static int file_close(int fd) { return native ? resident_close(fd) : close(fd); }
static int file_remove(const char *path) { return native ? resident_remove(path) : remove(path); }
static int file_errno(void) { return native ? resident_errno() : errno; }

static int read_file(const char *path, char *data, unsigned cap) {
    int fd = file_open(path, O_RDONLY), size = 0;
    if (fd < 0) return file_errno() == ENOENT ? -2 : -1;
    while ((unsigned)size < cap) {
        int n = file_read(fd, data + size, cap - (unsigned)size);
        if (n < 0) { size = -1; break; }
        if (!n) break;
        size += n;
    }
    if (size == (int)cap) {
        char extra;
        if (file_read(fd, &extra, 1) != 0) size = -1;
    }
    if (file_close(fd)) size = -1;
    return size;
}

static bool write_all(int fd, const void *data, unsigned size) {
    unsigned used = 0;
    while (used < size) {
        int n = file_write(fd, (const char *)data + used, size - used);
        if (n <= 0) return false;
        used += (unsigned)n;
    }
    return true;
}

static bool copy_file(const char *from, const char *to) {
    char buf[512];
    int in = file_open(from, O_RDONLY);
    if (in < 0) return false;
    int out = file_open(to, O_WRONLY | O_CREAT | O_TRUNC);
    bool ok = out >= 0;
    int n;
    while (ok && (n = file_read(in, buf, sizeof(buf))) != 0) {
        if (n < 0 || !write_all(out, buf, (unsigned)n)) ok = false;
    }
    if (out >= 0 && file_close(out)) ok = false;
    if (file_close(in)) ok = false;
    return ok;
}

static bool equal_file(const char *path, const char *data, unsigned size) {
    char buf[512];
    unsigned used = 0;
    int fd = file_open(path, O_RDONLY);
    if (fd < 0) return false;
    bool ok = true;
    while (ok && used < size) {
        unsigned count = size - used;
        if (count > sizeof(buf)) count = sizeof(buf);
        int n = file_read(fd, buf, count);
        if (n <= 0 || memcmp(data + used, buf, (size_t)n)) ok = false;
        else used += (unsigned)n;
    }
    if (ok && file_read(fd, buf, 1) != 0) ok = false;
    if (file_close(fd)) ok = false;
    return ok;
}

static bool save_file(const char *path, const char *data, unsigned size) {
    char tmp[280], backup[280];
    if (snprintf(tmp, sizeof(tmp), "%s.themeeditor.tmp", path) >= (int)sizeof(tmp) ||
        snprintf(backup, sizeof(backup), "%s.themeeditor.backup", path) >= (int)sizeof(backup)) return false;
    int existing = file_open(backup, O_RDONLY);
    if (existing >= 0) { file_close(existing); return false; }
    if (file_errno() != ENOENT) return false;
    int fd = file_open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return false;
    bool ok = write_all(fd, data, size);
    if (file_close(fd)) ok = false;
    if (!ok || !equal_file(tmp, data, size)) { file_remove(tmp); return false; }
    existing = file_open(path, O_RDONLY);
    bool had_file = existing >= 0;
    if (had_file) file_close(existing);
    else if (file_errno() != ENOENT) { file_remove(tmp); return false; }
    if (had_file && !copy_file(path, backup)) { file_remove(tmp); return false; }
    if (!copy_file(tmp, path) || !equal_file(path, data, size)) {
        if (had_file) copy_file(backup, path);
        else file_remove(path);
        file_remove(tmp);
        return false;
    }
    file_remove(tmp);
    if (had_file) file_remove(backup);
    return true;
}

static void repaint(void) {
    if (!native) { refresh_osscr(); return; }
    void (*home)(void) = resident_native(e_refresh_homescr);
    void (*browser)(int) = resident_native(e_refresh_docbrowser);
    home(); browser(0x1b5a);
}

static void apply(void) {
    theme_platform_apply(&app->platform, &app->theme);
    app->dirty = true;
    if (native) repaint();
}

static bool save_config(void) {
    char data[512];
    int len = snprintf(data, sizeof(data), "nKits ThemeEditor 1\n%s\n%u %u %u %u\n%s\n",
                       app->path[0] ? app->path : "-", app->index, app->mode, app->patch, app->column, app->program[0] ? app->program : "-");
    return len > 0 && len < (int)sizeof(data) && save_file(CONFIG_PATH, data, (unsigned)len);
}

static bool save_theme(void) {
    unsigned used = 0;
    for (unsigned i = 0; i < app->theme.count; ++i) {
        int n = theme_line(&app->theme, i, file_data + used, sizeof(file_data) - used);
        if (n < 0 || (unsigned)n >= sizeof(file_data) - used) return false;
        used += (unsigned)n;
    }
    if (!save_file(app->path, file_data, used)) return false;
    app->dirty = false;
    if (!save_config()) message("Theme saved. Could not remember its path or editor settings.");
    return true;
}

static bool load_theme(const char *path) {
    int size = read_file(path, file_data, sizeof(file_data));
    if (size < 0 || !theme_decode(&app->theme, file_data, (size_t)size)) {
        message("Cannot open theme: read error, no matching names, or invalid/duplicate RGB entries. Nothing was changed.");
        return false;
    }
    if (path != app->path) strcpy(app->path, path);
    theme_platform_apply(&app->platform, &app->theme);
    app->dirty = false;
    save_config();
    if (native) repaint();
    return true;
}

static bool choose_path(char path[256]) {
    char folder[256] = "/", name[256] = "mytheme.theme";
    unsigned actions;
    int result = resident_save_dialog(folder, name, &actions);
    if (native) clear_native_input();
    if (result != 5103) return false;
    size_t len = strlen(name);
    const char *suffix = len >= 4 && !strcmp(name + len - 4, ".tns") ? "" : ".tns";
    const char *root = !strncmp(folder, "/documents", 10) ? "" : "/documents";
    const char *slash = folder[0] == '/' ? "" : "/";
    const char *join = folder[0] && folder[strlen(folder) - 1] == '/' ? "" : "/";
    int n = snprintf(path, 256, "%s%s%s%s%s%s", root, slash, folder, join, name, suffix);
    if (!name[0] || n < 0 || n >= 256 || strncmp(path, "/documents/", 11)) {
        message("Theme path is too long or outside My Documents.");
        return false;
    }
    return true;
}

static void save_as(void) {
    char path[256], old[256];
    if (!choose_path(path)) return;
    int fd = file_open(path, O_RDONLY);
    if (fd >= 0) {
        file_close(fd);
        if (!question("Replace this existing theme?")) return;
    } else if (file_errno() != ENOENT) { message("Cannot access destination."); return; }
    strcpy(old, app->path);
    strcpy(app->path, path);
    if (!save_theme()) { strcpy(app->path, old); message("Save failed. Theme remains in memory; original file or .themeeditor.backup is kept."); }
}

static void reset_colors(void) {
    if (!question("Restore the original OS colors? The current theme file is not overwritten.")) return;
    memcpy(app->theme.colors, app->original, sizeof(app->original));
    theme_platform_apply(&app->platform, &app->theme);
    app->dirty = false;
    app->path[0] = 0;
    save_config();
    repaint();
}

static void menu_callback(void *button, unsigned command, unsigned event) {
    (void)button; (void)command;
    if (event == 0x14c0bu || event == 0x14c0du) app->request = true;
}

static void about(void) {
    message("ThemeEditor / nKits\n" NKITS_VERSION " (" NKITS_COMMIT ")\n" NKITS_STAGE
            "\nOriginal ThemeEditor: Levak, 2013.\nIndependent GPL-3.0 implementation.");
}

static bool association_remove(void) {
    static const char path[] = "/documents/ndless/ndless.cfg.tns";
    if (!app->program[0]) return true;
    int size = read_file(path, file_data, sizeof(file_data) - 1);
    if (size == -2) return true;
    if (size < 0) return false;
    file_data[size] = 0;
    char target[160];
    snprintf(target, sizeof(target), "ext.theme=%s", app->program);
    unsigned used = 0;
    for (int at = 0; at < size;) {
        int end = at;
        while (end < size && file_data[end] != '\n') ++end;
        int len = end - at;
        if (len && file_data[at + len - 1] == '\r') --len;
        bool ours = (size_t)len == strlen(target) && !memcmp(file_data + at, target, (size_t)len);
        if (end < size) ++end;
        if (!ours) { memmove(file_data + used, file_data + at, (size_t)(end - at)); used += (unsigned)(end - at); }
        at = end;
    }
    return used == (unsigned)size || save_file(path, file_data, used);
}

static void uninstall(void) {
    const char *error;
    if (!question("Restore colors and remove ThemeEditor settings? Theme files are kept. Remove a startup copy yourself.")) return;
    if (!association_remove()) { message("Could not remove the theme association. Installation settings were kept."); return; }
    if (!theme_platform_menu(&app->platform, false, app->menu_action, &error)) { message(error); return; }
    memcpy(app->theme.colors, app->original, sizeof(app->original));
    theme_platform_apply(&app->platform, &app->theme);
    app->dirty = false;
    app->path[0] = 0;
    app->patch = false;
    app->request = false;
    if ((file_remove(CONFIG_PATH) && file_errno() != ENOENT) ||
        (file_remove(ORIGINAL_PATH) && file_errno() != ENOENT)) {
        message("Could not remove settings. Original colors and OS menu were restored.");
        return;
    }
    app->removed = true;
    app->ready = false;
    app->patch = false;
    app->request = false;
    app->dirty = false;
    repaint();
}

static void options(void) {
    static const char *const items[] = {"UI", "Entry", "Reset", "Uninstall", "About"};
    int action = menu("Options", items, 5);
    if (action == 0) {
        static const char *const modes[] = {"Both", "Live", "List"};
        int mode = menu("Interface", modes, 3);
        if (mode >= 0) app->mode = (unsigned)mode;
    } else if (action == 1) {
        const char *error;
        bool enable = question("Enable ThemeEditor in Home > Settings? Disable removes only this entry.");
        if (theme_platform_menu(&app->platform, enable, app->menu_action, &error)) app->patch = enable;
        else message(error);
    } else if (action == 2) reset_colors();
    else if (action == 3) uninstall();
    else if (action == 4) about();
    if (!app->removed) save_config();
}

static void transform(bool random) {
    unsigned first = app->index, count = 1;
    static const char *const scopes[] = {"Selected item", "All items"};
    int scope = menu("Apply to", scopes, 2);
    if (scope < 0) return;
    if (scope == 1) { first = 0; count = app->theme.count; }
    if (random) theme_random(&app->theme, first, count, app->column, &app->seed);
    else theme_invert(&app->theme, first, count, app->column);
    apply();
}

static void native_editor(void) {
    static const char *const home[] = {"Colors", "Files", "Options"};
    static const char *const colors[] = {"RGB", "Item", "Invert", "Random", "Column"};
    static const char *const files[] = {"Open", "Save", "Save as", "New"};
    while (!app->removed) {
        char label[128];
        snprintf(label, sizeof(label), "%u/%u %.30s", app->index + 1, app->theme.count, app->theme.names[app->index]);
        int action = menu(label, home, 3);
        if (action < 0) break;
        if (action == 0) {
            action = menu(label, colors, 5);
            if (action == 0) {
                static const char *const channels[] = {"Red (0-255)", "Green (0-255)", "Blue (0-255)"};
                for (unsigned c = 0; c < 3; ++c) {
                    unsigned shift = (2 - c) * 8;
                    uint32_t *color = &app->theme.colors[app->index][app->column];
                    int value = (*color >> shift) & 255;
                    if (!numeric(label, channels[c], &value, 0, 255)) break;
                    *color = (*color & ~(255u << shift)) | (uint32_t)value << shift;
                    apply();
                }
            } else if (action == 1) {
                int index = (int)app->index + 1;
                if (numeric("Select item", "Item number", &index, 1, (int)app->theme.count)) app->index = (unsigned)index - 1;
            } else if (action == 2 || action == 3) transform(action == 3);
            else if (action == 4) {
                if (app->platform.stride == 1) message("CX II has a single color palette.");
                else app->column ^= 1;
            }
        } else if (action == 1) {
            action = menu("Files", files, 4);
            if (action == 0) {
                char path[256];
                if (app->dirty && !app->path[0]) save_as();
                if (app->dirty && !save_theme()) { message("Save failed. Open was cancelled."); continue; }
                message("Select a .theme.tns file in the chooser. Its button is labelled Save; opening does not overwrite it.");
                if (choose_path(path)) load_theme(path);
            } else if (action == 1) {
                if (!app->path[0]) save_as();
                else if (!save_theme()) message("Save failed. Keep this session open or use Save as.");
            } else if (action == 2) save_as();
            else if (action == 3) {
                if (app->dirty && !app->path[0]) save_as();
                if (app->dirty && !save_theme()) { message("Save failed. New was cancelled."); continue; }
                memcpy(app->theme.colors, app->original, sizeof(app->original));
                app->path[0] = 0;
                apply();
                save_as();
            }
        } else options();
    }
}

static bool tick(void *data) {
    typeof(storage) *state = data;
    return state->ready && !state->foreground && state->request;
}

static void drawn_editor(void);

static void event(void *data, struct s_ns_event *ev) {
    app = data;
    if (!app->ready || app->foreground || !app->request || app->removed) return;
    app->request = false;
    app->foreground = true;
    native = true;
    clear_native_input();
    bool screen_saved = resident_copy_screen(app->screen);
    bool again;
    do {
        if (app->mode == 2) drawn_editor();
        else native_editor();
        again = false;
        if (!app->removed) {
            if (app->dirty && !app->path[0]) save_as();
            if (app->dirty && app->path[0] && !save_theme()) {
                message("Save failed. Use Save as, or restore original colors before exiting.");
                again = true;
            } else if (app->dirty) again = true;
            if (!again && !save_config()) message("Could not remember editor settings. Saved theme files are kept.");
        }
    } while (again);
    if (screen_saved) resident_present_screen(app->screen);
    repaint();
    native = false;
    app->foreground = false;
    ev->type = 0;
}

static void box(int x, int y, int w, int h, uint32_t c) { theme_ui_box(x, y, w, h, c); }
static void text(int x, int y, const char *s, uint32_t c) { theme_ui_text(x, y, s, c); }
static void frame(void) { theme_ui_clear(); }
static void present(void) {
    if (native) resident_present_screen(theme_ui_pixels());
    else lcd_blit(theme_ui_pixels(), SCR_320x240_565);
}

#define BACK 1u
#define ENTER 2u
#define UP 4u
#define DOWN 8u
#define LEFT 16u
#define RIGHT 32u
#define INVERT 64u
#define RANDOM 128u
#define MENU 256u
#define COLUMN 512u

static bool pressed(const t_key *key) {
    return native ? resident_key_down(key) : isKeyPressed(*key);
}

static unsigned matrix_keys(void) {
    unsigned k = 0;
    if (pressed(&KEY_NSPIRE_ESC)) k |= BACK;
    if (pressed(&KEY_NSPIRE_ENTER) || pressed(&KEY_NSPIRE_RET) || pressed(&KEY_NSPIRE_CLICK)) k |= ENTER;
    if (pressed(&KEY_NSPIRE_UP) || pressed(&KEY_NSPIRE_LEFTUP) || pressed(&KEY_NSPIRE_UPRIGHT)) k |= UP;
    if (pressed(&KEY_NSPIRE_DOWN) || pressed(&KEY_NSPIRE_DOWNLEFT) || pressed(&KEY_NSPIRE_RIGHTDOWN)) k |= DOWN;
    if (pressed(&KEY_NSPIRE_LEFT)) k |= LEFT;
    if (pressed(&KEY_NSPIRE_RIGHT)) k |= RIGHT;
    if (pressed(&KEY_NSPIRE_I)) k |= INVERT;
    if (pressed(&KEY_NSPIRE_R)) k |= RANDOM;
    if (pressed(&KEY_NSPIRE_MENU)) k |= MENU;
    if (pressed(&KEY_NSPIRE_TAB)) k |= COLUMN;
    return k;
}

static unsigned keys(void) { return native ? native_held : matrix_keys(); }

static unsigned input(unsigned *previous, unsigned *repeat) {
    unsigned value = keys(), result = 0;
    if (value != *previous) { *repeat = 0; result = value; }
    else if (value && ++*repeat >= 12 && !(*repeat % 2)) result = value & (UP | DOWN | LEFT | RIGHT);
    if (native) {
        result = native_pressed;
        native_pressed = 0;
    }
    *previous = value;
    return result;
}

static int matrix_hex_key(void) {
    static const t_key *const keys[] = {&KEY_NSPIRE_0, &KEY_NSPIRE_1, &KEY_NSPIRE_2, &KEY_NSPIRE_3,
        &KEY_NSPIRE_4, &KEY_NSPIRE_5, &KEY_NSPIRE_6, &KEY_NSPIRE_7, &KEY_NSPIRE_8, &KEY_NSPIRE_9,
        &KEY_NSPIRE_A, &KEY_NSPIRE_B, &KEY_NSPIRE_C, &KEY_NSPIRE_D, &KEY_NSPIRE_E, &KEY_NSPIRE_F};
    for (int i = 0; i < 16; ++i) if (pressed(keys[i])) return i;
    return -1;
}

static int hex_key(void) { return native ? native_hex_pressed : matrix_hex_key(); }

static void pause_ui(void) {
    if (!native) { msleep(30); return; }
    struct s_ns_event ev;
    int (*next)(struct s_ns_event *) = resident_native(e_get_event);
    if (next(&ev) != 1 || (ev.type != 8 && ev.type != 16)) return;
    unsigned action = 0;
    switch (ev.key) {
    case 0x17: case 0xf3: action = UP; break;
    case 0x37: case 0xf4: action = DOWN; break;
    case 0x07: case 0xf1: action = LEFT; break;
    case 0x27: case 0xf2: action = RIGHT; break;
    case 0x36: action = MENU; break;
    case 0xfb: action = ENTER; break;
    }
    switch (ev.ascii) {
    case 27: action = BACK; break;
    case 13: action = ENTER; break;
    case 9: action = COLUMN; break;
    case 'i': case 'I': action = INVERT; break;
    case 'r': case 'R': action = RANDOM; break;
    }
    if (action & (UP | DOWN | LEFT | RIGHT))
        native_shifted = ev.key >= 0xf1 && ev.key <= 0xf4;
    if (ev.type == 8) {
        if (!(native_held & action) || (action & (UP | DOWN | LEFT | RIGHT)))
            native_pressed |= action;
        native_held |= action;
    } else {
        if (!(native_held & action)) native_pressed |= action;
        native_held &= ~action;
    }
    int digit = ev.ascii >= '0' && ev.ascii <= '9' ? ev.ascii - '0' :
                ev.ascii >= 'a' && ev.ascii <= 'f' ? ev.ascii - 'a' + 10 :
                ev.ascii >= 'A' && ev.ascii <= 'F' ? ev.ascii - 'A' + 10 : -1;
    if (digit >= 0) {
        unsigned bit = 1u << digit;
        if (!(native_hex_held & bit)) native_hex_pressed = digit;
        if (ev.type == 8) native_hex_held |= bit;
        else native_hex_held &= ~bit;
    }
}

static void release_keys(void) {
    if (!native) { wait_no_key_pressed(); return; }
    while (native_held || native_hex_held || matrix_keys() || matrix_hex_key() >= 0) pause_ui();
    clear_native_input();
}

static void picker(void) {
    uint32_t old = app->theme.colors[app->index][app->column], value = old;
    unsigned channel = 0, previous = 0, repeat = 0;
    bool hex_held = false;
    release_keys();
    for (;;) {
        unsigned key = input(&previous, &repeat);
        unsigned shift = (2 - channel) * 8, byte = (value >> shift) & 255;
        int digit = hex_key();
        if (native) native_hex_pressed = -1;
        if (key & BACK) break;
        if (key & ENTER) { app->theme.colors[app->index][app->column] = value; apply(); break; }
        if (key & LEFT) channel = (channel + 2) % 3;
        if (key & RIGHT) channel = (channel + 1) % 3;
        unsigned step = (native ? native_shifted : pressed(&KEY_NSPIRE_SHIFT)) ? 10 : 1;
        if (key & UP) byte = byte + step < 255 ? byte + step : 255;
        if (key & DOWN) byte = byte > step ? byte - step : 0;
        bool typed = digit >= 0 && (native || !hex_held);
        if (typed) byte = ((byte << 4) | (unsigned)digit) & 255;
        if ((key & (UP | DOWN)) || typed) value = (value & ~(255u << shift)) | byte << shift;
        hex_held = digit >= 0;
        if (key & INVERT) value ^= 0xffffff;
        if (key & RANDOM) { app->seed = app->seed * 1664525u + 1013904223u; value = app->seed & 0xffffff; }
        frame();
        text(8, 8, app->theme.names[app->index], 0);
        text(24, 36, "R             G             B", 0);
        for (unsigned c = 0; c < 3; ++c) {
            unsigned b = (value >> ((2 - c) * 8)) & 255;
            char line[24];
            box(24 + (int)c * 72, 64, 42, 124, c == channel ? 0x206090 : 0xc0c0c0);
            box(28 + (int)c * 72, 184 - (int)b * 116 / 255, 34, (int)b * 116 / 255, 0x808080);
            snprintf(line, sizeof(line), "%02X / %u", b, b);
            text(22 + (int)c * 72, 192, line, 0);
        }
        box(250, 54, 60, 60, old); box(250, 134, 60, 60, value);
        text(250, 38, "Old", 0); text(250, 118, "New", 0);
        text(8, 214, "Arrows: RGB  0-9 A-F: hex  I/R: invert/random", 0);
        text(8, 228, "Enter: apply    Esc: cancel", 0);
        present(); pause_ui();
    }
    release_keys();
}

static void drawn_editor(void) {
    unsigned previous = 0, repeat = 0, offset = 0;
    release_keys();
    while (!app->removed) {
        unsigned key = input(&previous, &repeat);
        unsigned step = (native ? native_shifted : pressed(&KEY_NSPIRE_SHIFT)) ? 10 : 1;
        if (key & BACK) break;
        if (key & UP) app->index = (app->index + app->theme.count - step) % app->theme.count;
        if (key & DOWN) app->index = (app->index + step) % app->theme.count;
        if ((key & LEFT) && offset) --offset;
        if ((key & RIGHT) && offset < 40) ++offset;
        if (key & COLUMN) { if (app->platform.stride == 2) app->column ^= 1; }
        if (key & ENTER) picker();
        if (key & INVERT) { theme_invert(&app->theme, 0, app->theme.count, app->column); apply(); }
        if (key & RANDOM) { theme_random(&app->theme, 0, app->theme.count, app->column, &app->seed); apply(); }
        if (key & MENU) { native_editor(); release_keys(); previous = repeat = 0; }
        frame();
        char line[128];
        snprintf(line, sizeof(line), "ThemeEditor   %u/%u   %s%s", app->index + 1, app->theme.count,
                 app->column ? "Color" : "B&W", app->dirty ? " *" : "");
        text(6, 4, line, 0);
        unsigned first = app->index >= 11 ? app->index - 10 : 0;
        for (unsigned i = first; i < app->theme.count && i < first + 11; ++i) {
            int y = 24 + (int)(i - first) * 15;
            bool selected = i == app->index;
            if (selected) box(0, y - 1, 292, 15, 0x205a90);
            const char *name = app->theme.names[i];
            size_t len = strlen(name);
            snprintf(line, sizeof(line), "%.48s", name + (offset < len ? offset : len));
            text(6, y, line, selected ? 0xffffff : 0);
            box(298, y, 16, 12, app->theme.colors[i][app->column]);
        }
        snprintf(line, sizeof(line), "RGB #%06lX   %.38s", (unsigned long)app->theme.colors[app->index][app->column], app->path[0] ? app->path : "Unsaved theme");
        text(6, 194, line, 0);
        text(6, 210, "Enter: RGB  I/R: invert/random ALL  Tab: column", 0);
        text(6, 226, "Menu: files/options/live  Esc: save + exit", 0);
        present(); pause_ui();
    }
    release_keys();
}

static bool originals(void) {
    uint32_t header[4] = {0x6e54484du, app->platform.marker, app->theme.count, theme_hash(&app->theme)};
    unsigned size = sizeof(header) + sizeof(app->original);
    int n = read_file(ORIGINAL_PATH, file_data, size);
    if (n == -2) {
        memcpy(app->original, app->theme.colors, sizeof(app->original));
        memcpy(file_data, header, sizeof(header));
        memcpy(file_data + sizeof(header), app->original, sizeof(app->original));
        return save_file(ORIGINAL_PATH, file_data, size);
    }
    if (n != (int)size || memcmp(file_data, header, sizeof(header))) return false;
    memcpy(app->original, file_data + sizeof(header), sizeof(app->original));
    return true;
}

static void load_config(void) {
    int n = read_file(CONFIG_PATH, file_data, 511);
    if (n < 0) return;
    file_data[n] = 0;
    unsigned index, mode, patch, column;
    char path[256], program[128];
    if (sscanf(file_data, "nKits ThemeEditor 1\n%255[^\n]\n%u %u %u %u\n%127[^\n]", path,
               &index, &mode, &patch, &column, program) == 6 && index < app->theme.count &&
        mode <= 2 && patch <= 1 && column <= 1 &&
        (!strncmp(path, "/documents/", 11) || !strcmp(path, "-"))) {
        strcpy(app->path, strcmp(path, "-") ? path : "");
        strcpy(app->program, strcmp(program, "-") ? program : "");
        app->index = index; app->mode = mode; app->patch = patch;
        app->column = app->platform.stride == 1 ? 1 : column;
    }
}

static int finish(bool fresh, int status) {
    app->foreground = false;
    quiet = false;
    if (fresh) _exit(status);
    return status;
}

int app_run(int argc, char **argv) {
    bool fresh;
    const char *error;
    native = false;
    quiet = nl_isstartup();
    app = resident_install(0x6e54484du, NKITS_BUILD_ID, &storage, sizeof(storage), tick, event, &fresh, &error);
    if (!app) { if (!quiet) show_msgbox(title, error); quiet = false; return 1; }
    if (fresh) app->menu_action = menu_callback;
    app->foreground = true;
    bool initialized = !app->ready || app->removed;
    if (initialized) {
        app->ready = false;
        if (!theme_platform_open(&app->platform, &app->theme, &error)) { message(error); return finish(fresh, 1); }
        app->column = 1; app->index = app->mode = 0; app->patch = true;
        strcpy(app->path, "/documents/mytheme.theme.tns");
        if (!originals()) {
            message("Cannot preserve original colors. Check themeeditor.original and its .themeeditor.backup file. No theme was applied.");
            return finish(fresh, 1);
        }
        if (!app->screen) app->screen = malloc(320 * 240 * sizeof(*app->screen));
        if (!app->screen) { message("Cannot preserve the OS display: not enough memory."); return finish(fresh, 1); }
        load_config();
        app->ready = true;
        app->removed = false;
        app->seed = resident_seconds() ^ app->platform.marker;
    }
    bool uninstall_name = false;
    if (argc > 0 && argv[0]) {
        const char *base = strrchr(argv[0], '/');
        base = base ? base + 1 : argv[0];
        uninstall_name = !strcmp(base, "uninstall.tns");
        if (!uninstall_name) {
            size_t len = strlen(base);
            if (len > 4 && len - 4 < sizeof(app->program)) {
                char candidate[128];
                memcpy(candidate, base, len - 4); candidate[len - 4] = 0;
                cfg_register_fileext("theme", candidate);
                cfg_open();
                const char *registered = cfg_get("ext.theme");
                if (registered && !strcmp(registered, candidate)) strcpy(app->program, candidate);
                else if (!registered || strcmp(registered, app->program)) app->program[0] = 0;
                cfg_close();
            }
        }
    }
    if (uninstall_name) { uninstall(); return finish(fresh, 0); }
    if (argc > 1 && argv[1] && strlen(argv[1]) < sizeof(app->path)) load_theme(argv[1]);
    else if ((fresh || initialized) && app->path[0]) {
        int fd = file_open(app->path, O_RDONLY);
        if (fd >= 0) { file_close(fd); load_theme(app->path); }
    }
    if (app->patch && !theme_platform_menu(&app->platform, true, app->menu_action, &error)) message(error);
    if (quiet) { save_config(); return finish(fresh, 0); }
    bool screen_saved = resident_copy_screen(app->screen);
    bool again;
    do {
        if (app->mode == 1) native_editor();
        else drawn_editor();
        again = false;
        if (!app->removed) {
            if (app->dirty && !app->path[0]) save_as();
            if (app->dirty && app->path[0] && !save_theme()) {
                message("Save failed. Colors remain in memory. Use Save as, or restore original colors before exiting.");
                again = true;
            } else if (app->dirty) again = true;
            if (!again && !save_config()) message("Could not remember editor settings. Saved theme files are kept.");
        }
    } while (again);
    app->foreground = false;
    wait_no_key_pressed();
    if (screen_saved) lcd_blit(app->screen, SCR_320x240_565);
    refresh_osscr();
    return finish(fresh, 0);
}
