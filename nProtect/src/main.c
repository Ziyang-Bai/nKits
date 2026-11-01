#include "protect.h"
#include "resident.h"
#include "resident_build.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROTECT_ID 0x6e505254u
#define CONFIG_PATH "/documents/nprotect.cfg"

static const t_key *const keys[] = {
#define PROTECT_NATIVE(key, name) &KEY_NSPIRE_##key,
    PROTECT_KEYS(PROTECT_NATIVE)
#undef PROTECT_NATIVE
};

typedef struct {
    Protect protect;
    ResidentExplorer policy;
} App;

static App state;
static const char title[] = "nProtect";

static bool held(const ProtectCombo *combo) {
    for (unsigned i = 0; i < combo->count; ++i)
        if (!resident_key_down(keys[combo->keys[i]])) return false;
    return combo->count != 0;
}

static ResidentExplore policy(void *data) {
    App *app = data;
    return !app->protect.enabled || held(&app->protect.combo) ?
        RESIDENT_EXPLORE_DOCUMENTS : RESIDENT_EXPLORE_DENY;
}

static bool load_config(Protect *protect) {
    char data[PROTECT_CONFIG_SIZE];
    FILE *file = fopen(CONFIG_PATH, "rb");
    size_t size;
    bool ok;
    if (!file) return false;
    size = fread(data, 1, sizeof(data), file);
    ok = !ferror(file);
    if (fclose(file)) ok = false;
    return ok && protect_decode(protect, data, size);
}

static bool save_config(const Protect *protect) {
    char data[PROTECT_CONFIG_SIZE];
    size_t size = protect_encode(protect, data);
    FILE *file = fopen(CONFIG_PATH, "wb");
    bool ok;
    if (!file) return false;
    ok = fwrite(data, 1, size, file) == size;
    if (fclose(file)) ok = false;
    return ok;
}

static bool authorize(const Protect *protect) {
    if (!protect->enabled) return true;
    unsigned choice = show_msgbox_2b(title,
        "Keep your secret key combination held while selecting Unlock. Esc cancels.", "Unlock", "Cancel");
    bool ok = choice == 1 && held(&protect->combo);
    wait_no_key_pressed();
    if (choice == 1 && !ok) show_msgbox(title, "Secret combination not held. Settings unchanged.");
    return ok;
}

static void publish(App *app, const Protect *next) {
    bool saved = save_config(next);
    unsigned mask = resident_lock();
    app->protect = *next;
    resident_unlock(mask);
    if (!saved) show_msgbox(title,
        "Settings active for this session, but nprotect.cfg could not be saved. Next boot may use the default '=' key.");
}

static void configure(App *app) {
    char *text = NULL;
    Protect next = app->protect;
    if (show_msg_user_input(title,
        "1-4 held keys, joined by +. Example: CTRL+SHIFT+A. '=' is the default. Enter/esc/home/arrows are not secret keys.",
        "", &text) < 0) { free(text); return; }
    if (!text || !protect_parse(&next.combo, text)) {
        free(text);
        show_msgbox(title, "Invalid combination. Use distinct keys: =, CTRL, SHIFT, A-Z, 0-9, PLUS, MINUS, MULTIPLY, DIVIDE, LP, RP, PERIOD, SPACE, VAR, TRIG, PI, EXP, SQU, EE, CAT, FRAC, NEGATIVE, DEL, TAB.");
        return;
    }
    free(text);
    char combo[PROTECT_CONFIG_SIZE];
    protect_format(&next.combo, combo);
    char message[240];
    snprintf(message, sizeof(message), "Hold %s while choosing Confirm to check that the combination works on your keypad. Esc cancels.", combo);
    if (show_msgbox_2b(title, message, "Confirm", "Cancel") != 1) return;
    bool ok = held(&next.combo);
    wait_no_key_pressed();
    if (!ok) { show_msgbox(title, "Combination not held. Settings unchanged."); return; }
    publish(app, &next);
}

int main(void) {
    bool fresh, loaded = false;
    bool startup = nl_isstartup();
    const char *error = NULL;
    App *app = resident_install(PROTECT_ID, NKITS_BUILD_ID, &state, sizeof(state), NULL, NULL, &fresh, &error);
    if (!app) {
        if (!startup) show_msgbox(title, error ? error : "Could not install nProtect.");
        return 1;
    }
    if (fresh) {
        protect_defaults(&app->protect);
        loaded = load_config(&app->protect);
        app->policy = policy;
    }
    if (!resident_explorer(PROTECT_ID, app->policy, &error)) {
        if (!startup) show_msgbox(title, error ? error : "Explorer hook unsupported. Protection was not enabled.");
        if (fresh) _exit(1);
        return 1;
    }
    if (!startup && ((fresh && !loaded) || authorize(&app->protect))) {
        for (;;) {
            char status[200];
            snprintf(status, sizeof(status), "Explorer protection: %s\nSecret keys: %u\nRecent documents and titles are not protected.",
                app->protect.enabled ? "ON" : "OFF", app->protect.combo.count);
            unsigned choice = show_msgbox_3b(title, status, "Protection", "Settings", "Close");
            if (choice == 1) {
                Protect next = app->protect;
                next.enabled = !next.enabled;
                if (next.enabled && show_msgbox_2b(title,
                    "Enable protection? Keep the configured combination held whenever opening Browse/My Documents.", "Enable", "Cancel") != 1) continue;
                publish(app, &next);
            } else if (choice == 2) {
                unsigned option = show_msgbox_3b(title, "Settings", "Secret keys", "About", "Back");
                if (option == 1) configure(app);
                else if (option == 2) show_msgbox(title,
                    "nProtect " NKITS_VERSION "\n" NKITS_STAGE "\nCommit " NKITS_COMMIT
                    "\nnKits / Ndless\nExplorer access gate, not encryption.\nIndependent implementation of Excale's nProtect behavior.");
            } else break;
        }
    }
    if (!startup) wait_no_key_pressed();
    if (fresh) _exit(0);
    return 0;
}
