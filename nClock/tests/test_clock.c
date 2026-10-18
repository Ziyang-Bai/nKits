#include "clock.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static bool test_calendar(void) {
    int64_t value, day;
    ClockDate date = {1970, 1, 1, 0, 0, 0}, result;
    CHECK(clock_from_date(&date, &value) && value == 0);
    date = (ClockDate){2000, 2, 29, 23, 59, 59};
    CHECK(clock_from_date(&date, &value));
    clock_to_date(value + 1, &result);
    CHECK(result.year == 2000 && result.month == 3 && result.day == 1);
    CHECK(result.hour == 0 && result.minute == 0 && result.second == 0);
    date = (ClockDate){2025, 2, 29, 0, 0, 0};
    CHECK(!clock_from_date(&date, &value));
    date = (ClockDate){2100, 2, 29, 0, 0, 0};
    CHECK(!clock_from_date(&date, &value));
    CHECK(clock_month_days(1900, 2) == 28);
    CHECK(clock_month_days(2000, 2) == 29);
    CHECK(clock_month_days(2100, 2) == 28);
    for (day = 0; day < CLOCK_LAST_SECOND / 86400; ++day) {
        clock_to_date(day * 86400 + 86399, &date);
        CHECK(clock_from_date(&date, &value));
        CHECK(value == day * 86400 + 86399);
    }
    clock_to_date(CLOCK_LAST_SECOND, &date);
    CHECK(date.year == 2106 && date.month == 2 && date.day == 7);
    CHECK(date.hour == 6 && date.minute == 28 && date.second == 15);
    CHECK(clock_from_date(&date, &value) && value == UINT32_MAX);
    ++date.second;
    CHECK(!clock_from_date(&date, &value));
    clock_to_date(-1, &date);
    CHECK(date.year == 1970 && date.month == 1 && date.day == 1);
    return true;
}

static bool test_format(void) {
    Clock clock;
    char text[CLOCK_TEXT_SIZE], date[40];
    clock_defaults(&clock);
    clock_text(&clock, 0, text);
    CHECK(!strcmp(text, "00:00:00"));
    clock.hour12 = true;
    clock_text(&clock, 0, text);
    CHECK(!strcmp(text, "12:00:00 AM"));
    clock_text(&clock, 43200, text);
    CHECK(!strcmp(text, "12:00:00 PM"));
    clock_text(&clock, 86399, text);
    CHECK(!strcmp(text, "11:59:59 PM"));
    clock.seconds = false;
    clock_text(&clock, 46801, text);
    CHECK(!strcmp(text, "01:00 PM"));
    clock_date_text(&clock, 0, date);
    CHECK(!strcmp(date, "Thu 01 Jan 1970"));
    clock_date_text(&clock, 951782400, date);
    CHECK(!strcmp(date, "Tue 29 Feb 2000"));
    CHECK(clock_start(&clock, 100, UINT32_MAX));
    clock_text(&clock, 100, text);
    CHECK(!strcmp(text, "49710d 06:28:15"));
    CHECK(clock_start(&clock, 100, 86400));
    clock_text(&clock, 100, text);
    CHECK(!strcmp(text, "1d 00:00:00"));
    return true;
}

static bool test_countdown(void) {
    Clock clock;
    ClockDate date = {2026, 10, 17, 12, 0, 0};
    clock_defaults(&clock);
    CHECK(!clock_start(&clock, 100, 0));
    CHECK(clock_start(&clock, 100, 301));
    clock_update(&clock, 100);
    CHECK(!clock_alert(&clock));
    clock_update(&clock, 101);
    CHECK(clock_remaining(&clock, 101) == 300);
    CHECK(clock_alert(&clock) == CLOCK_WARN);
    clock_update(&clock, 102);
    CHECK(!clock_alert(&clock));
    CHECK(clock_blink(&clock, 102) == 1);
    CHECK(clock_blink(&clock, 103) == 0);
    CHECK(clock_blink(&clock, 341) == 0);
    CHECK(clock_blink(&clock, 342) == 2);
    CHECK(clock_set_time(&clock, 342, &date));
    CHECK(clock_remaining(&clock, 342) == 59);
    clock_update(&clock, 401);
    CHECK(!clock.countdown && clock_alert(&clock) == CLOCK_ALARM);
    clock_update(&clock, 402);
    CHECK(!clock_alert(&clock));
    CHECK(clock_start(&clock, 402, 900));
    clock_update(&clock, 1402);
    CHECK(clock_alert(&clock) == CLOCK_ALARM);
    CHECK(clock_start(&clock, UINT32_MAX - 1, 3));
    clock_update(&clock, UINT32_MAX);
    CHECK(clock_remaining(&clock, UINT32_MAX) == 2);
    clock_update(&clock, 0);
    CHECK(clock_remaining(&clock, 0) == 1);
    clock_update(&clock, 1);
    CHECK(clock_alert(&clock) == CLOCK_ALARM);
    clock_defaults(&clock);
    CHECK(clock_start(&clock, 10, UINT32_MAX));
    CHECK(clock_remaining(&clock, 10) == UINT32_MAX);
    CHECK(clock_remaining(&clock, 11) == UINT32_MAX - 1);
    clock_stop(&clock);
    CHECK(!clock_remaining(&clock, 10) && !clock_alert(&clock));
    CHECK(clock_set_time(&clock, 1000, &date));
    ++date.day;
    CHECK(clock_target(&clock, 1000, &date));
    CHECK(clock_remaining(&clock, 1000) == 86400);
    --date.day;
    CHECK(!clock_target(&clock, 1000, &date));
    CHECK(clock_remaining(&clock, 1000) == 86400);
    return true;
}

static void put32(unsigned char *data, uint32_t value) {
    unsigned i;
    for (i = 0; i < 4; ++i) data[i] = (unsigned char)(value >> (i * 8));
}

static void seal(unsigned char *data, unsigned size) {
    uint32_t hash = 2166136261u;
    unsigned i;
    for (i = 0; i < size - 4; ++i) hash = (hash ^ data[i]) * 16777619u;
    put32(data + size - 4, hash);
}

static bool test_config(void) {
    Clock clock, loaded, untouched;
    unsigned char data[CLOCK_CONFIG_SIZE + 1];
    ClockDate date = {2000, 2, 29, 12, 0, 0};
    int64_t now;
    clock_defaults(&clock);
    clock.x = 0;
    clock.y = 229;
    clock.hour12 = true;
    clock.seconds = false;
    clock.visible = false;
    clock.reboot = true;
    clock.check_time = false;
    clock.flip = true;
    clock.fades = false;
    clock.timeout = 36500;
    CHECK(clock_set_time(&clock, 2000000000u, &date));
    now = clock_time(&clock, 2000000000u);
    CHECK(clock_start(&clock, 2000000000u, 86400 * 100));
    clock_encode(&clock, 2000000000u, data);
    CHECK(clock_decode(&loaded, 2000000010u, data, CLOCK_CONFIG_SIZE));
    CHECK(loaded.offset == clock.offset && loaded.x == 0 && loaded.y == 229);
    CHECK(loaded.hour12 && !loaded.seconds && !loaded.visible && loaded.countdown);
    CHECK(loaded.reboot && !loaded.check_time && loaded.flip && !loaded.fades);
    CHECK(loaded.timeout == 36500);
    CHECK(clock_time(&loaded, 2000000010u) == now + 10);
    CHECK(clock_remaining(&loaded, 2000000010u) == 86400 * 100 - 10);
    CHECK(clock_decode(&loaded, 0, data, CLOCK_CONFIG_SIZE));
    CHECK(clock_time(&loaded, 0) == now);
    CHECK(clock_remaining(&loaded, 0) == 86400 * 100);
    untouched = loaded;
    data[5] ^= 1;
    CHECK(!clock_decode(&loaded, 0, data, CLOCK_CONFIG_SIZE));
    CHECK(loaded.offset == untouched.offset && loaded.deadline == untouched.deadline);
    data[5] ^= 1;
    CHECK(!clock_decode(&loaded, 0, data, CLOCK_CONFIG_SIZE - 1));
    CHECK(!clock_decode(&loaded, 0, data, CLOCK_CONFIG_SIZE + 1));
    clock.pending = CLOCK_ALARM;
    clock.countdown = false;
    clock_encode(&clock, 2000000000u, data);
    CHECK(clock_decode(&loaded, 2000000000u, data, CLOCK_CONFIG_SIZE));
    CHECK(clock_alert(&loaded) == CLOCK_ALARM && !loaded.countdown);
    data[44] = 8;
    seal(data, CLOCK_CONFIG_SIZE);
    CHECK(!clock_decode(&loaded, 0, data, CLOCK_CONFIG_SIZE));
    return true;
}

static bool test_upgrade(void) {
    Clock loaded;
    unsigned char old[48] = {0};
    put32(old, 0x4b4c434e);
    put32(old + 4, 1);
    put32(old + 8, 15);
    put32(old + 12, 252);
    put32(old + 16, 229);
    put32(old + 20, 1000);
    put32(old + 28, 10060);
    put32(old + 32, 10000);
    seal(old, sizeof(old));
    CHECK(clock_decode(&loaded, 10010, old, sizeof(old)));
    CHECK(loaded.hour12 && loaded.seconds && loaded.visible && loaded.countdown);
    CHECK(loaded.x == 320 - CLOCK_WIDTH && loaded.y == 229);
    CHECK(clock_time(&loaded, 10010) == 11010);
    CHECK(clock_remaining(&loaded, 10010) == 50);
    CHECK(loaded.timeout == 180 && loaded.check_time && !loaded.reboot);
    CHECK(clock_decode(&loaded, 0, old, sizeof(old)));
    CHECK(clock_time(&loaded, 0) == 11000 && clock_remaining(&loaded, 0) == 60);
    CHECK(clock_decode(&loaded, 10061, old, sizeof(old)));
    clock_update(&loaded, 10061);
    CHECK(clock_alert(&loaded) == CLOCK_ALARM);
    return true;
}

static bool test_framebuffer(void) {
    static uint16_t storage[320 * 240 + 2], snapshot[320 * 240];
    Clock clock;
    unsigned i, x, y, painted;
    clock_defaults(&clock);
    clock.hour12 = true;
    clock.x = 320 - CLOCK_WIDTH;
    clock.y = 229;
    for (i = 0; i < sizeof(storage) / sizeof(storage[0]); ++i) storage[i] = 0x1234;
    clock_draw(&clock, 86399, storage + 1);
    CHECK(storage[0] == 0x1234 && storage[320 * 240 + 1] == 0x1234);
    painted = 0;
    for (y = 0; y < 240; ++y) {
        for (x = 0; x < 320; ++x) {
            uint16_t pixel = storage[1 + y * 320 + x];
            if (x < 320 - CLOCK_WIDTH || y < 229) CHECK(pixel == 0x1234);
            else { CHECK(pixel == 0 || pixel == 0xffff); ++painted; }
        }
    }
    CHECK(painted == CLOCK_WIDTH * CLOCK_HEIGHT);
    memcpy(snapshot, storage + 1, sizeof(snapshot));
    clock.visible = false;
    CHECK(clock_start(&clock, 0, 1));
    clock_update(&clock, 1);
    clock_draw(&clock, 1, storage + 1);
    CHECK(!memcmp(snapshot, storage + 1, sizeof(snapshot)));
    clock.visible = true;
    clock.x = UINT32_MAX;
    clock.y = UINT32_MAX;
    clock_draw(&clock, 1, storage + 1);
    CHECK(storage[0] == 0x1234 && storage[320 * 240 + 1] == 0x1234);
    clock_move(&clock, -INT32_MAX, -INT32_MAX);
    CHECK(clock.x == 0 && clock.y == 0);
    clock_move(&clock, 400, 400);
    CHECK(clock.x == 320 - CLOCK_WIDTH && clock.y == 229);
    memset(storage + 1, 0xff, sizeof(snapshot));
    clock_big(&clock, 0, storage + 1);
    memcpy(snapshot, storage + 1, sizeof(snapshot));
    clock_big(&clock, 1, storage + 1);
    CHECK(memcmp(snapshot, storage + 1, sizeof(snapshot)));
    CHECK(storage[0] == 0x1234 && storage[320 * 240 + 1] == 0x1234);
    clock_draw(&clock, 1, NULL);
    return true;
}

int main(void) {
    if (!test_calendar() || !test_format() || !test_countdown() ||
        !test_config() || !test_upgrade() || !test_framebuffer()) return 1;
    puts("nClock model tests passed");
    return 0;
}
