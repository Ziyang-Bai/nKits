#ifndef NHEX_UI_H
#define NHEX_UI_H

#include "editor.h"

#include <stdbool.h>

typedef struct {
    bool initialized;
} Ui;

bool ui_init(Ui *ui);
void ui_destroy(Ui *ui);
void ui_draw(Ui *ui, Editor *editor, const char *message);
void ui_message(const char *title, const char *message);
int ui_confirm_save(Ui *ui);
char *ui_prompt(const char *title, const char *label, const char *initial);
char *ui_choose_file(Ui *ui, const char *initial_path, bool *create);
void ui_show_menu(Ui *ui);

#endif
