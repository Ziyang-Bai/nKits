#ifndef NTEXTS_LIBRARY_H
#define NTEXTS_LIBRARY_H

#include "storage.h"
#include <ngc.h>
#include <stddef.h>

#define HOME_VISIBLE_ROWS 8

void draw_home(Gc gc, AppState *app, int sel, int top);
int browse_files(Gc gc, AppState *app, const char *data_dir, char *out, size_t outcap);

#endif
