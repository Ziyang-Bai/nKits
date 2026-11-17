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
#include "diagnose.h"
#include "../../common/ui.h"

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_PATH "/documents/nclock.cfg"
#define L ui_text
#define STARTUP_PATH "/documents/ndless/startup/nclock.tns"
#ifdef NCLOCK_DIAGNOSTIC
#define SAVE_CONFIG_PATH "/documents/nclock-diag.cfg.tns"
#else
#define SAVE_CONFIG_PATH CONFIG_PATH
#endif

static Clock state;
static const char title[] = "nClock";
static uint32_t backup_raw;
static void (*native_msgbox)(int, const char *, const char *, const char **);
static int (*native_interrupts)(int);
static void (*native_convert)(void *, const char *, int);
static char self_path[260];

#ifdef NCLOCK_DIAGNOSTIC
static Clock *diag_clock;
static bool diag_ui, diag_frame;

void clock_diag_frame_ready(void) {
    diag_frame = true;
}

void clock_diag_abort(const char *reason) {
    if (diag_clock) {
        diag_clock->disabled = true;
        diag_clock->foreground = false;
    }
    if (diag_ui) {
        diag_ui = false;
        clock_ui_close();
    }
    clock_diag("run.abort", 0);
    show_msgbox(title, reason);
    clock_diag_finish(1);
    _exit(1);
}
#endif

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
    if (!native) clock_diag("config.save.before", 0);
    unsigned mask = resident_lock();
    clock_encode(clock, resident_seconds(), data);
    resident_unlock(mask);
    fd = native ? resident_open(SAVE_CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600) :
                  open(SAVE_CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        if (!native) clock_diag("config.save.failed", (unsigned)errno);
        return false;
    }
    while (used < sizeof(data)) {
        int count = native ? resident_write(fd, data + used, sizeof(data) - used) :
                             write(fd, data + used, sizeof(data) - used);
        if (count <= 0) { ok = false; break; }
        used += (unsigned)count;
    }
    if (native ? resident_close(fd) : close(fd)) ok = false;
    if (!native) clock_diag("config.save.after", ok);
    return ok;
}

static void service(Clock *clock) {
    unsigned pending = clock_alert(clock);
    if (!pending) return;
    clock_ui_wake();
    save_config(clock, false);
    if (pending & CLOCK_ALARM) {
        clock_diag("alarm.dialog.before", pending);
        show_msgbox(title, L("Time is up.", "时间到。", "Temps écoulé."));
        clock_diag("alarm.dialog.after", pending);
        if (clock->reboot) clock_reboot();
    } else if (pending & CLOCK_WARN) {
        clock_diag("alarm.dialog.before", pending);
        show_msgbox(title, L("5 minutes left.", "还剩 5 分钟。", "Il reste 5 minutes."));
        clock_diag("alarm.dialog.after", pending);
    }
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
    pending = native_msgbox && native_interrupts && native_convert ? clock_alert(clock) : 0;
    resident_unlock(mask);
    if (pending || (uint32_t)(raw - backup_raw) >= 60) {
        backup_raw = raw;
        save_config(clock, true);
    }
    if (pending) {
        const char *message = pending & CLOCK_ALARM ?
            ui_text_for((UiLanguage)clock->language, "Time is up.", "时间到。", "Temps écoulé.") :
            ui_text_for((UiLanguage)clock->language, "5 minutes left.", "还剩 5 分钟。", "Il reste 5 minutes.");
        const char *dialog[] = {"DLG", NULL};
        static const uint16_t wide_title[] = {'n','C','l','o','c','k',0};
        uint16_t wide_message[80];
        int mask;
        native_convert(wide_message, message, sizeof(wide_message));
        /* The native ABI takes UTF-16; no SDK SWIs inside this callback. */
        mask = native_interrupts(0);
        native_msgbox(0, (const char *)wide_title, (const char *)wide_message, dialog);
        native_interrupts(mask);
        if ((pending & CLOCK_ALARM) && clock->reboot) clock_reboot();
    }
}

static bool number(const char *text, int minimum, int maximum, int *value) {
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (text == end || errno == ERANGE || parsed < minimum || parsed > maximum) return false;
    while (isspace((unsigned char)*end)) ++end;
    if (*end) return false;
    *value = (int)parsed;
    return true;
}

static bool edit_date_time(ClockDate *date, bool day, bool time, const char *caption) {
    ClockDate next = *date;
    unsigned page = 0, pages = day && time ? 2 : 1;
    for (;;) {
        bool date_page = day && page == 0;
        char heading[96], hour[12], minute[12], second[12];
        if (pages == 2) snprintf(heading, sizeof(heading), "%s (%u/2)", caption, page + 1);
        else snprintf(heading, sizeof(heading), "%s", caption);
        snprintf(hour, sizeof(hour), "%d", next.hour);
        snprintf(minute, sizeof(minute), "%d", next.minute);
        snprintf(second, sizeof(second), "%d", next.second);
        ndialog_t *dialog = ndlg_create(heading);
        if (!dialog) return false;
        bool ready;
        if (date_page) {
            ready = ndlg_add_spinbox(dialog, L("Year", "年", "Année"), 1970, 2106, 1, &next.year) == 0 &&
                ndlg_add_spinbox(dialog, L("Month", "月", "Mois"), 1, 12, 1, &next.month) == 0 &&
                ndlg_add_spinbox(dialog, L("Day", "日", "Jour"), 1, 31, 1, &next.day) == 0;
        } else {
            ready = ndlg_add_text_entry(dialog, L("Hour (0-23)", "小时（0-23）", "Heure (0-23)"), hour, sizeof(hour)) == 0 &&
                ndlg_add_text_entry(dialog, L("Minute (0-59)", "分钟（0-59）", "Minute (0-59)"), minute, sizeof(minute)) == 0 &&
                ndlg_add_text_entry(dialog, L("Second (0-59)", "秒（0-59）", "Seconde (0-59)"), second, sizeof(second)) == 0;
        }
        if (!ready) { ndlg_destroy(dialog); return false; }
        if (!ui_show(dialog)) return false;
        int64_t value;
        if (date_page) {
            ClockDate midnight = next;
            midnight.hour = midnight.minute = midnight.second = 0;
            if (clock_from_date(&midnight, &value)) {
                if (pages == 2) { page = 1; continue; }
                *date = next;
                return true;
            }
        } else if (number(hour, 0, 23, &next.hour) &&
                   number(minute, 0, 59, &next.minute) &&
                   number(second, 0, 59, &next.second)) {
            if (clock_from_date(&next, &value)) { *date = next; return true; }
            if (pages == 2) page = 0;
        }
        show_msgbox(title, L("Invalid date or time.", "日期或时间无效。", "Date ou heure invalide."));
    }
}

static void change_time(Clock *clock, bool day) {
    ClockDate date, current;
    clock_to_date(clock_time(clock, resident_seconds()), &date);
    if (!edit_date_time(&date, day, !day,
            day ? L("Date", "日期", "Date") : L("Time", "时间", "Heure"))) return;
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
        show_msgbox(title, L("Latest date: 2106-02-07 06:28:15.",
            "最晚日期：2106-02-07 06:28:15。", "Date limite : 2106-02-07 06:28:15."));
}

static bool duration(uint32_t *result) {
    char days[12] = "0", hours[12] = "0", minutes[12] = "3", seconds[12] = "0";
    for (;;) {
        ndialog_t *dialog = ndlg_create(L("Countdown duration", "倒计时时长", "Durée du minuteur"));
        if (!dialog) return false;
        if (ndlg_add_text_entry(dialog, L("Days", "天", "Jours"), days, sizeof(days)) ||
            ndlg_add_text_entry(dialog, L("Hours (0-23)", "小时（0-23）", "Heures (0-23)"), hours, sizeof(hours)) ||
            ndlg_add_text_entry(dialog, L("Minutes (0-59)", "分钟（0-59）", "Minutes (0-59)"), minutes, sizeof(minutes)) ||
            ndlg_add_text_entry(dialog, L("Seconds (0-59)", "秒（0-59）", "Secondes (0-59)"), seconds, sizeof(seconds))) {
            ndlg_destroy(dialog);
            return false;
        }
        if (!ui_show(dialog)) return false;
        int d, h, m, s;
        if (number(days, 0, 49710, &d) && number(hours, 0, 23, &h) &&
            number(minutes, 0, 59, &m) && number(seconds, 0, 59, &s)) {
            uint64_t value = (uint64_t)d * 86400 + (unsigned)h * 3600 + (unsigned)m * 60 + (unsigned)s;
            if (value && value <= UINT32_MAX) { *result = (uint32_t)value; return true; }
        }
        show_msgbox(title, L("Use 1 second to 49710 days 06:28:15.",
            "范围：1 秒至 49710 天 06:28:15。", "De 1 seconde à 49710 jours 06:28:15."));
    }
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
        show_msgbox(title, L("Startup is already enabled. Move nclock.tns out of /documents/ndless/startup to disable it.",
            "已启用自启动。移出 /documents/ndless/startup 中的 nclock.tns 即可关闭。",
            "Démarrage automatique actif. Retirez nclock.tns de /documents/ndless/startup pour le désactiver."));
        return;
    }
    if (show_msgbox_2b(title, L("Move nClock to startup?", "将 nClock 移至自启动目录？",
            "Déplacer nClock dans le dossier de démarrage ?"),
            L("Cancel", "取消", "Annuler"), L("Move", "移动", "Déplacer")) != 2) return;
    mkdir("/documents/ndless", 0);
    mkdir("/documents/ndless/startup", 0);
    if (copy_self(self_path, STARTUP_PATH, true)) {
        strcpy(self_path, STARTUP_PATH);
        show_msgbox(title, L("Startup enabled.", "已启用自启动。", "Démarrage automatique activé."));
        refresh_osscr();
    } else show_msgbox(title, L("Move failed. Check the startup folder for an existing copy.",
        "移动失败。请检查自启动目录中是否已有副本。",
        "Déplacement impossible. Vérifiez la copie du dossier de démarrage."));
}

static bool choose_bool(const char *heading, const char *label, bool *value) {
    int enabled = *value;
    ndialog_t *dialog = ndlg_create(heading);
    if (!dialog) return false;
    if (ndlg_add_checkbox(dialog, label, &enabled)) {
        ndlg_destroy(dialog);
        return false;
    }
    if (!ui_show(dialog)) return false;
    *value = enabled != 0;
    return true;
}

static void countdown_options(Clock *clock) {
    const char *items[] = {
        L("Start by duration", "按时长开始", "Démarrer pour une durée"),
        L("Start until date/time", "按目标时间开始", "Fin à une date/heure"),
        L("Use a preset", "选用预设", "Durée prédéfinie"),
        clock->reboot ? L("Restart at end: on", "到期重启：开", "Redémarrage à la fin : oui") :
                        L("Restart at end: off", "到期重启：关", "Redémarrage à la fin : non"),
        L("Stop countdown", "停止倒计时", "Arrêter le minuteur")
    };
    int action = clock_ui_menu(clock, L("Countdown", "倒计时", "Minuteur"), items, 5, false);
    if (action == 0) {
        uint32_t delay;
        if (!duration(&delay)) return;
        clock_start(clock, resident_seconds(), delay);
    } else if (action == 1) {
        ClockDate date;
        clock_to_date(clock_time(clock, resident_seconds()), &date);
        if (!edit_date_time(&date, true, true, L("Target time", "目标时间", "Heure cible"))) return;
        if (!clock_target(clock, resident_seconds(), &date)) {
            show_msgbox(title, L("Choose a future time before 2106-02-07 06:28:16.",
                "请选择未来时间，最晚为 2106-02-07 06:28:15。",
                "Choisissez une date future, au plus tard le 2106-02-07 à 06:28:15."));
            return;
        }
    } else if (action == 2) {
        const char *presets[] = {L("3 min", "3 分钟", "3 min"), L("5 min", "5 分钟", "5 min"),
            L("15 min", "15 分钟", "15 min"), L("30 min", "30 分钟", "30 min"),
            L("1 hour", "1 小时", "1 heure"), L("2 hours", "2 小时", "2 heures"),
            L("3 hours", "3 小时", "3 heures")};
        static const uint32_t delays[] = {180, 300, 900, 1800, 3600, 7200, 10800};
        int preset = clock_ui_menu(clock, L("Preset", "预设", "Préréglage"), presets, 7, false);
        if (preset < 0) return;
        clock_start(clock, resident_seconds(), delays[preset]);
    } else if (action == 3) {
        bool reboot = clock->reboot;
        if (!choose_bool(L("Restart at end", "到期重启", "Redémarrage à la fin"),
                L("Restart when time is up", "时间到后重启", "Redémarrer à la fin"), &reboot)) return;
        if (reboot && !clock->reboot && show_msgbox_2b(title,
                L("Restart when time is up? Save your work first.", "时间到后将重启，请先保存文档。",
                  "Redémarrer à la fin ? Enregistrez vos documents."),
                L("Cancel", "取消", "Annuler"), L("Enable", "启用", "Activer")) != 2) return;
        clock->reboot = reboot;
    } else if (action == 4) clock_stop(clock);
    else return;
    service(clock);
}

static void screen_options(Clock *clock) {
    int brightness = (int)clock_ui_brightness();
    int maximum = (int)clock_ui_max_brightness();
    char timeout[16];
    snprintf(timeout, sizeof(timeout), "%lu", (unsigned long)clock->timeout);
    for (;;) {
        ndialog_t *dialog = ndlg_create(L("Screen", "屏幕", "Écran"));
        if (!dialog) return;
        if (ndlg_add_spinbox(dialog, L("Brightness", "亮度", "Luminosité"), 1, maximum, 1, &brightness) ||
            ndlg_add_text_entry(dialog, L("Idle timeout (s)", "待机时间（秒）", "Veille (s)"), timeout, sizeof(timeout))) {
            ndlg_destroy(dialog);
            return;
        }
        if (!ui_show(dialog)) return;
        int value;
        if (number(timeout, 5, INT32_MAX, &value)) {
            clock->timeout = (uint32_t)value;
            clock_ui_level((unsigned)brightness);
            return;
        }
        show_msgbox(title, L("Enter at least 5 seconds.", "请输入至少 5 秒。", "Saisissez au moins 5 secondes."));
    }
}

static void display_options(Clock *clock) {
    const char *formats[] = {L("24-hour", "24 小时制", "24 heures"), L("12-hour", "12 小时制", "12 heures"), NULL};
    int format = clock->hour12, seconds = clock->seconds, visible = clock->visible, flip = clock->flip;
    ndialog_t *dialog = ndlg_create(L("Display", "显示", "Affichage"));
    if (!dialog) return;
    bool ready = ndlg_add_combobox(dialog, L("Format", "时制", "Format"), formats, &format) == 0 &&
        ndlg_begin_group(dialog, DLG_LAYOUT_FLOW_H) == 0 &&
        ndlg_add_checkbox(dialog, L("Seconds", "显示秒", "Secondes"), &seconds) == 0 &&
        ndlg_add_checkbox(dialog, L("Mini clock", "迷你时钟", "Mini-horloge"), &visible) == 0 &&
        ndlg_end_group(dialog) == 0 &&
        ndlg_add_checkbox(dialog, L("Rotate 180°", "翻转屏幕", "Rotation 180°"), &flip) == 0;
    if (!ready) { ndlg_destroy(dialog); return; }
    if (!ui_show(dialog)) return;
    clock->hour12 = format != 0;
    clock->seconds = seconds != 0;
    clock->visible = visible != 0;
    clock->flip = flip != 0;
}

static void options(Clock *clock) {
    const char *items[] = {
        L("Display", "显示", "Affichage"), L("Date", "日期", "Date"), L("Time", "时间", "Heure"),
        L("Mini-clock position", "小钟位置", "Position de la mini-horloge"),
        L("Screen", "屏幕", "Écran"), L("Check on boot", "开机校时", "Vérifier au démarrage"),
        L("Enable startup", "设为开机启动", "Démarrage automatique"), L("About", "关于", "À propos")
    };
    int choice = clock_ui_menu(clock, L("Clock settings", "时钟设置", "Réglages de l'horloge"), items, 8, false);
    if (choice == 0) display_options(clock);
    else if (choice == 1 || choice == 2) change_time(clock, choice == 1);
    else if (choice == 3) clock_ui_position(clock);
    else if (choice == 4) screen_options(clock);
    else if (choice == 5) choose_bool(L("Check on boot", "开机校时", "Vérifier au démarrage"),
        L("Confirm date/time on startup", "启动时确认日期和时间", "Vérifier la date au démarrage"), &clock->check_time);
    else if (choice == 6) startup_install();
    else if (choice == 7) ui_about("nClock",
        "Copyright (c) 2014 Levak\nXavier Andreani, 2016", "GPL-3.0-or-later");
    service(clock);
}

static void startup_check(Clock *clock, bool first) {
    char text[80], message[240];
    if (first) {
        int hour12 = clock->hour12, seconds = clock->seconds, check = clock->check_time;
        ndialog_t *dialog = ndlg_create(title);
        if (dialog) {
            if (ndlg_add_checkbox(dialog, L("12-hour format", "12 小时制", "Format 12 heures"), &hour12) ||
                ndlg_add_checkbox(dialog, L("Seconds", "显示秒", "Secondes"), &seconds) ||
                ndlg_add_checkbox(dialog, L("Check date on boot", "开机校时", "Vérifier la date au démarrage"), &check)) {
                ndlg_destroy(dialog);
            } else if (ui_show(dialog)) {
                clock->hour12 = hour12 != 0;
                clock->seconds = seconds != 0;
                clock->check_time = check != 0;
            }
        }
    }
    if (!first && !clock->check_time) return;
    clock_ui_date(clock, resident_seconds(), text);
    ClockDate date;
    clock_to_date(clock_time(clock, resident_seconds()), &date);
    snprintf(message, sizeof(message), "%s\n%02d:%02d:%02d", text, date.hour, date.minute, date.second);
    clock_diag("date.dialog.before", 0);
#ifdef NCLOCK_DIAGNOSTIC
    size_t used = strlen(message);
    snprintf(message + used, sizeof(message) - used, "\nDIAG D%04u", clock_diag_step());
#endif
    int choice = show_msgbox_2b(title, message, L("Keep", "保留", "Garder"), L("Change", "修改", "Modifier"));
    clock_diag("date.dialog.after", (unsigned)choice);
    if (choice == 2 && edit_date_time(&date, true, true, L("Date and time", "日期和时间", "Date et heure")) &&
        !clock_set_time(clock, resident_seconds(), &date))
        show_msgbox(title, L("Latest date: 2106-02-07 06:28:15.",
            "最晚日期：2106-02-07 06:28:15。", "Date limite : 2106-02-07 06:28:15."));
}

static bool uninstall(Clock *clock) {
    clock_stop(clock);
    clock->visible = false;
    clock->disabled = true;
    if (remove(CONFIG_PATH) && errno != ENOENT) {
        show_msgbox(title, L("Could not delete nclock.cfg.", "删除 nclock.cfg 失败。", "Suppression de nclock.cfg impossible."));
        return false;
    }
    refresh_osscr();
    show_msgbox(title, L("Clock stopped. Remove its startup copy to keep it stopped.",
        "时钟已停止。请移除自启动副本。", "Horloge arrêtée. Retirez sa copie du dossier de démarrage."));
    if (show_msgbox_2b(title, L("Restart now?", "现在重启？", "Redémarrer maintenant ?"),
        L("Later", "稍后", "Plus tard"), L("Restart", "重启", "Redémarrer")) == 2) clock_reboot();
    return true;
}

int app_run(int argc, char **argv) {
    UiLanguage language = ui_language_init();
    const char *items[] = {L("Settings", "设置", "Réglages"), L("Countdown", "倒计时", "Minuteur"), L("Quit", "退出", "Quitter")};
    bool fresh = false, loaded = true, power_ready;
    bool startup = nl_isstartup();
    const char *error = NULL, *name;
    Clock *clock;
    clock_diag("app.entry", startup);
    if (nl_ndless_rev() < 2023) {
        if (!startup) show_msgbox(title, L("Ndless r2023 required.", "需要 Ndless r2023。", "Ndless r2023 requis."));
        return 1;
    }
    if (argc < 1 || strlen(argv[0]) >= sizeof(self_path)) return 1;
    strcpy(self_path, argv[0]);
    name = strrchr(self_path, '/');
    name = name ? name + 1 : self_path;
#ifdef NCLOCK_DIAGNOSTIC
    if (!strcmp(name, "uninstall.tns")) clock_diag_abort(L("Run as nclock-diag.tns.", "请以 nclock-diag.tns 名称运行。", "Lancez nclock-diag.tns."));
#endif
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
        show_msgbox_2b(title, L("Uninstall nClock?", "卸载 nClock？", "Désinstaller nClock ?"),
            L("Cancel", "取消", "Annuler"), L("Uninstall", "卸载", "Désinstaller")) != 2) return 0;
    clock_diag("resident.install.before", 0);
    clock = resident_install(0x4e434c4bu, NKITS_BUILD_ID, &state, sizeof(state), tick, event, &fresh, &error);
#ifdef NCLOCK_DIAGNOSTIC
    diag_clock = clock;
    if (clock) clock->foreground = true;
#endif
    clock_diag("resident.install.after", (unsigned)(uintptr_t)clock);
    if (!clock) {
        if (!startup) show_msgbox(title, error ? ui_error(error) :
            L("Could not start nClock.", "启动 nClock 失败。", "Démarrage de nClock impossible."));
        return 1;
    }
#ifdef NCLOCK_DIAGNOSTIC
    if (!fresh) clock_diag_abort(L("Restart before another diagnostic.", "再次诊断前请重启。", "Redémarrez avant un nouveau diagnostic."));
#endif
    clock_diag("resident.fresh", fresh);
    clock->foreground = true;
    native_msgbox = resident_native(e_show_dialog_box2_);
    native_interrupts = resident_native(e_TCT_Local_Control_Interrupts);
    native_convert = resident_native(e_ascii2utf16);
    /* Record the hardware backend even for startup-only countdown reboots. */
    clock_diag("power.initial.before", 0);
    power_ready = clock_power_open();
    clock_diag("power.initial.after", power_ready);
    if (fresh) {
        unsigned mask = resident_lock();
        clock_defaults(clock);
        clock->foreground = true;
        resident_unlock(mask);
        clock_diag("config.load.before", 0);
        loaded = load_config(clock, resident_seconds());
        backup_raw = resident_seconds();
        clock_diag("config.load.after", loaded);
        clock_diag("config.check_time", clock->check_time);
        clock_diag("config.reboot", clock->reboot);
        clock_diag("config.pending", clock->pending);
    }
    clock->language = (unsigned)language;
    if (!startup && !strcmp(name, "uninstall.tns")) {
        uninstall(clock);
        clock->foreground = false;
        if (fresh) _exit(0);
        return 0;
    }
    clock->disabled = false;
    if (fresh) {
        clock_diag("startup.check.before", loaded);
        startup_check(clock, !loaded);
        clock_diag("startup.check.after", 0);
    }
    if (!startup) {
        clock_diag("ui.open.before", power_ready);
        bool opened = power_ready && clock_ui_open(service);
#ifdef NCLOCK_DIAGNOSTIC
        diag_ui = opened;
#endif
        clock_diag("ui.open.after", opened);
        if (!opened) show_msgbox(title, L("Cannot open the clock display.", "打开时钟界面失败。", "Affichage de l'horloge indisponible."));
        else {
            for (;;) {
                int choice = clock_ui_menu(clock, title, items, 3, true);
                clock_diag("menu.after", (unsigned)choice);
#ifdef NCLOCK_DIAGNOSTIC
                if (!diag_frame) clock_diag_abort(L("Stopped before displaying the clock.", "时钟画面显示前已停止。", "Arrêt avant l'affichage de l'horloge."));
#endif
                if (choice == 0) options(clock);
                else if (choice == 1) countdown_options(clock);
                else break;
                if (!save_config(clock, false)) show_msgbox(title, L(
                    "Settings active. Could not save " SAVE_CONFIG_PATH ".",
                    "设置已生效，写入失败：" SAVE_CONFIG_PATH,
                    "Réglages actifs. Enregistrement impossible : " SAVE_CONFIG_PATH));
            }
            clock_diag("ui.close.before", 0);
            clock_ui_close();
#ifdef NCLOCK_DIAGNOSTIC
            diag_ui = false;
#endif
            clock_diag("ui.close.after", 0);
        }
    }
#ifdef NCLOCK_DIAGNOSTIC
    clock->disabled = true;
#endif
    clock->foreground = false;
    if (!save_config(clock, false) && !startup)
        show_msgbox(title, L("Settings active. Could not save " SAVE_CONFIG_PATH ".",
            "设置已生效，写入失败：" SAVE_CONFIG_PATH,
            "Réglages actifs. Enregistrement impossible : " SAVE_CONFIG_PATH));
#ifdef NCLOCK_DIAGNOSTIC
    clock_diag("app.finish", diag_frame);
    clock_diag_finish(diag_frame ? 0 : 1);
    if (fresh) _exit(diag_frame ? 0 : 1);
    return diag_frame ? 0 : 1;
#else
    if (fresh) _exit(0);
    return 0;
#endif
}
