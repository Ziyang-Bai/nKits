#include "ui.h"
#include "version.h"

#include <libndls.h>
#include <nspireio/nspireio.h>
#include <dirent.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ROW_Y 43
#define ROW_HEIGHT 13
#define OFFSET_X 2
#define HEX_X 56
#define HEX_STEP 22
#define ASCII_X 239
#define ASCII_STEP 9

static void draw_text(int x, int y, const char *text, int background, int foreground) {
    nio_vram_pixel_puts(x, y, text, background, foreground);
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool ui_init(Ui *ui) {
    if (!ui || !nio_scrbuf_init()) {
        return false;
    }
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

static void draw_nibble_indicator(Editor *editor, int y, unsigned column) {
    int x;
    if (editor->mode != EDIT_MODE_HEX) {
        return;
    }
    x = HEX_X + (int)column * HEX_STEP + (editor->high_nibble ? 0 : 6);
    nio_vram_fill((unsigned)x, (unsigned)(y + 8), 5, 2, NIO_COLOR_LIGHTYELLOW);
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
    size = file_buffer_size(buffer);
    have_current = size != 0 && editor_current_byte(editor, &current);

    nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);

    snprintf(text, sizeof(text), "nHex - %.38s", base_name(file_buffer_path(buffer)));
    draw_text(2, 1, text, NIO_COLOR_WHITE, NIO_COLOR_BLACK);
    if (file_buffer_modified(buffer)) {
        draw_text(290, 1, "MOD", NIO_COLOR_WHITE, NIO_COLOR_LIGHTRED);
    }

    if (have_current) {
        snprintf(text, sizeof(text), "Size:%llu Off:%08llX Byte:%02X %s %s",
                 (unsigned long long)size,
                 (unsigned long long)editor->cursor_offset,
                 current,
                 editor->mode == EDIT_MODE_HEX ? "HEX" : "ASCII",
                 editor->mode == EDIT_MODE_HEX ? (editor->high_nibble ? "HIGH" : "LOW") : "");
    } else {
        snprintf(text, sizeof(text), "Size:%llu Off:%08llX Byte:-- %s %s",
                 (unsigned long long)size,
                 (unsigned long long)editor->cursor_offset,
                 editor->mode == EDIT_MODE_HEX ? "HEX" : "ASCII",
                 editor->mode == EDIT_MODE_HEX ? (editor->high_nibble ? "HIGH" : "LOW") : "");
    }
    draw_text(2, 13, text, NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);

    nio_vram_fill(0, 26, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    draw_text(OFFSET_X, 30, "OFFSET", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    for (row = 0; row < NHEX_BYTES_PER_ROW; ++row) {
        snprintf(text, sizeof(text), "%X", row);
        draw_text(HEX_X + (int)row * HEX_STEP + 3, 30, text,
                  NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    }
    draw_text(ASCII_X, 30, "ASCII", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);

    for (row = 0; row < NHEX_VISIBLE_ROWS; ++row) {
        uint64_t row_offset = editor->top_offset + (uint64_t)row * NHEX_BYTES_PER_ROW;
        int y = ROW_Y + (int)row * ROW_HEIGHT;
        unsigned column;
        if (row_offset >= size) {
            break;
        }
        snprintf(text, sizeof(text), "%08llX", (unsigned long long)row_offset);
        draw_text(OFFSET_X, y, text, NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);

        for (column = 0; column < NHEX_BYTES_PER_ROW; ++column) {
            uint64_t offset = row_offset + column;
            uint8_t value;
            char ascii[2];
            bool cursor;
            int hex_background = NIO_COLOR_WHITE;
            int hex_foreground = NIO_COLOR_BLACK;
            int ascii_background = NIO_COLOR_WHITE;
            int ascii_foreground = NIO_COLOR_BLACK;

            if (offset >= size || !file_buffer_read(buffer, offset, &value)) {
                break;
            }
            cursor = offset == editor->cursor_offset;
            if (cursor) {
                if (editor->mode == EDIT_MODE_HEX) {
                    hex_background = NIO_COLOR_BLUE;
                    hex_foreground = NIO_COLOR_WHITE;
                    ascii_background = NIO_COLOR_GRAY;
                } else {
                    ascii_background = NIO_COLOR_BLUE;
                    ascii_foreground = NIO_COLOR_WHITE;
                    hex_background = NIO_COLOR_GRAY;
                }
            }

            snprintf(text, sizeof(text), "%02X", value);
            draw_text(HEX_X + (int)column * HEX_STEP, y, text,
                      hex_background, hex_foreground);
            ascii[0] = value >= 0x20 && value <= 0x7e ? (char)value : '.';
            ascii[1] = '\0';
            draw_text(ASCII_X + (int)column * ASCII_STEP, y, ascii,
                      ascii_background, ascii_foreground);
            if (cursor) {
                draw_nibble_indicator(editor, y, column);
            }
        }
    }

    nio_vram_fill(0, 215, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    if (message && message[0]) {
        snprintf(text, sizeof(text), "%.52s", message);
        draw_text(2, 220, text, NIO_COLOR_WHITE, NIO_COLOR_BLUE);
    } else {
        draw_text(2, 220, "Tab mode Ctrl+S save Ctrl+O open",
                  NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    }
    nio_vram_draw();
}

#define BROWSER_MAX_ITEMS 128
#define BROWSER_NAME_MAX 128
#define BROWSER_VISIBLE_ROWS 20

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

        nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
        snprintf(text, sizeof(text), "Open: %.44s", base_name(directory));
        draw_text(2, 2, text, NIO_COLOR_WHITE, NIO_COLOR_BLACK);
        snprintf(text, sizeof(text), "%.52s", directory);
        draw_text(2, 13, text, NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
        nio_vram_fill(0, 23, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
        for (row = 0; row < BROWSER_VISIBLE_ROWS && top + row < count; ++row) {
            size_t index = top + row;
            int y = 27 + (int)row * 9;
            int background = index == selected ? NIO_COLOR_BLUE : NIO_COLOR_WHITE;
            int foreground = index == selected ? NIO_COLOR_WHITE : NIO_COLOR_BLACK;
            snprintf(text, sizeof(text), "%s%.47s",
                     items[index].is_directory ? "[D] " : "    ", items[index].name);
            draw_text(2, y, text, background, foreground);
        }
        if (count == 0) {
            draw_text(2, 31, "(empty directory)", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
        }
        nio_vram_fill(0, 211, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
        draw_text(2, 220, "Enter open/enter dir  Esc up/back",
                  NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
        nio_vram_draw();

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
    show_msgbox(title, message);
}

static void draw_save_confirmation(unsigned selected) {
    static const char *options[] = {
        "1  Save",
        "2  Discard changes",
        "3  Cancel"
    };
    unsigned index;
    nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
    draw_text(2, 4, "nHex", NIO_COLOR_WHITE, NIO_COLOR_BLACK);
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
    nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
    draw_text(2, 4, "nHex - Open", NIO_COLOR_WHITE, NIO_COLOR_BLACK);
    draw_text(2, 17, "Choose an action", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_fill(0, 30, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    for (index = 0; index < sizeof(options) / sizeof(options[0]); ++index) {
        int y = 43 + (int)index * 28;
        int background = index == selected ? NIO_COLOR_BLUE : NIO_COLOR_WHITE;
        int foreground = index == selected ? NIO_COLOR_WHITE : NIO_COLOR_BLACK;
        nio_vram_fill(20, (unsigned)(y - 6), 280, 24, (unsigned)background);
        draw_text(30, y, options[index], background, foreground);
    }
    nio_vram_fill(0, 211, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    draw_text(2, 220, "Up/Down choose Enter select Esc cancel",
              NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_draw();
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

static void draw_information_page(bool about) {
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
    const char *const *lines = about ? about_lines : help_lines;
    size_t count = about ? sizeof(about_lines) / sizeof(about_lines[0])
                         : sizeof(help_lines) / sizeof(help_lines[0]);
    size_t index;

    nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
    draw_text(2, 3, about ? "About nHex" : "nHex Help",
              NIO_COLOR_WHITE, NIO_COLOR_BLACK);
    draw_text(284, 3, about ? "2/2" : "1/2",
              NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_fill(0, 17, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    for (index = 0; index < count; ++index) {
        bool heading = !about && (!strcmp(lines[index], "Navigation") ||
                                  !strcmp(lines[index], "Editing") ||
                                  !strcmp(lines[index], "Files"));
        draw_text(6, 24 + (int)index * 9, lines[index], NIO_COLOR_WHITE,
                  heading ? NIO_COLOR_BLUE : NIO_COLOR_BLACK);
    }
    nio_vram_fill(0, 211, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    draw_text(2, 220, "Left/Right switch  Enter/Esc back",
              NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_draw();
}

static void show_information_pages(Ui *ui, bool about) {
    if (!ui || !ui->initialized) {
        return;
    }
    wait_no_key_pressed();
    for (;;) {
        int direction;
        draw_information_page(about);
        direction = wait_information_key();
        wait_no_key_pressed();
        if (direction == 0) {
            return;
        }
        about = !about;
    }
}

void ui_show_menu(Ui *ui) {
    int choice;
    if (!ui || !ui->initialized) {
        return;
    }
    choice = (int)show_msgbox_3b("nHex", "Information", "Help", "About", "Cancel");
    if (choice == 1 || choice == 2) {
        show_information_pages(ui, choice == 2);
    }
}
