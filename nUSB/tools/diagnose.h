#ifndef NUSB_DIAGNOSE_H
#define NUSB_DIAGNOSE_H

#include "resident.h"
#include <stdio.h>

typedef enum {
    DIAG_BASELINE, DIAG_PACE_2, DIAG_PACE_20, DIAG_CSW_100, DIAG_CSW_500,
    DIAG_ALIGNED, DIAG_UNALIGNED, DIAG_WAIT_15S, DIAG_REPEAT, DIAG_PROFILE_COUNT
} DiagProfile;

typedef struct {
    unsigned passed, failed, skipped;
    bool stopped, leftovers;
} DiagSummary;

bool diagnose_usb_install(ResidentApi *api, bool local_backend);
bool diagnose_usb_profile(DiagProfile profile);
bool diagnose_usb_pending(void);
bool diagnose_usb_information(FILE *report);
void diagnose_usb_finish(FILE *report);
void diagnose_usb_remove(void);
const char *diagnose_profile_name(DiagProfile profile);
DiagSummary diagnose_run(ResidentApi *api, FILE *report, bool native_paths);
void diagnose_progress(unsigned current, unsigned total, const char *label);
bool diagnose_cancelled(void);

#endif
