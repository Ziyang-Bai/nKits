#include "app.h"
#include "resident.h"
#include "ui.h"
#include "language.h"

#include <libndls.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    App *app;
    Ui *ui;
} Frontend;

static bool pump(void *ctx) {
    Frontend *frontend = ctx;
    App *app = frontend->app;
    MscInfo info;
    ui_present(frontend->ui);
    InputEvent event = input_read();
    bool cancel = event.action == INPUT_BACK;
    msc_poll(&info);
    if (app->job) {
        if (cancel) {
            fileops_cancel(app->job);
            app->dirty = true;
        }
        cancel = false;
    }
    if (storage_mutating()) cancel = false;
    if (app->dirty) {
        ui_draw(frontend->ui, app);
        app->dirty = false;
    }
    return cancel;
}

int main(void) {
    Ui ui = {0};
    App *app;
    Frontend frontend;
    MscHooks hooks;
    const char *error;
    bool publisher = false;
    int result = 0;

    language_init("/documents/nusb.cfg");
    if (!ui_init(&ui)) return 1;
    input_reset();
    error = resident_init(&publisher);
    app = error ? NULL : malloc(sizeof(*app));
    if (!error && !app) error = "Out of memory";
    if (error) {
        ui_error(&ui, error);
        while (input_read().action != INPUT_BACK) msleep(10);
        result = 1;
    } else {
        frontend.app = app;
        frontend.ui = &ui;
        hooks.pump = pump;
        hooks.ctx = &frontend;
        app_init(app, &hooks);
        if (publisher) strcpy(app->message, "Connect or reinsert an OTG drive");
        /* OS USB enumeration and completion callbacks run with IRQs enabled. */
        int intmask = TCT_Local_Control_Interrupts(0);
        while (app->running) {
            ui_present(&ui);
            app_tick(app);
            app_handle(app, input_read());
            if (app->dirty) {
                ui_draw(&ui, app);
                app->dirty = false;
            }
            msc_wait();
        }
        app_destroy(app);
        TCT_Local_Control_Interrupts(intmask);
        free(app);
    }
    wait_no_key_pressed();
    ui_destroy(&ui);
    if (!result) {
        /* Publish frontend filesystem changes to the OS document browser. */
        int intmask = TCT_Local_Control_Interrupts(0);
        refresh_osscr();
        TCT_Local_Control_Interrupts(intmask);
    }
    if (publisher) _exit(result);
    return result;
}
