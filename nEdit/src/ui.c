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

#define TEXT_X 36
#define BODY_Y 32
#define BODY_ROW_HEIGHT 10
#define BROWSER_MAX_ITEMS 128
#define BROWSER_NAME_MAX 128
#define BROWSER_VISIBLE_ROWS 20

static void draw_text(int x, int y, const char *text, int background, int foreground) {
    nio_vram_pixel_puts(x, y, text, background, foreground);
}

typedef struct {
    uint32_t value;
    const char *name;
    uint8_t rows[7];
} Symbol;

static const Symbol symbols[] = {
    {0x03b1, "alpha",   {0,0,13,18,18,18,13}},
    {0x03b2, "beta",    {12,18,18,28,18,18,28}},
    {0x03b3, "gamma",   {0,0,17,17,10,4,4}},
    {0x03b4, "delta",   {6,8,4,14,17,17,14}},
    {0x03b5, "epsilon", {0,0,14,16,12,16,14}},
    {0x03b6, "zeta",    {31,2,4,8,16,14,2}},
    {0x03b7, "eta",     {0,0,22,25,17,1,1}},
    {0x03b8, "theta",   {14,17,17,31,17,17,14}},
    {0x03b9, "iota",    {0,4,0,4,4,4,6}},
    {0x03ba, "kappa",   {0,0,18,20,24,20,18}},
    {0x03bb, "lambda",  {16,8,8,12,10,18,17}},
    {0x03bc, "mu",      {0,0,18,18,18,29,16}},
    {0x03bd, "nu",      {0,0,17,17,18,20,8}},
    {0x03be, "xi",      {14,16,14,8,14,1,14}},
    {0x03bf, "omicron", {0,0,14,17,17,17,14}},
    {0x03c0, "pi",      {0,0,31,10,10,10,11}},
    {0x03c1, "rho",     {0,14,17,17,30,16,16}},
    {0x03c3, "sigma",   {0,0,15,18,17,17,14}},
    {0x03c4, "tau",     {0,0,31,4,4,4,3}},
    {0x03c5, "upsilon", {0,0,17,17,17,17,14}},
    {0x03c6, "phi",     {4,4,14,21,21,14,4}},
    {0x03c7, "chi",     {0,17,10,4,10,17,0}},
    {0x03c8, "psi",     {4,21,21,21,14,4,4}},
    {0x03c9, "omega",   {0,0,17,17,21,21,10}},
    {0x0393, "Gamma",   {31,16,16,16,16,16,16}},
    {0x0394, "Delta",   {4,4,10,10,17,17,31}},
    {0x0398, "Theta",   {14,17,17,21,17,17,14}},
    {0x039b, "Lambda",  {4,4,10,10,17,17,17}},
    {0x039e, "Xi",      {31,0,0,14,0,0,31}},
    {0x03a0, "Pi",      {31,17,17,17,17,17,17}},
    {0x03a3, "Sigma",   {31,16,8,4,8,16,31}},
    {0x03a6, "Phi",     {4,14,21,21,21,14,4}},
    {0x03a8, "Psi",     {21,21,21,14,4,4,4}},
    {0x03a9, "Omega",   {14,17,17,17,10,10,27}},
    {0x00b0, "degree",  {6,9,9,6,0,0,0}},
    {0x00b1, "plus/minus", {0,4,4,31,4,0,31}},
    {0x00b2, "squared", {12,2,4,8,14,0,0}},
    {0x00b3, "cubed",   {12,2,4,2,12,0,0}},
    {0x00d7, "multiply",{0,17,10,4,10,17,0}},
    {0x00f7, "divide",  {0,4,0,31,0,4,0}},
    {0x221a, "square root", {0,7,4,4,20,12,4}},
    {0x221e, "infinity",{0,0,10,21,21,10,0}},
    {0x2264, "less/equal", {2,4,8,16,8,4,31}},
    {0x2265, "greater/equal", {8,4,2,1,2,4,31}},
    {0x2260, "not equal", {0,2,31,4,31,8,0}},
    {0x2248, "approximate", {0,9,22,0,9,22,0}},
    {0x222b, "integral",{6,8,8,8,8,8,16}},
    {0x2202, "partial", {12,2,1,15,17,17,14}},
    {0x2207, "nabla",   {31,17,17,10,10,4,4}},
    {0x2211, "sum",     {31,16,8,4,8,16,31}},
    {0x220f, "product", {31,10,10,10,10,10,10}},
    {0x2190, "left arrow", {0,4,8,31,8,4,0}},
    {0x2192, "right arrow", {0,4,2,31,2,4,0}},
    {0x21d2, "implies", {0,4,2,31,2,31,4}},
    {0x00b7, "middle dot", {0,0,0,4,0,0,0}}
};

static void draw_character(int x, int y, uint32_t value, int bg, int fg) {
    size_t index;
    char text[2] = {'?', '\0'};
    if (value >= 0x20 && value <= 0x7e) {
        text[0] = (char)value;
    } else if (value == '\t') {
        text[0] = ' ';
    } else {
        for (index = 0; index < sizeof(symbols) / sizeof(symbols[0]); ++index) {
            if (symbols[index].value == value) {
                unsigned row, column;
                nio_vram_fill(x, y, 6, 8, bg);
                for (row = 0; row < 7; ++row)
                    for (column = 0; column < 5; ++column)
                        if (symbols[index].rows[row] & (16u >> column))
                            nio_vram_fill(x + column, y + row, 1, 1, fg);
                return;
            }
        }
    }
    draw_text(x, y, text, bg, fg);
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

static void draw_editor_row(Editor *editor, size_t line_offset, size_t line_number,
                            size_t cursor_line, size_t cursor_column, int y) {
    size_t line_end = editor_line_end(editor, line_offset);
    size_t column, offset = line_offset;
    uint32_t value;
    char number[8];

    snprintf(number, sizeof(number), "%5lu", (unsigned long)line_number);
    draw_text(2, y, number, NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    for (column = 0; column < editor->left_column && offset < line_end; ++column)
        offset += editor_character_at(editor, offset, &value);
    for (column = 0; column < NEDIT_VISIBLE_COLUMNS; ++column) {
        size_t logical_column = editor->left_column + column;
        bool cursor = cursor_line == line_offset && cursor_column == logical_column;
        int background = cursor ? NIO_COLOR_BLUE : NIO_COLOR_WHITE;
        int foreground = cursor ? NIO_COLOR_WHITE : NIO_COLOR_BLACK;
        value = ' ';
        if (offset < line_end) offset += editor_character_at(editor, offset, &value);
        draw_character(TEXT_X + (int)column * 6, y, value, background, foreground);
    }
}

void ui_draw(Ui *ui, Editor *editor, const char *message,
             bool caps_lock, bool sticky_keys) {
    TextBuffer *buffer;
    size_t size;
    size_t line_offset;
    size_t line_number;
    size_t cursor_line, cursor_column;
    unsigned row;
    char text[128];
    bool done = false;

    if (!ui || !ui->initialized || !editor) return;
    buffer = editor->buffer;
    size = text_buffer_size(buffer);
    cursor_line = editor_line_start(editor, editor->cursor_offset);
    cursor_column = editor_column(editor);
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
             (unsigned long)(cursor_column + 1),
             (unsigned long)editor->cursor_offset,
             (unsigned long)size);
    draw_text(2, 13, text, NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_fill(0, 27, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    nio_vram_fill(34, 28, 1, 181, NIO_COLOR_GRAY);

    line_offset = editor->top_offset;
    line_number = line_number_at(editor, line_offset);
    for (row = 0; row < NEDIT_VISIBLE_ROWS && !done; ++row) {
        size_t next;
        draw_editor_row(editor, line_offset, line_number, cursor_line, cursor_column,
                        BODY_Y + (int)row * BODY_ROW_HEIGHT);
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
        draw_text(2, 220, "Ctrl+S save/back Menu actions Esc home",
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
        "Arrows        Move cursor", "Shift+Up/Dn   Page up/down",
        "Ctrl+Left/Rt  Row start/end", "Ctrl+Up/Down  Document start/end",
        "Ctrl+Shift    Caps Lock", "Ctrl x5       Sticky Keys",
        "Enter / Tab   New line / 4 spaces",
        "Del/Ctrl+Del  Backspace/Delete next",
        "Catalog/Trig/Var  Symbols", "Pi / Shift+Pi    pi / Pi",
        "x^2 / Shift+x^2  squared / root",
        "Shift+0..9    ) ! @ # $ % ^ & * (",
        "Ctrl+N        Create file", "Ctrl+O        Open",
        "Ctrl+S        Save, return home",
        "Ctrl+Shift+S  Save As, return home",
        "Ctrl+R        Reload", "Ctrl+G        Go To Line",
        "Menu          Editing actions",
        "Esc           Home; Esc at home exits"
    };
    static const char *about_lines[] = {
        "nEdit", "Native plain-text editor", "", "TI-Nspire CX / CX II",
        "Built for Ndless", "Version " NKITS_VERSION, NKITS_STAGE,
        "Commit " NKITS_COMMIT, "", "Copyright (c) 2026 Ziyang-Bai.",
        "GNU GPL v3 licensed.", "No warranty; see LICENSE."
    };
    const char *const *lines = about ? about_lines : help;
    size_t count = about ? sizeof(about_lines) / sizeof(about_lines[0]) : sizeof(help) / sizeof(help[0]);
    size_t index;
    nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
    draw_text(2, 3, about ? "About nEdit" : "nEdit Help", NIO_COLOR_WHITE, NIO_COLOR_BLACK);
    draw_text(284, 3, about ? "2/2" : "1/2", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
    nio_vram_fill(0, 17, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    for (index = 0; index < count; ++index) {
        draw_text(6, 24 + (int)index * 9, lines[index], NIO_COLOR_WHITE, NIO_COLOR_BLACK);
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

static void draw_open_menu(unsigned selected, bool home) {
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
        draw_text(30, y, home && index == 5 ? "6  Exit nEdit" : options[index], bg, fg);
    }
    nio_vram_fill(0, 211, SCREEN_WIDTH, 1, NIO_COLOR_GRAY);
    draw_text(2, 220, home ? "Up/Down choose Enter select Esc exit" :
              "Up/Down choose Enter select Esc cancel", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
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

char *ui_choose_file(Ui *ui, const char *initial_path, bool *create, bool home) {
    unsigned selected = 0;
    if (create) *create = false;
    for (;;) {
        char *path;
        int action;
        draw_open_menu(selected, home);
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

InputAction ui_show_menu(Ui *ui) {
    static const struct {
        const char *label;
        InputAction action;
    } items[] = {
        {"Insert symbol             Catalog", INPUT_SYMBOLS},
        {"Save and return home      Ctrl+S", INPUT_SAVE},
        {"Save As and return home", INPUT_SAVE_AS},
        {"Open file                 Ctrl+O", INPUT_OPEN},
        {"Create file               Ctrl+N", INPUT_NEW},
        {"Reload                    Ctrl+R", INPUT_RELOAD},
        {"Go To Line                Ctrl+G", INPUT_GOTO},
        {"Delete next character     Ctrl+Del", INPUT_DELETE},
        {"Document start            Ctrl+Up", INPUT_DOCUMENT_HOME},
        {"Document end              Ctrl+Down", INPUT_DOCUMENT_END},
        {"Help", INPUT_NONE},
        {"About", INPUT_NONE},
        {"Return home               Esc", INPUT_EXIT}
    };
    unsigned selected = 0;
    unsigned count = sizeof(items) / sizeof(items[0]);
    if (!ui || !ui->initialized) return INPUT_NONE;
    for (;;) {
        unsigned index;
        int key;
        nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
        draw_text(2, 4, "nEdit - Edit menu", NIO_COLOR_WHITE, NIO_COLOR_BLACK);
        for (index = 0; index < count; ++index) {
            int bg = index == selected ? NIO_COLOR_BLUE : NIO_COLOR_WHITE;
            int fg = index == selected ? NIO_COLOR_WHITE : NIO_COLOR_BLACK;
            nio_vram_fill(4, 26 + index * 14, 312, 13, bg);
            draw_text(8, 28 + index * 14, items[index].label, bg, fg);
        }
        draw_text(2, 220, "Up/Down choose Enter select Esc back",
                  NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
        nio_vram_draw();
        key = wait_browser_key();
        wait_no_key_pressed();
        if (key == 1) selected = selected ? selected - 1 : count - 1;
        else if (key == 2) selected = (selected + 1) % count;
        else if (key == 4) return INPUT_NONE;
        else if (key == 3) {
            if (items[selected].action != INPUT_NONE) return items[selected].action;
            show_information(ui, selected == 11);
        }
    }
}

static InputAction wait_symbol_key(void) {
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_LEFT)) return INPUT_LEFT;
        if (isKeyPressed(KEY_NSPIRE_RIGHT)) return INPUT_RIGHT;
        if (isKeyPressed(KEY_NSPIRE_UP)) return INPUT_UP;
        if (isKeyPressed(KEY_NSPIRE_DOWN)) return INPUT_DOWN;
        if (isKeyPressed(KEY_NSPIRE_TAB)) return INPUT_TAB;
        if (isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) ||
            isKeyPressed(KEY_NSPIRE_CLICK)) return INPUT_NEWLINE;
        if (isKeyPressed(KEY_NSPIRE_ESC) || isKeyPressed(KEY_NSPIRE_MENU)) return INPUT_EXIT;
        msleep(10);
    }
}

uint32_t ui_choose_symbol(Ui *ui) {
    static const char punctuation[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    const unsigned punctuation_count = sizeof(punctuation) - 1;
    const unsigned count = punctuation_count + sizeof(symbols) / sizeof(symbols[0]);
    unsigned selected = 0;
    if (!ui || !ui->initialized) return 0;
    for (;;) {
        unsigned index, start = selected / 24 * 24;
        InputAction key;
        char label[80];
        uint32_t value = selected < punctuation_count ? (uint8_t)punctuation[selected] :
                         symbols[selected - punctuation_count].value;
        nio_vram_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, NIO_COLOR_WHITE);
        snprintf(label, sizeof(label), "Symbols  %u/%u", selected / 24 + 1, (count + 23) / 24);
        draw_text(2, 4, label, NIO_COLOR_WHITE, NIO_COLOR_BLACK);
        for (index = start; index < start + 24 && index < count; ++index) {
            int x = 10 + (index - start) % 6 * 51;
            int y = 34 + (index - start) / 6 * 36;
            int bg = index == selected ? NIO_COLOR_BLUE : NIO_COLOR_WHITE;
            int fg = index == selected ? NIO_COLOR_WHITE : NIO_COLOR_BLACK;
            uint32_t code = index < punctuation_count ? (uint8_t)punctuation[index] :
                            symbols[index - punctuation_count].value;
            nio_vram_fill(x, y, 44, 28, bg);
            draw_character(x + 19, y + 10, code, bg, fg);
        }
        snprintf(label, sizeof(label), "U+%04lX  %s", (unsigned long)value,
                 selected < punctuation_count ? "Punctuation" : symbols[selected - punctuation_count].name);
        draw_text(8, 188, label, NIO_COLOR_WHITE, NIO_COLOR_BLACK);
        draw_text(2, 212, "Arrows choose Tab page Enter insert", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
        draw_text(2, 225, "Esc cancel", NIO_COLOR_WHITE, NIO_COLOR_LIGHTBLACK);
        nio_vram_draw();
        key = wait_symbol_key();
        wait_no_key_pressed();
        switch (key) {
            case INPUT_LEFT: selected = selected ? selected - 1 : count - 1; break;
            case INPUT_RIGHT: selected = (selected + 1) % count; break;
            case INPUT_UP: selected = selected >= 6 ? selected - 6 : selected; break;
            case INPUT_DOWN: if (selected + 6 < count) selected += 6; break;
            case INPUT_TAB: selected = start + 24 < count ? start + 24 : 0; break;
            case INPUT_NEWLINE: return value;
            case INPUT_EXIT:
            case INPUT_MENU: return 0;
            default: break;
        }
    }
}
