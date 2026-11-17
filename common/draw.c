#include "draw.h"

const UiTheme ui_themes[3] = {
    {0xffffff, 0x111111, 0x777777, 0x005bbb, 0xd8ebff, 0x000000},
    {0x111111, 0xe8e8e8, 0x999999, 0x80bfff, 0x26384a, 0xffffff},
    {0xf3ecd4, 0x24210f, 0x766f51, 0x4f7f3f, 0xdde7bf, 0x111111}
};

static void color(Gc gc, int rgb) {
    gui_gc_setColorRGB(gc, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

int ui_utf8_to_u16(const char *text, uint16_t *out, int capacity) {
    int n = 0;
    if (capacity <= 0) return 0;
    while (*text && n + 1 < capacity) {
        unsigned char c = (unsigned char)*text++;
        uint32_t value;
        int need = 0;
        if (c < 0x80) value = c;
        else if ((c & 0xe0) == 0xc0) { value = c & 0x1f; need = 1; }
        else if ((c & 0xf0) == 0xe0) { value = c & 0x0f; need = 2; }
        else if ((c & 0xf8) == 0xf0) { value = c & 7; need = 3; }
        else value = 0xfffd;
        for (int i = 0; i < need; ++i) {
            unsigned char d = (unsigned char)*text;
            if ((d & 0xc0) != 0x80) { value = 0xfffd; break; }
            ++text;
            value = (value << 6) | (d & 0x3f);
        }
        if (value > 0xffff) value = 0xfffd;
        out[n++] = (uint16_t)value;
    }
    out[n] = 0;
    return n;
}

void ui_draw_text(Gc gc, const char *text, int x, int y, gui_gc_Font font, int rgb) {
    uint16_t line[UI_LINE_UNITS];
    ui_utf8_to_u16(text, line, UI_LINE_UNITS);
    ui_draw_u16(gc, line, x, y, font, rgb);
}

void ui_draw_u16(Gc gc, const uint16_t *text, int x, int y, gui_gc_Font font, int rgb) {
    gui_gc_setFont(gc, font);
    color(gc, rgb);
    gui_gc_drawString(gc, (char *)text, x, y, GC_SM_TOP);
}

int ui_line_height(Gc gc, gui_gc_Font font) {
    static const uint16_t sample[] = {'M', 'g', 0x4e2d, 0};
    gui_gc_setFont(gc, font);
    /* String metrics include the extents used by drawString, including CJK. */
    return gui_gc_getStringSmallHeight(gc, font, (char *)sample, 0, 3) + 2;
}

int ui_draw_wrapped(Gc gc, const char *text, int x, int y, int width, gui_gc_Font font, int rgb) {
    uint16_t utf16[UI_LINE_UNITS], line[UI_LINE_UNITS];
    int n = ui_utf8_to_u16(text, utf16, UI_LINE_UNITS);
    int length = 0, pixels = 0;
    int height = ui_line_height(gc, font);
    for (int i = 0; i < n; ++i) {
        if (utf16[i] == '\n') {
            line[length] = 0;
            ui_draw_u16(gc, line, x, y, font, rgb);
            length = pixels = 0;
            y += height;
            continue;
        }
        int advance = gui_gc_getCharWidth(gc, font, (short)utf16[i]);
        if (advance <= 0) advance = (int)font & 31;
        if (length && pixels + advance > width) {
            line[length] = 0;
            ui_draw_u16(gc, line, x, y, font, rgb);
            length = pixels = 0;
            y += height;
        }
        line[length++] = utf16[i];
        pixels += advance;
    }
    if (length) {
        line[length] = 0;
        ui_draw_u16(gc, line, x, y, font, rgb);
        y += height;
    }
    return y;
}

void ui_fill(Gc gc, int x, int y, int width, int height, int rgb) {
    color(gc, rgb);
    gui_gc_fillRect(gc, x, y, width, height);
}

void ui_draw_header(Gc gc, const UiTheme *theme, const char *title) {
    ui_fill(gc, 0, 0, UI_WIDTH, UI_HEIGHT, theme->bg);
    ui_draw_text(gc, title, 8, 6, Bold12, theme->fg);
}

void ui_draw_row(Gc gc, const UiTheme *theme, int y, const char *text, bool selected) {
    if (selected) ui_fill(gc, 4, y - 2, UI_WIDTH - 8, 20, theme->highlight_bg);
    ui_draw_text(gc, text, 10, y, Regular10, selected ? theme->highlight_fg : theme->fg);
}

void ui_draw_footer(Gc gc, const UiTheme *theme, const char *text) {
    ui_draw_text(gc, text, 8, UI_FOOTER_Y, Regular9, theme->muted);
}
