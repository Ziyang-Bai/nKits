#ifndef NKITS_UI_H
#define NKITS_UI_H

#include "draw.h"
#include <ndialog.h>
#include <ngc.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_ENGLISH,
    UI_CHINESE,
    UI_FRENCH
} UiLanguage;

typedef struct {
    Gc gc;
    unsigned interrupt_mask;
} UiCanvas;

UiLanguage ui_language_init(void);
UiLanguage ui_language(void);
const char *ui_text(const char *english, const char *chinese, const char *french);
const char *ui_text_for(UiLanguage language, const char *english, const char *chinese, const char *french);
const char *ui_error(const char *english);

/* Shows a Save/Cancel form, destroys it, and restores the caller's IRQ/FIQ mask. */
bool ui_show(ndialog_t *dialog);

int ui_menu(UiCanvas *canvas, const char *heading, const char *const *items, unsigned count, unsigned selected);
void ui_about(const char *program, const char *credits, const char *license);

bool ui_canvas_open(UiCanvas *canvas);
void ui_canvas_close(UiCanvas *canvas);
void ui_canvas_begin(UiCanvas *canvas);
void ui_canvas_end(UiCanvas *canvas);
uint16_t *ui_canvas_pixels(const UiCanvas *canvas);
/* Coordinates are pixels; width clips text using the OS font. */
void ui_canvas_text(UiCanvas *canvas, int x, int y, int width, const char *text, int rgb);

#endif
