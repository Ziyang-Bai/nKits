/**
 * nCapture - Screenshots for TI-Nspire
 * Copyright (C) 2013 Levak
 * mail: levak92@gmail.com
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */

#include "app.h"
#include <libndls.h>
#include <ngc.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "resident.h"
#include "capture.h"
#include "resident_build.h"
#include "version.h"
#include "ui.h"

#define CAPTURE_ID 0x4e434150u

typedef struct {
    int enabled;
    int frame_ready;
    int configuring;
    int busy;
    int visible;
    int action_held;
    unsigned keypad_xor;
    unsigned modal_release;
    CaptureHotkey hotkey;
    unsigned saved;
    int attempted;
    int no_display;
    char folder[CAPTURE_DIALOG_MAX];
    char name[CAPTURE_DIALOG_MAX];
    char path[CAPTURE_PATH_MAX];
    char temporary[CAPTURE_PATH_MAX];
    char backup[CAPTURE_PATH_MAX];
    CaptureResult result;
    uint16_t pixels[CAPTURE_WIDTH * CAPTURE_HEIGHT];
    uint16_t frame[CAPTURE_WIDTH * CAPTURE_HEIGHT];
    UiLanguage language;
} Capture;

static Capture storage;
static Gc frame_source;
static int preview_frame(Capture *capture);

static const CaptureIo io = {
    resident_open, resident_write, resident_close, resident_remove, resident_errno,
    resident_rename
};

static void copy_screen(volatile uint16_t *screen, const uint16_t *pixels) {
    unsigned i;
    for (i = 0; i < CAPTURE_WIDTH * CAPTURE_HEIGHT; ++i) screen[i] = pixels[i];
}

static unsigned action_keys(const Capture *capture) {
    /* SDK Enter/Ret/Esc rows match on CX and CX II; cache hwtype polarity. */
    unsigned enter = *(volatile uint16_t *)(0x900e0000u + KEY_NSPIRE_ENTER.row);
    unsigned escape = *(volatile uint16_t *)(0x900e0000u + KEY_NSPIRE_ESC.row);
    return (((enter ^ capture->keypad_xor) &
             (KEY_NSPIRE_ENTER.col | KEY_NSPIRE_RET.col)) ? 1u : 0u) |
           (((escape ^ capture->keypad_xor) & KEY_NSPIRE_ESC.col) ? 2u : 0u);
}

static bool capture_tick(void *data) {
    Capture *capture = data;
    volatile uint16_t *screen;
    if (capture->busy || capture->configuring || !capture->visible) return false;
    screen = resident_framebuffer();
    if (!screen) return false;
    if (!action_keys(capture)) capture->action_held = 0;
    capture_preview(capture->pixels, capture->frame, screen);
    return false;
}

static void save_as(Capture *capture) {
    uint16_t title[16], message[64], cancel[16], replace_label[16];
    capture->busy = 1;
    int result = resident_save_dialog(capture->folder, capture->name,
                                      &capture->modal_release);
    if (result < 0) {
        capture->attempted = 1;
        capture->path[0] = '\0';
        capture->result = (CaptureResult){CAPTURE_DIALOG_ERROR, resident_errno(), 0, 0};
    } else if (result == 0x13ef) {
        char selected[CAPTURE_PATH_MAX];
        if (!capture_dialog_path(capture->folder, capture->name, selected, sizeof(selected))) {
            capture->attempted = 1;
            capture->path[0] = '\0';
            capture->result = (CaptureResult){CAPTURE_BAD_PATH, 0, 0, 0};
        } else {
            int replace = 0;
            CaptureResult outcome = {0};
            int fd = resident_open(selected, O_RDONLY, 0);
            if (fd >= 0) {
                if (resident_close(fd) < 0) {
                    outcome = (CaptureResult){CAPTURE_CLOSE_ERROR, resident_errno(), 0, 0};
                } else {
                    void (*convert)(void *, const char *, int) = resident_native(e_ascii2utf16);
                    if (!convert) {
                        outcome = (CaptureResult){CAPTURE_DIALOG_ERROR, ENOSYS, 0, 0};
                    } else {
                        const char *text = ui_text_for(capture->language,
                            "Replace file?", "替换文件？", "Remplacer le fichier ?");
                        size_t (*length)(const char *) = resident_native(e_strlen);
                        const char *heading = ui_text_for(capture->language,
                            "Replace file", "替换文件", "Remplacer");
                        convert(title, heading, length(heading) + 1);
                        convert(message, text, length(text) + 1);
                        text = ui_text_for(capture->language, "Cancel", "取消", "Annuler");
                        convert(cancel, text, length(text) + 1);
                        text = ui_text_for(capture->language, "Replace", "替换", "Remplacer");
                        convert(replace_label, text, length(text) + 1);
                        int choice = resident_question(title, message, cancel, replace_label,
                                                       &capture->modal_release);
                        if (choice != 2) {
                            if (choice >= 0 || !resident_errno()) goto finished;
                            outcome = (CaptureResult){CAPTURE_DIALOG_ERROR, resident_errno(), 0, 0};
                        } else replace = 1;
                    }
                }
            } else if (resident_errno() != ENOENT) {
                outcome = (CaptureResult){CAPTURE_OPEN_ERROR, resident_errno(), 0, 0};
            }
            char *(*copy)(char *, const char *) = resident_native(e_strcpy);
            copy(capture->path, selected);
            capture->attempted = 1;
            capture->result = outcome.code != CAPTURE_OK ? outcome :
                capture_save_path(&io, selected, replace, capture->pixels,
                    CAPTURE_WIDTH, CAPTURE_HEIGHT, capture->temporary, capture->backup,
                    sizeof(capture->temporary));
            if (capture->result.code == CAPTURE_OK || capture->result.code == CAPTURE_BACKUP_ERROR)
                ++capture->saved;
        }
    }
finished:
    capture->action_held = 0;
    capture->busy = 0;
    volatile uint16_t *screen = resident_framebuffer();
    if (screen) capture_preview(capture->pixels, capture->frame, screen);
}

static void capture_event(void *data, struct s_ns_event *event) {
    Capture *capture = data;
    unsigned type = event->type;
    int pressed = capture_hotkey(&capture->hotkey, type, event->ascii,
                                event->key, event->modifiers);
    int shortcut = (type == 8 || type == 16) &&
                   (event->ascii == '.' || event->key == CAPTURE_PERIOD_KEY) &&
                   (event->modifiers & 4);
    if (capture->configuring || capture->busy) return;
    if (capture->enabled && !capture->frame_ready) {
        /* OS dispatch begins after the installer restores its graphics state. */
        capture->configuring = 1;
        capture->frame_ready = preview_frame(capture);
        capture->enabled = capture->frame_ready;
        capture->configuring = 0;
    }
    if (capture->visible) {
        int save = 0, close = 0;
        unsigned keys = action_keys(capture);
        if (!keys && !(event->click & 8)) capture->action_held = 0;
        /* Short Enter/Esc presses arrive as release-only OS events. */
        if (type == 16 && !shortcut) {
            unsigned action = (event->ascii == '\r' || event->ascii == '\n' ||
                               event->key == 0x10u) ? 1u :
                              (event->ascii == 27 || event->key == 0x96u) ? 2u : 0u;
            if (capture->modal_release & action) {
                capture->modal_release &= ~action;
                /* Recompose after the native window handles its closing release. */
                volatile uint16_t *screen = resident_framebuffer();
                if (screen) capture_preview(capture->pixels, capture->frame, screen);
            } else { save = action == 1; close = action == 2; }
        }
        if ((event->click & 8) && type != 16 && !capture->action_held &&
            event->cursor_y >= 190 && event->cursor_y < 212) {
            close = event->cursor_x >= 36 && event->cursor_x < 146;
            save = event->cursor_x >= 160 && event->cursor_x < 284;
            if (save || close) {
                capture->action_held = 1;
            }
        }
        /* The preview is modal; no captured key reaches the underlying OS page. */
        if (type <= 16 || save || close) event->type = 0;
        if (close) {
            volatile uint16_t *screen = resident_framebuffer();
            capture->visible = 0;
            if (screen) copy_screen(screen, capture->pixels);
        } else if (save) save_as(capture);
        return;
    }
    if (!shortcut || !capture->enabled) return;
    event->type = 0;
    if (!pressed) return;
    /* Preserve the OS page before painting the logical preview target. */
    capture->no_display = !resident_copy_screen(capture->pixels);
    if (capture->no_display) return;
    volatile uint16_t *screen = resident_framebuffer();
    if (!screen) return;
    capture->visible = 1;
    capture->action_held = 0;
    capture_preview(capture->pixels, capture->frame, screen);
}

static void label(Gc gc, int x, int y, const char *text,
                  void (*draw)(Gc, char *, int, int, gui_gc_StringMode)) {
    uint16_t line[UI_LINE_UNITS];
    ui_utf8_to_u16(text, line, UI_LINE_UNITS);
    draw(gc, (char *)line, x, y, GC_SM_TOP);
}

static int preview_frame(Capture *capture) {
    Gc (*copy)(Gc, int, int) = resident_native(e_gui_gc_copy);
    int (*begin)(Gc) = resident_native(e_gui_gc_begin);
    void (*finish)(Gc) = resident_native(e_gui_gc_finish);
    void (*release)(Gc) = resident_native(e_gui_gc_free);
    void (*font)(Gc, gui_gc_Font) = resident_native(e_gui_gc_setFont);
    void (*color)(Gc, int, int, int) = resident_native(e_gui_gc_setColorRGB);
    void (*fill)(Gc, int, int, int, int) = resident_native(e_gui_gc_fillRect);
    void (*rect)(Gc, int, int, int, int) = resident_native(e_gui_gc_drawRect);
    void (*draw)(Gc, char *, int, int, gui_gc_StringMode) = resident_native(e_gui_gc_drawString);
    if (!frame_source || !copy || !begin || !finish || !release ||
        !font || !color || !fill || !rect || !draw) return 0;
    Gc gc = copy(frame_source, CAPTURE_WIDTH, CAPTURE_HEIGHT);
    if (!gc) return 0;
    begin(gc);
    const UiTheme *theme = &ui_themes[0];
    color(gc, (theme->bg >> 16) & 255, (theme->bg >> 8) & 255, theme->bg & 255);
    fill(gc, 0, 0, UI_WIDTH, UI_HEIGHT);
    font(gc, Bold12);
    color(gc, (theme->fg >> 16) & 255, (theme->fg >> 8) & 255, theme->fg & 255);
    label(gc, 8, 6, ui_text_for(capture->language,
        "nCapture - Preview", "nCapture - 预览", "nCapture - Aperçu"), draw);
    color(gc, (theme->muted >> 16) & 255, (theme->muted >> 8) & 255, theme->muted & 255);
    rect(gc, 59, 36, 201, 151);
    rect(gc, 36, 190, 110, 22);
    color(gc, (theme->highlight_bg >> 16) & 255, (theme->highlight_bg >> 8) & 255, theme->highlight_bg & 255);
    fill(gc, 160, 190, 124, 22);
    font(gc, Regular10);
    color(gc, (theme->fg >> 16) & 255, (theme->fg >> 8) & 255, theme->fg & 255);
    label(gc, 46, 193, ui_text_for(capture->language,
        "Close [Esc]", "关闭 [Esc]", "Fermer [Esc]"), draw);
    color(gc, (theme->highlight_fg >> 16) & 255, (theme->highlight_fg >> 8) & 255, theme->highlight_fg & 255);
    label(gc, 172, 193, ui_text_for(capture->language,
        "Save [Enter]", "保存 [Enter]", "Sauver [Enter]"), draw);
    font(gc, Regular9);
    color(gc, (theme->muted >> 16) & 255, (theme->muted >> 8) & 255, theme->muted & 255);
    label(gc, 8, UI_FOOTER_Y, ui_text_for(capture->language,
        "Save again, or close for a new shot.",
        "可重复保存；关闭后可重新截图。",
        "Sauvez encore ou fermez pour recapturer."), draw);
    finish(gc);
    /* Use the SDK gui_gc_blit_to_screen buffer walk without changing OS VRAM. */
    void **surface = *(void ***)gc[9];
    const uint16_t *pixels = *(const uint16_t **)surface[8];
    memcpy(capture->frame, pixels, sizeof(capture->frame));
    release(gc);
    return 1;
}

static const char *save_error(CaptureCode code) {
    switch (code) {
        case CAPTURE_BAD_PATH: return ui_text(
            "Invalid path", "路径无效", "Chemin invalide");
        case CAPTURE_NO_NAMES: return ui_text(
            "No free temporary name", "临时文件名已用尽", "Aucun nom temporaire libre");
        case CAPTURE_OPEN_ERROR: return ui_text(
            "Cannot open file", "无法打开文件", "Ouverture impossible");
        case CAPTURE_WRITE_ERROR: return ui_text(
            "Write failed", "写入失败", "Échec d'écriture");
        case CAPTURE_CLOSE_ERROR: return ui_text(
            "Cannot close file", "无法关闭文件", "Fermeture impossible");
        case CAPTURE_RENAME_ERROR: return ui_text(
            "Cannot replace file", "无法替换文件", "Remplacement impossible");
        case CAPTURE_BACKUP_ERROR: return ui_text(
            "Saved; cannot remove backup", "已保存；无法删除备份", "Sauvé ; suppression de sauvegarde impossible");
        case CAPTURE_DIALOG_ERROR: return ui_text(
            "Save As unavailable", "另存为不可用", "Enregistrer sous indisponible");
        default: return ui_text("Saved", "已保存", "Sauvé");
    }
}

static void status(const Capture *capture, char *text, size_t size) {
    char last[2048];
    if (capture->no_display) {
        snprintf(last, sizeof(last), "%s", ui_text(
            "Display unavailable.", "屏幕不可用。", "Écran indisponible."));
    } else if (!capture->attempted) {
        snprintf(last, sizeof(last), "%s", ui_text(
            "No saves yet.", "尚未保存。", "Aucune capture sauvée."));
    } else if (capture->result.code == CAPTURE_OK) {
        snprintf(last, sizeof(last), ui_text(
            "Saved: %s", "已保存：%s", "Sauvé : %s"), capture->path);
    } else {
        snprintf(last, sizeof(last), ui_text(
            "%s (error %d).\n%s%s%s%s%s", "%s（错误 %d）。\n%s%s%s%s%s",
            "%s (erreur %d).\n%s%s%s%s%s"),
            save_error(capture->result.code),
            capture->result.error ? capture->result.error : capture->result.backup_error,
            capture->path,
            capture->result.cleanup_error ? ui_text(
                "\nTemporary file: ", "\n残留临时文件：", "\nFichier temporaire : ") : "",
            capture->result.cleanup_error ? capture->temporary : "",
            capture->result.backup_error ? ui_text(
                "\nOld image backup: ", "\n原图备份：", "\nAncienne image : ") : "",
            capture->result.backup_error ? capture->backup : "");
    }
    snprintf(text, size, ui_text(
        "Folder: %s\nName: %s\nSaved this boot: %u\n%s\n%s (%s)",
        "文件夹：%s\n文件名：%s\n本次启动已保存：%u\n%s\n%s (%s)",
        "Dossier : %s\nNom : %s\nSauvées ce démarrage : %u\n%s\n%s (%s)"),
        capture->folder, capture->name, capture->saved, last, NKITS_VERSION, NKITS_COMMIT);
}

static bool output_input(const char *title, const char *prompt,
                         const char *initial, char output[CAPTURE_DIALOG_MAX]) {
    uint16_t heading[strlen(title) + 1], label[strlen(prompt) + 1];
    uint16_t text[strlen(initial) + 1];
    ascii2utf16(heading, title, sizeof(heading) / sizeof(*heading));
    ascii2utf16(label, prompt, sizeof(label) / sizeof(*label));
    ascii2utf16(text, initial, sizeof(text) / sizeof(*text));
    String value = string_new(), message = string_new();
    bool saved = false;
    if (value && message) {
        /* Request strings use UTF-16; the SDK's input helper uses single-byte text. */
        string_set_utf16(value, (const char *)text);
        string_set_utf16(message, (const char *)label);
        String fields[] = {message, value};
        if (_show_msgUserInput(0, fields, (const char *)heading, (const char *)label) &&
            value->len > 0 && value->len < CAPTURE_DIALOG_MAX) {
            char utf8[value->len * 3 + 1];
            utf162ascii(utf8, (const uint16_t *)value->str, sizeof(utf8));
            if (strlen(utf8) < CAPTURE_DIALOG_MAX) {
                strcpy(output, utf8);
                saved = true;
            }
        }
    }
    if (message) string_free(message);
    if (value) string_free(value);
    wait_no_key_pressed();
    return saved;
}

static void output_settings(Capture *capture) {
    char folder[CAPTURE_DIALOG_MAX], name[CAPTURE_DIALOG_MAX];
    char path[CAPTURE_PATH_MAX];
    const char *title = ui_text("Output defaults", "默认输出", "Sortie par défaut");
    if (!output_input(title, ui_text(
            "Existing folder below /documents:", "/documents 下的已有文件夹：",
            "Dossier existant sous /documents :"), capture->folder, folder))
        return;
    if (!output_input(title, ui_text(
            "Filename (with .bmp):", "文件名（含 .bmp）：", "Nom du fichier (avec .bmp) :"),
            capture->name, name))
        return;
    if (capture_dialog_path(folder, name, path, sizeof(path))) {
        strcpy(capture->folder, folder);
        strcpy(capture->name, name);
    } else show_msgbox("nCapture", ui_text(
        "Use a folder below /documents and a name without / or \\.",
        "请使用 /documents 下的文件夹，文件名不能含 / 或 \\。",
        "Dossier sous /documents ; nom sans / ni \\."));
}

int app_run(void) {
    UiLanguage language = ui_language_init();
    bool fresh = false;
    const char *error = NULL;
    Capture *capture = resident_install(CAPTURE_ID, NKITS_BUILD_ID, &storage,
        sizeof(storage), capture_tick, capture_event, &fresh, &error);
    if (!capture) {
        if (!nl_isstartup()) show_msgbox("nCapture", error ? ui_error(error) :
            ui_text("Install failed.", "安装失败。", "Échec d'installation."));
        return 1;
    }
    capture->configuring = 1;
    capture->language = language;
    frame_source = gui_gc_global_GC();
    if (fresh) {
        capture->keypad_xor = hwtype() ? 0 : 0xffffu;
        strcpy(capture->folder, "/");
        strcpy(capture->name, "capture.bmp");
        capture->enabled = 1;
    }
    if (!nl_isstartup()) {
        UiCanvas canvas;
        int selected = 0;
        if (capture->frame_ready) capture->frame_ready = preview_frame(capture);
        if (!ui_canvas_open(&canvas)) {
            show_msgbox("nCapture", ui_text(
                "Cannot open settings.", "无法打开设置。", "Réglages indisponibles."));
        } else {
            for (;;) {
                const char *items[] = {
                    capture->enabled ? ui_text(
                        "Hotkey: on", "快捷键：开", "Raccourci : actif") : ui_text(
                        "Hotkey: off", "快捷键：关", "Raccourci : inactif"),
                    ui_text("Output defaults", "默认输出", "Sortie par défaut"),
                    ui_text("Last save", "保存详情", "Dernière sauvegarde"),
                    ui_text("About", "关于", "À propos"),
                    ui_text("Back", "返回", "Retour")
                };
                selected = ui_menu(&canvas, "nCapture", items, 5, selected);
                if (selected < 0 || selected == 4) break;
                if (selected == 0) {
                    char text[96];
                    int enabled = capture->enabled;
                    ndialog_t *dialog = ndlg_create(ui_text(
                        "Hotkey settings", "快捷键设置", "Réglage du raccourci"));
                    if (!dialog) {
                        show_msgbox("nCapture", ui_text(
                            "Cannot open settings.", "无法打开设置。", "Réglages indisponibles."));
                        continue;
                    }
                    ndlg_add_checkbox(dialog, ui_text(
                        "Enable Ctrl+.", "启用 Ctrl+.", "Activer Ctrl+."), &enabled);
                    ndlg_add_label(dialog, ui_text(
                        "Enter: Save As. Esc: close preview.",
                        "Enter：另存为。Esc：关闭预览。",
                        "Enter : sauver. Esc : fermer l'aperçu."));
                    snprintf(text, sizeof(text), ui_text(
                        "Saved this boot: %u", "本次启动已保存：%u", "Sauvées ce démarrage : %u"),
                        capture->saved);
                    ndlg_add_label(dialog, text);
                    if (ui_show(dialog)) {
                        if (enabled && !capture->frame_ready)
                            capture->frame_ready = preview_frame(capture);
                        capture->enabled = enabled ? capture->frame_ready : 0;
                    }
                } else if (selected == 1) output_settings(capture);
                else if (selected == 2) {
                    char text[3200];
                    status(capture, text, sizeof(text));
                    show_msgbox("nCapture", text);
                } else if (selected == 3)
                    ui_about("nCapture", "Copyright (c) 2013 Levak", "LGPL-2.0-or-later");
            }
            ui_canvas_close(&canvas);
        }
        refresh_osscr();
        capture->hotkey.held = 0;
    }
    capture->configuring = 0;
    if (fresh) _exit(0);
    return 0;
}
