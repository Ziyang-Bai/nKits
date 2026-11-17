#ifndef NKITS_TEST_NGC_H
#define NKITS_TEST_NGC_H

#include <stdint.h>

typedef void *Gc;
typedef enum { Regular9 = 9, Regular10 = 10, Bold12 = 268 } gui_gc_Font;
typedef enum { GC_SM_TOP = 64 } gui_gc_StringMode;
void gui_gc_setFont(Gc gc, gui_gc_Font font);
void gui_gc_setColorRGB(Gc gc, int red, int green, int blue);
void gui_gc_drawString(Gc gc, char *text, int x, int y, gui_gc_StringMode mode);
void gui_gc_fillRect(Gc gc, int x, int y, int width, int height);
int gui_gc_getFontHeight(Gc gc, gui_gc_Font font);
int gui_gc_getStringSmallHeight(Gc gc, gui_gc_Font font, char *text, int start, int length);
int gui_gc_getCharWidth(Gc gc, gui_gc_Font font, short value);

#endif
