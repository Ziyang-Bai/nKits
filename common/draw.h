#ifndef NKITS_DRAW_H
#define NKITS_DRAW_H

#include <ngc.h>
#include <stdbool.h>
#include <stdint.h>

#define UI_WIDTH 320
#define UI_HEIGHT 240
#define UI_LINE_UNITS 192
#define UI_FOOTER_Y 216

typedef struct {
    int bg, fg, muted, accent, highlight_bg, highlight_fg;
} UiTheme;

extern const UiTheme ui_themes[3];

int ui_utf8_to_u16(const char *text, uint16_t *out, int capacity);
int ui_line_height(Gc gc, gui_gc_Font font);
void ui_draw_text(Gc gc, const char *text, int x, int y, gui_gc_Font font, int color);
void ui_draw_u16(Gc gc, const uint16_t *text, int x, int y, gui_gc_Font font, int color);
int ui_draw_wrapped(Gc gc, const char *text, int x, int y, int width, gui_gc_Font font, int color);
void ui_fill(Gc gc, int x, int y, int width, int height, int color);
/* Drawing uses an already begun GC; the caller owns presentation and input. */
void ui_draw_header(Gc gc, const UiTheme *theme, const char *title);
void ui_draw_row(Gc gc, const UiTheme *theme, int y, const char *text, bool selected);
void ui_draw_footer(Gc gc, const UiTheme *theme, const char *text);

#endif
