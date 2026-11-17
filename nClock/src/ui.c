/* nClock, Copyright (C) 2014 Levak. GPL-3.0-or-later. */
#include "ui.h"
#include "resident.h"
#include "diagnose.h"
#include "../../common/ui.h"

#include <stdio.h>

static uint16_t *screen;
static UiCanvas canvas;
static const UiTheme *const theme = &ui_themes[0];
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
    if (!ui_canvas_open(&canvas)) return false;
    screen = ui_canvas_pixels(&canvas);
    clock_diag("ui.alloc.after", (unsigned)(uintptr_t)screen);
    if (!clock_power_open()) {
        ui_canvas_close(&canvas);
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
    ui_canvas_close(&canvas);
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

void clock_ui_date(const Clock *clock, uint32_t raw, char text[80]) {
    static const char *const en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const zh[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    static const char *const fr[] = {"dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam."};
    int64_t value = clock_time(clock, raw);
    ClockDate date;
    if (value < 0) value = 0;
    if (value > CLOCK_LAST_SECOND) value = CLOCK_LAST_SECOND;
    clock_to_date(value, &date);
    unsigned day = (unsigned)(value / 86400 + 4) % 7;
    snprintf(text, 80, "%04d-%02d-%02d %s", date.year, date.month, date.day,
        ui_text_for((UiLanguage)clock->language, en[day], zh[day], fr[day]));
}

static void labels(const Clock *clock, uint32_t raw) {
    char text[80];
    if (clock->countdown) {
        snprintf(text, sizeof(text), clock->reboot ?
            ui_text("Restart in: %lu d", "%lu 天后重启", "Redémarrage : %lu j") :
            ui_text("Countdown: %lu d", "剩余 %lu 天", "Minuteur : %lu j"),
            (unsigned long)(clock_remaining(clock, raw) / 86400));
        ui_draw_text(canvas.gc, text, 10, 30, Regular10, theme->fg);
    }
    if (clock->hour12) {
        bool afternoon = clock_time(clock, raw) % 86400 >= 43200;
        ui_draw_text(canvas.gc, afternoon ?
            ui_text("PM", "下午", "PM") : ui_text("AM", "上午", "AM"),
            274, 128, Regular10, theme->fg);
    }
    clock_ui_date(clock, raw, text);
    ui_draw_text(canvas.gc, text, 92, 168, Regular10, theme->muted);
}

int clock_ui_menu(Clock *clock, const char *title, const char *const *items, unsigned count, bool big) {
    unsigned selected = 0, previous = 0, repeat = 0;
    unsigned drawn_selection = count;
    uint32_t drawn_time = 0;
    clock_ui_wake();
    wait_no_key_pressed();
    for (;;) {
        clock_diag("menu.input.before", 0);
        uint32_t raw = resident_seconds();
        bool was_dimmed = dimmed;
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
        if (dimmed || (raw == drawn_time && selected == drawn_selection && was_dimmed == dimmed)) {
            msleep(30);
            continue;
        }
        drawn_time = raw;
        drawn_selection = selected;
        ui_canvas_begin(&canvas);
        screen = ui_canvas_pixels(&canvas);
        ui_draw_header(canvas.gc, theme, title);
        if (big) {
            char text[160];
            clock_big(clock, raw, screen);
            labels(clock, raw);
            snprintf(text, sizeof(text), "%s (%u/%u)", items[selected], selected + 1, count);
            ui_draw_row(canvas.gc, theme, 194, text, true);
        } else {
            if (selected >= 9) first = selected - 8;
            for (i = first; i < count && i < first + 9; ++i)
                ui_draw_row(canvas.gc, theme, 32 + (int)(i - first) * 20, items[i], i == selected);
        }
        ui_draw_footer(canvas.gc, theme,
            ui_text("Arrows: select  Enter: open  Esc: back",
                    "方向键选择  Enter 打开  Esc 返回",
                    "Flèches : choisir  Entrée : ouvrir  Échap : retour"));
        ui_canvas_end(&canvas);
        screen = ui_canvas_pixels(&canvas);
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
        ui_canvas_begin(&canvas);
        screen = ui_canvas_pixels(&canvas);
        ui_draw_header(canvas.gc, theme, ui_text("Clock position", "小钟位置", "Position de l'horloge"));
        ui_draw_text(canvas.gc,
            ui_text("Arrows: move  Shift: 10 px", "方向键移动  Shift：10 像素",
                    "Flèches : déplacer  Maj : 10 px"), 10, 94, Regular10, theme->fg);
        ui_draw_footer(canvas.gc, theme,
            ui_text("Enter: save  Esc: cancel", "Enter 保存  Esc 取消",
                    "Entrée : enregistrer  Échap : annuler"));
        ui_canvas_end(&canvas);
        screen = ui_canvas_pixels(&canvas);
        clock_draw(clock, resident_seconds(), screen);
        present(clock);
        msleep(30);
    }
    clock->visible = visible;
    wait_no_key_pressed();
}
