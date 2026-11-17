/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "app.h"
#include "resident.h"
#include "resident_build.h"
#include "version.h"
#include "ui.h"

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

static void configure(Hide *hide) {
    int enabled = hide->enabled;
    ndialog_t *dialog = ndlg_create("nHide " NKITS_VERSION);
    if (!dialog) return;
    ndlg_set_padding(dialog, 1, 2);
    if (ndlg_add_checkbox(dialog, ui_text("Root access", "根目录入口", "Accès racine"), &enabled) < 0 ||
        ndlg_add_label(dialog, ui_text(
            "Hold = to open the root.", "按住 = 打开“我的文档”。",
            "Maintenir = pour ouvrir la racine.")) < 0 ||
        ndlg_add_label(dialog, ui_text(
            "Leave system folders unchanged.", "系统文件夹请保持原样。",
            "Garder les dossiers système intacts.")) < 0) {
        ndlg_destroy(dialog);
        show_msgbox(title, ui_text("Cannot open settings.", "无法打开设置。", "Réglages indisponibles."));
        return;
    }
    if (!ui_show(dialog)) return;
    if ((enabled != 0) != hide->enabled) {
        unsigned mask = resident_lock();
        hide->enabled = enabled != 0;
        resident_unlock(mask);
        const char *error = NULL;
        if (!resident_explorer(NHIDE_ID, hide->policy, &error)) {
            mask = resident_lock();
            hide->enabled = false;
            resident_unlock(mask);
            show_msgbox(title, error ? ui_error(error) : ui_text(
                "Cannot change root access.", "无法更改根目录入口。", "Accès racine indisponible."));
        }
    }
}

static void menu(Hide *hide) {
    UiCanvas canvas;
    int selected = 0;
    const char *items[] = {
        ui_text("Root-access settings", "根目录入口设置", "Réglages d'accès racine"),
        ui_text("Help", "帮助", "Aide"),
        ui_text("About", "关于", "À propos"),
        ui_text("Back", "返回", "Retour")
    };
    if (!ui_canvas_open(&canvas)) {
        show_msgbox(title, ui_text("Cannot open menu.", "无法打开菜单。", "Menu indisponible."));
        return;
    }
    for (;;) {
        selected = ui_menu(&canvas, title, items, 4, selected);
        if (selected < 0 || selected == 3) break;
        if (selected == 0) configure(hide);
        else if (selected == 1) show_msgbox(title, ui_text(
            "Hold = when opening My Documents.\n"
            "Hide: cut/paste into your folder outside /documents.\n"
            "Restore: move files back.\n"
            "Leave system folders unchanged.\n"
            "Restore files before removing nHide.\n"
            "Also hold nProtect's keys when enabled.",
            "按住 = 打开“我的文档”。\n"
            "隐藏：剪切到 /documents 外的自建文件夹。\n"
            "恢复：将文件移回原处。\n"
            "请保持系统文件夹原样。\n"
            "移除 nHide 前先恢复文件。\n"
            "启用 nProtect 时也须按住其密钥。",
            "Maintenir = à l'ouverture de Mes classeurs.\n"
            "Cacher : déplacer hors de /documents.\n"
            "Restaurer : remettre les fichiers.\n"
            "Garder les dossiers système intacts.\n"
            "Restaurer avant de retirer nHide.\n"
            "Maintenir aussi les touches de nProtect."));
        else if (selected == 2) ui_about(title, ui_text(
            "Behavior inspired by Excale's nHide.",
            "功能参考 Excale 的 nHide。",
            "Fonctions d'après nHide d'Excale."),
            "GPL-3.0-or-later");
    }
    ui_canvas_close(&canvas);
}

int app_run(int argc, char **argv) {
    const char *error = NULL;
    bool fresh = false;
    bool startup = nl_isstartup();
    ui_language_init();
    Hide *hide;
    (void)argc;
    (void)argv;
    hide = resident_install(NHIDE_ID, NKITS_BUILD_ID, &state, sizeof(state),
                            NULL, NULL, &fresh, &error);
    if (!hide) {
        if (!startup) show_msgbox(title, error ? ui_error(error) :
            ui_text("Cannot install nHide.", "无法安装 nHide。", "Impossible d'installer nHide."));
        return 1;
    }
    if (fresh) {
        hide->enabled = true;
        hide->policy = explore;
    }
    if (!resident_explorer(NHIDE_ID, hide->policy, &error)) {
        hide->enabled = false;
        if (!startup) show_msgbox(title, error ? ui_error(error) : ui_text(
            "Explorer unsupported on this OS.", "此系统不支持原生文件浏览器。",
            "Explorateur non pris en charge sur cet OS."));
        if (fresh) _exit(1);
        return 1;
    }
    if (!startup) {
        menu(hide);
        refresh_osscr();
    }
    if (fresh) _exit(0);
    return 0;
}
