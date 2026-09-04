#ifndef NEDIT_APP_H
#define NEDIT_APP_H

#include "editor.h"
#include "text_buffer.h"
#include "ui.h"

#include <stdbool.h>

#define NEDIT_STATUS_MAX 96u

typedef struct {
    TextBuffer buffer;
    Editor editor;
    Ui ui;
    bool file_open;
    bool quit;
    bool caps_lock;
    bool sticky_keys;
    char status[NEDIT_STATUS_MAX];
} App;

bool app_init(App *app, const char *initial_path);
void app_run(App *app);
void app_destroy(App *app);

#endif
