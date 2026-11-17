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

#define BODY_Y 50
#define BROWSER_MAX_ITEMS 128
#define BROWSER_NAME_MAX 128
#define BROWSER_VISIBLE_ROWS 8

static Gc graphics;
static unsigned interrupt_state;
static int cell_width, digit_width, row_height, text_x;
static const UiTheme *theme = &ui_themes[0];

static unsigned interrupt_mask(void) {
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
    interrupt_state = interrupt_mask();
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

typedef struct {
    uint32_t value;
    const char *name;
} Symbol;

static const Symbol symbols[] = {
    {0x03b1, "alpha"}, {0x03b2, "beta"}, {0x03b3, "gamma"},
    {0x03b4, "delta"}, {0x03b5, "epsilon"}, {0x03b6, "zeta"},
    {0x03b7, "eta"}, {0x03b8, "theta"}, {0x03b9, "iota"},
    {0x03ba, "kappa"}, {0x03bb, "lambda"}, {0x03bc, "mu"},
    {0x03bd, "nu"}, {0x03be, "xi"}, {0x03bf, "omicron"},
    {0x03c0, "pi"}, {0x03c1, "rho"}, {0x03c3, "sigma"},
    {0x03c4, "tau"}, {0x03c5, "upsilon"}, {0x03c6, "phi"},
    {0x03c7, "chi"}, {0x03c8, "psi"}, {0x03c9, "omega"},
    {0x0393, "Gamma"}, {0x0394, "Delta"}, {0x0398, "Theta"},
    {0x039b, "Lambda"}, {0x039e, "Xi"}, {0x03a0, "Pi"},
    {0x03a3, "Sigma"}, {0x03a6, "Phi"}, {0x03a8, "Psi"},
    {0x03a9, "Omega"}, {0x00b0, "degree"}, {0x00b1, "plus/minus"},
    {0x00b2, "squared"}, {0x00b3, "cubed"}, {0x00d7, "multiply"},
    {0x00f7, "divide"}, {0x221a, "square root"}, {0x221e, "infinity"},
    {0x2264, "less/equal"}, {0x2265, "greater/equal"}, {0x2260, "not equal"},
    {0x2248, "approximate"}, {0x222b, "integral"}, {0x2202, "partial"},
    {0x2207, "nabla"}, {0x2211, "sum"}, {0x220f, "product"},
    {0x2190, "left arrow"}, {0x2192, "right arrow"}, {0x21d2, "implies"},
    {0x00b7, "middle dot"}
};

static void draw_character(int x, int y, uint32_t value, int bg, int fg) {
    uint16_t text[3] = {0, 0, 0};
    if (value == '\t') value = ' ';
    if (value > 0xffff) {
        value -= 0x10000;
        text[0] = 0xd800 | (value >> 10);
        text[1] = 0xdc00 | (value & 0x3ff);
    } else text[0] = value;
    ui_fill(graphics, x, y, cell_width, row_height, bg);
    gui_gc_clipRect(graphics, x, y, cell_width, row_height, GC_CRO_SET);
    ui_draw_u16(graphics, text, x, y, Regular9, fg);
    gui_gc_clipRect(graphics, 0, 0, UI_WIDTH, UI_HEIGHT, GC_CRO_SET);
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool ui_init(Ui *ui) {
    unsigned mask;
    unsigned value;
    size_t index;
    Gc source;
    if (!ui) return false;
    mask = interrupt_mask();
    source = gui_gc_global_GC();
    graphics = source ? gui_gc_copy(source, UI_WIDTH, UI_HEIGHT) : NULL;
    if (!graphics) { restore_interrupts(mask); return false; }
    gui_gc_setRegion(graphics, 0, 0, UI_WIDTH, UI_HEIGHT, 0, 0, UI_WIDTH, UI_HEIGHT);
    gui_gc_begin(graphics);
    cell_width = digit_width = 1;
    row_height = ui_line_height(graphics, Regular9);
    for (value = 32; value <= 126; ++value) {
        uint16_t text[2] = {value, 0};
        int width = gui_gc_getStringWidth(graphics, Regular9, (char *)text, 0, 1);
        if (width > cell_width) cell_width = width;
        if (value >= '0' && value <= '9' && width > digit_width) digit_width = width;
    }
    for (index = 0; index < sizeof(symbols) / sizeof(symbols[0]); ++index) {
        uint16_t text[2] = {symbols[index].value, 0};
        int width = gui_gc_getStringWidth(graphics, Regular9, (char *)text, 0, 1);
        if (width > cell_width) cell_width = width;
    }
    text_x = 5 * digit_width + 8;
    gui_gc_finish(graphics);
    restore_interrupts(mask);
    ui->initialized = true;
    return true;
}

void ui_destroy(Ui *ui) {
    if (ui && ui->initialized) {
        unsigned mask = interrupt_mask();
        gui_gc_free(graphics);
        graphics = NULL;
        restore_interrupts(mask);
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
    ui_draw_text(graphics, number, 4, y, Regular9, theme->muted);
    for (column = 0; column < editor->left_column && offset < line_end; ++column)
        offset += editor_character_at(editor, offset, &value);
    for (column = 0; column < editor->visible_columns; ++column) {
        size_t logical_column = editor->left_column + column;
        bool cursor = cursor_line == line_offset && cursor_column == logical_column;
        int background = cursor ? theme->highlight_bg : theme->bg;
        int foreground = cursor ? theme->highlight_fg : theme->fg;
        value = ' ';
        if (offset < line_end) offset += editor_character_at(editor, offset, &value);
        draw_character(text_x + (int)column * cell_width, y, value, background, foreground);
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
    editor->visible_rows = (212 - BODY_Y) / row_height;
    editor->visible_columns = (UI_WIDTH - text_x - 4) / cell_width;
    editor_ensure_visible(editor);
    size = text_buffer_size(buffer);
    cursor_line = editor_line_start(editor, editor->cursor_offset);
    cursor_column = editor_column(editor);
    snprintf(text, sizeof(text), "nEdit - %.28s%s", base_name(text_buffer_path(buffer)),
             text_buffer_modified(buffer) ? " *" : "");
    begin_screen(text);
    snprintf(text, sizeof(text), "Ln:%lu Col:%lu  INS%s%s",
             (unsigned long)editor_line_number(editor),
             (unsigned long)(cursor_column + 1),
             caps_lock ? " CAPS" : "", sticky_keys ? " STKY" : "");
    ui_draw_text(graphics, text, 8, 24, Regular9, theme->muted);
    snprintf(text, sizeof(text), "Off:%lu  Size:%lu",
             (unsigned long)editor->cursor_offset, (unsigned long)size);
    ui_draw_text(graphics, text, 8, 37, Regular9, theme->muted);

    line_offset = editor->top_offset;
    line_number = line_number_at(editor, line_offset);
    for (row = 0; row < editor->visible_rows && !done; ++row) {
        size_t next;
        draw_editor_row(editor, line_offset, line_number, cursor_line, cursor_column,
                        BODY_Y + (int)row * row_height);
        next = editor_next_line(editor, line_offset);
        if (next == line_offset) {
            done = true;
        } else {
            line_offset = next;
            line_number++;
        }
    }

    finish_screen(message && message[0] ? message : "Ctrl+S save/back  Menu actions  Esc home");
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

char *ui_prompt(const char *title, const char *label, const char *initial) {
    char *value = NULL;
    if (show_msg_user_input(title, label, initial ? initial : "", &value) < 0) {
        free(value);
        return NULL;
    }
    wait_no_key_pressed();
    return value;
}

static void draw_save_confirmation(unsigned selected) {
    static const char *options[] = {"1  Save", "2  Discard changes", "3  Cancel"};
    unsigned index;
    begin_screen("nEdit - Save changes?");
    ui_draw_text(graphics, "This file has unsaved modifications.", 10, 36, Regular9, theme->muted);
    for (index = 0; index < sizeof(options) / sizeof(options[0]); ++index)
        ui_draw_row(graphics, theme, 72 + (int)index * 32, options[index], index == selected);
    finish_screen("Up/Down choose  Enter select  Esc cancel");
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
        snprintf(text, sizeof(text), "Open: %.30s", base_name(directory));
        begin_screen(text);
        ui_draw_text(graphics, directory, 8, 26, Regular9, theme->muted);
        for (row = 0; row < BROWSER_VISIBLE_ROWS && top + row < count; ++row) {
            size_t index = top + row;
            snprintf(text, sizeof(text), "%s%s", items[index].is_directory ? "[D] " : "    ", items[index].name);
            ui_draw_row(graphics, theme, 48 + (int)row * 20, text, index == selected);
        }
        if (!count) ui_draw_text(graphics, "(empty directory)", 10, 48, Regular10, theme->muted);
        finish_screen("Enter open/enter dir  Esc up/back");
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

static void draw_information_page(unsigned page) {
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
    bool about = page == 2;
    const char *const *lines = about ? about_lines : help;
    size_t count = about ? sizeof(about_lines) / sizeof(about_lines[0]) : sizeof(help) / sizeof(help[0]);
    size_t index, start = about ? 0 : page * 10;
    char title[48];
    snprintf(title, sizeof(title), "%s  %u/3", about ? "About nEdit" : "nEdit Help", page + 1);
    begin_screen(title);
    for (index = start; index < count && index < start + (about ? 12 : 10); ++index)
        ui_draw_text(graphics, lines[index], 10, 32 + (int)(index - start) * 14, Regular9, theme->fg);
    finish_screen("Left/Right pages  Enter/Esc back");
}

static void show_information(Ui *ui, bool about) {
    unsigned page = about ? 2 : 0;
    if (!ui || !ui->initialized) return;
    wait_no_key_pressed();
    for (;;) {
        int direction;
        draw_information_page(page);
        direction = wait_information_key();
        wait_no_key_pressed();
        if (!direction) return;
        page = (page + (direction < 0 ? 2 : 1)) % 3;
    }
}

static void draw_open_menu(unsigned selected, bool home) {
    static const char *options[] = {
        "1  Browse files", "2  Enter path", "3  Create file",
        "4  Help", "5  About nEdit", "6  Cancel"
    };
    unsigned index;
    begin_screen("nEdit - Open");
    for (index = 0; index < sizeof(options) / sizeof(options[0]); ++index)
        ui_draw_row(graphics, theme, 40 + (int)index * 26,
                    home && index == 5 ? "6  Exit nEdit" : options[index], index == selected);
    finish_screen(home ? "Up/Down choose  Enter select  Esc exit" :
                  "Up/Down choose  Enter select  Esc cancel");
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
    unsigned selected = 0, top = 0;
    unsigned count = sizeof(items) / sizeof(items[0]);
    if (!ui || !ui->initialized) return INPUT_NONE;
    for (;;) {
        unsigned index;
        int key;
        if (selected < top) top = selected;
        if (selected >= top + 8) top = selected - 7;
        begin_screen("nEdit - Edit menu");
        for (index = top; index < count && index < top + 8; ++index)
            ui_draw_row(graphics, theme, 40 + (int)(index - top) * 20, items[index].label, index == selected);
        finish_screen("Up/Down choose  Enter select  Esc back");
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
        snprintf(label, sizeof(label), "Symbols  %u/%u", selected / 24 + 1, (count + 23) / 24);
        begin_screen(label);
        for (index = start; index < start + 24 && index < count; ++index) {
            int x = 10 + (index - start) % 6 * 51;
            int y = 34 + (index - start) / 6 * 36;
            int bg = index == selected ? theme->highlight_bg : theme->bg;
            int fg = index == selected ? theme->highlight_fg : theme->fg;
            uint32_t code = index < punctuation_count ? (uint8_t)punctuation[index] :
                            symbols[index - punctuation_count].value;
            ui_fill(graphics, x, y, 44, 28, bg);
            draw_character(x + (44 - cell_width) / 2, y + 7, code, bg, fg);
        }
        snprintf(label, sizeof(label), "U+%04lX  %s", (unsigned long)value,
                 selected < punctuation_count ? "Punctuation" : symbols[selected - punctuation_count].name);
        ui_draw_text(graphics, label, 8, 188, Regular9, theme->fg);
        finish_screen("Arrows choose  Tab page  Enter insert  Esc cancel");
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
