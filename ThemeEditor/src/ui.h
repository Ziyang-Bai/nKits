#ifndef NKITS_THEME_UI_H
#define NKITS_THEME_UI_H

#include <stdint.h>
#include <stdbool.h>

void theme_ui_init(void);
bool theme_ui_open(bool native);
void theme_ui_close(void);
void theme_ui_clear(const char *title);
void theme_ui_box(int x, int y, int w, int h, uint32_t color);
void theme_ui_text(int x, int y, const char *text, uint32_t color);
void theme_ui_row(int y, const char *text, bool selected);
void theme_ui_hint(int y, const char *text);
void theme_ui_footer(const char *text);
uint16_t *theme_ui_pixels(void);

#endif
