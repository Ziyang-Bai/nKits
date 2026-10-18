/* nClock, Copyright (C) 2014 Levak; Xavier Andreani (2016). GPL-3.0-or-later. */
#include "clock.h"

#include <stdio.h>
#include <string.h>

static bool leap(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int clock_month_days(int year, int month) {
    static const unsigned char days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && leap(year));
}

void clock_defaults(Clock *clock) {
    memset(clock, 0, sizeof(*clock));
    clock->x = 320 - CLOCK_WIDTH;
    clock->seconds = true;
    clock->visible = true;
    clock->check_time = true;
    clock->fades = true;
    clock->timeout = 180;
}

bool clock_from_date(const ClockDate *date, int64_t *seconds) {
    int year, month;
    int64_t days = 0, value;
    if (date->year < 1970 || date->year > 2106 || date->month < 1 || date->month > 12 ||
        date->day < 1 || date->day > clock_month_days(date->year, date->month) ||
        date->hour < 0 || date->hour > 23 || date->minute < 0 || date->minute > 59 ||
        date->second < 0 || date->second > 59) return false;
    for (year = 1970; year < date->year; ++year) days += 365 + leap(year);
    for (month = 1; month < date->month; ++month) days += clock_month_days(date->year, month);
    days += date->day - 1;
    value = days * 86400 + date->hour * 3600 + date->minute * 60 + date->second;
    if (value > CLOCK_LAST_SECOND) return false;
    *seconds = value;
    return true;
}

void clock_to_date(int64_t seconds, ClockDate *date) {
    int days, count;
    if (seconds < 0) seconds = 0;
    if (seconds > CLOCK_LAST_SECOND) seconds = CLOCK_LAST_SECOND;
    days = (int)(seconds / 86400);
    date->hour = (int)(seconds % 86400) / 3600;
    date->minute = (int)(seconds % 3600) / 60;
    date->second = (int)(seconds % 60);
    date->year = 1970;
    while (days >= (count = 365 + leap(date->year))) {
        days -= count;
        ++date->year;
    }
    date->month = 1;
    while (days >= (count = clock_month_days(date->year, date->month))) {
        days -= count;
        ++date->month;
    }
    date->day = days + 1;
}

/* Extend an RTC wrap without confusing it with a reset when loading a backup. */
static int64_t now(const Clock *clock, uint32_t raw) {
    int64_t value = (int64_t)raw + clock->offset;
    if (raw < clock->last_raw && clock->last_raw - raw > INT32_MAX)
        value += 4294967296LL;
    return value;
}

int64_t clock_time(const Clock *clock, uint32_t raw) {
    int64_t value = now(clock, raw);
    if (value < 0) return 0;
    if (value > CLOCK_LAST_SECOND) return CLOCK_LAST_SECOND;
    return value;
}

bool clock_set_time(Clock *clock, uint32_t raw, const ClockDate *date) {
    int64_t seconds, difference;
    if (!clock_from_date(date, &seconds)) return false;
    difference = seconds - now(clock, raw);
    if (clock->countdown) clock->deadline += difference;
    clock->offset = seconds - raw;
    clock->last_raw = raw;
    return true;
}

bool clock_start(Clock *clock, uint32_t raw, uint32_t delay) {
    if (!delay) return false;
    clock->deadline = now(clock, raw) + delay;
    clock->countdown = true;
    clock->warned = delay <= 300;
    clock->previous = delay;
    clock->pending = 0;
    return true;
}

bool clock_target(Clock *clock, uint32_t raw, const ClockDate *date) {
    int64_t value, delay;
    if (!clock_from_date(date, &value)) return false;
    delay = value - now(clock, raw);
    if (delay <= 0 || delay > UINT32_MAX) return false;
    return clock_start(clock, raw, (uint32_t)delay);
}

void clock_stop(Clock *clock) {
    clock->countdown = false;
    clock->pending = 0;
    clock->previous = 0;
}

uint32_t clock_remaining(const Clock *clock, uint32_t raw) {
    int64_t delta = clock->deadline - now(clock, raw);
    if (!clock->countdown || delta <= 0) return 0;
    return delta > UINT32_MAX ? UINT32_MAX : (uint32_t)delta;
}

void clock_update(Clock *clock, uint32_t raw) {
    uint32_t left;
    if (clock->disabled) return;
    left = clock_remaining(clock, raw);
    if (clock->countdown) {
        if (!left) {
            clock->pending |= CLOCK_ALARM;
            clock->pending &= ~CLOCK_WARN;
            clock->countdown = false;
        } else if (!clock->warned && clock->previous > 300 && left <= 300) {
            clock->warned = true;
            clock->pending |= CLOCK_WARN;
        }
        clock->previous = left;
    }
    if (raw < clock->last_raw && clock->last_raw - raw > INT32_MAX)
        clock->offset += 4294967296LL;
    clock->last_raw = raw;
}

unsigned clock_alert(Clock *clock) {
    unsigned pending = clock->pending;
    clock->pending = 0;
    return pending;
}

unsigned clock_blink(const Clock *clock, uint32_t raw) {
    uint32_t left = clock_remaining(clock, raw);
    if (!clock->countdown || !(left & 1)) return 0;
    if (left < 60) return 2;
    return left < 300 ? 1 : 0;
}

void clock_move(Clock *clock, int dx, int dy) {
    int x = (int)clock->x + dx, y = (int)clock->y + dy;
    if (x < 0) x = 0;
    if (x > 320 - CLOCK_WIDTH) x = 320 - CLOCK_WIDTH;
    if (y < 0) y = 0;
    if (y > 240 - CLOCK_HEIGHT) y = 240 - CLOCK_HEIGHT;
    clock->x = (unsigned)x;
    clock->y = (unsigned)y;
}

void clock_text(const Clock *clock, uint32_t raw, char text[CLOCK_TEXT_SIZE]) {
    uint32_t value = clock->countdown ? clock_remaining(clock, raw) : (uint32_t)(clock_time(clock, raw) % 86400);
    unsigned hour = value / 3600 % 24;
    char *out = text;
    if (clock->countdown && value >= 86400) {
        unsigned days = value / 86400, divisor = 10000;
        while (divisor > days && divisor > 1) divisor /= 10;
        do {
            *out++ = (char)('0' + days / divisor);
            days %= divisor;
            divisor /= 10;
        } while (divisor);
        *out++ = 'd';
        *out++ = ' ';
    }
    if (!clock->countdown && clock->hour12) {
        hour %= 12;
        if (!hour) hour = 12;
    }
    *out++ = (char)('0' + hour / 10);
    *out++ = (char)('0' + hour % 10);
    *out++ = ':';
    *out++ = (char)('0' + value / 600 % 6);
    *out++ = (char)('0' + value / 60 % 10);
    if (clock->seconds || clock->countdown) {
        *out++ = ':';
        *out++ = (char)('0' + value / 10 % 6);
        *out++ = (char)('0' + value % 10);
    }
    if (!clock->countdown && clock->hour12) {
        *out++ = ' ';
        *out++ = value / 3600 >= 12 ? 'P' : 'A';
        *out++ = 'M';
    }
    *out = 0;
}

void clock_date_text(const Clock *clock, uint32_t raw, char text[40]) {
    static const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int64_t value = clock_time(clock, raw);
    ClockDate date;
    clock_to_date(value, &date);
    snprintf(text, 40, "%s %02d %s %04d", days[(value / 86400 + 4) % 7], date.day, months[date.month - 1], date.year);
}

static const unsigned char *glyph(char ch) {
    static const unsigned char font[][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},
        {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e},
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},
        {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,1},
        {0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e},{0x7f,9,9,9,6},
        {0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},
        {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
        {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
        {0,0x36,0x36,0,0},{8,8,8,8,8},{0,0,0x5f,0,0},{0,0x60,0x60,0,0},
        {0x20,0x10,8,4,2},{0,0,0,0,0}
    };
    unsigned index = 41;
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    if (ch >= '0' && ch <= '9') index = (unsigned)(ch - '0');
    else if (ch >= 'A' && ch <= 'Z') index = 10u + (unsigned)(ch - 'A');
    else if (ch == ':') index = 36;
    else if (ch == '-') index = 37;
    else if (ch == '!') index = 38;
    else if (ch == '.') index = 39;
    else if (ch == '/') index = 40;
    return font[index];
}

static void box(volatile uint16_t *pixels, int x, int y, int w, int h, uint16_t color) {
    int row, col;
    for (row = y; row < y + h && row < 240; ++row)
        for (col = x; col < x + w && col < 320; ++col)
            if (row >= 0 && col >= 0) pixels[row * 320 + col] = color;
}

void clock_label(volatile uint16_t *pixels, int x, int y, const char *text, uint16_t fg, uint16_t bg) {
    unsigned i, row, col;
    for (i = 0; text[i]; ++i) {
        const unsigned char *bits = glyph(text[i]);
        for (col = 0; col < 6; ++col)
            for (row = 0; row < 8; ++row)
                box(pixels, x + (int)i * 6 + (int)col, y + (int)row, 1, 1,
                    col < 5 && row < 7 && (bits[col] & (1u << row)) ? fg : bg);
    }
}

void clock_draw(const Clock *clock, uint32_t raw, volatile uint16_t *pixels) {
    char text[CLOCK_TEXT_SIZE];
    unsigned x, y, i, blink;
    if (!pixels || !clock->visible || clock->disabled) return;
    x = clock->x > 320 - CLOCK_WIDTH ? 320 - CLOCK_WIDTH : clock->x;
    y = clock->y > 240 - CLOCK_HEIGHT ? 240 - CLOCK_HEIGHT : clock->y;
    box(pixels, (int)x, (int)y, CLOCK_WIDTH, CLOCK_HEIGHT, 0xffff);
    clock_text(clock, raw, text);
    blink = clock_blink(clock, raw);
    for (i = 0; text[i]; ++i) {
        char ch[2] = {text[i], 0};
        if ((blink == 2 && ch[0] >= '0' && ch[0] <= '9') || (blink == 1 && ch[0] == ':')) ch[0] = ' ';
        clock_label(pixels, (int)x + 1 + (int)i * 6, (int)y + 2, ch, 0, 0xffff);
    }
}

static void digit(volatile uint16_t *pixels, int x, int y, unsigned value, int scale, bool blank) {
    static const unsigned char segments[] = {0x3f,6,0x5b,0x4f,0x66,0x6d,0x7d,7,0x7f,0x6f};
    static const unsigned char rects[7][4] = {{1,0,4,1},{5,1,1,4},{5,6,1,4},{1,10,4,1},{0,6,1,4},{0,1,1,4},{1,5,4,1}};
    unsigned i;
    for (i = 0; i < 7; ++i)
        box(pixels, x + rects[i][0] * scale, y + rects[i][1] * scale,
            rects[i][2] * scale, rects[i][3] * scale,
            !blank && (segments[value % 10] & (1u << i)) ? 0 : 0xef7d);
}

static void display(volatile uint16_t *pixels, int y, uint32_t value, int scale, unsigned blink, bool seconds) {
    unsigned values[6] = {value / 36000 % 3, value / 3600 % 10, value / 600 % 6, value / 60 % 10, value / 10 % 6, value % 10};
    unsigned i, count = seconds ? 6 : 4;
    int width = (seconds ? 46 : 30) * scale;
    int x = (320 - width) / 2;
    /* Hours are modulo one day, not modulo 30 hours. */
    values[0] = value / 3600 % 24 / 10;
    values[1] = value / 3600 % 24 % 10;
    for (i = 0; i < count; ++i) {
        digit(pixels, x, y, values[i], scale, blink == 2);
        x += 7 * scale;
        if (i == 1 || (i == 3 && seconds)) {
            box(pixels, x, y + 3 * scale, scale, scale, blink == 1 ? 0xffff : 0);
            box(pixels, x, y + 7 * scale, scale, scale, blink == 1 ? 0xffff : 0);
            x += 2 * scale;
        }
    }
}

void clock_big(const Clock *clock, uint32_t raw, volatile uint16_t *pixels) {
    uint32_t value = (uint32_t)(clock_time(clock, raw) % 86400);
    char text[40];
    unsigned hour = value / 3600;
    if (clock->countdown) {
        uint32_t left = clock_remaining(clock, raw);
        snprintf(text, sizeof(text), "%s %lu days", clock->reboot ? "RESET IN:" : "COUNTDOWN:", (unsigned long)(left / 86400));
        clock_label(pixels, 35, 38, text, 0, 0xffff);
        display(pixels, 52, left % 86400, 3, clock_blink(clock, raw), true);
    }
    if (clock->hour12) {
        hour %= 12;
        if (!hour) hour = 12;
        value = hour * 3600 + value % 3600;
        clock_label(pixels, 284, 126, clock_time(clock, raw) % 86400 >= 43200 ? "PM" : "AM", 0, 0xffff);
    }
    display(pixels, clock->countdown ? 102 : 78, value, 5, 0, clock->seconds);
    clock_date_text(clock, raw, text);
    clock_label(pixels, (320 - (int)strlen(text) * 6) / 2, 170, text, 0, 0xffff);
}

static void put32(unsigned char *data, uint32_t value) {
    unsigned i;
    for (i = 0; i < 4; ++i) data[i] = (unsigned char)(value >> (i * 8));
}

static uint32_t get32(const unsigned char *data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void put64(unsigned char *data, int64_t value) {
    put32(data, (uint32_t)value);
    put32(data + 4, (uint32_t)((uint64_t)value >> 32));
}

static int64_t get64(const unsigned char *data) {
    uint64_t value = get32(data) | (uint64_t)get32(data + 4) << 32;
    return value >> 63 ? -(int64_t)(~value) - 1 : (int64_t)value;
}

static uint32_t checksum(const unsigned char *data, unsigned size) {
    uint32_t hash = 2166136261u;
    unsigned i;
    for (i = 0; i < size; ++i) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

void clock_encode(const Clock *clock, uint32_t raw, unsigned char data[CLOCK_CONFIG_SIZE]) {
    unsigned flags = clock->hour12 | clock->seconds << 1 | clock->visible << 2 | clock->countdown << 3 |
        clock->reboot << 4 | clock->check_time << 5 | clock->flip << 6 | clock->fades << 7 | clock->warned << 8;
    unsigned i;
    volatile unsigned char *clear = data;
    /* Reserved bytes use explicit stores for resident callback safety. */
    for (i = 48; i < 76; ++i) clear[i] = 0;
    put32(data, 0x4b4c434eu);
    put32(data + 4, 2);
    put32(data + 8, flags);
    put32(data + 12, clock->x);
    put32(data + 16, clock->y);
    put64(data + 20, clock_time(clock, raw) - raw);
    put64(data + 28, clock->deadline);
    put32(data + 36, raw);
    put32(data + 40, clock->timeout);
    put32(data + 44, clock->pending);
    put32(data + 76, checksum(data, 76));
}

bool clock_decode(Clock *clock, uint32_t raw, const unsigned char *data, size_t size) {
    Clock loaded;
    uint32_t flags, saved;
    unsigned i;
    bool old = size == 48;
    if ((!old && size != CLOCK_CONFIG_SIZE) || get32(data) != 0x4b4c434eu ||
        get32(data + 4) != (old ? 1u : 2u) ||
        get32(data + size - 4) != checksum(data, (unsigned)size - 4)) return false;
    clock_defaults(&loaded);
    flags = get32(data + 8);
    loaded.x = get32(data + 12);
    loaded.y = get32(data + 16);
    if ((flags & ~(old ? 15u : 511u)) || loaded.x > 319 || loaded.y > 229) return false;
    loaded.offset = get64(data + 20);
    if (loaded.offset < -(int64_t)UINT32_MAX || loaded.offset > CLOCK_LAST_SECOND) return false;
    loaded.hour12 = (flags & 1) != 0;
    loaded.seconds = (flags & 2) != 0;
    loaded.visible = (flags & 4) != 0;
    loaded.countdown = (flags & 8) != 0;
    if (old) {
        uint32_t delta;
        if (get32(data + 36) || get32(data + 40)) return false;
        saved = get32(data + 32);
        delta = get32(data + 28) - saved;
        loaded.deadline = (int64_t)saved + loaded.offset + (delta <= 3599999u ? delta : 0);
    } else {
        loaded.deadline = get64(data + 28);
        saved = get32(data + 36);
        loaded.timeout = get32(data + 40);
        loaded.pending = get32(data + 44);
        if (!loaded.timeout || loaded.timeout > INT32_MAX || loaded.pending & ~3u ||
            loaded.deadline < -4294967295LL || loaded.deadline > 8589934590LL) return false;
        for (i = 48; i < 76; ++i) if (data[i]) return false;
        loaded.reboot = (flags & 16) != 0;
        loaded.check_time = (flags & 32) != 0;
        loaded.flip = (flags & 64) != 0;
        loaded.fades = (flags & 128) != 0;
        loaded.warned = (flags & 256) != 0;
    }
    /* A reset RTC cannot reveal elapsed powered-off time. Restore the saved
       wall time, then ask the configured Keep/Change question on installation. */
    if (raw < saved && saved - raw <= INT32_MAX) loaded.offset += (int64_t)saved - raw;
    else if (raw < saved) loaded.offset += 4294967296LL;
    loaded.last_raw = raw;
    loaded.previous = clock_remaining(&loaded, raw);
    if (old) loaded.warned = loaded.previous <= 300;
    clock_move(&loaded, 0, 0);
    *clock = loaded;
    return true;
}
