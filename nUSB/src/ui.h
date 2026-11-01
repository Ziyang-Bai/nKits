#ifndef NUSB_UI_H
#define NUSB_UI_H

#include "app.h"

typedef struct {
    bool initialized;
    bool foreground;
    uint32_t cursor_control;
    uint32_t foreground_irqs;
    void *refresh_timer;
    int (*control_timer)(void *timer, unsigned command);
} Ui;

bool ui_init(Ui *ui);
void ui_destroy(Ui *ui);
void ui_draw(Ui *ui, const App *app);
void ui_error(Ui *ui, const char *message);
void ui_present(Ui *ui);

#endif
