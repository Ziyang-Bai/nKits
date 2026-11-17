#ifndef NTEXTS_READER_H
#define NTEXTS_READER_H

#include "storage.h"
#include <ngc.h>

typedef struct Reader Reader;

int open_reader(const char *path, Gc gc, AppState *app, const char *data_dir);
/* Settings rebuild the current session's index without exposing its state. */
int reader_apply_layout(Reader *r, int font_changed);
int reader_reindex(Reader *r);

#endif
