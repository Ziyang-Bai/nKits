#include "ui.h"

#include <libndls.h>
#include <nspireio/nspireio.h>
#include <dirent.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEXT_X 36
#define BODY_Y 32
#define BODY_ROW_HEIGHT 10
#define BROWSER_MAX_ITEMS 128
#define BROWSER_NAME_MAX 128
#define BROWSER_VISIBLE_ROWS 20

static void draw_text(int x, int y, const char *text, int background, int foreground) {
    nio_vram_pixel_puts(x, y, text, background, foreground);
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool ui_init(Ui *ui) {
    if (!ui || !nio_scrbuf_init()) return false;
    ui->initialized = true;
    nio_scrbuf_clear();
    nio_vram_draw();
    return true;
}

void ui_destroy(Ui *ui) {
    if (ui && ui->initialized) {
        nio_scrbuf_free();
        ui->initialized = false;
    }
}

static size_t line_number_at(const Editor *editor, size_t offset) {
    size_t line = 1;
    size_t index;
    uint8_t value;
    for (index = 0; index < offset; ++index)
        if (text_buffer_get(editor->buffer, index, &value) && value == '\n') line++;
    return line;
}

static void draw_editor_row(Editor *editor, size_t line_offset, size_t line_number, int y) {
    size_t line_end = editor_line_end(editor, line_offset);
    size_t cursor_line = editor_line_start(editor, editor->cursor_offset);
    size_t cursor_column = editor->cursor_offset - cursor_line;
    size_t column;
    char number[8];

    snprintf(number, sizeof(number), "%5lu", (unsigned long)line_number);
    draw_text(2, y, number, NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    for (column = 0; column < NEDIT_VISIBLE_COLUMNS; ++column) {
        size_t logical_column = editor->left_column + column;
        size_t offset = line_offset + logical_column;
        bool cursor = cursor_line == line_offset && cursor_column == logical_column;
        int background = cursor ? NIO_COLOR_BLUE : NIO_COLOR_WHITE;
        int foreground = cursor ? NIO_COLOR_WHITE : NIO_COLOR_BLACK;
        char cell[2] = {' ', '\0'};
        uint8_t value;
        if (offset < line_end && text_buffer_get(editor->buffer, offset, &value)) {
            cell[0] = value >= 0x20 && value <= 0x7e ? (char)value :
                      value == '\t' ? ' ' : '.';
        }
        draw_text(TEXT_X + (int)column * 6, y, cell, background, foreground);
    }
}

void ui_draw(Ui *ui, Editor *editor, const char *message,
             bool caps_lock, bool sticky_keys) {
    TextBuffer *buffer;
    size_t size;
    size_t line_offset;
    size_t line_number;
    unsigned row;
    char text[128];
    bool done = false;

    if (!ui || !ui->initialized || !editor) return;
    buffer = editor->buffer;
    size = text_buffer_size(buffer);
    nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);

    snprintf(text, sizeof(text), "nEdit - %.30s", base_name(text_buffer_path(buffer)));
    draw_text(2, 1, text, NIO_COLOR_WHITE, NIO_COLOR_BLACK);
    if (sticky_keys)
        draw_text(226, 1, "STKY", NIO_COLOR_WHITE, NIO_COLOR_BLUE);
    if (caps_lock)
        draw_text(258, 1, "CAPS", NIO_COLOR_WHITE, NIO_COLOR_BLUE);
    if (text_buffer_modified(buffer))
        draw_text(290, 1, "MOD", NIO_COLOR_WHITE, NIO_COLOR_LIGHTRED);

    snprintf(text, sizeof(text), "Ln:%lu Col:%lu Off:%lu Size:%lu INS",
             (unsigned long)editor_line_number(editor),
             (unsigned long)(editor_column(editor) + 1),
             (unsigned long)editor->cursor_offset,
             (unsigned long)size);
    draw_text(2, 13, text, NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_fill(0, 27, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    nio_vram_fill(34, 28, 1, 181, NIO_COLOR_GRAY);

    line_offset = editor->top_offset;
    line_number = line_number_at(editor, line_offset);
    for (row = 0; row < NEDIT_VISIBLE_ROWS && !done; ++row) {
        size_t next;
        draw_editor_row(editor, line_offset, line_number, BODY_Y + (int)row * BODY_ROW_HEIGHT);
        next = editor_next_line(editor, line_offset);
        if (next == line_offset) {
            done = true;
        } else {
            line_offset = next;
            line_number++;
        }
    }

    nio_vram_fill(0, 211, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    if (message && message[0]) {
        snprintf(text, sizeof(text), "%.52s", message);
        draw_text(2, 220, text, NIO_COLOR_WHITE, NIO_COLOR_BLUE);
    } else {
        draw_text(2, 220, "Ctrl+S save Ctrl+O open Menu help",
                  NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    }
    nio_vram_draw();
}

void ui_message(const char *title, const char *message) {
    show_msgbox(title, message);
}

char *ui_prompt(const char *title, const char *label, const char *initial) {
    char *value = NULL;
    if (show_msg_user_input(title, label, initial ? initial : "", &value) < 0) {
        free(value);
        return NULL;
    }
    return value;
}

static void draw_save_confirmation(unsigned selected) {
    static const char *options[] = {"1  Save", "2  Discard changes", "3  Cancel"};
    unsigned index;
    nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
    draw_text(2, 4, "nEdit", NIO_COLOR_WHITE, NIO_COLOR_BLACK);
    draw_text(2, 18, "Save changes?", NIO_COLOR_WHITE, NIO_COLOR_LIGHTRED);
    draw_text(2, 31, "This file has unsaved modifications.",
              NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_fill(0, 44, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    for (index = 0; index < sizeof(options) / sizeof(options[0]); ++index) {
        int y = 72 + (int)index * 39;
        int background = index == selected ? NIO_COLOR_BLUE : NIO_COLOR_WHITE;
        int foreground = index == selected ? NIO_COLOR_WHITE : NIO_COLOR_BLACK;
        nio_vram_fill(20, (unsigned)(y - 8), 280, 28, (unsigned)background);
        draw_text(30, y, options[index], background, foreground);
    }
    nio_vram_fill(0, 211, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    draw_text(2, 220, "Up/Down choose Enter select Esc cancel",
              NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_draw();
}

static int wait_save_key(void) {
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
    if (!ui || !ui->initialized) return 3;
    for (;;) {
        int action;
        draw_save_confirmation(selected);
        action = wait_save_key();
        wait_no_key_pressed();
        if (action == -1) selected = selected > 0 ? selected - 1 : 2;
        else if (action == -2) selected = selected < 2 ? selected + 1 : 0;
        else return action == 100 ? (int)selected + 1 : action;
    }
}

typedef struct {
    char name[BROWSER_NAME_MAX];
    bool is_directory;
} BrowserItem;


static bool join_path(char *output, size_t capacity, const char *directory, const char *name) {
    size_t a = strlen(directory), b = strlen(name);
    bool slash = a && directory[a - 1] != '/';
    if (a + (slash ? 1u : 0u) + b >= capacity) return false;
    memcpy(output, directory, a);
    if (slash) output[a++] = '/';
    memcpy(output + a, name, b + 1);
    return true;
}

static int compare_items(const void *left, const void *right) {
    const BrowserItem *a = (const BrowserItem *)left;
    const BrowserItem *b = (const BrowserItem *)right;
    if (a->is_directory != b->is_directory) return b->is_directory - a->is_directory;
    return strcmp(a->name, b->name);
}

static bool load_directory(const char *directory, BrowserItem *items, size_t *count) {
    DIR *stream = opendir(directory);
    struct dirent *entry;
    *count = 0;
    if (!stream) return false;
    while (*count < BROWSER_MAX_ITEMS && (entry = readdir(stream)) != NULL) {
        char full[NEDIT_PATH_MAX];
        struct stat status;
        size_t length;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !join_path(full, sizeof(full), directory, entry->d_name) || stat(full, &status)) continue;
        length = strlen(entry->d_name);
        if (length >= BROWSER_NAME_MAX) continue;
        memcpy(items[*count].name, entry->d_name, length + 1);
        items[*count].is_directory = S_ISDIR(status.st_mode);
        (*count)++;
    }
    closedir(stream);
    qsort(items, *count, sizeof(*items), compare_items);
    return true;
}

static void parent_directory(char *path, const char *root) {
    char *slash;
    if (!strcmp(path, root)) return;
    slash = strrchr(path, '/');
    if (!slash || slash == path || (size_t)(slash - path) < strlen(root)) strcpy(path, root);
    else *slash = '\0';
}

static bool initial_directory(char output[NEDIT_PATH_MAX], const char *initial) {
    const char *documents = get_documents_dir();
    struct stat status;
    size_t length;
    if (!initial || !initial[0] || stat(initial, &status)) {
        initial = documents;
        if (stat(initial, &status)) return false;
    }
    length = strlen(initial);
    if (length >= NEDIT_PATH_MAX) return false;
    memcpy(output, initial, length + 1);
    if (!S_ISDIR(status.st_mode)) parent_directory(output, documents);
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

static char *browse_for_file(Ui *ui, const char *initial) {
    BrowserItem *items;
    char directory[NEDIT_PATH_MAX];
    const char *documents = get_documents_dir();
    size_t count = 0, selected = 0, top = 0;
    char *result = NULL;
    if (!ui || !ui->initialized || !initial_directory(directory, initial)) return NULL;
    items = (BrowserItem *)malloc(BROWSER_MAX_ITEMS * sizeof(*items));
    if (!items) {
        ui_message("Open failed", "Not enough memory for browser");
        return NULL;
    }
    for (;;) {
        char text[160];
        size_t row;
        int key;
        if (!load_directory(directory, items, &count)) {
            ui_message("Open failed", "Cannot read this directory");
            break;
        }
        if (selected >= count) selected = count ? count - 1 : 0;
        if (top > selected) top = selected;
        if (selected >= top + BROWSER_VISIBLE_ROWS) top = selected - BROWSER_VISIBLE_ROWS + 1;
        nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
        snprintf(text, sizeof(text), "Open: %.44s", base_name(directory));
        draw_text(2, 2, text, NIO_COLOR_WHITE, NIO_COLOR_BLACK);
        snprintf(text, sizeof(text), "%.52s", directory);
        draw_text(2, 13, text, NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
        nio_vram_fill(0, 23, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
        for (row = 0; row < BROWSER_VISIBLE_ROWS && top + row < count; ++row) {
            size_t index = top + row;
            int y = 27 + (int)row * 9;
            int bg = index == selected ? NIO_COLOR_BLUE : NIO_COLOR_WHITE;
            int fg = index == selected ? NIO_COLOR_WHITE : NIO_COLOR_BLACK;
            snprintf(text, sizeof(text), "%s%.47s", items[index].is_directory ? "[D] " : "    ", items[index].name);
            draw_text(2, y, text, bg, fg);
        }
        if (!count) draw_text(2, 31, "(empty directory)", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
        nio_vram_fill(0, 211, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
        draw_text(2, 220, "Enter open/enter dir Esc up/back", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
        nio_vram_draw();
        key = wait_browser_key();
        wait_no_key_pressed();
        if (key == 1 && count) selected = selected ? selected - 1 : count - 1;
        else if (key == 2 && count) selected = selected + 1 < count ? selected + 1 : 0;
        else if (key == 3 && count) {
            char full[NEDIT_PATH_MAX];
            if (!join_path(full, sizeof(full), directory, items[selected].name))
                ui_message("Open failed", "Selected path is too long");
            else if (items[selected].is_directory) {
                strcpy(directory, full); selected = top = 0;
            } else {
                size_t length = strlen(full);
                result = (char *)malloc(length + 1);
                if (result) memcpy(result, full, length + 1);
                break;
            }
        } else if (key == 4) {
            if (!strcmp(directory, documents)) break;
            parent_directory(directory, documents); selected = top = 0;
        }
    }
    free(items);
    return result;
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

static void draw_information_page(bool about) {
    static const char *help[] = {
        "Navigation", "Arrows        Move cursor", "Shift+Up/Dn   Page up/down",
        "Ctrl+Left     Row start", "Ctrl+Right    Row end", "Editing",
        "Ctrl+Shift    Caps Lock", "Ctrl x5       Sticky Keys",
        "Enter         New line", "Del           Backspace", "Tab           4 spaces",
        "Files", "Ctrl+N        Create file", "Ctrl+O        Open",
        "Ctrl+S        Save", "Ctrl+Shift+S  Save As", "Ctrl+R        Reload",
        "Ctrl+G        Go To Line", "Menu          Help / About",
        "Esc           Cancel / Exit"
    };
    static const char *about_lines[] = {
        "nEdit", "Native plain-text editor", "", "TI-Nspire CX / CX II",
        "Built for Ndless", "", "Copyright (c) 2026 Ziyang-Bai.", "All rights reserved."
    };
    const char *const *lines = about ? about_lines : help;
    size_t count = about ? sizeof(about_lines) / sizeof(about_lines[0]) : sizeof(help) / sizeof(help[0]);
    size_t index;
    nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
    draw_text(2, 3, about ? "About nEdit" : "nEdit Help", NIO_COLOR_WHITE, NIO_COLOR_BLACK);
    draw_text(284, 3, about ? "2/2" : "1/2", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_fill(0, 17, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    for (index = 0; index < count; ++index) {
        bool heading = !about && (!strcmp(lines[index], "Navigation") || !strcmp(lines[index], "Editing") || !strcmp(lines[index], "Files"));
        draw_text(6, 24 + (int)index * 9, lines[index], NIO_COLOR_WHITE, heading ? NIO_COLOR_BLUE : NIO_COLOR_BLACK);
    }
    nio_vram_fill(0, 211, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    draw_text(2, 220, "Left/Right switch Enter/Esc back", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_draw();
}

static void show_information(Ui *ui, bool about) {
    if (!ui || !ui->initialized) return;
    wait_no_key_pressed();
    for (;;) {
        int direction;
        draw_information_page(about);
        direction = wait_information_key();
        wait_no_key_pressed();
        if (!direction) return;
        about = !about;
    }
}

static void draw_open_menu(unsigned selected) {
    static const char *options[] = {
        "1  Browse files", "2  Enter path", "3  Create file",
        "4  Help", "5  About nEdit", "6  Cancel"
    };
    unsigned index;
    nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
    draw_text(2, 4, "nEdit - Open", NIO_COLOR_WHITE, NIO_COLOR_BLACK);
    draw_text(2, 17, "Choose an action", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_fill(0, 30, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    for (index = 0; index < sizeof(options) / sizeof(options[0]); ++index) {
        int y = 43 + (int)index * 28;
        int bg = index == selected ? NIO_COLOR_BLUE : NIO_COLOR_WHITE;
        int fg = index == selected ? NIO_COLOR_WHITE : NIO_COLOR_BLACK;
        nio_vram_fill(20, (unsigned)(y - 6), 280, 24, (unsigned)bg);
        draw_text(30, y, options[index], bg, fg);
    }
    nio_vram_fill(0, 211, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    draw_text(2, 220, "Up/Down choose Enter select Esc cancel", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_draw();
}

static int wait_open_key(void) {
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_UP)) return -1;
        if (isKeyPressed(KEY_NSPIRE_DOWN)) return -2;
        if (isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_CLICK)) return 100;
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
        action = wait_open_key();
        wait_no_key_pressed();
        if (action == -1) { selected = selected ? selected - 1 : 5; continue; }
        if (action == -2) { selected = selected < 5 ? selected + 1 : 0; continue; }
        if (action == 100) action = (int)selected + 1;
        if (action == 1) {
            path = browse_for_file(ui, initial_path);
            if (path) return path;
        } else if (action == 2) {
            path = ui_prompt("nEdit - Open", "File path",
                             initial_path && initial_path[0] ? initial_path : get_documents_dir());
            if (path) return path;
        } else if (action == 3) {
            char suggested[NEDIT_PATH_MAX];
            snprintf(suggested, sizeof(suggested), "%s/new.txt", get_documents_dir());
            path = ui_prompt("nEdit - New File", "New file path", suggested);
            if (path) {
                if (create) *create = true;
                return path;
            }
        } else if (action == 4) show_information(ui, false);
        else if (action == 5) show_information(ui, true);
        else return NULL;
    }
}

void ui_show_menu(Ui *ui) {
    int choice;
    if (!ui || !ui->initialized) return;
    choice = (int)show_msgbox_3b("nEdit", "Information", "Help", "About", "Cancel");
    if (choice == 1 || choice == 2) show_information(ui, choice == 2);
}
