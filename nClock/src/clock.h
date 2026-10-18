#ifndef NCLOCK_CLOCK_H
#define NCLOCK_CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLOCK_WIDTH 104
#define CLOCK_HEIGHT 11
#define CLOCK_CONFIG_SIZE 80
#define CLOCK_MAX_DELAY UINT32_MAX
#define CLOCK_LAST_SECOND 4294967295LL
#define CLOCK_TEXT_SIZE 20
#define CLOCK_WARN 1u
#define CLOCK_ALARM 2u

typedef struct {
    int year, month, day, hour, minute, second;
} ClockDate;

typedef struct {
    int64_t offset, deadline;
    uint32_t last_raw, previous, timeout;
    unsigned x, y, pending;
    bool hour12, seconds, visible, countdown;
    bool reboot, check_time, flip, fades, warned, foreground, disabled;
} Clock;

void clock_defaults(Clock *clock);
int clock_month_days(int year, int month);
bool clock_from_date(const ClockDate *date, int64_t *seconds);
void clock_to_date(int64_t seconds, ClockDate *date);
int64_t clock_time(const Clock *clock, uint32_t raw);
bool clock_set_time(Clock *clock, uint32_t raw, const ClockDate *date);
bool clock_start(Clock *clock, uint32_t raw, uint32_t delay);
bool clock_target(Clock *clock, uint32_t raw, const ClockDate *date);
void clock_stop(Clock *clock);
uint32_t clock_remaining(const Clock *clock, uint32_t raw);
void clock_update(Clock *clock, uint32_t raw);
unsigned clock_alert(Clock *clock);
unsigned clock_blink(const Clock *clock, uint32_t raw);
void clock_move(Clock *clock, int dx, int dy);
void clock_text(const Clock *clock, uint32_t raw, char text[CLOCK_TEXT_SIZE]);
void clock_date_text(const Clock *clock, uint32_t raw, char text[40]);
void clock_draw(const Clock *clock, uint32_t raw, volatile uint16_t *pixels);
void clock_label(volatile uint16_t *pixels, int x, int y, const char *text, uint16_t fg, uint16_t bg);
void clock_big(const Clock *clock, uint32_t raw, volatile uint16_t *pixels);
void clock_encode(const Clock *clock, uint32_t raw, unsigned char data[CLOCK_CONFIG_SIZE]);
bool clock_decode(Clock *clock, uint32_t raw, const unsigned char *data, size_t size);

#endif
