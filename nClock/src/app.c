/*
  nClock for TI-Nspire
  Copyright (C) 2014 Levak
  Contributions by Xavier Andreani (2016).

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "app.h"
#include "clock.h"
#include "ui.h"
#include "resident.h"
#include "resident_build.h"
#include "version.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_PATH "/documents/nclock.cfg"
#define STARTUP_PATH "/documents/ndless/startup/nclock.tns"

static Clock state;
static const char title[] = "nClock";
static uint32_t backup_raw;
static void (*native_msgbox)(int, const char *, const char *, const char **);
static int (*native_interrupts)(int);
static char self_path[260];

static bool tick(void *data) {
    Clock *clock = data;
    if (clock->foreground || clock->disabled) return false;
    uint32_t raw = resident_seconds();
    clock_update(clock, raw);
    if (clock->visible) clock_draw(clock, raw, resident_framebuffer());
    return clock->pending || (uint32_t)(raw - backup_raw) >= 60;
}

static bool load_config(Clock *clock, uint32_t raw) {
    unsigned char data[CLOCK_CONFIG_SIZE + 1];
    FILE *file = fopen(CONFIG_PATH, "rb");
    size_t size;
    bool ok;
    if (!file) return false;
    size = fread(data, 1, sizeof(data), file);
    ok = !ferror(file);
    if (fclose(file)) ok = false;
    /* Publish the decoded model while keeping the foreground owner. */
    unsigned mask = resident_lock();
    bool decoded = ok && clock_decode(clock, raw, data, size);
    clock->foreground = true;
    resident_unlock(mask);
    return decoded;
}

static bool save_config(const Clock *clock, bool native) {
    unsigned char data[CLOCK_CONFIG_SIZE];
    unsigned used = 0;
    bool ok = true;
    int fd;
    unsigned mask = resident_lock();
    clock_encode(clock, resident_seconds(), data);
    resident_unlock(mask);
    fd = native ? resident_open(CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600) :
                  open(CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    while (used < sizeof(data)) {
        int count = native ? resident_write(fd, data + used, sizeof(data) - used) :
                             write(fd, data + used, sizeof(data) - used);
        if (count <= 0) { ok = false; break; }
        used += (unsigned)count;
    }
    if (native ? resident_close(fd) : close(fd)) ok = false;
    return ok;
}

static void service(Clock *clock) {
    unsigned pending = clock_alert(clock);
    if (!pending) return;
    clock_ui_wake(clock);
    save_config(clock, false);
    if (pending & CLOCK_ALARM) {
        show_msgbox(title, "Time's up !");
        if (clock->reboot) clock_reboot();
    } else if (pending & CLOCK_WARN) show_msgbox(title, "5 minutes left !");
    wait_no_key_pressed();
    save_config(clock, false);
}

static void event(void *data, struct s_ns_event *event_data) {
    Clock *clock = data;
    uint32_t raw = resident_seconds();
    unsigned pending;
    (void)event_data;
    if (clock->foreground || clock->disabled) return;
    unsigned mask = resident_lock();
    clock_update(clock, raw);
    pending = native_msgbox && native_interrupts ? clock_alert(clock) : 0;
    resident_unlock(mask);
    if (pending || (uint32_t)(raw - backup_raw) >= 60) {
        backup_raw = raw;
        save_config(clock, true);
    }
    if (pending) {
        const char *message = pending & CLOCK_ALARM ? "Time's up !" : "5 minutes left !";
        const char *dialog[] = {"DLG", NULL};
        static const uint16_t wide_title[] = {'n','C','l','o','c','k',0};
        uint16_t wide_message[32];
        unsigned i;
        int mask;
        for (i = 0; message[i]; ++i) wide_message[i] = (unsigned char)message[i];
        wide_message[i] = 0;
        /* The native ABI takes UTF-16; no SDK SWIs inside this callback. */
        mask = native_interrupts(0);
        native_msgbox(0, (const char *)wide_title, (const char *)wide_message, dialog);
        native_interrupts(mask);
        if ((pending & CLOCK_ALARM) && clock->reboot) clock_reboot();
    }
}

static bool ask_day(ClockDate *date, const char *subtitle) {
    return show_1numeric_input(title, subtitle, "Year (1970-2106)", &date->year, 1970, 2106) &&
           show_1numeric_input(title, subtitle, "Month (1-12)", &date->month, 1, 12) &&
           show_1numeric_input(title, subtitle, "Day", &date->day, 1, clock_month_days(date->year, date->month));
}

static bool ask_time(ClockDate *date, const char *subtitle) {
    return show_1numeric_input(title, subtitle, "Hour (0-23)", &date->hour, 0, 23) &&
           show_1numeric_input(title, subtitle, "Minute (0-59)", &date->minute, 0, 59) &&
           show_1numeric_input(title, subtitle, "Second (0-59)", &date->second, 0, 59);
}

static void change_time(Clock *clock, bool day) {
    ClockDate date, current;
    clock_to_date(clock_time(clock, resident_seconds()), &date);
    if (day ? !ask_day(&date, "Change date") : !ask_time(&date, "Change time")) return;
    clock_to_date(clock_time(clock, resident_seconds()), &current);
    if (day) {
        date.hour = current.hour;
        date.minute = current.minute;
        date.second = current.second;
    } else {
        date.year = current.year;
        date.month = current.month;
        date.day = current.day;
    }
    if (!clock_set_time(clock, resident_seconds(), &date))
        show_msgbox(title, "The RTC date range ends at 2106-02-07 06:28:15.");
}

static void choose_bool(const char *message, bool *value) {
    unsigned choice = show_msgbox_3b(title, message, "Yes", "No", "Cancel");
    if (choice == 1 || choice == 2) *value = choice == 1;
}

static bool copy_self(const char *source, const char *target, bool move) {
    unsigned char data[1024], check[1024];
    FILE *in = NULL, *out = NULL;
    bool ok = false;
    size_t size;
    if (!strcmp(source, target)) return true;
    out = fopen(target, "rb");
    if (out) { fclose(out); return false; }
    in = fopen(source, "rb");
    if (!in) return false;
    out = fopen(target, "wb");
    if (!out) { fclose(in); return false; }
    ok = true;
    while ((size = fread(data, 1, sizeof(data), in)))
        if (fwrite(data, 1, size, out) != size) { ok = false; break; }
    if (ferror(in)) ok = false;
    if (fclose(in)) ok = false;
    if (fclose(out)) ok = false;
    in = out = NULL;
    if (ok) {
        in = fopen(source, "rb");
        out = fopen(target, "rb");
        ok = in && out;
        while (ok && (size = fread(data, 1, sizeof(data), in)))
            if (fread(check, 1, size, out) != size || memcmp(data, check, size)) ok = false;
        if (ok && (ferror(in) || fgetc(out) != EOF || ferror(out))) ok = false;
        if (in && fclose(in)) ok = false;
        if (out && fclose(out)) ok = false;
    }
    if (!ok) { remove(target); return false; }
    /* rename is ENOSYS in this SDK. Never remove the source until both
       copies have been closed and the destination has been read back. */
    if (move && remove(source)) return false;
    return true;
}

static void startup_install(void) {
    if (!strcmp(self_path, STARTUP_PATH)) {
        show_msgbox(title, "Already installed in startup. Move nclock.tns out of /documents/ndless/startup to disable automatic loading.");
        return;
    }
    if (show_msgbox_2b(title, "Move nClock to /documents/ndless/startup/nclock.tns and start it on each boot?", "Install", "Cancel") != 1) return;
    mkdir("/documents/ndless", 0);
    mkdir("/documents/ndless/startup", 0);
    if (copy_self(self_path, STARTUP_PATH, true)) {
        strcpy(self_path, STARTUP_PATH);
        show_msgbox(title, "nClock moved to /documents/ndless/startup/nclock.tns.");
        refresh_osscr();
    } else show_msgbox(title, "Could not move nClock. Source was kept unless a complete verified destination already exists. Remove an existing startup copy first.");
}

static void quick_countdowns(Clock *clock) {
    static const char *const items[] = {"3 min - eggs", "5 min", "15 min - rice", "30 min", "1 hour - essay", "2 hours - test", "3 hours - big test", "Back"};
    static const uint32_t delays[] = {180,300,900,1800,3600,7200,10800};
    int choice = clock_ui_menu(clock, "Quick countdowns", items, 8, false);
    if (choice >= 0 && choice < 7) clock_start(clock, resident_seconds(), delays[choice]);
}

static void countdown_options(Clock *clock) {
    static const char *const items[] = {"...delay", "...from date", "Quick countdowns", "Stop current countdown", "Reboot on countdown", "Back"};
    for (;;) {
        int choice = clock_ui_menu(clock, "Countdown", items, 6, false);
        if (choice == 0) {
            int days = 0, hours = 0, minutes = 3, seconds = 0;
            uint64_t delay;
            if (show_1numeric_input(title, "Countdown delay", "Days (0-49710)", &days, 0, 49710) &&
                show_1numeric_input(title, "Countdown delay", "Hours (0-23)", &hours, 0, 23) &&
                show_1numeric_input(title, "Countdown delay", "Minutes (0-59)", &minutes, 0, 59) &&
                show_1numeric_input(title, "Countdown delay", "Seconds (0-59)", &seconds, 0, 59)) {
                delay = (uint64_t)days * 86400 + hours * 3600 + minutes * 60 + seconds;
                if (!delay || delay > UINT32_MAX) show_msgbox(title, "Delay must be 1 second to 49710 days 06:28:15.");
                else clock_start(clock, resident_seconds(), (uint32_t)delay);
            }
        } else if (choice == 1) {
            ClockDate date;
            clock_to_date(clock_time(clock, resident_seconds()), &date);
            if (ask_day(&date, "Countdown target") && ask_time(&date, "Countdown target") &&
                !clock_target(clock, resident_seconds(), &date))
                show_msgbox(title, "Target must be in the future, no later than 2106-02-07 06:28:15.");
        } else if (choice == 2) quick_countdowns(clock);
        else if (choice == 3) { clock_stop(clock); show_msgbox(title, "Countdown removed."); }
        else if (choice == 4) choose_bool("Reboot after the Time's up alert? This resets the calculator. Default is No.", &clock->reboot);
        else return;
        service(clock);
        save_config(clock, false);
    }
}

static void more_options(Clock *clock) {
    static const char *const items[] = {"Reverse the screen", "Reset calc on countdown", "Load nClock on startup", "About", "Back"};
    for (;;) {
        int choice = clock_ui_menu(clock, "Moar !", items, 5, false);
        if (choice == 0) clock->flip = !clock->flip;
        else if (choice == 1) choose_bool("Reboot after the Time's up alert?", &clock->reboot);
        else if (choice == 2) startup_install();
        else if (choice == 3) show_msgbox(title,
            "nClock by Levak and Xavier Andreani (Critor).\n"
            "Live clock, resident mini-clock and countdown. GPL v3 or later.\nhttp://levak.free.fr\nhttps://tiplanet.org\n\n"
            NKITS_VERSION " (" NKITS_COMMIT ")\n" NKITS_STAGE);
        else return;
    }
}

static void options(Clock *clock) {
    static const char *const items[] = {"Change time", "Change date", "12 / 24 hour format", "Display seconds", "Move mini-clock", "Hide mini-clock", "Load nClock on startup", "Check time after reboot", "Idle dim / off timeout", "Fading transitions", "Brightness", "Moar !", "Back"};
    for (;;) {
        int choice = clock_ui_menu(clock, "Options", items, 13, false);
        if (choice == 0 || choice == 1) change_time(clock, choice == 1);
        else if (choice == 2) {
            unsigned result = show_msgbox_3b(title, "Hour format", "12 hours", "24 hours", "Cancel");
            if (result == 1 || result == 2) clock->hour12 = result == 1;
        } else if (choice == 3) choose_bool("Display seconds? Countdown always includes seconds.", &clock->seconds);
        else if (choice == 4) clock_ui_position(clock);
        else if (choice == 5) {
            bool hide = !clock->visible;
            choose_bool("Hide the mini-clock? Countdown alerts remain enabled.", &hide);
            clock->visible = !hide;
            refresh_osscr();
        } else if (choice == 6) startup_install();
        else if (choice == 7) choose_bool("Ask Keep / Change when nClock installs after reboot?", &clock->check_time);
        else if (choice == 8) {
            int timeout = (int)clock->timeout;
            if (show_1numeric_input(title, "Full-screen clock idle", "Seconds before dim / off", &timeout, 5, INT32_MAX)) clock->timeout = (uint32_t)timeout;
        } else if (choice == 9) choose_bool("Use fading transitions and gradual idle dimming?", &clock->fades);
        else if (choice == 10) {
            int brightness = (int)clock_ui_brightness();
            unsigned maximum = clock_ui_max_brightness();
            char label[32];
            snprintf(label, sizeof(label), "Level (1-%u)", maximum);
            if (show_1numeric_input(title, "Brightness", label, &brightness, 1, (int)maximum)) clock_ui_level((unsigned)brightness);
        } else if (choice == 11) more_options(clock);
        else return;
        service(clock);
        save_config(clock, false);
    }
}

static void startup_check(Clock *clock, bool first) {
    char text[40], message[160];
    if (first) {
        show_msgbox(title, "Welcome to nClock. Configure the display, then check the date and time. All options remain available in Options.");
        choose_bool("Use 12-hour format?", &clock->hour12);
        choose_bool("Display seconds?", &clock->seconds);
        choose_bool("Check the date and time after each reboot?", &clock->check_time);
    }
    if (!first && !clock->check_time) return;
    clock_date_text(clock, resident_seconds(), text);
    snprintf(message, sizeof(message), "Is this date valid?\n%s\nChoose Change to set date and time.", text);
    if (show_msgbox_2b(title, message, "Keep", "Change") == 2) {
        ClockDate date;
        clock_to_date(clock_time(clock, resident_seconds()), &date);
        if (ask_day(&date, "Set date") && ask_time(&date, "Set time") && !clock_set_time(clock, resident_seconds(), &date))
            show_msgbox(title, "The RTC date range ends at 2106-02-07 06:28:15.");
    }
}

static bool uninstall(Clock *clock) {
    clock_stop(clock);
    clock->visible = false;
    clock->disabled = true;
    if (remove(CONFIG_PATH) && errno != ENOENT) {
        show_msgbox(title, "Could not delete /documents/nclock.cfg.");
        return false;
    }
    refresh_osscr();
    show_msgbox(title, "nClock stopped and configuration removed. Move the program out of startup to prevent reinstallation.");
    if (show_msgbox_2b(title, "Reboot now to release the resident hook?", "Reboot", "No") == 1) clock_reboot();
    return true;
}

int app_run(int argc, char **argv) {
    static const char *const items[] = {"Options", "Countdown", "Quit"};
    bool fresh = false, loaded = true, power_ready;
    bool startup = nl_isstartup();
    const char *error = NULL, *name;
    Clock *clock;
    if (argc < 1 || strlen(argv[0]) >= sizeof(self_path)) return 1;
    strcpy(self_path, argv[0]);
    name = strrchr(self_path, '/');
    name = name ? name + 1 : self_path;
    if (startup && !strcmp(name, "uninstall.tns")) {
        char target[260];
        size_t folder = (size_t)(name - self_path);
        memcpy(target, self_path, folder);
        strcpy(target + folder, "nclock.tns");
        if (!copy_self(self_path, target, true)) return 1;
        strcpy(self_path, target);
        name = self_path + folder;
    }
    if (!startup && !strcmp(name, "uninstall.tns") &&
        show_msgbox_2b(title, "Do you want to uninstall nClock?", "Yes", "No") != 1) return 0;
    clock = resident_install(0x4e434c4bu, NKITS_BUILD_ID, &state, sizeof(state), tick, event, &fresh, &error);
    if (!clock) {
        if (!startup) show_msgbox(title, error ? error : "Could not install nClock.");
        return 1;
    }
    clock->foreground = true;
    native_msgbox = resident_native(e_show_dialog_box2_);
    native_interrupts = resident_native(e_TCT_Local_Control_Interrupts);
    /* Record the hardware backend even for startup-only countdown reboots. */
    power_ready = clock_power_open();
    if (fresh) {
        unsigned mask = resident_lock();
        clock_defaults(clock);
        clock->foreground = true;
        resident_unlock(mask);
        loaded = load_config(clock, resident_seconds());
        backup_raw = resident_seconds();
    }
    if (!startup && !strcmp(name, "uninstall.tns")) {
        uninstall(clock);
        clock->foreground = false;
        if (fresh) _exit(0);
        return 0;
    }
    clock->disabled = false;
    if (fresh) startup_check(clock, !loaded);
    if (!startup) {
        if (!power_ready || !clock_ui_open(service)) show_msgbox(title, "Could not open the full-screen clock (memory or unsupported display driver).");
        else {
            for (;;) {
                int choice = clock_ui_menu(clock, title, items, 3, true);
                if (choice == 0) options(clock);
                else if (choice == 1) countdown_options(clock);
                else break;
                if (!save_config(clock, false)) show_msgbox(title, "Could not save /documents/nclock.cfg. Settings remain active until reboot.");
            }
            clock_ui_close();
        }
    }
    clock->foreground = false;
    if (!save_config(clock, false) && !startup)
        show_msgbox(title, "Could not save /documents/nclock.cfg. Settings remain active until reboot.");
    if (fresh) _exit(0);
    return 0;
}
