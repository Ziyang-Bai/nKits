#ifndef NHEX_APP_H
#define NHEX_APP_H

#include "editor.h"
#include "file_buffer.h"
#include "ui.h"

#include <stdbool.h>

#define NHEX_STATUS_MAX 96u

typedef struct {
    FileBuffer buffer;
    Editor editor;
    Ui ui;
    bool file_open;
    bool quit;
    char status[NHEX_STATUS_MAX];
} App;

bool app_init(App *app, const char *initial_path);
void app_run(App *app);
void app_destroy(App *app);

#endif
