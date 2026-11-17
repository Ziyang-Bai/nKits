#include "diagnose.h"
#include "resident_check.h"
#include "resident_build.h"
#include "ui.h"
#include "common/draw.h"
#include "input.h"

#include <libndls.h>
#include <ngc.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define LEGACY_BUILD "42a68c85c31b226001a71e5c06e5ff63af6d4d4e70f7c6e1aa1d0d858b13a5c6"

static bool screen_ready, cancelled;
static Ui ui;
static Gc graphics;

static void clear(void) {
    ui_present(&ui);
    gui_gc_begin(graphics);
    ui_draw_header(graphics, &ui_themes[0], "nUSB - automatic diagnostics");
}

static void present(void) {
    gui_gc_finish(graphics);
    gui_gc_blit_to_screen(graphics);
    ui_present(&ui);
}

static void text(int y, const char *line) {
    ui_draw_text(graphics, line, 8, y, Regular10, ui_themes[0].fg);
}

bool diagnose_cancelled(void) {
    if (screen_ready) ui_present(&ui);
    if (isKeyPressed(KEY_NSPIRE_ESC)) cancelled = true;
    return cancelled;
}

void diagnose_progress(unsigned current, unsigned total, const char *label) {
    char line[64];
    if (!screen_ready) return;
    clear();
    if (total) snprintf(line, sizeof(line), "Step %u / %u", current, total);
    else snprintf(line, sizeof(line), "Completed checks: %u", current);
    text(34, line);
    text(54, label);
    text(88, "Keep the drive connected.");
    text(106, "Results are saved to internal storage.");
    text(124, "Failed cases do not require a key press.");
    ui_draw_footer(graphics, &ui_themes[0], "Esc: stop safely and save the report");
    present();
}

static bool confirm(void) {
    clear();
    text(30, "Back up the drive before this test.");
    text(48, "Tests create their own temporary files.");
    text(66, "A device fault can affect filesystem metadata.");
    text(98, "Close all USB documents first.");
    text(116, "Connect the drive and keep it connected.");
    text(148, "All software cases run automatically.");
    text(166, "One report is saved inside the calculator.");
    ui_draw_footer(graphics, &ui_themes[0], "Enter/Return/Click: run   Esc: cancel");
    present();
    wait_no_key_pressed();
    input_reset();
    InputAction action;
    for (;;) {
        action = input_read().action;
        if (action == INPUT_ENTER || action == INPUT_BACK) break;
        msleep(20);
    }
    wait_no_key_pressed();
    return action == INPUT_ENTER;
}

static FILE *report_open(char path[80]) {
    for (unsigned i = 1; i <= 999; ++i) {
        snprintf(path, 80, "/documents/nusb-diag-%03u.txt.tns", i);
        if (!access(path, F_OK)) continue;
        FILE *report = fopen(path, "a");
        if (!report) return NULL;
        if (!fseek(report, 0, SEEK_END) && ftell(report) == 0) {
            setvbuf(report, NULL, _IONBF, 0);
            return report;
        }
        fclose(report);
    }
    return NULL;
}

static ResidentApi *legacy_resident(uint32_t marker) {
    uint32_t entry = marker == 0x1042aee0u ? 0x10477278u : 0x10476258u;
    const uint32_t *code = (void *)entry;
    if (code[0] != 0xe51ff004u || !resident_ram_range(code[1], 20)) return NULL;
    const uint32_t *gate = (void *)code[1];
    if (gate[0] != 0xe59ff004u || gate[1] != NUSB_RESIDENT_MAGIC ||
        gate[2] != NUSB_RESIDENT_ABI || !resident_ram_range(gate[3], 16) ||
        !resident_ram_range(gate[4], sizeof(ResidentApi))) return NULL;
    const uint32_t *trampoline = (void *)gate[3];
    if (!resident_prologue(trampoline[0], trampoline[1], marker == 0x1042aee0u) ||
        trampoline[2] != 0xe51ff004u || trampoline[3] != entry + 8) return NULL;
    ResidentApi *api = (void *)gate[4];
    return resident_check_api(api, LEGACY_BUILD) == RESIDENT_COMPATIBLE ? api : NULL;
}

int main(void) {
    char path[80] = "", message[384];
    bool publisher = false, installed = false, pending = false;
    DiagSummary summary = {0};
    ResidentApi *api = NULL;
    const char *error = NULL;
    uint32_t marker = is_cx2 ? *(volatile uint32_t *)0x10000020u : 0;
    if (!is_cx2 || (marker != 0x1042aee0u && marker != 0x10429ec0u) ||
        nl_ndless_rev() < 2022) {
        show_msgbox("nUSB diagnostics", "Requires CX II / CX II CAS, OS 6.4.0.74 and Ndless r2022 or newer.");
        return 1;
    }
    screen_ready = ui_init(&ui);
    if (!screen_ready) return 1;
    graphics = gui_gc_global_GC();
    if (!confirm()) { ui_destroy(&ui); return 0; }
    FILE *report = report_open(path);
    if (!report) {
        ui_destroy(&ui);
        show_msgbox("nUSB diagnostics", "Cannot create the internal report. Free internal storage and retry.");
        return 1;
    }
    fprintf(report, "nUSB automatic diagnostics 1\nos=%08lx\nndless=%lu\ndiagnostic_build=%s\n"
            "report=%s\nphysical_bus_capture=unavailable\n"
            "external_checks=power,cable,wire_PID_payload_ACK\n"
            "scope=bounded scratch-file tests; no raw writes or forced endpoint toggles\n",
            (unsigned long)marker, (unsigned long)nl_ndless_rev(), NUSB_BUILD_ID, path);
    diagnose_progress(0, DIAG_PROFILE_COUNT, "Initializing USB");
    api = legacy_resident(marker);
    if (!api) {
        error = resident_init(&publisher);
        if (!error) api = resident_api();
    }
    if (!error && (!api || api->session_active)) error = "USB session busy: close USB documents before testing";
    if (!error && !api->folder_ready()) error = "System USB folder is unavailable";
    int mask = TCT_Local_Control_Interrupts(0);
    if (!error) {
        fprintf(report, "backend=%s\nresident_build=%s\n", publisher ? "diagnostic_bootstrap" : "existing_resident", api->build_id);
        MscInfo info = {0};
        for (unsigned elapsed = 0; elapsed < 20u * 32768u && !diagnose_cancelled();) {
            api->poll(&info);
            if (info.connected) break;
            diagnose_progress(0, DIAG_PROFILE_COUNT, "Waiting for OTG drive (20 second limit)");
            elapsed += msc_wait();
        }
        if (!info.connected) error = "No drive detected: a new driver may require one unplug/reinsert";
    }
    if (!error) {
        installed = diagnose_usb_install(api, publisher);
        if (!installed) error = "USB transport layout is unsupported or already instrumented";
    }
    if (!error) summary = diagnose_run(api, report, true);
    else {
        summary.stopped = true;
        fprintf(report, "[prerequisite]\nresult=STOPPED\nreason=%s\n", error);
    }
    if (installed) {
        pending = diagnose_usb_pending();
        diagnose_usb_remove();
        if (pending) nl_set_resident();
    }
    bool saved = !ferror(report);
    bool restart = publisher || pending || (api && api->state == RESIDENT_FAILED);
    fprintf(report, "[completion]\npassed=%u\nfailed=%u\nskipped=%u\nstopped=%u\n"
            "leftovers=%u\npending_callback=%u\nrestart_required=%u\nreport_complete=%u\n",
            summary.passed, summary.failed, summary.skipped, summary.stopped,
            summary.leftovers, pending, restart, saved);
    saved = saved && !ferror(report);
    if (fclose(report)) saved = false;
    TCT_Local_Control_Interrupts(mask);
    wait_no_key_pressed();
    ui_destroy(&ui);
    screen_ready = false;
    snprintf(message, sizeof(message), "%s\nPass: %u  Fail: %u  Skip: %u\n%s%s\n%s\n%s%s",
             saved ? "Finished. Send this report:" : "Report write failed:",
             summary.passed, summary.failed, summary.skipped,
             summary.stopped ? "Stopped safely. " : "", error ? "See prerequisite in report." : "",
             path, summary.leftovers ? "Scratch files are listed in the report.\n" : "",
             restart ? "Restart before running the original nUSB." : "Original USB transport restored.");
    show_msgbox("nUSB diagnostic result", message);
    int result = !saved || error || summary.failed || summary.stopped;
    if (publisher) _exit(result);
    return result;
}
