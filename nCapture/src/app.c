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
    static const uint16_t title[] = {'n','C','a','p','t','u','r','e',0};
    static const uint16_t message[] = {
        'R','e','p','l','a','c','e',' ','e','x','i','s','t','i','n','g',' ',
        'f','i','l','e','?',0
    };
    static const uint16_t cancel[] = {'C','a','n','c','e','l',0};
    static const uint16_t replace_label[] = {'R','e','p','l','a','c','e',0};
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
                    int choice = resident_question(title, message, cancel, replace_label,
                                                   &capture->modal_release);
                    if (choice != 2) {
                        if (choice >= 0 || !resident_errno()) goto finished;
                        outcome = (CaptureResult){CAPTURE_DIALOG_ERROR, resident_errno(), 0, 0};
                    } else replace = 1;
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
            event->cursor_y >= 194 && event->cursor_y < 219) {
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
    /* Preserve the displayed image before painting the logical preview target. */
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
    uint16_t line[64];
    unsigned i;
    for (i = 0; text[i] && i < 63; ++i) line[i] = (unsigned char)text[i];
    line[i] = 0;
    draw(gc, (char *)line, x, y - 4, GC_SM_TOP);
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
    font(gc, Regular10);
    color(gc, 239, 239, 239);
    fill(gc, 0, 0, 320, 240);
    color(gc, 24, 72, 128);
    fill(gc, 0, 0, 320, 27);
    color(gc, 255, 255, 255);
    label(gc, 34, 6, "New capture - nCapture", draw);
    color(gc, 0, 0, 0);
    rect(gc, 59, 36, 201, 151);
    rect(gc, 36, 194, 110, 25);
    rect(gc, 160, 194, 124, 25);
    rect(gc, 162, 196, 120, 21);
    label(gc, 46, 199, "Close [Esc]", draw);
    label(gc, 172, 199, "Save as [Enter]", draw);
    label(gc, 33, 224, "Save again, or close for a new shot.", draw);
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
        case CAPTURE_BAD_PATH: return "Invalid output path";
        case CAPTURE_NO_NAMES: return "No free temporary filename";
        case CAPTURE_OPEN_ERROR: return "Cannot create temporary file";
        case CAPTURE_WRITE_ERROR: return "Write failed";
        case CAPTURE_CLOSE_ERROR: return "Cannot finish file";
        case CAPTURE_RENAME_ERROR: return "Cannot replace chosen file";
        case CAPTURE_BACKUP_ERROR: return "Saved; old backup could not be removed";
        case CAPTURE_DIALOG_ERROR: return "Native Save As unavailable on this OS";
        default: return "Saved";
    }
}

static void status(const Capture *capture, char *text, size_t size) {
    char last[1800];
    if (capture->no_display) {
        snprintf(last, sizeof(last), "OS display is unavailable.");
    } else if (!capture->attempted) {
        snprintf(last, sizeof(last), "No saves yet.");
    } else if (capture->result.code == CAPTURE_OK) {
        snprintf(last, sizeof(last), "Saved: %s", capture->path);
    } else {
        snprintf(last, sizeof(last), "%s (error %d).\n%s%s%s%s%s",
            save_error(capture->result.code),
            capture->result.error ? capture->result.error : capture->result.backup_error,
            capture->path,
            capture->result.cleanup_error ? "\nTemporary file remains: " : "",
            capture->result.cleanup_error ? capture->temporary : "",
            capture->result.backup_error ? "\nOld image retained in backup: " : "",
            capture->result.backup_error ? capture->backup : "");
    }
    snprintf(text, size,
        "%s - Ctrl+. opens a screenshot preview.\n"
        "Enter: native Save As; Esc: close.\n"
        "Remembered folder: %s\nName: %s\nSaved this boot: %u\n%s\n"
        NKITS_VERSION " (" NKITS_COMMIT ") - " NKITS_STAGE,
        capture->enabled ? "Enabled" : "Disabled", capture->folder,
        capture->name, capture->saved, last);
}

static void output_settings(Capture *capture) {
    int choice;
    while ((choice = show_msgbox_3b("nCapture output",
            "Defaults for native Save As. Folder must already exist.\n"
            "Folder is relative to /documents (default /).\n"
            "Keep .bmp in the name; OS files also receive .tns.",
            "Folder", "Name", "Back")) == 1 || choice == 2) {
        char *value = NULL;
        char path[CAPTURE_PATH_MAX];
        char *dest = choice == 1 ? capture->folder : capture->name;
        if (show_msg_user_input("nCapture output", choice == 1 ?
                "Folder below /documents:" : "Filename (including .bmp):", dest, &value) >= 0 && value) {
            if (capture_dialog_path(choice == 1 ? value : capture->folder,
                                    choice == 2 ? value : capture->name, path, sizeof(path)))
                strcpy(dest, value);
            else show_msgbox("nCapture", "Use a folder below /documents and a filename without path separators.");
        }
        free(value);
    }
}

int app_run(void) {
    bool fresh = false;
    const char *error = NULL;
    Capture *capture = resident_install(CAPTURE_ID, NKITS_BUILD_ID, &storage,
        sizeof(storage), capture_tick, capture_event, &fresh, &error);
    if (!capture) {
        if (!nl_isstartup()) show_msgbox("nCapture", error ? error : "Install failed.");
        return 1;
    }
    frame_source = gui_gc_global_GC();
    if (fresh) {
        capture->configuring = 1;
        capture->keypad_xor = hwtype() ? 0 : 0xffffu;
        strcpy(capture->folder, "/");
        strcpy(capture->name, "capture.bmp");
        capture->enabled = 1;
    }
    if (!nl_isstartup()) {
        char text[2800];
        int choice;
        capture->configuring = 1;
        if (fresh) show_msgbox("nCapture installed",
            "Ctrl+. freezes the full OS screen and opens its preview.\n"
            "Press Enter for native Save As, or Esc to close.\n"
            "Default: /documents/capture.bmp.tns\n"
            "Put ncapture.tns in /ndless/startup for silent startup installation.");
        do {
            status(capture, text, sizeof(text));
            choice = show_msgbox_3b(fresh ? "nCapture" : "nCapture already installed", text,
                capture->enabled ? "Disable" : "Enable", "Output", "Close");
            if (choice == 1) {
                if (!capture->enabled && !capture->frame_ready)
                    capture->frame_ready = preview_frame(capture);
                capture->enabled = capture->enabled ? 0 : capture->frame_ready;
            }
            if (choice == 2) output_settings(capture);
        } while (choice == 1 || choice == 2);
        capture->hotkey.held = 0;
    }
    capture->configuring = 0;
    if (fresh) _exit(0);
    return 0;
}
