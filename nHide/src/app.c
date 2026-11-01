/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "app.h"
#include "resident.h"
#include "resident_build.h"
#include "version.h"

#include <unistd.h>

#define NHIDE_ID 0x6e484944u

typedef struct {
    bool enabled;
    ResidentExplorer policy;
} Hide;

static Hide state;
static const char title[] = "nHide";

static ResidentExplore explore(void *data) {
    const Hide *hide = data;
    return hide->enabled && resident_key_down(&KEY_NSPIRE_EQU) ?
        RESIDENT_EXPLORE_ROOT : RESIDENT_EXPLORE_DOCUMENTS;
}

static void help(void) {
    for (;;) {
        unsigned choice = show_msgbox_3b(title,
            "Home: hold = while opening My Documents (2) to browse the filesystem root. "
            "Without =, My Documents opens normally.\n"
            "Use the native explorer's Cut / Paste to move documents out of /documents "
            "to hide them, and back to restore them. You may create your own folder outside /documents.\n"
            "Do not delete system folders, even if they look empty. Hiding is not encryption.",
            "About", "Startup", "Back");
        if (choice == 1) {
            show_msgbox(title, "nHide " NKITS_VERSION "\nCommit: " NKITS_COMMIT
                "\nStage: " NKITS_STAGE "\nnKits\nGPL-3.0-or-later\n"
                "Independent implementation of Excale's nHide behavior.");
        } else if (choice == 2) {
            show_msgbox(title,
                "To load nHide after each reboot, put nhide.tns in "
                "/documents/ndless/startup. Startup loading is silent.\n"
                "Disable access in nHide to stop the secret shortcut for this boot. "
                "Move the program out of startup to stop automatic loading. "
                "Hidden files remain in their folders; restore them before removing nHide.");
        } else return;
    }
}

int app_run(int argc, char **argv) {
    const char *error = NULL;
    bool fresh = false;
    bool startup = nl_isstartup();
    Hide *hide;
    (void)argc;
    (void)argv;
    hide = resident_install(NHIDE_ID, NKITS_BUILD_ID, &state, sizeof(state),
                            NULL, NULL, &fresh, &error);
    if (!hide) {
        if (!startup) show_msgbox(title, error ? error : "Could not install nHide.");
        return 1;
    }
    if (fresh) {
        hide->enabled = true;
        hide->policy = explore;
    }
    if (!resident_explorer(NHIDE_ID, hide->policy, &error)) {
        hide->enabled = false;
        if (!startup) show_msgbox(title, error ? error : "Native explorer is not supported on this OS.");
        if (fresh) _exit(1);
        return 1;
    }
    if (!startup) {
        for (;;) {
            unsigned choice = show_msgbox_3b(title,
                hide->enabled ?
                    "Root access: enabled\nHold = while opening My Documents to browse /. "
                    "Open without = to browse /documents. Files are moved with the native explorer." :
                    "Root access: disabled\nMy Documents opens normally. Hidden files are unchanged; "
                    "enable access to reach them again.",
                hide->enabled ? "Disable" : "Enable", "Help", "Close");
            if (choice == 1) {
                hide->enabled = !hide->enabled;
                if (!resident_explorer(NHIDE_ID, hide->policy, &error)) {
                    hide->enabled = false;
                    show_msgbox(title, error ? error : "Could not change root access.");
                }
            }
            else if (choice == 2) help();
            else break;
        }
        refresh_osscr();
    }
    if (fresh) _exit(0);
    return 0;
}
