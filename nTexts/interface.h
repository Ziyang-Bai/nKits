#ifndef NTEXTS_INTERFACE_H
#define NTEXTS_INTERFACE_H

#include "storage.h"
#include "../common/draw.h"
#include <libndls.h>
#include <stddef.h>

extern int last_key_ctrl;

enum {
    SHORTCUT_HOME = 1,
    SHORTCUT_BROWSER = 2,
    SHORTCUT_READER = 4
};

void wait_release(void);
int wait_key(void);
void app_message(Gc gc, const AppState *app, const char *title, const char *msg);
void debug_set_runtime(Gc gc, AppState *app, scr_type_t type);
void debug_install_handlers(void);
void debug_fail(const char *where, const char *fmt, ...);
void debug_fail_errno(const char *where, const char *what);
int choose_list_rows(Gc gc, AppState *app, const char *title, const char **items,
                     const uint16_t *first, size_t stride, int count, int zero_pick);
int choose_list(Gc gc, AppState *app, const char *title, const char **items, int count);
int choose_u16_list(Gc gc, AppState *app, const char *title, const uint16_t *first,
                    size_t stride, int count);
int request_u16_input(const char *title, const char *message, const uint16_t *initial,
                      uint16_t *out, size_t capacity);
void about_page(Gc gc, AppState *app);
void usage_page(Gc gc, AppState *app);
int shortcut_hints(Gc gc, AppState *app, const char *data_dir,
                   uint32_t context, int search_active, int manual);
void shortcut_hints_reset(void);

#endif
