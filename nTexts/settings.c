#include "settings.h"
#include "app_language.h"
#include "app_strings.h"
#include "interface.h"
#include <stdio.h>
#include <string.h>

static int apply_setting_change(Reader *r, AppState *app, const char *data_dir, int pick, int delta) {
    int old_font = app->font_choice;
    int old_theme = app->theme;
    int old_margin = app->margin_choice;
    if (pick == 0) app->font_choice = (app->font_choice + delta + 3) % 3;
    else if (pick == 1) app->theme = (app->theme + delta + 3) % 3;
    else app->margin_choice = app->margin_choice ? 0 : 1;
    if (!storage_save_app(app, data_dir)) {
        app->font_choice = old_font;
        app->theme = old_theme;
        app->margin_choice = old_margin;
        debug_fail_errno("apply_setting_change:save", data_dir);
        return -1;
    }
    if (r) return reader_apply_layout(r, pick == 0);
    return 1;
}

static int manage_chapter_rules(Gc gc, AppState *app, const char *data_dir, Reader *reader) {
    uint16_t rows[MAX_CHAPTER_PATTERNS + 1][CHAPTER_PATTERN_LEN];
    uint16_t input[CHAPTER_PATTERN_LEN];
    static const uint16_t empty[1] = {0};
    ChapterRules before;
    int pick;
    int result;
    int changed = 0;
    uint32_t i;

    memset(rows, 0, sizeof(rows));
    ui_utf8_to_u16(TXT_ADD_CHAPTER_RULE, rows[0], CHAPTER_PATTERN_LEN);
    for (i = 0; i < app->chapter_rules.count; ++i)
        copy_u16(rows[i + 1], CHAPTER_PATTERN_LEN, app->chapter_rules.patterns[i]);
    pick = choose_u16_list(gc, app, TXT_CHAPTER_RULES, rows[0], CHAPTER_PATTERN_LEN,
                           (int)app->chapter_rules.count + 1);
    if (pick < 0) return 0;
    before = app->chapter_rules;
    if (pick == 0) {
        if (!request_u16_input(TXT_CHAPTER_RULES, TXT_RULE_INPUT_HINT, empty,
                               input, CHAPTER_PATTERN_LEN)) return 0;
        result = chapter_rules_add(&app->chapter_rules, input);
        if (result == 1) changed = 1;
        else if (result == -1) app_message(gc, app, TXT_CHAPTER_RULES, TXT_RULE_DUPLICATE);
        else if (result == -2) app_message(gc, app, TXT_CHAPTER_RULES, TXT_RULE_FULL);
        else app_message(gc, app, TXT_CHAPTER_RULES, TXT_RULE_INVALID);
    } else {
        const char *actions[] = {L("删除", "Delete", "Supprimer"), L("取消", "Cancel", "Annuler")};
        if (choose_list(gc, app, TXT_CHAPTER_RULES, actions, 2) == 0)
            changed = chapter_rules_remove(&app->chapter_rules, (uint32_t)pick - 1);
    }
    if (!changed) return 0;
    if (!storage_save_app(app, data_dir)) {
        app->chapter_rules = before;
        debug_fail_errno("manage_chapter_rules:save", data_dir);
        return -1;
    }
    if (reader) return reader_reindex(reader);
    return 1;
}

static int run_settings_item(Gc gc, AppState *app, const char *data_dir, Reader *r,
                             int selection, int delta) {
    if (selection < 3) return apply_setting_change(r, app, data_dir, selection, delta);
    if (selection == 3) return manage_chapter_rules(gc, app, data_dir, r);
    if (selection == 4) about_page(gc, app);
    else if (selection == 5) usage_page(gc, app);
    else if (selection == 7) {
        uint32_t old_locale = app->locale;
        app->locale = (app->locale + (delta < 0 ? 2 : 1)) % 3;
        if (!storage_save_app(app, data_dir)) {
            app->locale = old_locale;
            debug_fail_errno("run_settings_item:locale", data_dir);
            return -1;
        }
        app_locale = app->locale;
    } else {
        uint32_t old_disabled = app->shortcut_hints_disabled;
        app->shortcut_hints_disabled = !old_disabled;
        if (!storage_save_app(app, data_dir)) {
            app->shortcut_hints_disabled = old_disabled;
            debug_fail_errno("run_settings_item:shortcut_hints", data_dir);
            return -1;
        }
        if (!app->shortcut_hints_disabled) shortcut_hints_reset();
    }
    return 1;
}

int settings_menu(Gc gc, AppState *app, const char *data_dir, Reader *r) {
    const UiTheme *t;
    const char *font_labels[3], *theme_labels[3], *margin_labels[2];
    char items[8][96];
    int sel = 0;
    for (;;) {
        int key;
        font_labels[0] = L("小", "Small", "Petit");
        font_labels[1] = L("中", "Medium", "Moyen");
        font_labels[2] = L("大", "Large", "Grand");
        theme_labels[0] = L("浅色", "Light", "Clair");
        theme_labels[1] = L("深色", "Dark", "Sombre");
        theme_labels[2] = L("护眼", "Sepia", "Sépia");
        margin_labels[0] = L("窄", "Narrow", "Étroite");
        margin_labels[1] = L("宽", "Wide", "Large");
        t = &ui_themes[app->theme];
        snprintf(items[0], sizeof(items[0]), L("1. 字号: %s/%s/%s  当前:%s", "1. Font: %s/%s/%s [%s]", "1. Taille : %s/%s/%s [%s]"),
                 font_labels[0], font_labels[1], font_labels[2], font_labels[app->font_choice]);
        snprintf(items[1], sizeof(items[1]), L("2. 主题: %s/%s/%s  当前:%s", "2. Theme: %s/%s/%s [%s]", "2. Thème : %s/%s/%s [%s]"),
                 theme_labels[0], theme_labels[1], theme_labels[2], theme_labels[app->theme]);
        snprintf(items[2], sizeof(items[2]), L("3. 边距: %s/%s  当前:%s", "3. Margin: %s/%s [%s]", "3. Marges : %s/%s [%s]"),
                 margin_labels[0], margin_labels[1], margin_labels[app->margin_choice ? 1 : 0]);
        snprintf(items[3], sizeof(items[3]), L("4. 章节规则: 自定义%lu条", "4. Chapter rules: %lu", "4. Règles de chapitre : %lu"),
                 (unsigned long)app->chapter_rules.count);
        snprintf(items[4], sizeof(items[4]), "%s", L("5. 关于", "5. About", "5. À propos"));
        snprintf(items[5], sizeof(items[5]), "%s", L("6. 使用说明", "6. Help", "6. Aide"));
        snprintf(items[6], sizeof(items[6]), L("7. 自动快捷键提示: %s", "7. Auto shortcut hints: %s", "7. Raccourcis automatiques : %s"),
                 app->shortcut_hints_disabled ? L("关", "Off", "Non") : L("开", "On", "Oui"));
        snprintf(items[7], sizeof(items[7]), L("8. 语言: %s", "8. Language: %s", "8. Langue : %s"),
                 app->locale == 2 ? "Français" : app->locale == 1 ? "English" : L("中文", "Chinese", "Chinois"));
        gui_gc_begin(gc);
        ui_draw_header(gc, t, L("阅读设置", "Reader settings", "Réglages de lecture"));
        for (int i = 0; i < 8; ++i) {
            int y = 29 + i * 23;
            ui_draw_row(gc, t, y, items[i], i == sel);
        }
        ui_draw_footer(gc, t, L("1-8直达  左右/Enter执行  Esc返回", "1-8 select  Left/Right/Enter  Esc back", "1-8 choisir  G/D/Enter  Esc retour"));
        gui_gc_finish(gc);
        gui_gc_blit_to_screen(gc);
        key = wait_key(); wait_release();
        if (key == 1) sel = sel > 0 ? sel - 1 : 7;
        else if (key == 2) sel = sel < 7 ? sel + 1 : 0;
        else if (key >= 21 && key <= 28) {
            sel = key - 21;
            if (run_settings_item(gc, app, data_dir, r, sel, 1) < 0) return -1;
        } else if (key == 3) {
            if (run_settings_item(gc, app, data_dir, r, sel, -1) < 0) return -1;
        } else if (key == 4 || key == 5) {
            if (run_settings_item(gc, app, data_dir, r, sel, 1) < 0) return -1;
        } else if (key == 6) {
            return 0;
        }
    }
}
