#include "diagnose.h"

#include <libndls.h>

unsigned __real_msleep(unsigned milliseconds);
void __real_wait_no_key_pressed(void);
void __real_lcd_blit(void *buffer, scr_type_t type);

unsigned __wrap_msleep(unsigned milliseconds) {
    clock_diag("sdk.sleep.before", milliseconds);
    unsigned result = __real_msleep(milliseconds);
    clock_diag("sdk.sleep.after", result);
    return result;
}

void __wrap_wait_no_key_pressed(void) {
    clock_diag("sdk.key_release.before", 0);
    __real_wait_no_key_pressed();
    clock_diag("sdk.key_release.after", 0);
}

void __wrap_lcd_blit(void *buffer, scr_type_t type) {
    clock_diag("sdk.blit.before", (unsigned)type);
    __real_lcd_blit(buffer, type);
    clock_diag("sdk.blit.after", (unsigned)type);
}
