#include "tasks.h"
#include "common/ui.h"

#include <libndls.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VISIBLE_ROWS 4u
#define DETAIL_ROWS 5u
#define T(en, zh, fr) ui_text(en, zh, fr)

static UiCanvas canvas;
static const UiTheme *const theme = &ui_themes[0];

static const char *title(unsigned kind) {
    switch (kind) {
    case OBJECT_TASK: return T("Tasks", "任务", "Tâches");
    case OBJECT_POOL: return T("Memory pools", "内存池", "Pools mémoire");
    case OBJECT_EVENT: return T("Event groups", "事件组", "Groupes d'événements");
    case OBJECT_QUEUE: return T("Queues", "队列", "Files");
    case 4: return T("Settings", "设置", "Réglages");
    default: return T("About", "关于", "À propos");
    }
}

static const char *status(unsigned state) {
    switch (state) {
    case 0: return T("Ready", "就绪", "Prête");
    case 1: return T("Suspended", "挂起", "Suspendue");
    case 2: return T("Sleeping", "休眠", "En sommeil");
    case 3: return T("Mailbox wait", "等待邮箱", "Attend boîte");
    case 4: return T("Queue wait", "等待队列", "Attend file");
    case 5: return T("Pipe wait", "等待管道", "Attend tube");
    case 6: return T("Semaphore wait", "等待信号量", "Attend sémaphore");
    case 7: return T("Event wait", "等待事件", "Attend événement");
    case 8: return T("Partition wait", "等待分区", "Attend partition");
    case 9: return T("Memory wait", "等待内存", "Attend mémoire");
    case 10: return T("Driver wait", "等待驱动", "Attend pilote");
    case 11: return T("Finished", "已完成", "Finie");
    case 12: return T("Terminated", "已终止", "Terminée");
    default: return T("Unknown", "未知", "Inconnue");
    }
}

static const char *error_text(const char *error) {
#define ERROR(en, zh, fr) if (!strcmp(error, en)) return T(en, zh, fr)
    ERROR("Success", "成功", "Succès");
    ERROR("Invalid event group", "事件组无效", "Groupe d'événements invalide");
    ERROR("Invalid memory pool", "内存池无效", "Pool mémoire invalide");
    ERROR("Invalid queue", "队列无效", "File invalide");
    ERROR("Invalid task", "任务无效", "Tâche invalide");
    ERROR("Invalid object count", "对象数量无效", "Nombre d'objets invalide");
    ERROR("Not enough memory", "内存不足", "Mémoire insuffisante");
    ERROR("List changed; refresh again", "列表已变，请刷新", "Liste modifiée : actualisez");
    ERROR("Object disappeared or was replaced", "对象已消失或被替换", "Objet disparu ou remplacé");
    ERROR("Protected object; action refused", "对象受保护，操作被拒绝", "Objet protégé : action refusée");
    ERROR("Nucleus error", "Nucleus 错误", "Erreur Nucleus");
    ERROR("Current application task is protected", "当前应用任务受保护", "Tâche de l'application protégée");
    ERROR("Task has already stopped", "任务已停止", "Tâche déjà arrêtée");
    ERROR("Unknown task state; action refused", "状态未知，操作被拒绝", "État inconnu : action refusée");
    ERROR("Unsupported Ndless native dispatcher", "不支持此 Ndless 原生分派器", "Répartiteur Ndless incompatible");
    ERROR("Unsupported Nucleus task routines", "不支持此 Nucleus 任务接口", "Routines de tâches incompatibles");
    ERROR("Unsupported Nucleus pool/event/queue routines", "不支持此 Nucleus 对象接口", "Routines d'objets incompatibles");
#undef ERROR
    return ui_error(error);
}

static void text(int y, const char *format, ...) {
    char line[192];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    ui_draw_text(canvas.gc, line, 10, y, Regular10, theme->fg);
}

static void clear(const char *heading) {
    ui_canvas_begin(&canvas);
    char line[96];
    snprintf(line, sizeof(line), "TaskManager - %s", heading);
    ui_draw_header(canvas.gc, theme, line);
}

static void present(void) {
    ui_canvas_end(&canvas);
    lcd_blit(ui_canvas_pixels(&canvas), SCR_320x240_565);
}

static unsigned key(void) {
    for (;;) {
        if (isKeyPressed(KEY_NSPIRE_ESC)) return 1;
        if (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) ||
            isKeyPressed(KEY_NSPIRE_CLICK)) return 2;
        if (isKeyPressed(KEY_NSPIRE_UP)) return 3;
        if (isKeyPressed(KEY_NSPIRE_DOWN)) return 4;
        if (isKeyPressed(KEY_NSPIRE_1) || isKeyPressed(KEY_NSPIRE_R)) return 5;
        if (isKeyPressed(KEY_NSPIRE_LEFT)) return 6;
        if (isKeyPressed(KEY_NSPIRE_RIGHT)) return 7;
        msleep(30);
    }
}

static void message(const char *heading, const char *body, int result) {
    ndialog_t *dlg = ndlg_create(heading);
    if (!dlg) return;
    if (!strcmp(body, "CX 4.5.5.79 / CX II 6.4.0.74 and Ndless r2023 required")) {
        ndlg_add_label(dlg, T("Required:", "需要：", "Requis :"));
        ndlg_add_label(dlg, "CX: 4.5.5.79 / CX II: 6.4.0.74");
        ndlg_add_label(dlg, "Ndless r2023");
    } else ndlg_add_label(dlg, error_text(body));
    if (result) {
        char line[48];
        snprintf(line, sizeof(line), "%s: %d", T("Code", "代码", "Code"), result);
        ndlg_add_label(dlg, line);
    }
    ui_show(dlg);
}

static bool confirm(const Object *object) {
    ndialog_t *dlg = ndlg_create(object->kind == OBJECT_TASK ?
        T("Terminate task?", "终止任务？", "Terminer la tâche ?") :
        T("Delete object?", "删除对象？", "Supprimer l'objet ?"));
    if (!dlg) return false;
    char line[64];
    snprintf(line, sizeof(line), "%.8s @ %08lX", object->name, (unsigned long)object->address);
    ndlg_add_label(dlg, line);
    ndlg_add_label(dlg, T("May freeze the OS or lose data.", "可能导致死机或数据丢失。", "Risque : blocage, perte de données."));
    ndlg_add_label(dlg, T("Only act on objects you know.", "仅操作用途明确的对象。", "Objet connu uniquement."));
    int proceed = 0;
    ndlg_add_checkbox(dlg, object->kind == OBJECT_TASK ?
        T("Terminate", "终止", "Terminer") : T("Delete", "删除", "Supprimer"), &proceed);
    return ui_show(dlg) && proceed;
}

static void field(unsigned *index, unsigned page, const char *label, const char *format, ...) {
    unsigned row = (*index)++;
    if (row / DETAIL_ROWS != page) return;
    int y = 42 + (int)(row % DETAIL_ROWS) * 31;
    ui_draw_text(canvas.gc, label, 10, y, Regular9, theme->muted);
    char value[96];
    va_list args;
    va_start(args, format);
    vsnprintf(value, sizeof(value), format, args);
    va_end(args);
    ui_draw_text(canvas.gc, value, 12, y + 14, Regular10, theme->fg);
}

static unsigned detail_fields(const Object *object, unsigned page) {
    unsigned n = 0;
#define FIELD(label, ...) field(&n, page, label, __VA_ARGS__)
    FIELD(T("Name", "名称", "Nom"), "%.8s", object->name);
    FIELD(T("Address", "地址", "Adresse"), "%08lX", (unsigned long)object->address);
    switch (object->kind) {
    case OBJECT_TASK:
        FIELD(T("State", "状态", "État"), "%u (%s)", object->data.task.status, status(object->data.task.status));
        FIELD(T("RTOS state", "RTOS 状态", "État RTOS"), "%s", task_status(object->data.task.status));
        FIELD(T("Scheduled", "调度次数", "Ordonnancements"), "%u", object->data.task.scheduled);
        FIELD(T("Priority", "优先级", "Priorité"), "%u", object->data.task.priority);
        FIELD(T("Preemption", "抢占", "Préemption"), "%u (%s)", object->data.task.preempt,
            object->data.task.preempt == 10 ? T("Yes", "是", "Oui") : T("No", "否", "Non"));
        FIELD(T("Time slice", "时间片", "Quantum"), "%u", object->data.task.time_slice);
        FIELD(T("Stack base", "栈基址", "Base de pile"), "%08lX", (unsigned long)object->data.task.stack_base);
        FIELD(T("Stack size", "栈大小", "Taille de pile"), "%u", object->data.task.stack_size);
        FIELD(T("Minimum stack", "最小栈空间", "Pile minimale"), "%u", object->data.task.minimum_stack);
        FIELD(T("Entry", "入口", "Entrée"), "%08lX", (unsigned long)object->data.task.entry);
        FIELD(T("Current task", "当前任务", "Tâche courante"), "%s", object->address == native_current() ? T("Yes", "是", "Oui") : T("No", "否", "Non"));
        break;
    case OBJECT_POOL:
        FIELD(T("Start", "起始地址", "Début"), "%08lX", (unsigned long)object->data.pool.start);
        FIELD(T("Pool size", "内存池大小", "Taille du pool"), "%u", object->data.pool.size);
        FIELD(T("Min. allocation", "最小分配量", "Allocation minimale"), "%u", object->data.pool.minimum);
        FIELD(T("Available", "可用空间", "Disponible"), "%u", object->data.pool.available);
        FIELD(T("Suspend type", "挂起类型", "Type de suspension"), "%u", object->data.pool.suspend);
        FIELD(T("Tasks waiting", "等待任务数", "Tâches en attente"), "%u", object->data.pool.waiting);
        FIELD(T("First task", "首个任务", "Première tâche"), "%08lX", (unsigned long)object->data.pool.first_task);
        break;
    case OBJECT_EVENT:
        FIELD(T("Event flags", "事件标志", "Indicateurs"), "%08X (%u)", object->data.event.flags, object->data.event.flags);
        FIELD(T("Tasks waiting", "等待任务数", "Tâches en attente"), "%u", object->data.event.waiting);
        FIELD(T("First task", "首个任务", "Première tâche"), "%08lX", (unsigned long)object->data.event.first_task);
        break;
    case OBJECT_QUEUE:
        FIELD(T("Start", "起始地址", "Début"), "%08lX", (unsigned long)object->data.queue.start);
        FIELD(T("Queue size", "队列大小", "Taille de file"), "%u", object->data.queue.size);
        FIELD(T("Available", "可用空间", "Disponible"), "%u", object->data.queue.available);
        FIELD(T("Messages", "消息数", "Messages"), "%u", object->data.queue.messages);
        FIELD(T("Message type", "消息类型", "Type de message"), "%u", object->data.queue.message_type);
        FIELD(T("Message size", "消息长度", "Taille de message"), "%u", object->data.queue.message_size);
        FIELD(T("Suspend type", "挂起类型", "Type de suspension"), "%u", object->data.queue.suspend);
        FIELD(T("Tasks waiting", "等待任务数", "Tâches en attente"), "%u", object->data.queue.waiting);
        FIELD(T("First task", "首个任务", "Première tâche"), "%08lX", (unsigned long)object->data.queue.first_task);
        break;
    }
#undef FIELD
    return (n + DETAIL_ROWS - 1) / DETAIL_ROWS;
}

static void details(Object selected) {
    unsigned page = 0;
    for (;;) {
        Object live;
        int result = native_info(selected.kind, selected.address, &live);
        if (result || !object_same(&live, &selected)) {
            message(T("Object unavailable", "对象不可用", "Objet indisponible"), native_error(result ? result : -1003), result);
            return;
        }
        clear(title(selected.kind));
        unsigned pages = detail_fields(&live, page);
        text(26, "%s %u/%u", T("Page", "页", "Page"), page + 1, pages);
        ui_draw_text(canvas.gc, T("Left/Right: page  R: refresh", "左/右：翻页  R：刷新", "←→ page  R: actualiser"),
            8, 200, Regular9, theme->muted);
        char hint[96];
        snprintf(hint, sizeof(hint), "1: %s  Esc: %s",
            selected.kind == OBJECT_TASK ? T("terminate", "终止", "terminer") : T("delete", "删除", "supprimer"),
            T("back", "返回", "retour"));
        ui_draw_footer(canvas.gc, theme, hint);
        present();
        unsigned k = key();
        bool action = k == 5 && isKeyPressed(KEY_NSPIRE_1);
        wait_no_key_pressed();
        if (k == 1) return;
        if ((k == 6 || k == 3) && page) --page;
        if ((k == 7 || k == 4) && page + 1 < pages) ++page;
        const char *blocked = object_blocked(&live, native_current(), native_current());
        if (action && blocked) {
            message(T("Action refused", "操作被拒绝", "Action refusée"), blocked, 0);
            continue;
        }
        if (action && confirm(&selected)) {
            result = native_action(&selected);
            message(result ? T("Action failed", "操作失败", "Échec") : T("Action complete", "操作完成", "Action terminée"), native_error(result), result);
            return;
        }
    }
}

static void listing(ObjectKind kind) {
    Object *objects = NULL;
    unsigned count = 0, selection = 0;
    bool refresh = true;
    for (;;) {
        if (refresh) {
            Object selected;
            bool had_selection = count != 0;
            if (had_selection) selected = objects[selection];
            int result = native_list(kind, &objects, &count);
            if (result) {
                message(T("Refresh failed", "刷新失败", "Échec d'actualisation"), native_error(result), result);
                free(objects);
                return;
            }
            int found = had_selection ? object_find(objects, count, &selected) : -1;
            if (found >= 0) selection = (unsigned)found;
            else if (selection >= count) selection = count ? count - 1 : 0;
            refresh = false;
        }
        clear(title(kind));
        text(26, "%s: %u   %u/%u", T("Objects", "对象", "Objets"), count, count ? selection + 1 : 0, count);
        unsigned first = selection / VISIBLE_ROWS * VISIBLE_ROWS;
        for (unsigned i = first; i < count && i < first + VISIBLE_ROWS; ++i) {
            const Object *object = objects + i;
            int y = 48 + (int)(i - first) * 38;
            char line[128];
            bool selected = i == selection;
            snprintf(line, sizeof(line), "%.8s @ %08lX%s", object->name,
                (unsigned long)object->address, kind == OBJECT_TASK && object->address == native_current() ? " *" : "");
            ui_draw_row(canvas.gc, theme, y, line, selected);
            if (selected) ui_fill(canvas.gc, 4, y + 18, UI_WIDTH - 8, 18, theme->highlight_bg);
            if (kind == OBJECT_TASK)
                snprintf(line, sizeof(line), "%s | %s %u", status(object->data.task.status), T("Priority", "优先级", "Priorité"), object->data.task.priority);
            else if (kind == OBJECT_POOL)
                snprintf(line, sizeof(line), "%s: %u/%u (%u%%)", T("Free", "可用", "Libre"), object->data.pool.available, object->data.pool.size,
                    object->data.pool.size ? (unsigned)((uint64_t)object->data.pool.available * 100 / object->data.pool.size) : 0);
            else if (kind == OBJECT_EVENT)
                snprintf(line, sizeof(line), "%s: %08X", T("Flags", "标志", "Indicateurs"), object->data.event.flags);
            else
                snprintf(line, sizeof(line), "%u/%u | %s: %u", object->data.queue.available, object->data.queue.size, T("Msgs", "消息", "Msgs"), object->data.queue.messages);
            ui_draw_text(canvas.gc, line, 10, y + 17, Regular9,
                selected ? theme->highlight_fg : theme->muted);
        }
        if (!count) text(70, "%s", T("No objects", "无对象", "Aucun objet"));
        ui_draw_text(canvas.gc, T("↑↓ select  ←→ page", "↑↓ 选择  ←→ 翻页", "↑↓ choisir  ←→ page"),
            8, 200, Regular9, theme->muted);
        ui_draw_footer(canvas.gc, theme,
            T("1/R: refresh  Enter: info  Esc: back", "1/R：刷新  Enter：详情  Esc：返回", "1/R: actu.  Enter: infos  Esc: retour"));
        present();
        unsigned k = key();
        wait_no_key_pressed();
        if (k == 1) break;
        if (k == 3 && selection) --selection;
        if (k == 4 && selection + 1 < count) ++selection;
        if (k == 6) selection = selection >= VISIBLE_ROWS ? selection - VISIBLE_ROWS : 0;
        if (k == 7 && count) selection = selection + VISIBLE_ROWS < count ? selection + VISIBLE_ROWS : count - 1;
        if (k == 5) refresh = true;
        if (k == 2 && count) {
            details(objects[selection]);
            refresh = true;
        }
    }
    free(objects);
}

static void settings(void) {
    for (;;) {
        ndialog_t *dlg = ndlg_create(title(4));
        if (!dlg) return;
        int off = 0;
        ndlg_add_label(dlg, T("Temporary interrupt pause", "临时关闭中断", "Pause temporaire des interruptions"));
        ndlg_add_label(dlg, T("Timers and USB will pause.", "定时器和 USB 将暂停。", "Minuteries et USB en pause."));
        ndlg_add_label(dlg, T("Enter/Esc restores the prior state.", "实体 Enter/Esc 恢复原状态。", "Enter/Esc : rétablir l'état initial."));
        ndlg_add_checkbox(dlg, T("Turn interrupts off", "关闭中断", "Couper les interruptions"), &off);
        if (!ui_show(dlg) || !off) return;
        wait_no_key_pressed();
        clear(title(4));
        text(56, "%s", T("Interrupts: off", "中断：关闭", "Interruptions : coupées"));
        text(106, "%s", T("Enter: restore", "实体 Enter：恢复", "Enter : rétablir"));
        text(140, "%s", T("Esc: restore and back", "Esc：恢复并返回", "Esc : rétablir et revenir"));
        ui_draw_footer(canvas.gc, theme, T("Physical keys only", "仅接受实体按键", "Touches physiques uniquement"));
        present();
        int previous = native_interrupts(-1);
        /* Only physical keys are safe until the exact IRQ/FIQ mask is back. */
        while (!isKeyPressed(KEY_NSPIRE_ENTER) && !isKeyPressed(KEY_NSPIRE_RET) &&
               !isKeyPressed(KEY_NSPIRE_ESC)) {}
        bool back = isKeyPressed(KEY_NSPIRE_ESC);
        native_interrupts(previous);
        wait_no_key_pressed();
        if (back) return;
    }
}

int task_ui(void) {
    ui_language_init();
    if (nl_ndless_rev() < 2023) {
        show_msgbox("TaskManager", T("Ndless r2023 required", "需要 Ndless r2023", "Ndless r2023 requis"));
        return 1;
    }
    const char *error;
    if (!native_open(&error)) {
        message(T("Cannot open", "无法打开", "Ouverture impossible"), error, 0);
        refresh_osscr();
        return 1;
    }
    if (!ui_canvas_open(&canvas)) {
        message(T("Cannot open", "无法打开", "Ouverture impossible"), "Not enough memory", 0);
        refresh_osscr();
        return 1;
    }
    int selected = 0;
    const char *categories[] = {title(0), title(1), title(2), title(3), title(4), title(5)};
    for (;;) {
        selected = ui_menu(&canvas, "TaskManager", categories, 6, selected);
        if (selected < 0) break;
        if (selected < 4) listing((ObjectKind)selected);
        else if (selected == 4) settings();
        else ui_about("TaskManager",
            T("Original: compu / Excale\nIndependent native implementation",
              "原作：compu / Excale\n独立原生实现",
              "Original : compu / Excale\nImplémentation native indépendante"),
            "GPL-3.0-or-later");
    }
    ui_canvas_close(&canvas);
    refresh_osscr();
    return 0;
}
