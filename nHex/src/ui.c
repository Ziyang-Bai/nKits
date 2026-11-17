#include "ui.h"
#include "version.h"

#include <libndls.h>
#include "common/draw.h"
#include <dirent.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GRID_FONT SerifRegular7
#define ROW_Y 66
static int row_height;
static int digit_width, ascii_width;
static int hex_x, hex_step, ascii_x;
static Gc graphics;
static unsigned interrupt_state;
static const UiTheme *theme = &ui_themes[0];

static unsigned lock_interrupts(void) {
    unsigned mask;
    __asm__ volatile("mrs %0, cpsr" : "=r"(mask));
    return mask & 0xc0u;
}

static void restore_interrupts(unsigned mask) {
    unsigned value;
    __asm__ volatile("mrs %0, cpsr" : "=r"(value));
    value = (value & ~0xc0u) | mask;
    __asm__ volatile("msr cpsr_c, %0" :: "r"(value) : "memory", "cc");
}

static void begin_screen(const char *title) {
    interrupt_state = lock_interrupts();
    gui_gc_begin(graphics);
    gui_gc_clipRect(graphics, 0, 0, UI_WIDTH, UI_HEIGHT, GC_CRO_SET);
    ui_draw_header(graphics, theme, title);
}

static void finish_screen(const char *hint) {
    if (hint) ui_draw_footer(graphics, theme, hint);
    gui_gc_finish(graphics);
    gui_gc_blit_to_screen(graphics);
    restore_interrupts(interrupt_state);
}

static void draw_cells(int x, int y, const char *text, int width, int bg, int fg) {
    char cell[2] = {0, 0};
    while (*text) {
        cell[0] = *text++;
        ui_fill(graphics, x, y, width, row_height, bg);
        ui_draw_text(graphics, cell, x, y, GRID_FONT, fg);
        x += width;
    }
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool ui_init(Ui *ui) {
    unsigned mask;
    unsigned value;
    Gc source;
    if (!ui) return false;
    mask = lock_interrupts();
    source = gui_gc_global_GC();
    graphics = source ? gui_gc_copy(source, UI_WIDTH, UI_HEIGHT) : NULL;
    if (!graphics) { restore_interrupts(mask); return false; }
    gui_gc_setRegion(graphics, 0, 0, UI_WIDTH, UI_HEIGHT, 0, 0, UI_WIDTH, UI_HEIGHT);
    gui_gc_begin(graphics);
    row_height = ui_line_height(graphics, GRID_FONT);
    digit_width = ascii_width = 1;
    for (value = 32; value <= 126; ++value) {
        uint16_t cell[2] = {value, 0};
        int width = gui_gc_getStringWidth(graphics, GRID_FONT, (char *)cell, 0, 1);
        if (width > ascii_width) ascii_width = width;
        if (((value >= '0' && value <= '9') || (value >= 'A' && value <= 'F')) &&
            width > digit_width) digit_width = width;
    }
    hex_x = 8 * digit_width + 8;
    ascii_x = UI_WIDTH - 4 - NHEX_BYTES_PER_ROW * ascii_width;
    hex_step = (ascii_x - hex_x - 4) / NHEX_BYTES_PER_ROW;
    gui_gc_finish(graphics);
    restore_interrupts(mask);
    ui->initialized = true;
    return true;
}

void ui_destroy(Ui *ui) {
    if (ui && ui->initialized) {
        unsigned mask = lock_interrupts();
        gui_gc_free(graphics);
        graphics = NULL;
        restore_interrupts(mask);
        ui->initialized = false;
    }
}

static void draw_nibble_indicator(Editor *editor, int y, unsigned column) {
    int x;
    if (editor->mode != EDIT_MODE_HEX) {
        return;
    }
    x = hex_x + (int)column * hex_step + (editor->high_nibble ? 0 : digit_width);
    ui_fill(graphics, x, y + row_height - 2, digit_width, 2, theme->accent);
}

void ui_draw(Ui *ui, Editor *editor, const char *message) {
    FileBuffer *buffer;
    uint64_t size;
    uint8_t current = 0;
    bool have_current;
    char text[128];
    unsigned row;

    if (!ui || !ui->initialized || !editor) {
        return;
    }
    buffer = editor->buffer;
    editor->visible_rows = (212 - ROW_Y) / row_height;
    editor_ensure_visible(editor);
    size = file_buffer_size(buffer);
    have_current = size != 0 && editor_current_byte(editor, &current);

    snprintf(text, sizeof(text), "nHex - %.28s%s", base_name(file_buffer_path(buffer)),
             file_buffer_modified(buffer) ? " *" : "");
    begin_screen(text);

    snprintf(text, sizeof(text), "Size:%llu  %s %s",
             (unsigned long long)size, editor->mode == EDIT_MODE_HEX ? "HEX" : "ASCII",
             editor->mode == EDIT_MODE_HEX ? (editor->high_nibble ? "HIGH" : "LOW") : "");
    ui_draw_text(graphics, text, 8, 24, Regular9, theme->muted);
    if (have_current)
        snprintf(text, sizeof(text), "Off:%016llX  Byte:%02X",
                 (unsigned long long)editor->cursor_offset, current);
    else
        snprintf(text, sizeof(text), "Off:%016llX  Byte:--",
                 (unsigned long long)editor->cursor_offset);
    ui_draw_text(graphics, text, 8, 38, Regular9, theme->muted);
    ui_draw_text(graphics, "OFFSET", 4, 52, Regular9, theme->muted);
    for (row = 0; row < NHEX_BYTES_PER_ROW; ++row) {
        snprintf(text, sizeof(text), "%X", row);
        draw_cells(hex_x + (int)row * hex_step, 52, text, digit_width,
                   theme->bg, theme->muted);
    }
    ui_draw_text(graphics, "ASCII", ascii_x, 52, Regular9, theme->muted);

    for (row = 0; row < editor->visible_rows; ++row) {
        uint64_t row_offset = editor->top_offset + (uint64_t)row * NHEX_BYTES_PER_ROW;
        int y = ROW_Y + (int)row * row_height;
        unsigned column;
        if (row_offset >= size) {
            break;
        }
        snprintf(text, sizeof(text), "%08llX", (unsigned long long)row_offset);
        draw_cells(4, y, text, digit_width, theme->bg, theme->muted);

        for (column = 0; column < NHEX_BYTES_PER_ROW; ++column) {
            uint64_t offset = row_offset + column;
            uint8_t value;
            char ascii[2];
            bool cursor;
            int hex_background = theme->bg;
            int hex_foreground = theme->fg;
            int ascii_background = theme->bg;
            int ascii_foreground = theme->fg;

            if (offset >= size || !file_buffer_read(buffer, offset, &value)) {
                break;
            }
            cursor = offset == editor->cursor_offset;
            if (cursor) {
                if (editor->mode == EDIT_MODE_HEX) {
                    hex_background = theme->highlight_bg;
                    hex_foreground = theme->highlight_fg;
                    ascii_background = 0xeeeeee;
                } else {
                    ascii_background = theme->highlight_bg;
                    ascii_foreground = theme->highlight_fg;
                    hex_background = 0xeeeeee;
                }
            }

            snprintf(text, sizeof(text), "%02X", value);
            draw_cells(hex_x + (int)column * hex_step, y, text,
                       digit_width, hex_background, hex_foreground);
            ascii[0] = value >= 0x20 && value <= 0x7e ? (char)value : '.';
            ascii[1] = '\0';
            draw_cells(ascii_x + (int)column * ascii_width, y, ascii,
                       ascii_width, ascii_background, ascii_foreground);
            if (cursor) {
                draw_nibble_indicator(editor, y, column);
            }
        }
    }

    finish_screen(message && message[0] ? message : "Tab mode  Ctrl+S save  Ctrl+O open");
}

#define BROWSER_MAX_ITEMS 128
#define BROWSER_NAME_MAX 128
#define BROWSER_VISIBLE_ROWS 8

typedef struct {
    char name[BROWSER_NAME_MAX];
    bool is_directory;
} BrowserItem;

static bool join_path(char *output, size_t capacity, const char *directory, const char *name) {
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    bool needs_slash = directory_length > 0 && directory[directory_length - 1] != '/';
    size_t total = directory_length + (needs_slash ? 1u : 0u) + name_length;
    if (total >= capacity) {
        return false;
    }
    memcpy(output, directory, directory_length);
    if (needs_slash) {
        output[directory_length++] = '/';
    }
    memcpy(output + directory_length, name, name_length + 1);
    return true;
}

static int compare_browser_items(const void *left, const void *right) {
    const BrowserItem *a = (const BrowserItem *)left;
    const BrowserItem *b = (const BrowserItem *)right;
    if (a->is_directory != b->is_directory) {
        return b->is_directory - a->is_directory;
    }
    return strcmp(a->name, b->name);
}

static bool load_browser_directory(const char *directory, BrowserItem *items, size_t *count) {
    DIR *stream = opendir(directory);
    struct dirent *entry;
    *count = 0;
    if (!stream) {
        return false;
    }
    while (*count < BROWSER_MAX_ITEMS && (entry = readdir(stream)) != NULL) {
        char full_path[NHEX_PATH_MAX];
        struct stat status;
        size_t length;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !join_path(full_path, sizeof(full_path), directory, entry->d_name) ||
            stat(full_path, &status) != 0) {
            continue;
        }
        length = strlen(entry->d_name);
        if (length >= BROWSER_NAME_MAX) {
            continue;
        }
        memcpy(items[*count].name, entry->d_name, length + 1);
        items[*count].is_directory = S_ISDIR(status.st_mode);
        (*count)++;
    }
    closedir(stream);
    qsort(items, *count, sizeof(*items), compare_browser_items);
    return true;
}

static int wait_browser_key(void) {
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_UP)) return 1;
        if (isKeyPressed(KEY_NSPIRE_DOWN)) return 2;
        if (isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) ||
            isKeyPressed(KEY_NSPIRE_CLICK)) return 3;
        if (isKeyPressed(KEY_NSPIRE_ESC)) return 4;
        msleep(10);
    }
}

static void parent_directory(char *path, const char *root) {
    char *slash;
    if (!strcmp(path, root)) {
        return;
    }
    slash = strrchr(path, '/');
    if (!slash || slash == path || (size_t)(slash - path) < strlen(root)) {
        strcpy(path, root);
    } else {
        *slash = '\0';
    }
}

static bool initial_directory(char output[NHEX_PATH_MAX], const char *initial_path) {
    const char *documents = get_documents_dir();
    struct stat status;
    size_t length;
    if (!initial_path || !initial_path[0] || stat(initial_path, &status) != 0) {
        initial_path = documents;
        if (stat(initial_path, &status) != 0) {
            return false;
        }
    }
    length = strlen(initial_path);
    if (length >= NHEX_PATH_MAX) {
        return false;
    }
    memcpy(output, initial_path, length + 1);
    if (!S_ISDIR(status.st_mode)) {
        parent_directory(output, documents);
    }
    return true;
}

static char *browse_for_file(Ui *ui, const char *initial_path) {
    BrowserItem *items;
    char directory[NHEX_PATH_MAX];
    const char *documents = get_documents_dir();
    size_t count = 0;
    size_t selected = 0;
    size_t top = 0;
    char *result = NULL;

    if (!ui || !ui->initialized || !initial_directory(directory, initial_path)) {
        return NULL;
    }
    items = (BrowserItem *)malloc(BROWSER_MAX_ITEMS * sizeof(*items));
    if (!items) {
        ui_message("Open failed", "Not enough memory for file browser");
        return NULL;
    }
    for (;;) {
        char text[160];
        size_t row;
        int key;
        if (!load_browser_directory(directory, items, &count)) {
            ui_message("Open failed", "Cannot read this directory");
            break;
        }
        if (selected >= count) selected = count ? count - 1 : 0;
        if (top > selected) top = selected;
        if (selected >= top + BROWSER_VISIBLE_ROWS) {
            top = selected - BROWSER_VISIBLE_ROWS + 1;
        }

        snprintf(text, sizeof(text), "Open: %.30s", base_name(directory));
        begin_screen(text);
        ui_draw_text(graphics, directory, 8, 26, Regular9, theme->muted);
        for (row = 0; row < BROWSER_VISIBLE_ROWS && top + row < count; ++row) {
            size_t index = top + row;
            snprintf(text, sizeof(text), "%s%s",
                     items[index].is_directory ? "[D] " : "    ", items[index].name);
            ui_draw_row(graphics, theme, 48 + (int)row * 20, text, index == selected);
        }
        if (!count) ui_draw_text(graphics, "(empty directory)", 10, 48, Regular10, theme->muted);
        finish_screen("Enter open/enter dir  Esc up/back");

        key = wait_browser_key();
        wait_no_key_pressed();
        if (key == 1 && count) {
            selected = selected > 0 ? selected - 1 : count - 1;
        } else if (key == 2 && count) {
            selected = selected + 1 < count ? selected + 1 : 0;
        } else if (key == 3 && count) {
            char full_path[NHEX_PATH_MAX];
            if (!join_path(full_path, sizeof(full_path), directory, items[selected].name)) {
                ui_message("Open failed", "Selected path is too long");
            } else if (items[selected].is_directory) {
                strcpy(directory, full_path);
                selected = top = 0;
            } else {
                size_t length = strlen(full_path);
                result = (char *)malloc(length + 1);
                if (result) {
                    memcpy(result, full_path, length + 1);
                } else {
                    ui_message("Open failed", "Not enough memory for selected path");
                }
                break;
            }
        } else if (key == 4) {
            if (!strcmp(directory, documents)) {
                break;
            }
            parent_directory(directory, documents);
            selected = top = 0;
        }
    }
    free(items);
    return result;
}

void ui_message(const char *title, const char *message) {
    begin_screen(title);
    ui_draw_wrapped(graphics, message, 10, 40, 300, Regular10, theme->fg);
    finish_screen("Enter/Esc back");
    wait_no_key_pressed();
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) ||
            isKeyPressed(KEY_NSPIRE_CLICK) || isKeyPressed(KEY_NSPIRE_ESC)) break;
        msleep(10);
    }
    wait_no_key_pressed();
}

static void draw_save_confirmation(unsigned selected) {
    static const char *options[] = {
        "1  Save",
        "2  Discard changes",
        "3  Cancel"
    };
    unsigned index;
    begin_screen("nHex - Save changes?");
    ui_draw_text(graphics, "This file has unsaved modifications.", 10, 36, Regular9, theme->muted);
    for (index = 0; index < sizeof(options) / sizeof(options[0]); ++index)
        ui_draw_row(graphics, theme, 72 + (int)index * 32, options[index], index == selected);
    finish_screen("Up/Down choose  Enter select  Esc cancel");
}

static int wait_save_confirmation_key(void) {
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_UP)) return -1;
        if (isKeyPressed(KEY_NSPIRE_DOWN)) return -2;
        if (isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) ||
            isKeyPressed(KEY_NSPIRE_CLICK)) return 100;
        if (isKeyPressed(KEY_NSPIRE_ESC)) return 3;
        if (isKeyPressed(KEY_NSPIRE_1)) return 1;
        if (isKeyPressed(KEY_NSPIRE_2)) return 2;
        if (isKeyPressed(KEY_NSPIRE_3)) return 3;
        msleep(10);
    }
}

int ui_confirm_save(Ui *ui) {
    unsigned selected = 0;
    if (!ui || !ui->initialized) {
        return 3;
    }
    for (;;) {
        int action;
        draw_save_confirmation(selected);
        action = wait_save_confirmation_key();
        wait_no_key_pressed();
        if (action == -1) {
            selected = selected > 0 ? selected - 1 : 2;
        } else if (action == -2) {
            selected = selected < 2 ? selected + 1 : 0;
        } else {
            return action == 100 ? (int)selected + 1 : action;
        }
    }
}

char *ui_prompt(const char *title, const char *label, const char *initial) {
    char *value = NULL;
    if (show_msg_user_input(title, label, initial ? initial : "", &value) < 0) {
        free(value);
        return NULL;
    }
    return value;
}

static void show_information_pages(Ui *ui, bool about);

static void draw_open_menu(unsigned selected) {
    static const char *options[] = {
        "1  Browse files", "2  Enter path", "3  Create file",
        "4  Help", "5  About nHex", "6  Cancel"
    };
    unsigned index;
    begin_screen("nHex - Open");
    for (index = 0; index < sizeof(options) / sizeof(options[0]); ++index)
        ui_draw_row(graphics, theme, 40 + (int)index * 26, options[index], index == selected);
    finish_screen("Up/Down choose  Enter select  Esc cancel");
}

static int wait_open_menu_key(void) {
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_UP)) return -1;
        if (isKeyPressed(KEY_NSPIRE_DOWN)) return -2;
        if (isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) ||
            isKeyPressed(KEY_NSPIRE_CLICK)) return 100;
        if (isKeyPressed(KEY_NSPIRE_ESC)) return 0;
        if (isKeyPressed(KEY_NSPIRE_1)) return 1;
        if (isKeyPressed(KEY_NSPIRE_2)) return 2;
        if (isKeyPressed(KEY_NSPIRE_3)) return 3;
        if (isKeyPressed(KEY_NSPIRE_4)) return 4;
        if (isKeyPressed(KEY_NSPIRE_5)) return 5;
        if (isKeyPressed(KEY_NSPIRE_6)) return 6;
        msleep(10);
    }
}

char *ui_choose_file(Ui *ui, const char *initial_path, bool *create) {
    unsigned selected = 0;
    if (create) *create = false;
    for (;;) {
        char *path;
        int action;
        draw_open_menu(selected);
        action = wait_open_menu_key();
        wait_no_key_pressed();
        if (action == -1) {
            selected = selected > 0 ? selected - 1 : 5;
            continue;
        }
        if (action == -2) {
            selected = selected < 5 ? selected + 1 : 0;
            continue;
        }
        if (action == 100) action = (int)selected + 1;
        if (action == 1) {
            path = browse_for_file(ui, initial_path);
            if (path) return path;
        } else if (action == 2) {
            path = ui_prompt("nHex - Open", "File path",
                             initial_path && initial_path[0] ? initial_path : get_documents_dir());
            if (path) return path;
        } else if (action == 3) {
            char suggested[NHEX_PATH_MAX];
            snprintf(suggested, sizeof(suggested), "%s/new.bin", get_documents_dir());
            path = ui_prompt("nHex - New File", "New file path", suggested);
            if (path) {
                if (create) *create = true;
                return path;
            }
        } else if (action == 4) {
            show_information_pages(ui, false);
        } else if (action == 5) {
            show_information_pages(ui, true);
        } else {
            return NULL;
        }
    }
}

static int wait_information_key(void) {
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_LEFT)) return -1;
        if (isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_TAB)) return 1;
        if (isKeyPressed(KEY_NSPIRE_ESC) || isKeyPressed(KEY_NSPIRE_MENU) ||
            isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) ||
            isKeyPressed(KEY_NSPIRE_CLICK)) return 0;
        msleep(10);
    }
}

static void draw_information_page(unsigned page) {
    static const char *help_lines[] = {
        "Navigation",
        "Arrows        Move cursor",
        "Shift+Up/Dn   Page up/down",
        "Ctrl+Left     Row start",
        "Ctrl+Right    Row end",
        "Editing",
        "Tab           HEX / ASCII",
        "0-9 A-F       Enter hex digit",
        "ASCII keys    Enter character",
        "Files",
        "Ctrl+N        Create file",
        "Ctrl+E        Extend size",
        "Ctrl+O        Open (Browse/Path)",
        "Ctrl+S        Save",
        "Ctrl+Shift+S  Save As",
        "Ctrl+R        Reload",
        "Ctrl+G        Go To offset",
        "Menu          Help / About",
        "Esc           Cancel / Exit"
    };
    static const char *about_lines[] = {
        "nHex",
        "Native byte-oriented hex editor",
        "",
        "TI-Nspire CX / CX II",
        "Built for Ndless",
        "Version " NKITS_VERSION,
        NKITS_STAGE,
        "Commit " NKITS_COMMIT,
        "",
        "Copyright (c) 2026 Ziyang-Bai.",
        "GNU GPL v3 licensed.",
        "No warranty; see LICENSE."
    };
    bool about = page == 2;
    const char *const *lines = about ? about_lines : help_lines;
    size_t count = about ? sizeof(about_lines) / sizeof(about_lines[0])
                         : sizeof(help_lines) / sizeof(help_lines[0]);
    size_t index, start = about ? 0 : page * 10;
    char title[48];
    snprintf(title, sizeof(title), "%s  %u/3", about ? "About nHex" : "nHex Help", page + 1);
    begin_screen(title);
    for (index = start; index < count && index < start + (about ? 12 : 10); ++index)
        ui_draw_text(graphics, lines[index], 10, 32 + (int)(index - start) * 14, Regular9, theme->fg);
    finish_screen("Left/Right pages  Enter/Esc back");
}

static void show_information_pages(Ui *ui, bool about) {
    unsigned page = about ? 2 : 0;
    if (!ui || !ui->initialized) {
        return;
    }
    wait_no_key_pressed();
    for (;;) {
        int direction;
        draw_information_page(page);
        direction = wait_information_key();
        wait_no_key_pressed();
        if (direction == 0) {
            return;
        }
        page = (page + (direction < 0 ? 2 : 1)) % 3;
    }
}

void ui_show_menu(Ui *ui) {
    unsigned selected = 0;
    static const char *items[] = {"Help", "About", "Cancel"};
    if (!ui || !ui->initialized) return;
    wait_no_key_pressed();
    for (;;) {
        unsigned index;
        int key;
        begin_screen("nHex - Information");
        for (index = 0; index < 3; ++index)
            ui_draw_row(graphics, theme, 48 + (int)index * 28, items[index], index == selected);
        finish_screen("Up/Down choose  Enter select  Esc back");
        key = wait_browser_key();
        wait_no_key_pressed();
        if (key == 1) selected = selected ? selected - 1 : 2;
        else if (key == 2) selected = (selected + 1) % 3;
        else if (key == 4) return;
        else if (key == 3) {
            if (selected == 2) return;
            show_information_pages(ui, selected == 1);
        }
    }
}
