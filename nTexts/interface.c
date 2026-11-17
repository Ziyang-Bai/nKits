#include "interface.h"
#include "app_language.h"
#include "app_log.h"
#include "app_strings.h"
#include "../version.h"
#include <errno.h>
#include <libndls.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    Gc gc;
    AppState *app;
    int app_ready;
    int fatal_active;
    int error_count;
    char where[64];
    char detail[192];
} DebugState;

static scr_type_t screen_type = SCR_320x240_565;
static DebugState debug_state;
int last_key_ctrl;

uint32_t app_locale;
void wait_release(void);
static void debug_error_page(const char *title, const char *where, const char *detail, int fatal);

void app_message(Gc gc, const AppState *app, const char *title, const char *msg) {
    const UiTheme *t = &ui_themes[app->theme];
    gui_gc_begin(gc);
    ui_draw_header(gc, t, title);
    ui_draw_text(gc, msg, 16, 84, Regular10, t->fg);
    ui_draw_footer(gc, t, TXT_ENTER_CONTINUE);
    gui_gc_finish(gc);
    gui_gc_blit_to_screen(gc);
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) ||
            isKeyPressed(KEY_NSPIRE_ESC) || isKeyPressed(KEY_NSPIRE_CLICK)) {
            wait_release();
            return;
        }
        msleep(25);
    }
}

void debug_set_runtime(Gc gc, AppState *app, scr_type_t type) {
    screen_type = type;
    debug_state.gc = gc;
    debug_state.app = app;
    debug_state.app_ready = 1;
}

static void debug_format(char *out, size_t cap, const char *fmt, va_list ap) {
    if (!fmt || !cap) return;
    vsnprintf(out, cap, fmt, ap);
    out[cap - 1] = 0;
}

void debug_fail(const char *where, const char *fmt, ...) {
    va_list ap;
    debug_state.error_count++;
    snprintf(debug_state.where, sizeof(debug_state.where), "%s", where ? where : "unknown");
    va_start(ap, fmt);
    debug_format(debug_state.detail, sizeof(debug_state.detail), fmt, ap);
    va_end(ap);
    debug_error_page(L("未知错误", "Unexpected error", "Erreur inattendue"), debug_state.where, debug_state.detail, 0);
}

void debug_fail_errno(const char *where, const char *what) {
    char buf[192];
    snprintf(buf, sizeof(buf), "%s (errno=%d: %s)", what, errno, strerror(errno));
    debug_fail(where, "%s", buf);
}

static void debug_error_page(const char *title, const char *where, const char *detail, int fatal) {
    AppState fallback_app;
    AppState *app = debug_state.app_ready && debug_state.app ? debug_state.app : &fallback_app;
    const UiTheme *t;
    Gc gc = debug_state.gc;
    char head[64];
    char meta[96];
    if (!gc) return;
    if (app == &fallback_app) storage_app_defaults(&fallback_app);
    if (app->theme < 0 || app->theme > 2) app->theme = 0;
    t = &ui_themes[app->theme];
    snprintf(head, sizeof(head), "%s #%d", title, debug_state.error_count ? debug_state.error_count : 1);
    snprintf(meta, sizeof(meta), L("Ndless %u  HW subtype %u  lcd %d", "Ndless %u  HW subtype %u  lcd %d", "Ndless %u  mat. %u  écran %d"),
             nl_ndless_rev(), nl_hwsubtype(), (int)screen_type);
    gui_gc_begin(gc);
    ui_draw_header(gc, t, head);
    ui_draw_text(gc, fatal ? L("程序已进入致命错误处理", "Fatal error", "Erreur fatale") :
              L("程序捕获到错误", "An error occurred", "Une erreur est survenue"), 10, 38, Regular10, t->fg);
    ui_draw_text(gc, L("位置:", "Where:", "Emplacement :"), 10, 70, Bold10, t->accent);
    (void)ui_draw_wrapped(gc, where ? where : L("未知", "unknown", "inconnu"), 10, 88, UI_WIDTH - 20, Regular10, t->fg);
    ui_draw_text(gc, L("详情:", "Details:", "Détails :"), 10, 122, Bold10, t->accent);
    (void)ui_draw_wrapped(gc, detail ? detail : L("无", "none", "aucun"), 10, 140, UI_WIDTH - 20, Regular10, t->fg);
    ui_draw_text(gc, meta, 10, 198, Regular9, t->muted);
    ui_draw_footer(gc, t, fatal ? TXT_ESC_OR_ENTER_STAY : TXT_ESC_OR_ENTER_BACK);
    gui_gc_finish(gc);
    gui_gc_blit_to_screen(gc);
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) ||
            isKeyPressed(KEY_NSPIRE_ESC) || isKeyPressed(KEY_NSPIRE_CLICK)) {
            wait_release();
            if (!fatal) return;
        }
        msleep(25);
    }
}

static void debug_signal_handler(int sig) {
    const char *name = "SIGNAL";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGABRT: name = "SIGABRT"; break;
        case SIGBUS: name = "SIGBUS"; break;
        case SIGILL: name = "SIGILL"; break;
        case SIGFPE: name = "SIGFPE"; break;
        default: break;
    }
    if (debug_state.fatal_active) for (;;) msleep(1000);
    debug_state.fatal_active = 1;
    debug_state.error_count++;
    snprintf(debug_state.where, sizeof(debug_state.where), "signal:%s", name);
    snprintf(debug_state.detail, sizeof(debug_state.detail),
             L("捕获到运行时信号 %s (%d)", "Runtime signal %s (%d)", "Signal reçu %s (%d)"), name, sig);
    debug_error_page(L("致命错误", "Fatal error", "Erreur fatale"), debug_state.where, debug_state.detail, 1);
}

void debug_install_handlers(void) {
    signal(SIGSEGV, debug_signal_handler);
    signal(SIGABRT, debug_signal_handler);
    signal(SIGBUS, debug_signal_handler);
    signal(SIGILL, debug_signal_handler);
    signal(SIGFPE, debug_signal_handler);
}

void wait_release(void) {
    while (any_key_pressed()) msleep(20);
}

int wait_key(void) {
    for (;;) {
        last_key_ctrl = isKeyPressed(KEY_NSPIRE_CTRL);
        if (isKeyPressed(KEY_NSPIRE_UP)) return 1;
        if (isKeyPressed(KEY_NSPIRE_DOWN)) return 2;
        if (isKeyPressed(KEY_NSPIRE_LEFT)) return 3;
        if (isKeyPressed(KEY_NSPIRE_RIGHT)) return 4;
        if (isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_CLICK)) return 5;
        if (isKeyPressed(KEY_NSPIRE_ESC)) return 6;
        if (isKeyPressed(KEY_NSPIRE_MENU)) return 7;
        if (isKeyPressed(KEY_NSPIRE_B)) return 8;
        if (isKeyPressed(KEY_NSPIRE_F)) return 9;
        if (isKeyPressed(KEY_NSPIRE_G)) return 10;
        if (isKeyPressed(KEY_NSPIRE_HOME)) return 11;
        if (isKeyPressed(KEY_NSPIRE_0)) return 12;
        if (isKeyPressed(KEY_NSPIRE_NEGATIVE) || isKeyPressed(KEY_NSPIRE_MINUS)) return 13;
        if (isKeyPressed(KEY_NSPIRE_H)) return 14;
        if (isKeyPressed(KEY_NSPIRE_1)) return 21;
        if (isKeyPressed(KEY_NSPIRE_2)) return 22;
        if (isKeyPressed(KEY_NSPIRE_3)) return 23;
        if (isKeyPressed(KEY_NSPIRE_4)) return 24;
        if (isKeyPressed(KEY_NSPIRE_5)) return 25;
        if (isKeyPressed(KEY_NSPIRE_6)) return 26;
        if (isKeyPressed(KEY_NSPIRE_7)) return 27;
        if (isKeyPressed(KEY_NSPIRE_8)) return 28;
        if (isKeyPressed(KEY_NSPIRE_9)) return 29;
        msleep(25);
    }
}

int choose_list_rows(Gc gc, AppState *app, const char *title, const char **items,
                            const uint16_t *first, size_t stride, int count, int zero_pick) {
    int sel = 0, top = 0, key, visible = 9;
    const UiTheme *t = &ui_themes[app->theme];
    if (count <= 0) return -1;
    for (;;) {
        gui_gc_begin(gc);
        ui_draw_header(gc, t, title);
        for (int i = 0; i < visible && top + i < count; ++i) {
            int row = top + i;
            int y = 30 + i * 21;
            int color = row == sel ? t->highlight_fg : t->fg;
            if (items) ui_draw_row(gc, t, y, items[row], row == sel);
            else {
                if (row == sel) ui_fill(gc, 4, y - 2, UI_WIDTH - 8, 20, t->highlight_bg);
                ui_draw_u16(gc, first + (size_t)row * stride, 10, y, Regular10, color);
            }
        }
        ui_draw_footer(gc, t, zero_pick >= 0 ?
                  L("0编码  上下滚动  Enter选择  Esc返回", "0 encoding  Up/Down  Enter open  Esc back", "0 encodage  Haut/Bas  Enter ouvrir  Esc retour") :
                  count > visible ? L("上下滚动  Enter选择  Esc返回", "Up/Down scroll  Enter open  Esc back", "Haut/Bas défiler  Enter ouvrir  Esc retour") :
                  TXT_FOOTER_LIST);
        gui_gc_finish(gc);
        gui_gc_blit_to_screen(gc);
        key = wait_key(); wait_release();
        if (key >= 21 && key <= 29) {
            int pick = key - 21;
            if (pick < count) return pick;
        }
        if (key == 12 && zero_pick >= 0) return zero_pick;
        if (key == 1) {
            sel = sel > 0 ? sel - 1 : count - 1;
            if (sel < top) top = sel;
            if (sel >= top + visible) top = sel - visible + 1;
        } else if (key == 2) {
            sel = sel + 1 < count ? sel + 1 : 0;
            if (sel >= top + visible) top = sel - visible + 1;
            if (sel < top) top = sel;
        } else if (key == 5) return sel;
        else if (key == 6) return -1;
    }
}

int choose_list(Gc gc, AppState *app, const char *title, const char **items, int count) {
    return choose_list_rows(gc, app, title, items, NULL, 0, count, -1);
}

int choose_u16_list(Gc gc, AppState *app, const char *title, const uint16_t *first,
                           size_t stride, int count) {
    return choose_list_rows(gc, app, title, NULL, first, stride, count, -1);
}

int request_u16_input(const char *title, const char *message, const uint16_t *initial,
                             uint16_t *out, size_t capacity) {
    String request_value;
    String s_title;
    String s_msg;
    String request_struct[2];
    uint16_t title_u16[64];
    uint16_t msg_u16[96];
    static const uint16_t empty[1] = {0};
    int accepted = 0;
    int no_error;
    if (!capacity) return 0;
    out[0] = 0;
    request_value = string_new();
    s_title = string_new();
    s_msg = string_new();
    request_struct[0] = s_msg;
    request_struct[1] = request_value;
    ui_utf8_to_u16(title, title_u16, (int)(sizeof(title_u16) / sizeof(title_u16[0])));
    ui_utf8_to_u16(message, msg_u16, (int)(sizeof(msg_u16) / sizeof(msg_u16[0])));
    string_set_utf16(s_title, (const char *)title_u16);
    string_set_utf16(s_msg, (const char *)msg_u16);
    string_set_utf16(request_value, (const char *)(initial ? initial : empty));
    no_error = _show_msgUserInput(0, request_struct, s_title->str, s_msg->str);
    wait_release();
    if (no_error && request_value->len > 0 && request_value->str) {
        const uint16_t *source = (const uint16_t *)request_value->str;
        size_t length = 0;
        while (length < capacity && source[length]) ++length;
        if (length > 0 && length < capacity) {
            memcpy(out, source, length * sizeof(*out));
            out[length] = 0;
            accepted = 1;
        }
    }
    string_free(s_title);
    string_free(s_msg);
    string_free(request_value);
    return accepted;
}

void about_page(Gc gc, AppState *app) {
    const UiTheme *t = &ui_themes[app->theme];
    char line[96];
    for (;;) {
        int key;
        gui_gc_begin(gc);
        ui_draw_header(gc, t, L("关于 nTexts", "About nTexts", "À propos de nTexts"));
        ui_draw_text(gc, L("nTexts阅读器", "nTexts Text Reader", "nTexts Lecteur de textes"), 10, 42, Regular10, t->fg);
        snprintf(line, sizeof(line), L("版本: %s", "Version: %s", "Version : %s"), NKITS_STAGE " " NKITS_VERSION);
        ui_draw_text(gc, line, 10, 68, Regular10, t->fg);
        snprintf(line, sizeof(line), L("提交: %.12s", "Commit: %.12s", "Révision : %.12s"), NKITS_COMMIT);
        ui_draw_text(gc, line, 10, 92, Regular9, t->fg);
        ui_draw_text(gc, L("作者: Ziyang-Bai", "Author: Ziyang-Bai", "Auteur : Ziyang-Bai"), 10, 118, Regular10, t->fg);
        ui_draw_text(gc, "Copyright (c) 2026 Ziyang-Bai", 10, 146, Regular9, t->fg);
        ui_draw_text(gc, L("许可证: GNU GPL v3.0", "License: GNU GPL v3.0", "Licence : GNU GPL v3.0"), 10, 166, Regular9, t->fg);
        ui_draw_footer(gc, t, TXT_ESC_OR_ENTER_BACK);
        gui_gc_finish(gc);
        gui_gc_blit_to_screen(gc);
        key = wait_key();
        wait_release();
        if (key == 5 || key == 6) return;
    }
}

void usage_page(Gc gc, AppState *app) {
    const UiTheme *t = &ui_themes[app->theme];
    static const char *zh_pages[][8] = {
        {"主页与阅读", "0 浏览 Documents，Enter 打开最近阅读",
         "浏览器上下选文件，Enter 打开或进入目录",
         "F按文件名筛选，0清除筛选",
         "左右翻页，上下按物理行移动",
         "Ctrl+上/下 跳到开头/结尾",
         "Menu 功能菜单，Esc 返回书库",
         "阅读进度自动保存，重新打开可续读"},
        {"搜索与书签", "F 输入搜索词并跳到结果",
         "搜索中 Enter/下 找下一处，上找上一处",
         "Menu 中向后/向前查找沿用搜索词",
         "B 添加当前位置书签",
         "Menu 管理书签可跳转或删除",
         "命中词精确高亮，跨页可左右查看", NULL},
        {"跳转与设置", "G 按百分比、页码、物理行号跳转",
         "G 进入章节目录，Ctrl+左/右 换章",
         "主页 (-) 或阅读 Menu 打开设置",
         "设置第8项切换中文/English/Français",
         "阅读 Menu 按0手动选择单本编码",
         "编码: 自动/UTF-8/GB18030/UTF-16LE/BE",
         "章节规则: *多字符，?单字符"},
        {"快捷键提示", "H 打开当前界面的快捷键提示",
         "阅读搜索中按H显示搜索快捷键",
         "Enter/Esc 关闭本次提示",
         "每个界面每次启动自动提示一次",
         "提示中按0永久关闭自动提示",
         "设置第7项开启或关闭自动提示",
         "关闭自动提示后，仍可按H查看"}
    };
    static const char *en_pages[][8] = {
        {"Home and reading", "0 browse Documents; Enter resumes a book",
         "Up/Down pick; Enter opens file/folder",
         "Browser: F filters names; 0 clears filter",
         "Left/Right pages; Up/Down source lines",
         "Ctrl+Up/Down jumps to start/end",
         "Menu for actions; Esc returns home",
         "Progress saves automatically"},
        {"Search and bookmarks", "F searches text",
         "Search: Enter/Down next; Up previous",
         "Menu repeats forward/backward search",
         "B bookmarks this position",
         "Menu manages bookmarks: go to or delete",
         "Matches are highlighted across pages", NULL},
        {"Go to and settings", "G goes to percent, page or source line",
         "G lists chapters; Ctrl+Left/Right changes chapter",
         "Home (-) or reader Menu opens settings",
         "Settings item 8: Chinese/English/Français",
         "Reader Menu: 0 picks this book's encoding",
         "Auto, UTF-8, GB18030, UTF-16 LE/BE",
         "Chapter rules: * many, ? one character"},
        {"Shortcut hints", "H: open shortcut hints",
         "H during search: search-specific shortcuts",
         "Enter/Esc: close hints for this context",
         "Auto hints: once per context, per launch",
         "0 in hints: stop ALL auto hints permanently",
         "Settings 7: toggle; enabling resets hints",
         "H still works when auto hints are off"}
    };
    static const char *fr_pages[][8] = {
        {"Accueil et lecture", "0 parcourt Documents ; Enter reprend un livre",
         "Haut/Bas : choisir ; Enter : ouvrir",
         "F filtre les noms ; 0 efface le filtre",
         "Gauche/Droite : pages ; Haut/Bas : lignes",
         "Ctrl+Haut/Bas : début/fin du texte",
         "Menu : actions ; Esc : accueil",
         "La progression est sauvegardée"},
        {"Recherche et signets", "F cherche dans le texte",
         "Enter/Bas : suivant ; Haut : précédent",
         "Menu relance la recherche dans les 2 sens",
         "B ajoute un signet ici",
         "Menu : voir, rejoindre ou supprimer un signet",
         "Résultats surlignés même entre pages", NULL},
        {"Navigation et réglages", "G : pourcentage, page ou ligne source",
         "G : chapitres ; Ctrl+Gauche/Droite : chapitre",
         "(-) accueil ou Menu lecture : réglages",
         "Réglage 8 : 中文/English/Français",
         "Menu lecture, 0 : encodage du livre",
         "Auto, UTF-8, GB18030, UTF-16 LE/BE",
         "Règles : * plusieurs, ? un caractère"},
        {"Aide aux raccourcis", "H : aide accueil, bibliothèque ou lecture",
         "H en recherche : raccourcis de recherche",
         "Enter/Esc : fermer l'aide de ce contexte",
         "Auto : une fois par contexte et lancement",
         "0 dans l'aide : arrêt définitif de toute aide auto",
         "Réglage 7 : activer/désactiver l'aide auto",
         "Réactiver réinitialise ; H reste disponible"}
    };
    const char *(*pages)[8] = app_locale == 2 ? fr_pages : app_locale == 1 ? en_pages : zh_pages;
    int page = 0;
    for (;;) {
        int key;
        gui_gc_begin(gc);
        ui_draw_header(gc, t, pages[page][0]);
        for (int i = 1; i < 8 && pages[page][i]; ++i)
            ui_draw_text(gc, pages[page][i], 10, 20 + i * 23, Regular9, t->fg);
        ui_draw_footer(gc, t, L("左右切换  Enter/Esc返回", "Left/Right pages  Enter/Esc back", "Gauche/Droite pages  Enter/Esc retour"));
        gui_gc_finish(gc);
        gui_gc_blit_to_screen(gc);
        key = wait_key();
        wait_release();
        if (key == 5 || key == 6) return;
        if (key == 3 && page > 0) --page;
        if (key == 4 && page < 3) ++page;
    }
}

static uint32_t shortcut_hints_seen;

int shortcut_hints(Gc gc, AppState *app, const char *data_dir,
                          uint32_t context, int search_active, int manual) {
    const UiTheme *t = &ui_themes[app->theme];
    const char *title;
    const char *lines[6];
    if (!manual && (app->shortcut_hints_disabled || (shortcut_hints_seen & context))) return 0;
    shortcut_hints_seen |= context;
    if (context == SHORTCUT_HOME) {
        title = L("主页快捷键", "Home shortcuts", "Raccourcis : accueil");
        lines[0] = L("上/下：选择最近阅读或菜单", "Up/Down: select a book or action", "Haut/Bas : choisir livre ou action");
        lines[1] = L("Enter：打开选中项，继续阅读", "Enter: open selection, resume reading", "Enter : ouvrir, reprendre la lecture");
        lines[2] = L("0：浏览 Documents 文档目录", "0: browse Documents", "0 : parcourir Documents");
        lines[3] = L("(-)：打开阅读设置", "(-): open settings", "(-) : ouvrir les réglages");
        lines[4] = L("H：打开快捷键提示", "H: open shortcut hints", "H : afficher les raccourcis");
        lines[5] = L("Esc：退出 nTexts", "Esc: exit nTexts", "Esc : quitter nTexts");
    } else if (context == SHORTCUT_BROWSER) {
        title = L("书库快捷键", "Library shortcuts", "Raccourcis : bibliothèque");
        lines[0] = L("上/下：选择文件或文件夹", "Up/Down: select a file or folder", "Haut/Bas : choisir fichier ou dossier");
        lines[1] = L("Enter：打开文件或进入文件夹", "Enter: open file or enter folder", "Enter : ouvrir fichier ou dossier");
        lines[2] = L("F：按部分文件名筛选", "F: filter by part of the filename", "F : filtrer une partie du nom");
        lines[3] = L("0：清除文件名筛选", "0: clear the filename filter", "0 : effacer le filtre des noms");
        lines[4] = L("Esc：返回上级目录或主页", "Esc: parent folder or home", "Esc : dossier parent ou accueil");
        lines[5] = L("H：打开快捷键提示", "H: open shortcut hints", "H : afficher les raccourcis");
    } else if (search_active) {
        title = L("阅读搜索快捷键", "Search shortcuts", "Raccourcis : recherche");
        lines[0] = L("Enter/下/F：查找下一处", "Enter/Down/F: next match", "Enter/Bas/F : résultat suivant");
        lines[1] = L("上：查找上一处", "Up: previous match", "Haut : résultat précédent");
        lines[2] = L("左/右：翻页查看跨页命中词", "Left/Right: pages, including long matches", "Gauche/Droite : pages du résultat");
        lines[3] = L("Esc：结束搜索，留在阅读页", "Esc: end search, stay in the reader", "Esc : finir la recherche, rester ici");
        lines[4] = L("Menu：更多功能，G跳转，B书签", "Menu: actions; G: go to; B: bookmark", "Menu : actions ; G : aller ; B : signet");
        lines[5] = L("H：打开快捷键提示", "H: open shortcut hints", "H : afficher les raccourcis");
    } else {
        title = L("阅读快捷键", "Reader shortcuts", "Raccourcis : lecture");
        lines[0] = L("左/右：翻页；上/下：按物理行移动", "Left/Right: pages; Up/Down: source lines", "Gauche/Droite : pages ; Haut/Bas : lignes");
        lines[1] = L("Ctrl+上/下：跳到开头/结尾", "Ctrl+Up/Down: start/end of text", "Ctrl+Haut/Bas : début/fin du texte");
        lines[2] = L("Ctrl+左/右：上一章/下一章", "Ctrl+Left/Right: previous/next chapter", "Ctrl+Gauche/Droite : changer de chapitre");
        lines[3] = L("F：搜索；B：添加当前位置书签", "F: search; B: bookmark this position", "F : rechercher ; B : ajouter un signet");
        lines[4] = L("G：跳转；Menu：功能与设置", "G: go to; Menu: actions and settings", "G : aller à ; Menu : actions et réglages");
        lines[5] = L("Esc：返回主页，进度自动保存", "Esc: home; progress saves automatically", "Esc : accueil ; progression sauvegardée");
    }
    app_log("hints", "show context=%lu manual=%d search=%d",
            (unsigned long)context, manual, search_active);
    gui_gc_begin(gc);
    ui_draw_header(gc, t, title);
    for (int i = 0; i < 6; ++i)
        ui_draw_text(gc, lines[i], 10, 36 + i * 20, Regular9, t->fg);
    gui_gc_setColorRGB(gc, (t->muted >> 16) & 255, (t->muted >> 8) & 255, t->muted & 255);
    gui_gc_drawLine(gc, 8, 155, UI_WIDTH - 8, 155);
    ui_draw_text(gc, TXT_HINTS_CLOSE_ONCE, 10, 164, Regular9, t->fg);
    ui_draw_text(gc, TXT_HINTS_DISABLE, 10, 182, Regular9, t->fg);
    ui_draw_text(gc, TXT_HINTS_REOPEN, 10, 204, Regular9, t->muted);
    ui_draw_text(gc, TXT_HINTS_RESTORE, 10, 222, Regular9, t->muted);
    gui_gc_finish(gc);
    gui_gc_blit_to_screen(gc);
    for (;;) {
        int key = wait_key();
        wait_release();
        if (key == 5 || key == 6) return 1;
        if (key == 12) {
            uint32_t old_disabled = app->shortcut_hints_disabled;
            app->shortcut_hints_disabled = 1;
            if (!storage_save_app(app, data_dir)) {
                app->shortcut_hints_disabled = old_disabled;
                debug_fail_errno("shortcut_hints:disable", data_dir);
            }
            return 1;
        }
    }
}

void shortcut_hints_reset(void) {
    shortcut_hints_seen = 0;
}
