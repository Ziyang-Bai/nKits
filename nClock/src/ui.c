/* nClock, Copyright (C) 2014 Levak. GPL-3.0-or-later. */
#include "ui.h"
#include "resident.h"
#include "diagnose.h"

#include <stdlib.h>
#include <string.h>

static uint16_t *screen;
static ClockService service;
static unsigned saved_brightness;
static unsigned original_brightness;
static int original_interrupts;
static uint32_t activity;
static bool dimmed;

#define KEY_BACK 1u
#define KEY_ENTER 2u
#define KEY_UP 4u
#define KEY_DOWN 8u
#define KEY_LEFT 16u
#define KEY_RIGHT 32u
#define KEY_ON 64u

static unsigned keys(void) {
    unsigned value = 0;
    if (isKeyPressed(KEY_NSPIRE_ESC)) value |= KEY_BACK;
    if (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_CLICK)) value |= KEY_ENTER;
    if (isKeyPressed(KEY_NSPIRE_UP) || isKeyPressed(KEY_NSPIRE_LEFTUP) || isKeyPressed(KEY_NSPIRE_UPRIGHT)) value |= KEY_UP;
    if (isKeyPressed(KEY_NSPIRE_DOWN) || isKeyPressed(KEY_NSPIRE_DOWNLEFT) || isKeyPressed(KEY_NSPIRE_RIGHTDOWN)) value |= KEY_DOWN;
    if (isKeyPressed(KEY_NSPIRE_LEFT)) value |= KEY_LEFT;
    if (isKeyPressed(KEY_NSPIRE_RIGHT)) value |= KEY_RIGHT;
    if (on_key_pressed()) value |= KEY_ON;
    return value;
}

static void fill(uint16_t color) {
    unsigned i;
    for (i = 0; i < 320 * 240; ++i) screen[i] = color;
}

static void present(const Clock *clock) {
    if (clock->flip) {
        unsigned i;
        for (i = 0; i < 320 * 240 / 2; ++i) {
            uint16_t pixel = screen[i];
            screen[i] = screen[320 * 240 - 1 - i];
            screen[320 * 240 - 1 - i] = pixel;
        }
    }
    lcd_blit(screen, SCR_320x240_565);
}

bool clock_ui_open(ClockService callback) {
    clock_diag("ui.alloc.before", 320 * 240 * sizeof(*screen));
    screen = malloc(320 * 240 * sizeof(*screen));
    clock_diag("ui.alloc.after", (unsigned)(uintptr_t)screen);
    if (!screen) return false;
    if (!clock_power_open()) {
        free(screen);
        screen = NULL;
        return false;
    }
    /* SDK waits acknowledge the one-shot timer themselves with CPU IRQs masked. */
    original_interrupts = TCT_Local_Control_Interrupts(-1);
    service = callback;
    saved_brightness = clock_ui_brightness();
    original_brightness = saved_brightness;
    activity = resident_seconds();
    dimmed = false;
    return true;
}

void clock_ui_wake(void) {
    activity = resident_seconds();
    clock_power_on();
    if (dimmed) {
        clock_ui_set_brightness(saved_brightness);
        dimmed = false;
    }
}

void clock_ui_level(unsigned value) {
    saved_brightness = value;
    clock_ui_set_brightness(value);
}

void clock_ui_close(void) {
    clock_power_on();
    clock_ui_set_brightness(original_brightness);
    free(screen);
    screen = NULL;
    refresh_osscr();
    TCT_Local_Control_Interrupts(original_interrupts);
}

/* The first key after idle wakes the display before accepting commands. */
static unsigned input(Clock *clock, unsigned *previous, unsigned *repeat) {
    uint32_t raw = resident_seconds();
    unsigned value = keys(), result = 0;
    clock_update(clock, raw);
    if (service) service(clock);
    if (value) {
        activity = raw;
        if (dimmed) {
            clock_ui_wake();
            wait_no_key_pressed();
            value = 0;
        }
    } else if (!dimmed && (uint32_t)(raw - activity) >= clock->timeout) {
        clock_ui_set_brightness(0);
        clock_power_off();
        dimmed = true;
    }
    if (value != *previous) {
        *repeat = 0;
        result = value;
    } else if (value && ++*repeat >= 12 && !(*repeat % 3)) {
        result = value & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
    }
    *previous = value;
    return result;
}

int clock_ui_menu(Clock *clock, const char *title, const char *const *items, unsigned count, bool big) {
    unsigned selected = 0, previous = 0, repeat = 0;
    clock_ui_wake();
    wait_no_key_pressed();
    for (;;) {
        clock_diag("menu.input.before", 0);
        uint32_t raw = resident_seconds();
        unsigned key = input(clock, &previous, &repeat), i, first = 0;
        clock_diag("menu.input.after", key);
        if (key & KEY_BACK) { wait_no_key_pressed(); return -1; }
        if (key & (KEY_UP | KEY_LEFT)) selected = selected ? selected - 1 : count - 1;
        if (key & (KEY_DOWN | KEY_RIGHT)) selected = (selected + 1) % count;
        if (key & KEY_ENTER) { clock_ui_wake(); wait_no_key_pressed(); return (int)selected; }
        if (key & KEY_ON) {
            clock_ui_set_brightness(0);
            clock_power_off();
            dimmed = true;
            wait_no_key_pressed();
        }
        fill(0xffff);
        clock_label(screen, (320 - (int)strlen(title) * 6) / 2, 10, title, 0, 0xffff);
        if (big) {
            clock_big(clock, raw, screen);
            for (i = 0; i < count; ++i)
                clock_label(screen, 12 + (int)i * 104, 204, items[i], i == selected ? 0xffff : 0, i == selected ? 0x001f : 0xffff);
        } else {
            if (selected >= 10) first = selected - 9;
            for (i = first; i < count && i < first + 10; ++i)
                clock_label(screen, 14, 34 + (int)(i - first) * 17, items[i], i == selected ? 0xffff : 0, i == selected ? 0x001f : 0xffff);
        }
        clock_label(screen, 14, 228, "ARROWS: SELECT  ENTER: OK  ESC: BACK", 0x4208, 0xffff);
        present(clock);
        msleep(30);
#ifdef NCLOCK_DIAGNOSTIC
        clock_diag("menu.frame.complete", 0);
        clock_diag_frame_ready();
        return -1;
#endif
    }
}

void clock_ui_position(Clock *clock) {
    unsigned x = clock->x, y = clock->y, previous = 0, repeat = 0;
    bool visible = clock->visible;
    clock->visible = true;
    clock_ui_wake();
    wait_no_key_pressed();
    for (;;) {
        unsigned key = input(clock, &previous, &repeat);
        int step = isKeyPressed(KEY_NSPIRE_SHIFT) ? 10 : 1;
        if (key & KEY_BACK) { clock->x = x; clock->y = y; break; }
        if (key & KEY_ENTER) break;
        if (key & KEY_UP) clock_move(clock, 0, -step);
        if (key & KEY_DOWN) clock_move(clock, 0, step);
        if (key & KEY_LEFT) clock_move(clock, -step, 0);
        if (key & KEY_RIGHT) clock_move(clock, step, 0);
        fill(0xffff);
        clock_label(screen, 20, 100, "MOVE WITH ARROWS / TOUCHPAD", 0, 0xffff);
        clock_label(screen, 20, 116, "SHIFT: 10 PX  ENTER: SAVE  ESC: CANCEL", 0, 0xffff);
        clock_draw(clock, resident_seconds(), screen);
        present(clock);
        msleep(30);
    }
    clock->visible = visible;
    wait_no_key_pressed();
}
