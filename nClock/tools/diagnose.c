/* nClock, GPL-3.0-or-later. Foreground-only diagnostic reporting. */
#include "diagnose.h"
#include "clock.h"
#include "resident.h"

#include <libndls.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#ifndef NCLOCK_DIAGNOSTIC_MARKER_CACHE
#define NCLOCK_DIAGNOSTIC_MARKER_CACHE 1
#endif

static char report_path[80];
static unsigned sequence;
static bool active, starting, failed, finished, startup, cancelled, hardware, cx2;
static scr_type_t layout = SCR_TYPE_INVALID;
static uint16_t strip[320 * 8];

/* These are read-only snapshots, taken before any report file operation. */
typedef struct {
    unsigned cpsr, sp, caller;
    bool hardware;
    uint32_t rtc, vic[5], timer[2][5], lcd[5];
} Snapshot;

static void capture(Snapshot *s, void *caller) {
    s->cpsr = resident_lock();
    __asm__ volatile("mov %0, sp" : "=r" (s->sp));
    s->caller = (uintptr_t)caller;
    s->hardware = hardware;
    if (s->hardware) {
        s->rtc = *(const volatile uint32_t *)0x90090000u;
        const volatile uint32_t *vic = (const volatile uint32_t *)0xdc000000u;
        for (unsigned i = 0; i < 5; ++i) s->vic[i] = vic[i];
        for (unsigned i = 0; i < 2; ++i) {
            const volatile uint32_t *timer = (const volatile uint32_t *)(0x900d0000u + i * 0x20u);
            s->timer[i][0] = timer[0];
            s->timer[i][1] = timer[1];
            s->timer[i][2] = timer[2];
            s->timer[i][3] = timer[4];
            s->timer[i][4] = timer[5];
        }
        const volatile uint32_t *lcd = (const volatile uint32_t *)0xc0000000u;
        s->lcd[0] = lcd[0];
        s->lcd[1] = lcd[1];
        s->lcd[2] = lcd[4];
        s->lcd[3] = lcd[5];
        s->lcd[4] = lcd[6];
    }
    resident_unlock(s->cpsr);
}

static bool close_report(FILE *file) {
    bool ok = !ferror(file);
    if (fclose(file)) ok = false;
    return ok;
}

static FILE *new_report(void) {
    struct stat st;
    /* Append mode never truncates a report, including on an unexpected collision.
       SDK O_EXCL is not usable on all supported Ndless builds. */
    for (unsigned i = 1; i; ++i) {
        snprintf(report_path, sizeof(report_path), "/documents/nclock-diag-%03u.txt.tns", i);
        if (!stat(report_path, &st)) continue;
        if (errno != ENOENT) return NULL;
        FILE *file = fopen(report_path, "a");
        if (!file) return NULL;
        int seek_result = fseek(file, 0, SEEK_END);
        long size = seek_result ? -1 : ftell(file);
        if (size == 0) return file;
        bool closed = close_report(file);
        if (!closed || size < 0) return NULL;
    }
    return NULL;
}

unsigned clock_diag_step(void) {
    return sequence;
}

void clock_diag(const char *stage, unsigned detail) {
    int saved_errno = errno;
    if (!active || failed || finished) return;
    Snapshot s;
    capture(&s, __builtin_return_address(0));
    ++sequence;
    clock_diag_mark(stage, detail);
    FILE *file = fopen(report_path, "a");
    bool ok = false;
    if (file) {
        ok = fprintf(file, "D%04u stage=%s detail=%u cpsr=%08x sp=%08x caller=%08x\n",
                     sequence, stage, detail, s.cpsr, s.sp, s.caller) >= 0;
        if (s.hardware) {
            if (fprintf(file, " rtc=%08lx vic_irq=%08lx vic_fiq=%08lx vic_raw=%08lx"
                        " vic_select=%08lx vic_enable=%08lx\n",
                        (unsigned long)s.rtc, (unsigned long)s.vic[0], (unsigned long)s.vic[1],
                        (unsigned long)s.vic[2], (unsigned long)s.vic[3], (unsigned long)s.vic[4]) < 0)
                ok = false;
            for (unsigned i = 0; i < 2; ++i)
                if (fprintf(file, " timer2%c_load=%08lx value=%08lx control=%08lx raw=%08lx masked=%08lx\n",
                            'A' + i, (unsigned long)s.timer[i][0], (unsigned long)s.timer[i][1],
                            (unsigned long)s.timer[i][2], (unsigned long)s.timer[i][3],
                            (unsigned long)s.timer[i][4]) < 0) ok = false;
            if (fprintf(file, " lcd_timing0=%08lx lcd_timing1=%08lx lcd_upbase=%08lx"
                        " lcd_lpbase=%08lx lcd_control=%08lx\n",
                        (unsigned long)s.lcd[0], (unsigned long)s.lcd[1], (unsigned long)s.lcd[2],
                        (unsigned long)s.lcd[3], (unsigned long)s.lcd[4]) < 0) ok = false;
        } else if (fprintf(file, " hardware_snapshot=unavailable\n") < 0) ok = false;
        if (!close_report(file)) ok = false;
    }
    if (!ok) failed = true;
    resident_unlock(s.cpsr);
    errno = saved_errno;
    if (failed && !starting) clock_diag_abort("Report write or close failed");
}

void clock_diag_mark(const char *stage, unsigned detail) {
    int saved_errno = errno;
    if (!active || failed || finished || !hardware ||
        (layout != SCR_320x240_565 && layout != SCR_240x320_565)) return;
    unsigned mask = resident_lock();
    char label[29], text[54];
    unsigned i;
    for (i = 0; i < sizeof(label) - 1 && stage[i]; ++i) {
        char ch = stage[i];
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (ch == '_') ch = '-';
        label[i] = ch;
    }
    label[i] = 0;
    snprintf(text, sizeof(text), "D%04u %s %u", sequence, label, detail);
    for (i = 0; i < 320 * 8; ++i) strip[i] = 0xffff;
    clock_label(strip, 0, 0, text, 0, 0xffff);

    const volatile uint32_t *lcd = (const volatile uint32_t *)0xc0000000u;
    uint32_t timing0 = lcd[0], timing1 = lcd[1], base = lcd[4], control = lcd[6];
    unsigned width = (((timing0 >> 2) & 63u) + 1u) * 16u;
    unsigned height = (timing1 & 1023u) + 1u;
    bool portrait = layout == SCR_240x320_565;
    bool range = !(base & 1u) &&
        ((base >= 0x10000000u && base <= 0x14000000u - 320u * 240u * 2u) ||
         (cx2 && base == 0xa8000000u));
    if (range && (control & 0x801u) == 0x801u && ((control >> 1) & 7u) == 6u &&
        width == (portrait ? 240u : 320u) && height >= (portrait ? 320u : 240u)) {
        volatile uint16_t *pixels = (volatile uint16_t *)base;
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 320; ++x)
                pixels[portrait ? x * 240 + y : y * 320 + x] = strip[y * 320 + x];
        if (NCLOCK_DIAGNOSTIC_MARKER_CACHE) clear_cache();
    }
    resident_unlock(mask);
    errno = saved_errno;
}

void clock_diag_finish(int result) {
    int saved_errno = errno;
    if (!active || finished) return;
    unsigned mask = resident_lock();
    resident_unlock(mask);
    /* Set this before either I/O or a dialog; abort cleanup may call us again. */
    finished = true;
    bool was_failed = failed;
    FILE *file = fopen(report_path, "a");
    bool ok = false;
    if (file) {
        ok = fprintf(file, "[completion]\nresult=%s\nstatus=%d\nlast_checkpoint=D%04u\n"
                     "report_write_failed=%u\nrestart_required=1\n",
                     result || was_failed ? "failed" : cancelled ? "cancelled" : "complete",
                     result, sequence, was_failed) >= 0;
        if (!close_report(file)) ok = false;
    }
    if (!ok) failed = true;
    resident_unlock(mask);
    if (!startup) {
        char message[320];
        snprintf(message, sizeof(message), "%s\n%s\nRestart the calculator before running nClock or this diagnostic again.\n"
                 "If it froze, note the last on-screen D number and send the report after restart.",
                 failed ? "Report write failed; the report may be incomplete:" :
                 result ? "Diagnostic stopped. Send this report:" :
                 cancelled ? "Diagnostic cancelled. Report:" : "Diagnostic complete. Send this report:",
                 report_path);
        show_msgbox("nClock diagnostic result", message);
    }
    resident_unlock(mask);
    errno = saved_errno;
}

bool clock_diag_start(const char *build) {
    int saved_errno = errno;
    unsigned mask = resident_lock();
    resident_unlock(mask);
    starting = true;
    startup = nl_isstartup();
    FILE *file = new_report();
    if (!file) goto error;
    active = true;
    bool ok = fprintf(file, "nClock diagnostic 1\ndiagnostic_build=%s\nreport=%s\nstartup=%u\nmarker_cache_flush=%u\n"
                      "scope=foreground checkpoints; production config is read-only\n"
                      "checkpoint=CPU and read-only hardware snapshot before append; file closed after each checkpoint\n"
                      "marker=last checkpoint number plus current stage/detail; top 320x8 native RGB565 strip\n"
                      "limits=logging and markers perturb timing; close does not guarantee survival of a reset\n",
                      build, report_path, startup, (unsigned)NCLOCK_DIAGNOSTIC_MARKER_CACHE) >= 0;
    if (!close_report(file)) ok = false;
    if (!ok) goto error;
    clock_diag("metadata-before", 0);
    if (failed) goto error;

    unsigned model = hwtype(), subtype = nl_hwsubtype(), revision = nl_ndless_rev();
    cx2 = model == 1 && is_cx2;
    hardware = model == 1 && (subtype == 0 || cx2);
    uint32_t marker = hardware ? *(const volatile uint32_t *)0x10000020u : 0;
    if (hardware && (marker == 0x103b18e0u || marker == 0x103b1f80u ||
                     marker == 0x10429ec0u || marker == 0x1042aee0u))
        layout = lcd_type();
    static const uintptr_t entries[] = {
        0x10004b20u, 0x10004b24u, 0x10004b28u, 0x10004b2cu,
        0x10004c58u, 0x10004c5cu, 0x10004c60u,
        0x10004bb8u, 0x10004be0u, 0x10004bdcu, 0x10004c04u
    };
    static const uint32_t expected[] = {
        0xe92d4010u, 0xe24dd028u, 0xe3a04000u, 0xe3a03ffbu,
        0xe59f301cu, 0xe3520000u, 0xe3a02000u,
        0xe59f101cu, 0xe59f101cu, 0x3eau, 0x3e9u
    };
    uint32_t words[sizeof(entries) / sizeof(entries[0])];
    bool checked = cx2;
    if (cx2)
        for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
            words[i] = *(const volatile uint32_t *)entries[i];
            if (words[i] != expected[i]) checked = false;
        }
    struct stat st;
    int config_status = stat("/documents/nclock.cfg", &st);
    int config_errno = config_status ? errno : 0;
    file = fopen(report_path, "a");
    if (!file) goto error;
    ok = fprintf(file, "model=%u\nsubtype=%u\nis_cx2=%u\nndless=%u\nos=%08lx\n"
                 "hardware_snapshot=%s\nnative_layout=%d\n",
                 model, subtype, cx2, revision, (unsigned long)marker,
                 hardware ? "CX" : "unavailable", (int)layout) >= 0;
    if (!config_status) {
        if (fprintf(file, "config=/documents/nclock.cfg\nconfig_size=%ld\n", (long)st.st_size) < 0) ok = false;
    } else if (fprintf(file, "config=/documents/nclock.cfg\nconfig_state=%s\nconfig_errno=%d\n",
                       config_errno == ENOENT ? "absent" : "stat_failed", config_errno) < 0) ok = false;
    if (cx2) {
        if (fprintf(file, "brightness_entries_checked=%u\n", checked) < 0) ok = false;
        for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
            if (fprintf(file, "native_%08lx=%08lx expected=%08lx\n", (unsigned long)entries[i],
                        (unsigned long)words[i], (unsigned long)expected[i]) < 0) ok = false;
    }
    if (!close_report(file)) ok = false;
    if (!ok) goto error;
    clock_diag("metadata-after", 0);
    if (failed) goto error;
    if (startup) {
        clock_diag("startup-refused", 0);
        clock_diag_finish(1);
        starting = false;
        resident_unlock(mask);
        errno = saved_errno;
        return false;
    }

    char message[288];
    snprintf(message, sizeof(message), "At the date check choose Keep. This test stops after one clock frame.\n"
             "If it freezes: photograph the D stage, reset, then send:\n%s\n"
             "Restart before running nClock again.", report_path);
    clock_diag("confirmation-before", 0);
    if (failed) goto error;
    unsigned choice = show_msgbox_2b("nClock diagnostics", message, "Run", "Cancel");
    clock_diag("confirmation-after", choice);
    if (failed) goto error;
    if (choice != 1) {
        cancelled = true;
        clock_diag("cancelled", choice);
        if (failed) goto error;
        clock_diag_finish(0);
        starting = false;
        resident_unlock(mask);
        errno = saved_errno;
        return false;
    }
    starting = false;
    resident_unlock(mask);
    errno = saved_errno;
    return true;

error:
    failed = true;
    finished = true;
    starting = false;
    resident_unlock(mask);
    if (!startup) {
        char message[240];
        snprintf(message, sizeof(message), "Cannot create or save the diagnostic report. Free internal storage and retry.\n"
                 "No clock test was started.\n%s", active ? report_path : "");
        show_msgbox("nClock diagnostics", message);
    }
    resident_unlock(mask);
    errno = saved_errno;
    return false;
}
