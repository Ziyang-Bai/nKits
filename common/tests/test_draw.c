#include "draw.h"

#include <assert.h>
#include <stdio.h>

static struct {
    uint16_t text[UI_LINE_UNITS];
    int x, y;
} lines[16];
static unsigned line_count;
static gui_gc_Font selected_font;

void gui_gc_setFont(Gc gc, gui_gc_Font font) { (void)gc; selected_font = font; }
void gui_gc_setColorRGB(Gc gc, int r, int g, int b) { (void)gc; (void)r; (void)g; (void)b; }
void gui_gc_fillRect(Gc gc, int x, int y, int w, int h) {
    (void)gc; (void)x; (void)y; (void)w; (void)h;
}
int gui_gc_getFontHeight(Gc gc, gui_gc_Font font) { (void)gc; (void)font; return 0; }
int gui_gc_getStringSmallHeight(Gc gc, gui_gc_Font font, char *text, int start, int length) {
    (void)gc; (void)font; (void)text; (void)start; (void)length;
    return selected_font ? 14 : 0;
}
int gui_gc_getCharWidth(Gc gc, gui_gc_Font font, short value) {
    (void)gc; (void)font;
    return value == 'i' ? 3 : value == 0x4e2d ? 12 : 7;
}
void gui_gc_drawString(Gc gc, char *text, int x, int y, gui_gc_StringMode mode) {
    const uint16_t *wide = (const uint16_t *)text;
    unsigned n = 0;
    (void)gc; (void)mode;
    assert(line_count < sizeof(lines) / sizeof(lines[0]));
    while (wide[n]) { lines[line_count].text[n] = wide[n]; ++n; }
    lines[line_count].text[n] = 0;
    lines[line_count].x = x;
    lines[line_count++].y = y;
}

static void conversion(void) {
    uint16_t text[8] = {0};
    assert(ui_utf8_to_u16("中éA", text, 8) == 3);
    assert(text[0] == 0x4e2d && text[1] == 0xe9 && text[2] == 'A' && text[3] == 0);
    text[3] = 0x1234;
    assert(ui_utf8_to_u16("中éA", text, 3) == 2);
    assert(text[0] == 0x4e2d && text[1] == 0xe9 && text[2] == 0 && text[3] == 0x1234);
    assert(ui_utf8_to_u16("\xe4\xb8", text, 8) == 1 && text[0] == 0xfffd);
    text[0] = 0x1234;
    assert(ui_utf8_to_u16("A", text, 0) == 0 && text[0] == 0x1234);
}

static void wrapping(void) {
    line_count = 0;
    int bottom = ui_draw_wrapped(NULL, "ii中A\nB", 8, 10, 18, Regular10, 0);
    assert(line_count == 3 && bottom == 58);
    assert(lines[0].text[0] == 'i' && lines[0].text[1] == 'i' &&
           lines[0].text[2] == 0x4e2d && lines[0].text[3] == 0);
    assert(lines[1].text[0] == 'A' && lines[1].text[1] == 0);
    assert(lines[2].text[0] == 'B' && lines[2].text[1] == 0);
    assert(lines[0].x == 8 && lines[0].y == 10 && lines[1].y == 26 && lines[2].y == 42);
    line_count = 0;
    assert(ui_draw_wrapped(NULL, "中", 0, 0, 4, Regular10, 0) == 16);
    assert(line_count == 1 && lines[0].text[0] == 0x4e2d && lines[0].text[1] == 0);
}

int main(void) {
    conversion();
    wrapping();
    puts("shared UI text tests passed");
    return 0;
}
