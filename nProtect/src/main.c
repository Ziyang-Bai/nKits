#include "protect.h"
#include "resident.h"
#include "resident_build.h"
#include "version.h"
#include "ui.h"

#include <stdio.h>
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
    unsigned choice = show_msgbox_2b(title, ui_text(
        "Hold your secret keys and choose Unlock.",
        "按住秘密组合键，选择“解锁”。",
        "Maintenez les touches secrètes et choisissez Déverrouiller."),
        ui_text("Unlock", "解锁", "Déverrouiller"), ui_text("Cancel", "取消", "Annuler"));
    bool ok = choice == 1 && held(&protect->combo);
    wait_no_key_pressed();
    if (choice == 1 && !ok) show_msgbox(title, ui_text(
        "Keys not held. No changes.", "未按住组合键，设置不变。", "Touches non maintenues. Aucun changement."));
    return ok;
}

static void publish(App *app, const Protect *next) {
    bool saved = save_config(next);
    unsigned mask = resident_lock();
    app->protect = *next;
    resident_unlock(mask);
    if (!saved) show_msgbox(title, ui_text(
        "Active this session only: cannot save nprotect.cfg. Next boot may use '='.",
        "无法保存 nprotect.cfg，仅本次生效；下次开机可能使用“=”。",
        "Actif pour cette session : nprotect.cfg non enregistré. Au redémarrage, '=' peut être utilisé."));
}

static void configure(App *app) {
    Protect next = app->protect;
    int enabled = next.enabled;
    char text[PROTECT_CONFIG_SIZE], combo[PROTECT_CONFIG_SIZE];
    protect_format(&next.combo, combo);
    strcpy(text, combo);
    ndialog_t *dialog = ndlg_create("nProtect " NKITS_VERSION);
    if (!dialog) return;
    ndlg_set_padding(dialog, 1, 2);
    if (ndlg_add_checkbox(dialog, ui_text("Protection", "保护", "Protection"), &enabled) < 0 ||
        ndlg_add_text_entry(dialog, ui_text("Keys (1-4, +)", "密钥（1-4，+）", "Touches (1-4, +)"), text, sizeof(text)) < 0 ||
        ndlg_add_label(dialog, ui_text(
            "Hold keys to open My Documents.", "按住密钥打开“我的文档”。",
            "Maintenir les touches pour ouvrir.")) < 0 ||
        ndlg_add_label(dialog, ui_text(
            "Browser only; recent files stay visible.", "仅保护入口，最近文档仍可访问。",
            "Récents accessibles ; accès au navigateur.")) < 0 ||
        ndlg_add_label(dialog, "nKits / GPL-3.0+") < 0) {
        ndlg_destroy(dialog);
        show_msgbox(title, ui_text("Cannot open settings.", "无法打开设置。", "Impossible d'ouvrir les réglages."));
        return;
    }
    if (!ui_show(dialog)) return;
    next.enabled = enabled != 0;
    if (!protect_parse(&next.combo, text)) {
        char message[360];
        snprintf(message, sizeof(message), "%s\n%s", ui_text(
            "Use 1-4 distinct keys joined by +:",
            "用 + 连接 1–4 个不同按键：",
            "1–4 touches distinctes séparées par + :"),
            "= CTRL SHIFT A-Z 0-9 PLUS MINUS MULTIPLY DIVIDE LP RP PERIOD SPACE "
            "VAR TRIG PI EXP SQU EE CAT FRAC NEGATIVE DEL TAB");
        show_msgbox(title, message);
        return;
    }
    protect_format(&next.combo, text);
    if (strcmp(text, combo)) {
        char message[360];
        snprintf(message, sizeof(message), ui_text(
            "Hold %s and choose Confirm to test the keys.",
            "按住 %s 并选择“确认”，验证组合键。",
            "Maintenez %s et choisissez Confirmer pour tester."),
            text);
        if (show_msgbox_2b(title, message, ui_text("Confirm", "确认", "Confirmer"),
            ui_text("Cancel", "取消", "Annuler")) != 1) return;
        bool ok = held(&next.combo);
        wait_no_key_pressed();
        if (!ok) {
            show_msgbox(title, ui_text("Keys not held. No changes.",
                "未按住组合键，设置不变。", "Touches non maintenues. Aucun changement."));
            return;
        }
    }
    if (next.enabled && !app->protect.enabled &&
        show_msgbox_2b(title, ui_text(
            "Enable protection? Hold the secret keys whenever opening My Documents.",
            "启用保护？每次打开“我的文档”都须按住秘密组合。",
            "Activer la protection ? Maintenez les touches secrètes pour ouvrir Mes classeurs."),
            ui_text("Enable", "启用", "Activer"), ui_text("Cancel", "取消", "Annuler")) != 1) return;
    if (next.enabled != app->protect.enabled || strcmp(text, combo)) publish(app, &next);
}

static void menu(App *app, bool first_setup) {
    UiCanvas canvas;
    int selected = 0;
    const char *items[] = {
        ui_text("Protection settings", "保护设置", "Réglages de protection"),
        ui_text("About", "关于", "À propos"),
        ui_text("Back", "返回", "Retour")
    };
    if (!ui_canvas_open(&canvas)) {
        show_msgbox(title, ui_text("Cannot open menu.", "无法打开菜单。", "Menu indisponible."));
        return;
    }
    for (;;) {
        selected = ui_menu(&canvas, title, items, 3, selected);
        if (selected < 0 || selected == 2) break;
        if (selected == 0) {
            if (first_setup || authorize(&app->protect)) {
                first_setup = false;
                configure(app);
            }
        } else if (selected == 1) {
            ui_about(title, ui_text(
                "Behavior inspired by Excale's nProtect.",
                "功能参考 Excale 的 nProtect。",
                "Fonctions d'après nProtect d'Excale."),
                "GPL-3.0-or-later");
        }
    }
    ui_canvas_close(&canvas);
}

int main(void) {
    bool fresh, loaded = false;
    bool startup = nl_isstartup();
    ui_language_init();
    const char *error = NULL;
    App *app = resident_install(PROTECT_ID, NKITS_BUILD_ID, &state, sizeof(state), NULL, NULL, &fresh, &error);
    if (!app) {
        if (!startup) show_msgbox(title, error ? ui_error(error) :
            ui_text("Cannot install nProtect.", "无法安装 nProtect。", "Impossible d'installer nProtect."));
        return 1;
    }
    if (fresh) {
        protect_defaults(&app->protect);
        loaded = load_config(&app->protect);
        app->policy = policy;
    }
    if (!resident_explorer(PROTECT_ID, app->policy, &error)) {
        if (!startup) show_msgbox(title, error ? ui_error(error) : ui_text(
            "Explorer unsupported. Protection is off.", "不支持此文件浏览器，保护未启用。",
            "Explorateur non pris en charge. Protection inactive."));
        if (fresh) _exit(1);
        return 1;
    }
    if (!startup) {
        menu(app, fresh && !loaded);
        wait_no_key_pressed();
        refresh_osscr();
    }
    if (fresh) _exit(0);
    return 0;
}
