#include "ui.h"
#include "common/draw.h"
#include "resident.h"

static Gc source, graphics;
static bool native;
static unsigned mask;
static int (*begin)(Gc);
static void (*finish)(Gc);
static void (*release)(Gc);
static void (*font)(Gc, gui_gc_Font);
static void (*color)(Gc, int, int, int);
static void (*fill)(Gc, int, int, int, int);
static void (*draw)(Gc, char *, int, int, gui_gc_StringMode);
static void (*clip)(Gc, int, int, int, int, gui_gc_ClipRectOp);

void theme_ui_init(void) { source = gui_gc_global_GC(); }

bool theme_ui_open(bool callback) {
    native = callback;
    /* GC calls can change interrupt masks; preserve the event caller's state. */
    unsigned saved = resident_lock();
    resident_unlock(saved);
    if (native) {
        Gc (*copy)(Gc, int, int) = resident_native(e_gui_gc_copy);
        void (*region)(Gc, int, int, int, int, int, int, int, int) =
            resident_native(e_gui_gc_setRegion);
        begin = resident_native(e_gui_gc_begin);
        finish = resident_native(e_gui_gc_finish);
        release = resident_native(e_gui_gc_free);
        font = resident_native(e_gui_gc_setFont);
        color = resident_native(e_gui_gc_setColorRGB);
        fill = resident_native(e_gui_gc_fillRect);
        draw = resident_native(e_gui_gc_drawString);
        clip = resident_native(e_gui_gc_clipRect);
        if (!source || !copy || !region || !begin || !finish || !release ||
            !font || !color || !fill || !draw || !clip) {
            resident_unlock(saved);
            return false;
        }
        graphics = copy(source, UI_WIDTH, UI_HEIGHT);
        if (graphics) region(graphics, 0, 0, UI_WIDTH, UI_HEIGHT, 0, 0, UI_WIDTH, UI_HEIGHT);
    } else {
        graphics = source ? gui_gc_copy(source, UI_WIDTH, UI_HEIGHT) : NULL;
        if (graphics) gui_gc_setRegion(graphics, 0, 0, UI_WIDTH, UI_HEIGHT, 0, 0, UI_WIDTH, UI_HEIGHT);
    }
    resident_unlock(saved);
    return graphics != NULL;
}

void theme_ui_close(void) {
    unsigned saved = resident_lock();
    resident_unlock(saved);
    if (native) release(graphics);
    else gui_gc_free(graphics);
    graphics = NULL;
    resident_unlock(saved);
}

void theme_ui_box(int x, int y, int w, int h, uint32_t rgb) {
    if (!native) { ui_fill(graphics, x, y, w, h, rgb); return; }
    color(graphics, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    fill(graphics, x, y, w, h);
}

static void text(int x, int y, const char *s, gui_gc_Font size, int rgb) {
    if (!native) { ui_draw_text(graphics, s, x, y, size, rgb); return; }
    uint16_t line[UI_LINE_UNITS];
    ui_utf8_to_u16(s, line, UI_LINE_UNITS);
    font(graphics, size);
    color(graphics, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    draw(graphics, (char *)line, x, y, GC_SM_TOP);
}

static void text_clip(int x, int width) {
    if (native) clip(graphics, x, 0, width, UI_HEIGHT, GC_CRO_SET);
    else gui_gc_clipRect(graphics, x, 0, width, UI_HEIGHT, GC_CRO_SET);
}

void theme_ui_clear(const char *title) {
    mask = resident_lock();
    resident_unlock(mask);
    if (native) begin(graphics);
    else gui_gc_begin(graphics);
    text_clip(0, UI_WIDTH);
    if (!native) ui_draw_header(graphics, &ui_themes[0], title);
    else {
        theme_ui_box(0, 0, UI_WIDTH, UI_HEIGHT, ui_themes[0].bg);
        text(8, 6, title, Bold12, ui_themes[0].fg);
    }
}

void theme_ui_text(int x, int y, const char *s, uint32_t rgb) {
    text_clip(x, 312 - x);
    text(x, y, s, Regular10, rgb);
    text_clip(0, UI_WIDTH);
}

void theme_ui_row(int y, const char *s, bool selected) {
    if (!native) ui_draw_row(graphics, &ui_themes[0], y, "", selected);
    else if (selected) theme_ui_box(4, y - 2, UI_WIDTH - 8, 20, ui_themes[0].highlight_bg);
    text_clip(10, 274);
    text(10, y, s, Regular10, selected ? ui_themes[0].highlight_fg : ui_themes[0].fg);
    text_clip(0, UI_WIDTH);
}

void theme_ui_hint(int y, const char *s) { text(8, y, s, Regular9, ui_themes[0].muted); }

void theme_ui_footer(const char *s) {
    if (!native) ui_draw_footer(graphics, &ui_themes[0], s);
    else theme_ui_hint(UI_FOOTER_Y, s);
}

uint16_t *theme_ui_pixels(void) {
    if (native) finish(graphics);
    else gui_gc_finish(graphics);
    resident_unlock(mask);
    /* Same logical RGB565 surface walk as SDK gui_gc_blit_to_screen. */
    void **surface = *(void ***)graphics[9];
    return *(uint16_t **)surface[8];
}
