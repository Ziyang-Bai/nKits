#include "ui.h"
#include "version.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static UiLanguage language;
static Gc source;

static unsigned interrupt_mask(void) {
    unsigned value;
    __asm__ volatile("mrs %0, cpsr" : "=r"(value));
    return value & 0xc0u;
}

static void restore_interrupts(unsigned mask) {
    unsigned value;
    __asm__ volatile("mrs %0, cpsr" : "=r"(value));
    value = (value & ~0xc0u) | (mask & 0xc0u);
    __asm__ volatile("msr cpsr_c, %0" :: "r"(value) : "memory", "cc");
}

static bool prefix(const uint16_t *text, const uint16_t *start) {
    if (!text) return false;
    while (*start)
        if (*text++ != *start++) return false;
    return true;
}

UiLanguage ui_language_init(void) {
    static const uint16_t french[] = {'F','r','a','n',0xe7,'a','i','s',0};
    static const uint16_t simplified[] = {0x7b80,0x4f53,0x4e2d,0x6587,0};
    static const uint16_t traditional[] = {0x7e41,0x9ad4,0x4e2d,0x6587,0};
    language = UI_ENGLISH;
    if (!nl_hassyscall(get_res_string)) return language;
    /* The active system pack starts with its language name, date and time formats. */
    const uint16_t *name = (const uint16_t *)get_res_string(0x73797374, 0);
    if (prefix(name, french)) language = UI_FRENCH;
    else if (prefix(name, simplified) || prefix(name, traditional)) language = UI_CHINESE;
    return language;
}

UiLanguage ui_language(void) {
    return language;
}

const char *ui_text_for(UiLanguage selected, const char *english, const char *chinese, const char *french) {
    if (selected == UI_CHINESE) return chinese;
    if (selected == UI_FRENCH) return french;
    return english;
}

const char *ui_text(const char *english, const char *chinese, const char *french) {
    return ui_text_for(language, english, chinese, french);
}

const char *ui_error(const char *english) {
    static const struct {
        const char *key, *en, *zh, *fr;
    } messages[] = {
        {"CX OS 4.5.5.79 / CX II OS 6.4.0.74 / Ndless r2022 required",
         "CX OS 4.5.5.79 or CX II OS 6.4.0.74 required.", "需要 CX OS 4.5.5.79 或 CX II OS 6.4.0.74。",
         "OS requis : CX 4.5.5.79 ou CX II 6.4.0.74."},
        {"Invalid resident image", "Invalid program image.", "程序映像无效。", "Programme invalide."},
        {"OS display timer unsupported", "Display timer unavailable.", "系统显示计时器不可用。", "Minuterie d'affichage indisponible."},
        {"OS one-second timer unsupported", "Clock timer unavailable.", "系统时钟计时器不可用。", "Minuterie système indisponible."},
        {"Display hook conflict; restart calculator", "Display conflict. Restart the calculator.", "显示钩子冲突，请重启计算器。", "Conflit d'affichage. Redémarrez."},
        {"Restart calculator to update nKits", "Restart to update nKits.", "请重启以更新 nKits。", "Redémarrez pour mettre à jour nKits."},
        {"One-second hook conflict; restart calculator", "Timer conflict. Restart the calculator.", "计时器钩子冲突，请重启计算器。", "Conflit de minuterie. Redémarrez."},
        {"OS time callback unsupported", "System clock unavailable.", "系统时钟回调不可用。", "Horloge système indisponible."},
        {"OS framebuffer unsupported", "System display unavailable.", "系统显示缓冲区不可用。", "Affichage système indisponible."},
        {"Native callback ABI unsupported", "System callbacks unavailable.", "系统回调接口不可用。", "Rappels système indisponibles."},
        {"Event hook conflict; restart calculator", "Input conflict. Restart the calculator.", "输入钩子冲突，请重启计算器。", "Conflit d'entrée. Redémarrez."},
        {"Native event queue unsupported", "System input queue unavailable.", "系统输入队列不可用。", "File d'événements indisponible."},
        {"Native event activity ABI unsupported", "System input interface unavailable.", "系统输入接口不可用。", "Interface d'entrée indisponible."},
        {"Resident display allocation failed", "Not enough display memory.", "显示内存不足。", "Mémoire d'affichage insuffisante."},
        {"Resident hook changed during installation", "Installation conflict. Restart the calculator.", "安装时发生冲突，请重启计算器。", "Conflit d'installation. Redémarrez."},
        {"Restart calculator to update this program", "Restart to update this program.", "请重启以更新程序。", "Redémarrez pour mettre à jour ce programme."},
        {"Resident slots full", "Too many resident programs. Restart the calculator.", "驻留程序过多，请重启计算器。", "Trop de programmes résidents. Redémarrez."},
        {"Resident program is not installed", "Program is not loaded.", "程序尚未加载。", "Programme non chargé."},
        {"Native explorer unsupported; no explorer changes made", "Document browser unavailable.", "系统文档浏览器不可用。", "Navigateur de documents indisponible."},
        {"Explorer hook changed; no explorer changes made", "Browser conflict. Restart the calculator.", "浏览器钩子冲突，请重启计算器。", "Conflit du navigateur. Redémarrez."}
    };
    if (!english) return ui_text("Operation failed.", "操作失败。", "Échec de l'opération.");
    for (unsigned i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i)
        if (!strcmp(english, messages[i].key))
            return ui_text(messages[i].en, messages[i].zh, messages[i].fr);
    return english;
}

bool ui_show(ndialog_t *dialog) {
    if (!dialog) return false;
    unsigned mask = interrupt_mask();
    ndlg_set_button_bar(dialog, NDLG_BTN_SAVE_CANCEL);
    restore_interrupts(0);
    ndialog_result_t result = ndlg_show(dialog);
    ndlg_destroy(dialog);
    wait_no_key_pressed();
    restore_interrupts(mask);
    return result == NDLG_RESULT_SAVE || result == NDLG_RESULT_OK;
}

bool ui_canvas_open(UiCanvas *canvas) {
    unsigned mask = interrupt_mask();
    if (!source) source = gui_gc_global_GC();
    canvas->gc = source ? gui_gc_copy(source, 320, 240) : NULL;
    if (!canvas->gc) { restore_interrupts(mask); return false; }
    /* clipRect intersects the copied GC's viewport, so initialize its bounds. */
    gui_gc_setRegion(canvas->gc, 0, 0, 320, 240, 0, 0, 320, 240);
    ui_canvas_begin(canvas);
    gui_gc_setColorRGB(canvas->gc, 255, 255, 255);
    gui_gc_fillRect(canvas->gc, 0, 0, 320, 240);
    ui_canvas_end(canvas);
    restore_interrupts(mask);
    return true;
}

void ui_canvas_close(UiCanvas *canvas) {
    unsigned mask = interrupt_mask();
    if (canvas->gc) gui_gc_free(canvas->gc);
    canvas->gc = NULL;
    restore_interrupts(mask);
}

void ui_canvas_begin(UiCanvas *canvas) {
    canvas->interrupt_mask = interrupt_mask();
    gui_gc_begin(canvas->gc);
    gui_gc_setFont(canvas->gc, Regular10);
    gui_gc_clipRect(canvas->gc, 0, 0, 320, 240, GC_CRO_SET);
}

void ui_canvas_end(UiCanvas *canvas) {
    gui_gc_finish(canvas->gc);
    restore_interrupts(canvas->interrupt_mask);
}

uint16_t *ui_canvas_pixels(const UiCanvas *canvas) {
    void **surface = *(void ***)canvas->gc[9];
    return *(uint16_t **)surface[8];
}

void ui_canvas_text(UiCanvas *canvas, int x, int y, int width, const char *text, int rgb) {
    gui_gc_clipRect(canvas->gc, x, 0, width, 240, GC_CRO_SET);
    ui_draw_text(canvas->gc, text, x, y - 4, Regular10, rgb);
    gui_gc_clipRect(canvas->gc, 0, 0, 320, 240, GC_CRO_SET);
}

int ui_menu(UiCanvas *canvas, const char *heading, const char *const *items, unsigned count, unsigned selected) {
    wait_no_key_pressed();
    for (;;) {
        ui_canvas_begin(canvas);
        ui_draw_header(canvas->gc, &ui_themes[0], heading);
        unsigned first = selected >= 9 ? selected - 8 : 0;
        for (unsigned i = first; i < count && i < first + 9; ++i)
            ui_draw_row(canvas->gc, &ui_themes[0], 30 + (int)(i - first) * 20,
                        items[i], i == selected);
        ui_draw_footer(canvas->gc, &ui_themes[0],
            ui_text("Arrows: select  Enter: open  Esc: back",
                    "方向键选择  Enter 打开  Esc 返回",
                    "Flèches : choisir  Entrée : ouvrir  Échap : retour"));
        ui_canvas_end(canvas);
        lcd_blit(ui_canvas_pixels(canvas), SCR_320x240_565);
        for (;;) {
            if (isKeyPressed(KEY_NSPIRE_ESC)) {
                wait_no_key_pressed();
                return -1;
            }
            if (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) ||
                isKeyPressed(KEY_NSPIRE_CLICK)) {
                wait_no_key_pressed();
                return (int)selected;
            }
            bool previous = isKeyPressed(KEY_NSPIRE_UP) || isKeyPressed(KEY_NSPIRE_LEFT);
            bool next = isKeyPressed(KEY_NSPIRE_DOWN) || isKeyPressed(KEY_NSPIRE_RIGHT);
            if (previous || next) {
                if (previous) selected = selected ? selected - 1 : count - 1;
                else selected = (selected + 1) % count;
                wait_no_key_pressed();
                break;
            }
            msleep(30);
        }
    }
}

void ui_about(const char *program, const char *credits, const char *license) {
    char text[512];
    const char *stage = !strcmp(NKITS_STAGE, "Alpha Testing") ?
        ui_text("Alpha testing", "Alpha 测试", "Test alpha") : NKITS_STAGE;
    snprintf(text, sizeof(text), "%s %s (%s)\n%s\nCopyright (c) 2026 Ziyang-Bai\n%s\n%s",
        program, NKITS_VERSION, NKITS_COMMIT, stage, credits, license);
    show_msgbox(ui_text("About", "关于", "À propos"), text);
    wait_no_key_pressed();
}
