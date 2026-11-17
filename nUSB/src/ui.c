#include "ui.h"
#include "language.h"
#include "text.h"
#include "version.h"
#include "common/draw.h"

#include <ngc.h>
#include <stdio.h>
#include <string.h>

#define LCD_CURSOR_CONTROL (*(volatile uint32_t *)0xc0000c00)
#define VIC_ENABLE (*(volatile uint32_t *)0xdc000010)
#define VIC_DISABLE (*(volatile uint32_t *)0xdc000014)
#define KEYPAD_IRQ (1u << 16)
#define LCD_IRQ (1u << 21)
#define UI_BLACK (ui_themes[0].fg)
#define UI_FONT Regular10
#define PAGE_ROWS 9
#define LINE_UNITS 320
#define L language_text

static Gc graphics;

static bool pause_refresh(Ui *ui) {
    uint32_t marker = *(volatile uint32_t *)0x10000020u;
    if (marker != 0x1042aee0u && marker != 0x10429ec0u) return true;

    /* OS 6.4's display driver: context literal and TMS_Control_Timer call.
       Resolve relocated data/code through the HAL shared by CAS and non-CAS. */
    if (*(const uint32_t *)0x10006c94u != 0xe59f406cu) return false;
    uint32_t slot = *(const uint32_t *)0x10006d08u;
    uint32_t call = *(const uint32_t *)0x10006e90u;
    if (slot < 0x10000000u || slot > 0x13fffffcu || (slot & 3) ||
        (call & 0xff000000u) != 0xeb000000u) return false;
    uint32_t context = *(const uint32_t *)slot;
    if (context < 0x10000000u || context > 0x13ffff7cu || (context & 3))
        return false;
    uint8_t *timer = (uint8_t *)context + 0x10;
    if (*(const uint32_t *)(timer + 0x0c) != 0x54494d45u ||
        memcmp(timer + 0x10, "DISPCPY", 8)) return false;
    uintptr_t control = 0x10006e98u + ((int32_t)(call << 8) >> 6);
    if (control < 0x10000000u || control >= 0x14000000u) return false;
    if (*(volatile uint8_t *)(timer + 0x20)) {
        ui->control_timer = (int (*)(void *, unsigned))control;
        if (ui->control_timer(timer, 4)) return false;
        ui->refresh_timer = timer;
    }
    return true;
}

void ui_present(Ui *ui) {
    if (ui && ui->foreground && (LCD_CURSOR_CONTROL & 1))
        LCD_CURSOR_CONTROL &= ~1u;
}

static void fill(int x, int y, int width, int height, int rgb) {
    ui_fill(graphics, x, y, width, height, rgb);
}

static unsigned scalar_width(uint32_t scalar) {
    int width;
    if (scalar <= 0xffff) width = gui_gc_getCharWidth(graphics, UI_FONT, (short)scalar);
    else {
        uint16_t pair[3];
        text_put_utf16(scalar, pair);
        pair[2] = 0;
        width = gui_gc_getStringWidth(graphics, UI_FONT, (char *)pair, 0, 2);
    }
    return width > 0 ? (unsigned)width : 0;
}

static const char *text_line(const char *text, uint16_t *line, size_t capacity,
                             unsigned pixels, bool wrap_words, unsigned *advance) {
    const char *word_break = NULL;
    unsigned used = 0, break_width = 0;
    size_t units = 0, break_units = 0;
    while (*text && *text != '\n') {
        uint32_t scalar;
        size_t bytes = text_next(text, &scalar);
        unsigned width = scalar_width(scalar);
        size_t count = scalar > 0xffff ? 2 : 1;
        if (width > pixels - used || count >= capacity - units) break;
        if (wrap_words && scalar == ' ' && units) {
            word_break = text + bytes;
            break_units = units;
            break_width = used;
        }
        units += text_put_utf16(scalar, line + units);
        text += bytes;
        used += width;
    }
    if (wrap_words && *text == ' ') {
        while (*text == ' ') ++text;
    } else if (wrap_words && *text && *text != '\n' && word_break) {
        text = word_break;
        units = break_units;
        used = break_width;
    }
    line[units] = 0;
    if (advance) *advance = used;
    return text;
}

static void draw_line(int x, int y, const uint16_t *line, int foreground) {
    /* OS 6.4 CJK glyphs have a four-pixel top inset and 15-pixel ink height. */
    ui_draw_u16(graphics, line, x, y - 4, UI_FONT, foreground);
}

static unsigned draw_clipped(int x, int y, const char *text, unsigned pixels, int foreground) {
    uint16_t line[LINE_UNITS + 1];
    unsigned advance;
    const char *end = text_line(text, line, LINE_UNITS + 1, pixels, false, &advance);
    if (*end) {
        unsigned dots = scalar_width('.') * 3;
        if (dots <= pixels) {
            size_t n = 0;
            text_line(text, line, LINE_UNITS - 2, pixels - dots, false, &advance);
            while (line[n]) ++n;
            line[n++] = '.'; line[n++] = '.'; line[n++] = '.'; line[n] = 0;
            advance += dots;
        }
    }
    draw_line(x, y, line, foreground);
    return advance;
}

static void draw_text(int x, int y, const char *text, int foreground) {
    draw_clipped(x, y, text, 318 - x, foreground);
}

bool ui_init(Ui *ui) {
    /* lcd_init keeps CX II's panel conversion in dedicated VRAM. */
    if (!ui || !lcd_init(lcd_type() == SCR_320x240_4 ? SCR_320x240_4 : SCR_320x240_565))
        return false;
    graphics = gui_gc_global_GC();
    if (!graphics || (is_cx2 && !pause_refresh(ui))) {
        lcd_init(SCR_TYPE_INVALID);
        graphics = NULL;
        return false;
    }
    ui->initialized = true;
    if (is_cx2) {
        ui->foreground = true;
        ui->cursor_control = LCD_CURSOR_CONTROL;
        /* Keep USB/scheduler IRQs active while owning keys and display refresh. */
        ui->foreground_irqs = VIC_ENABLE & (KEYPAD_IRQ | LCD_IRQ);
        VIC_DISABLE = KEYPAD_IRQ | LCD_IRQ;
    }
    ui_present(ui);
    return true;
}

void ui_destroy(Ui *ui) {
    if (ui && ui->initialized) {
        if (ui->foreground) {
            LCD_CURSOR_CONTROL = ui->cursor_control;
            VIC_ENABLE = ui->foreground_irqs;
            if (ui->refresh_timer) {
                ui->control_timer(ui->refresh_timer, 5);
                ui->refresh_timer = NULL;
            }
            ui->foreground = false;
        }
        lcd_init(SCR_TYPE_INVALID);
        graphics = NULL;
        ui->initialized = false;
    }
}

static void frame(const char *title) {
    gui_gc_begin(graphics);
    ui_draw_header(graphics, &ui_themes[0], title);
    gui_gc_setFont(graphics, UI_FONT);
}

static void finish(Ui *ui) {
    gui_gc_finish(graphics);
    gui_gc_blit_to_screen(graphics);
    ui_present(ui);
}

typedef struct {
    unsigned line, scroll;
} TextPage;

static void page_text(TextPage *page, const char *text) {
    uint16_t line[LINE_UNITS + 1];
    do {
        const char *end;
        if (page->line >= page->scroll + PAGE_ROWS) return;
        end = text_line(text, line, LINE_UNITS + 1, 304, true, NULL);
        if (end == text && *text && *text != '\n') {
            uint32_t scalar;
            end += text_next(text, &scalar);
            line[0] = '?'; line[1] = 0;
        }
        if (page->line >= page->scroll)
            draw_line(8, 34 + (page->line - page->scroll) * 18, line, UI_BLACK);
        ++page->line;
        text = *end == '\n' ? end + 1 : end;
    } while (*text);
}

void ui_error(Ui *ui, const char *message) {
    TextPage page = {0, 0};
    if (!ui || !ui->initialized) return;
    frame("nUSB");
    page_text(&page, L(message));
    ui_draw_footer(graphics, &ui_themes[0], L("Esc: Exit"));
    finish(ui);
}

static void information(const App *app) {
    static const char *help[] = {
        "Up/Down: select; Shift+Up/Down: page",
        "Enter: enter folder / file details",
        "Right: details (including folders)",
        "Esc: parent / exit at storage root",
        "Menu: Copy Move Rename Delete Storage",
        "Menu > Language: English / Chinese / French",
        "Destination: U use folder, S switch storage",
        "Destination Menu: New folder / Cancel",
        "Conflicts: Skip (default), new name, Cancel",
        "Esc requests cancel at next I/O boundary.",
        "Completed files stay after cancel/failure.",
        "Cross-storage move copies all before delete.",
        "Names: Left/Right/Home, Ctrl+Right: End",
        "Del: delete; Ctrl+Del: backspace",
        "Catalog: 1-6 hex U+ digits, Enter inserts",
        "FAT32 / exFAT; dirty exFAT is read-only.",
        "OS files >2 GiB: use nUSB instead.",
        "OS long names use temporary identity tokens.",
        "Deep paths: USB/~nUSB-paths flat directory",
        "Portal scans may be slow on large drives.",
        "Details show full original and system paths.",
        "Restart calculator after resident updates."
    };
    static const char *about[] = {
        "TI-Nspire CX II / CX II CAS", "OS 6.4.0.74, standard Ndless r2022+",
        "FAT32 / exFAT file manager and OS folder",
        "Restart calculator to update resident USB",
        "Use an OTG cable and adequate USB power"
    };
    unsigned i;
    TextPage page = {0, app->detail_scroll};
    frame(L(app->view == APP_HELP ? "nUSB - Help" : "nUSB - About"));
    if (app->view == APP_HELP) {
        for (i = 0; i < sizeof(help) / sizeof(help[0]); ++i) page_text(&page, L(help[i]));
    } else {
        char commit[96];
        page_text(&page, "nUSB " NKITS_VERSION);
        page_text(&page, L(NKITS_STAGE));
        snprintf(commit, sizeof(commit), "%s%s", L("Commit: "), NKITS_COMMIT);
        page_text(&page, commit);
        for (i = 0; i < sizeof(about) / sizeof(about[0]); ++i) page_text(&page, L(about[i]));
    }
    ui_draw_footer(graphics, &ui_themes[0], L("Up/Down: Scroll   Esc / Enter: Back"));
}

static void menu_row(unsigned row, unsigned selected, const char *text) {
    ui_draw_row(graphics, &ui_themes[0], 34 + row * 20, text, row == selected);
}

static void language_view(const App *app) {
    char text[96];
    unsigned i;
    frame(L("nUSB - Language"));
    for (i = 0; i < LANGUAGE_COUNT; ++i) {
        snprintf(text, sizeof(text), "%s %s", i == (unsigned)language_current() ? "[x]" : "[ ]",
                 language_name((Language)i));
        menu_row(i, app->menu_selected, text);
    }
    if (app->language_status)
        draw_text(2, 140, L(app->language_status > 0 ? "Language saved" :
                          "Language changed; setting not saved"), UI_BLACK);
    ui_draw_footer(graphics, &ui_themes[0], L("Enter: Apply   Esc: Back"));
}

static void details(const App *app) {
    const FsEntry *entry = &app->browser.entries[app->browser.selected];
    TextPage page = {0, app->detail_scroll};
    char text[128], size[21];
    StorageLocation location;
    frame(L("nUSB - Details"));
    snprintf(size, sizeof(size), "%llu", (unsigned long long)entry->size);
    snprintf(text, sizeof(text), "%s, %s, %s",
        app->browser.kind == STORAGE_USB ? "USB" : L("Internal"),
        app->detail_limits.format == FS_FORMAT_EXFAT ? "exFAT" :
        app->detail_limits.format == FS_FORMAT_FAT32 ? "FAT32" : L("Documents"),
        L(app->detail_limits.writable ? "writable" : "read-only"));
    page_text(&page, text);
    page_text(&page, L(entry->is_directory ? "Directory" : "File size (bytes):"));
    if (!entry->is_directory) page_text(&page, size);
    if (app->detail_limits.dirty || app->detail_limits.media_failure)
        page_text(&page, L("Check this volume on a computer before writing."));
    if (entry->size > INT32_MAX && !entry->is_directory)
        page_text(&page, L("Use nUSB for files larger than 2 GiB"));
    page_text(&page, L("Original name:"));
    page_text(&page, entry->name);
    page_text(&page, L("Original path:"));
    if (browser_selection(&app->browser, &location) == FS_OK)
        page_text(&page, location.path);
    if (app->browser.kind == STORAGE_USB) {
        page_text(&page, L("System entry path (after exiting nUSB):"));
        page_text(&page, app->portal_result == FS_OK ? app->portal_path :
                  L(app_result_message(app->portal_result)));
        page_text(&page, L("Tokens expire on media change or foreground access."));
    }
    ui_draw_footer(graphics, &ui_themes[0], L("Up/Down: Scroll   Esc / Enter: Back"));
}

static unsigned percentage(uint64_t done, uint64_t total) {
    unsigned low = 0, high = 100;
    uint64_t quotient, remainder;
    if (!total) return 0;
    if (done >= total) return 100;
    quotient = total / 100;
    remainder = total % 100;
    while (low < high) {
        unsigned middle = (low + high + 1) / 2;
        uint64_t threshold = quotient * middle + (remainder * middle + 99) / 100;
        if (done >= threshold) low = middle;
        else high = middle - 1;
    }
    return low;
}

static void job_view(const App *app) {
    const FileJobStatus *status = fileops_status(app->job);
    static const char *phases[] = {
        "Scanning", "Copying", "Finishing", "Removing source", "Deleting",
        "Conflict", "Cancelling", "Result"
    };
    TextPage page = {0, app->detail_scroll};
    char text[160];
    frame(L(app->view == APP_RESULT ? "nUSB - Result" : "nUSB - File operation"));
    page_text(&page, L(phases[status->phase]));
    if (app->view == APP_RESULT) page_text(&page, L(app_result_message(status->result)));
    snprintf(text, sizeof(text), L("%llu / %llu bytes (%u%%)"),
        (unsigned long long)status->bytes_done, (unsigned long long)status->bytes_total,
        percentage(status->bytes_done, status->bytes_total));
    page_text(&page, text);
    snprintf(text, sizeof(text), L("Scanned: %llu   Copied: %llu"),
        (unsigned long long)status->items_total, (unsigned long long)status->copied);
    page_text(&page, text);
    snprintf(text, sizeof(text), L("Skipped: %llu   Deleted: %llu"),
        (unsigned long long)status->skipped, (unsigned long long)status->deleted);
    page_text(&page, text);
    if (status->cancel_requested && status->phase != FILE_RESULT)
        page_text(&page, L("Cancel requested; finishing current I/O"));
    if (status->failure_path[0]) {
        page_text(&page, L("First failure:"));
        page_text(&page, status->failure_path);
    }
    if (status->temporary_path[0]) {
        page_text(&page, L("Unpublished temporary file:"));
        page_text(&page, status->temporary_path);
    }
    page_text(&page, L("Source:"));
    page_text(&page, status->source.path);
    if (app->operation != FILE_DELETE) {
        page_text(&page, L("Destination:"));
        page_text(&page, status->destination.path);
    }
    ui_draw_footer(graphics, &ui_themes[0], L(app->view == APP_RESULT ?
        "Up/Down: Scroll   Enter: Acknowledge" : "Esc: Request cancellation"));
}

static void editor_view(const App *app) {
    char text[768];
    unsigned cursor;
    frame(L(app->edit == EDIT_FOLDER ? "nUSB - New folder" : "nUSB - Choose name"));
    name_editor_view(&app->editor, text, sizeof(text), 303, scalar_width, &cursor);
    draw_text(8, 60, text, UI_BLACK);
    fill(8 + cursor, 76, 6, 1, UI_BLACK);
    if (app->editor.hex_mode) {
        snprintf(text, sizeof(text), "U+%s_", app->editor.hex);
        draw_text(2, 88, text, UI_BLACK);
        draw_text(2, 108, L("1-6 hex digits; Enter inserts; Esc returns"), UI_BLACK);
    }
    draw_text(2, 145, L(app->editor.error), UI_BLACK);
    snprintf(text, sizeof(text), "U+%04lX   UTF-16: %u/255",
        (unsigned long)name_editor_scalar(&app->editor),
        (unsigned)name_editor_units(&app->editor));
    draw_text(2, 181, text, UI_BLACK);
    ui_draw_text(graphics, L("Del: Forward   Ctrl+Del: Backspace"), 8, 196, Regular9, ui_themes[0].muted);
    ui_draw_footer(graphics, &ui_themes[0], L("Catalog: U+  Enter: Accept  Esc: Cancel"));
}

void ui_draw(Ui *ui, const App *app) {
    const Browser *browser = &app->browser;
    char text[96], size[16];
    unsigned i;
    if (!ui || !ui->initialized) return;
    if (app->view == APP_HELP || app->view == APP_ABOUT) information(app);
    else if (app->view == APP_LANGUAGE) language_view(app);
    else if (app->view == APP_EDITOR) editor_view(app);
    else if (app->view == APP_DETAILS) details(app);
    else if (app->view == APP_JOB || app->view == APP_RESULT) job_view(app);
    else if (app->view == APP_CONFLICT) {
        frame(L("nUSB - Destination exists"));
        menu_row(0, app->menu_selected, L("Skip (keep existing target)"));
        menu_row(1, app->menu_selected, L("Choose another name"));
        menu_row(2, app->menu_selected, L("Cancel operation"));
        draw_text(2, 110, fileops_status(app->job)->destination.path, UI_BLACK);
        ui_draw_footer(graphics, &ui_themes[0], L("Enter: Choose   Esc: Cancel operation"));
    } else if (app->view == APP_DELETE_CONFIRM) {
        frame(L(app->source_directory ? "nUSB - Delete entire directory tree?" : "nUSB - Delete file?"));
        menu_row(0, app->menu_selected, L("Cancel"));
        menu_row(1, app->menu_selected, L("Delete permanently"));
        draw_text(2, 90, strrchr(app->source.path, '/') + 1, UI_BLACK);
        draw_text(2, 120, L(app->message), UI_BLACK);
        ui_draw_footer(graphics, &ui_themes[0], L("Enter: Choose   Esc: Cancel"));
    } else {
        bool picker = app->view == APP_PICKER || app->view == APP_PICKER_MENU;
        unsigned prefix;
        frame(picker ? L("nUSB - Destination folder") :
              app->view == APP_MENU ? L("nUSB - Menu") : "nUSB");
        if (app->view != APP_MENU && app->view != APP_PICKER_MENU) {
            snprintf(text, sizeof(text), "%s:", browser->kind == STORAGE_USB ? "USB" : L("Internal"));
            prefix = draw_clipped(8, 26, text, 110, UI_BLACK) + 12;
            draw_clipped(prefix, 26, browser->path, 312 - prefix, UI_BLACK);
        }
        if (app->view == APP_MENU) {
            static const char *items[] = {
                "Copy", "Move", "Rename", "Delete", "Storage", "Refresh", "Help", "About", "Language", "Exit"
            };
            unsigned first = app->menu_selected >= 9 ? app->menu_selected - 8 : 0;
            for (i = first; i < sizeof(items) / sizeof(items[0]) && i < first + 9; ++i)
                menu_row(i - first, app->menu_selected - first, L(items[i]));
        } else if (app->view == APP_PICKER_MENU) {
            menu_row(0, app->menu_selected, L("New folder"));
            menu_row(1, app->menu_selected, L("Cancel destination selection"));
        } else if (app->state == APP_LIST || (app->state == APP_ERROR && browser->count)) {
            for (i = 0; i < browser->count; ++i) {
                const FsEntry *entry = &browser->entries[i];
                uint16_t units[16];
                unsigned size_width;
                int fg = i == browser->selected ? ui_themes[0].highlight_fg : UI_BLACK;
                int y = 42 + i * 18;
                if (entry->is_directory) strcpy(size, L("<DIR>"));
                else {
                    uint64_t value = entry->size;
                    const char *suffix[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
                    unsigned unit = 0;
                    while (value >= 1024 && unit < 6) { value /= 1024; ++unit; }
                    snprintf(size, sizeof(size), "%llu%s", (unsigned long long)value, suffix[unit]);
                }
                size_t count = text_utf16(size, units, sizeof(units) / sizeof(units[0]));
                size_width = gui_gc_getStringWidth(graphics, UI_FONT, (char *)units, 0, count);
                ui_draw_row(graphics, &ui_themes[0], y, "", i == browser->selected);
                if (entry->is_directory) draw_text(8, y, "/", fg);
                draw_clipped(19, y, entry->name, 286 - size_width, fg);
                draw_text(312 - size_width, y, size, fg);
            }
            if (!browser->count) draw_text(2, 45, L("Empty directory"), UI_BLACK);
        } else {
            TextPage page = {0, 0};
            page_text(&page, L(app->message));
        }
        ui_draw_footer(graphics, &ui_themes[0], L(picker ? "U: Use this folder   S: Switch storage   Menu" :
                  "Enter: Open  Right: Details  Esc: Back  Menu"));
        if (app->view != APP_MENU && app->view != APP_PICKER_MENU)
            draw_text(8, 202, app->message[0] ? L(app->message) : browser->count ?
                      browser->entries[browser->selected].name : "", UI_BLACK);
    }
    finish(ui);
}
