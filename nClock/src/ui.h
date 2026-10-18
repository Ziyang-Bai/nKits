#ifndef NCLOCK_UI_H
#define NCLOCK_UI_H

#include "clock.h"

typedef void (*ClockService)(Clock *clock);

bool clock_ui_open(ClockService service);
void clock_ui_close(void);
int clock_ui_menu(Clock *clock, const char *title, const char *const *items, unsigned count, bool big);
void clock_ui_position(Clock *clock);
bool clock_power_open(void);
void clock_power_off(void);
void clock_power_on(void);
void clock_ui_level(unsigned value);
void clock_ui_wake(const Clock *clock);
unsigned clock_ui_brightness(void);
unsigned clock_ui_max_brightness(void);
void clock_ui_set_brightness(unsigned value);
void clock_reboot(void);

#endif
