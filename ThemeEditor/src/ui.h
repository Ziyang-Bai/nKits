#ifndef NKITS_THEME_UI_H
#define NKITS_THEME_UI_H

#include <stdint.h>

void theme_ui_clear(void);
void theme_ui_box(int x, int y, int w, int h, uint32_t color);
void theme_ui_text(int x, int y, const char *text, uint32_t color);
uint16_t *theme_ui_pixels(void);

#endif
