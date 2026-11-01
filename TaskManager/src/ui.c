#include "tasks.h"
#include "version.h"

#include <libndls.h>
#include <nspireio/nspireio.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VISIBLE_ROWS 17u

static const char *const titles[] = {
    "Tasks", "Memory pools", "Event groups", "Queues", "Settings", "About"
};

static void text(int row, const char *format, ...) {
    char line[54];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    for (unsigned i = 0; line[i]; ++i)
        if ((unsigned char)line[i] < 32 || (unsigned char)line[i] > 126)
            line[i] = '?';
    nio_vram_pixel_puts(2, row * 10 + 2, line, NIO_COLOR_WHITE, NIO_COLOR_BLACK);
}

static void clear(const char *title) {
    nio_vram_fill(0, 0, 320, 240, NIO_COLOR_WHITE);
    text(0, "TaskManager - %s", title);
    nio_vram_fill(0, 13, 320, 1, NIO_COLOR_GRAY);
}

static void present(void) {
    nio_vram_draw();
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

static void message(const char *title, const char *body, int result) {
    clear(title);
    text(5, "%s", body);
    if (result) text(7, "Status: %d", result);
    text(21, "ENTER / ESC: back");
    present();
    wait_no_key_pressed();
    while (key() != 1 && !isKeyPressed(KEY_NSPIRE_ENTER) &&
           !isKeyPressed(KEY_NSPIRE_RET) && !isKeyPressed(KEY_NSPIRE_CLICK))
        wait_no_key_pressed();
    wait_no_key_pressed();
}

static bool confirm(const Object *object) {
    clear(object->kind == OBJECT_TASK ? "Terminate task?" : "Delete object?");
    text(3, "Name: %.8s  Address: %08lX", object->name, (unsigned long)object->address);
    text(6, "This may freeze the OS or lose data.");
    text(8, "Only stop objects whose owner you know.");
    text(11, "ENTER: confirm   ESC: cancel");
    present();
    wait_no_key_pressed();
    for (;;) {
        unsigned k = key();
        wait_no_key_pressed();
        if (k == 1) return false;
        if (k == 2) return true;
    }
}

static void detail_fields(const Object *object) {
    text(2, "Name: %.8s", object->name);
    text(3, "Address: %08lX", (unsigned long)object->address);
    switch (object->kind) {
    case OBJECT_TASK:
        text(4, "Status: %u (%s)", object->data.task.status,
             task_status(object->data.task.status));
        text(5, "Scheduled count: %u", object->data.task.scheduled);
        text(6, "Priority: %u", object->data.task.priority);
        text(7, "Preempt: %u (%s)", object->data.task.preempt,
             object->data.task.preempt == 10 ? "Yes" : "No");
        text(8, "Time slice: %u", object->data.task.time_slice);
        text(9, "Stack base: %08lX", (unsigned long)object->data.task.stack_base);
        text(10, "Stack size: %u", object->data.task.stack_size);
        text(11, "Minimum stack: %u", object->data.task.minimum_stack);
        text(12, "Entry: %08lX", (unsigned long)object->data.task.entry);
        text(14, "Current: %s", object->address == native_current() ? "Yes" : "No");
        break;
    case OBJECT_POOL:
        text(4, "Start address: %08lX", (unsigned long)object->data.pool.start);
        text(5, "Pool size: %u", object->data.pool.size);
        text(6, "Min. allocation: %u", object->data.pool.minimum);
        text(7, "Available: %u", object->data.pool.available);
        text(8, "Suspend type: %u", object->data.pool.suspend);
        text(9, "Tasks waiting: %u", object->data.pool.waiting);
        text(10, "First task: %08lX", (unsigned long)object->data.pool.first_task);
        break;
    case OBJECT_EVENT:
        text(4, "Event flags: %08X (%u)", object->data.event.flags,
             object->data.event.flags);
        text(5, "Tasks waiting: %u", object->data.event.waiting);
        text(6, "First task: %08lX", (unsigned long)object->data.event.first_task);
        break;
    case OBJECT_QUEUE:
        text(4, "Start address: %08lX", (unsigned long)object->data.queue.start);
        text(5, "Queue size: %u", object->data.queue.size);
        text(6, "Available: %u", object->data.queue.available);
        text(7, "Messages: %u", object->data.queue.messages);
        text(8, "Message type: %u", object->data.queue.message_type);
        text(9, "Message size: %u", object->data.queue.message_size);
        text(10, "Suspend type: %u", object->data.queue.suspend);
        text(11, "Tasks waiting: %u", object->data.queue.waiting);
        text(12, "First task: %08lX", (unsigned long)object->data.queue.first_task);
        break;
    }
}

static void details(Object selected) {
    for (;;) {
        Object live;
        int result = native_info(selected.kind, selected.address, &live);
        if (result || !object_same(&live, &selected)) {
            message("Object unavailable", native_error(result ? result : -1003), result);
            return;
        }
        clear(titles[selected.kind]);
        detail_fields(&live);
        text(20, "R: refresh  1: %s", selected.kind == OBJECT_TASK ? "terminate" : "delete");
        text(22, "ESC: back");
        present();
        unsigned k = key();
        bool action = k == 5 && isKeyPressed(KEY_NSPIRE_1);
        wait_no_key_pressed();
        if (k == 1) return;
        const char *blocked = object_blocked(&live, native_current(), native_current());
        if (action && blocked) {
            message("Action refused", blocked, 0);
            continue;
        }
        if (action && confirm(&selected)) {
            result = native_action(&selected);
            message(result ? "Action failed" : "Action complete", native_error(result), result);
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
                message("Refresh failed", native_error(result), result);
                free(objects);
                return;
            }
            int found = had_selection ? object_find(objects, count, &selected) : -1;
            if (found >= 0) selection = (unsigned)found;
            else if (selection >= count) selection = count ? count - 1 : 0;
            refresh = false;
        }
        clear(titles[kind]);
        text(2, "%u objects | %u/%u", count, count ? selection + 1 : 0, count);
        text(3, kind == OBJECT_TASK ? "Name     Status              Priority" :
             kind == OBJECT_POOL ? "Name     Free / total bytes" :
             kind == OBJECT_EVENT ? "Name     Event flags" : "Name     Free / total   Messages");
        unsigned first = selection >= VISIBLE_ROWS ? selection - VISIBLE_ROWS + 1 : 0;
        for (unsigned i = first; i < count && i < first + VISIBLE_ROWS; ++i) {
            const Object *object = objects + i;
            char line[54];
            if (kind == OBJECT_TASK)
                snprintf(line, sizeof(line), "%c%-8.8s %-18s %u%s", i == selection ? '>' : ' ',
                    object->name, task_status(object->data.task.status), object->data.task.priority,
                    object->address == native_current() ? " *" : "");
            else if (kind == OBJECT_POOL)
                snprintf(line, sizeof(line), "%c%-8.8s %u/%u (%u%%)", i == selection ? '>' : ' ',
                    object->name, object->data.pool.available, object->data.pool.size,
                    object->data.pool.size ? (unsigned)((uint64_t)object->data.pool.available * 100 / object->data.pool.size) : 0);
            else if (kind == OBJECT_EVENT)
                snprintf(line, sizeof(line), "%c%-8.8s %08X", i == selection ? '>' : ' ',
                    object->name, object->data.event.flags);
            else
                snprintf(line, sizeof(line), "%c%-8.8s %u/%u %u", i == selection ? '>' : ' ',
                    object->name, object->data.queue.available, object->data.queue.size, object->data.queue.messages);
            text(4 + (int)(i - first), "%s", line);
        }
        if (!count) text(7, "No objects");
        text(22, "1/R: refresh ENTER: info ESC: back");
        present();
        unsigned k = key();
        wait_no_key_pressed();
        if (k == 1) break;
        if (k == 3 && selection) --selection;
        if (k == 4 && selection + 1 < count) ++selection;
        if (k == 6) selection = selection > VISIBLE_ROWS ? selection - VISIBLE_ROWS : 0;
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
        clear("Settings");
        text(3, "Interrupts: restored state");
        text(6, "ENTER: turn interrupts off");
        text(8, "While off, timers and USB are paused.");
        text(10, "ENTER / ESC restores the prior state.");
        text(22, "ESC: back");
        present();
        unsigned k = key();
        wait_no_key_pressed();
        if (k == 1) return;
        if (k != 2) continue;
        clear("Settings");
        text(3, "Interrupts: off");
        text(7, "ENTER: restore   ESC: restore and back");
        present();
        int previous = native_interrupts(-1);
        /* SDK sleep, touchpad I/O and OS dialogs need interrupts. Only poll
           the maintained hardware-key API until the original mask is back. */
        while (!isKeyPressed(KEY_NSPIRE_ENTER) && !isKeyPressed(KEY_NSPIRE_RET) &&
               !isKeyPressed(KEY_NSPIRE_ESC)) {}
        bool back = isKeyPressed(KEY_NSPIRE_ESC);
        native_interrupts(previous);
        wait_no_key_pressed();
        if (back) return;
    }
}

int task_ui(void) {
    if (!nio_scrbuf_init()) return 1;
    const char *error;
    if (!native_open(&error)) {
        message("Cannot open", error, 0);
        nio_scrbuf_free();
        refresh_osscr();
        return 1;
    }
    unsigned selected = 0;
    wait_no_key_pressed();
    for (;;) {
        clear("nKits");
        for (unsigned i = 0; i < sizeof(titles) / sizeof(titles[0]); ++i)
            text(4 + (int)i * 2, "%c %s", selected == i ? '>' : ' ', titles[i]);
        text(22, "UP/DOWN: select  ENTER: open  ESC: exit");
        present();
        unsigned k = key();
        wait_no_key_pressed();
        if (k == 1) break;
        if (k == 3 && selected) --selected;
        if (k == 4 && selected < 5) ++selected;
        if (k == 2) {
            if (selected < 4) listing((ObjectKind)selected);
            else if (selected == 4) settings();
            else {
                clear("About");
                text(4, "TaskManager / nKits");
                text(6, "Version: %s", NKITS_VERSION);
                text(8, "Commit: %s", NKITS_COMMIT);
                text(10, "%s", NKITS_STAGE);
                text(13, "Original program: compu / Excale");
                text(15, "Independent native implementation");
                text(22, "ENTER / ESC: back");
                present();
                key();
                wait_no_key_pressed();
            }
        }
    }
    nio_scrbuf_free();
    refresh_osscr();
    return 0;
}
