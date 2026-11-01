#include "tasks.h"

#include <string.h>

bool object_same(const Object *a, const Object *b) {
    if (!a || !b || a->kind != b->kind || a->address != b->address ||
        memcmp(a->name, b->name, TASK_NAME_SIZE)) return false;
    switch (a->kind) {
    case OBJECT_TASK:
        return a->data.task.entry == b->data.task.entry &&
            a->data.task.stack_base == b->data.task.stack_base &&
            a->data.task.stack_size == b->data.task.stack_size;
    case OBJECT_POOL:
        return a->data.pool.start == b->data.pool.start &&
            a->data.pool.size == b->data.pool.size &&
            a->data.pool.minimum == b->data.pool.minimum;
    case OBJECT_QUEUE:
        return a->data.queue.start == b->data.queue.start &&
            a->data.queue.size == b->data.queue.size &&
            a->data.queue.message_type == b->data.queue.message_type &&
            a->data.queue.message_size == b->data.queue.message_size;
    case OBJECT_EVENT:
        return true;
    }
    return false;
}

int object_find(const Object *objects, unsigned count, const Object *selected) {
    for (unsigned i = 0; i < count; ++i)
        if (object_same(objects + i, selected)) return (int)i;
    return -1;
}

const char *task_status(unsigned status) {
    static const char *const names[] = {
        "READY", "PURE_SUSPEND", "SLEEP_SUSPEND", "MAILBOX_SUSPEND",
        "QUEUE_SUSPEND", "PIPE_SUSPEND", "SEMAPHORE_SUSPEND",
        "EVENT_SUSPEND", "PARTITION_SUSPEND", "MEMORY_SUSPEND",
        "DRIVER_SUSPEND", "FINISHED", "TERMINATED"
    };
    return status < sizeof(names) / sizeof(names[0]) ? names[status] : "UNKNOWN";
}

const char *object_blocked(const Object *object, uintptr_t owner, uintptr_t current) {
    if (object->kind == OBJECT_TASK) {
        if (object->address == owner || object->address == current)
            return "Current application task is protected";
        if (object->data.task.status == 11 || object->data.task.status == 12)
            return "Task has already stopped";
        if (object->data.task.status > 12)
            return "Unknown task state; action refused";
    }
    return NULL;
}

const char *native_error(int status) {
    switch (status) {
    case 0: return "Success";
    case -8: return "Invalid event group";
    case -16: return "Invalid memory pool";
    case -19: return "Invalid queue";
    case -25: return "Invalid task";
    case -1000: return "Invalid object count";
    case -1001: return "Not enough memory";
    case -1002: return "List changed; refresh again";
    case -1003: return "Object disappeared or was replaced";
    case -1004: return "Protected object; action refused";
    default: return "Nucleus error";
    }
}
