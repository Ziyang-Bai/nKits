#include "tasks.h"

#include <assert.h>
#include <string.h>

static Object task(uintptr_t address, const char *name) {
    Object object;
    memset(&object, 0, sizeof(object));
    object.kind = OBJECT_TASK;
    object.address = address;
    strncpy(object.name, name, TASK_NAME_SIZE);
    object.data.task.entry = 0x10002000;
    object.data.task.stack_base = 0x11000000;
    object.data.task.stack_size = 8192;
    return object;
}

int main(void) {
    Object original = task(0x11001000, "sample");
    Object updated = original;
    updated.data.task.status = 2;
    updated.data.task.scheduled = 900;
    updated.data.task.minimum_stack = 4000;
    assert(object_same(&original, &updated));
    Object list[] = { task(0x11003000, "other"), updated };
    assert(object_find(list, 2, &original) == 1);
    assert(object_find(list, 0, &original) == -1);
    updated.data.task.stack_base += 8192;
    assert(!object_same(&original, &updated));
    updated = original;
    updated.data.task.entry += 4;
    assert(!object_same(&original, &updated));
    updated = original;
    updated.data.task.stack_size *= 2;
    assert(!object_same(&original, &updated));
    updated = original;
    updated.address += 4;
    assert(!object_same(&original, &updated));
    updated = original;
    updated.name[0] = 'x';
    assert(!object_same(&original, &updated));
    assert(object_find(list, 2, &updated) == -1);
    assert(object_blocked(&original, original.address, 7));
    assert(object_blocked(&original, 7, original.address));
    assert(!object_blocked(&original, 7, 8));
    for (unsigned state = 0; state < 11; ++state) {
        original.data.task.status = (uint8_t)state;
        assert(!object_blocked(&original, 7, 8));
        assert(strcmp(task_status(state), "UNKNOWN"));
    }
    for (unsigned state = 11; state <= 13; ++state) {
        original.data.task.status = (uint8_t)state;
        assert(object_blocked(&original, 7, 8));
    }
    assert(!strcmp(task_status(12), "TERMINATED"));
    assert(!strcmp(task_status(255), "UNKNOWN"));

    Object pool;
    memset(&pool, 0, sizeof(pool));
    pool.kind = OBJECT_POOL;
    pool.address = 0x11004000;
    strcpy(pool.name, "fixture");
    pool.data.pool.start = 0x12000000;
    pool.data.pool.size = 8192;
    pool.data.pool.minimum = 32;
    updated = pool;
    updated.data.pool.available = 4096;
    updated.data.pool.waiting = 1;
    assert(object_same(&pool, &updated));
    updated.data.pool.start += 4;
    assert(!object_same(&pool, &updated));

    Object queue;
    memset(&queue, 0, sizeof(queue));
    queue.kind = OBJECT_QUEUE;
    queue.address = 0x11005000;
    queue.data.queue.start = 0x12002000;
    queue.data.queue.size = 128;
    queue.data.queue.message_size = 1;
    updated = queue;
    updated.data.queue.messages = 8;
    updated.data.queue.available = 120;
    assert(object_same(&queue, &updated));
    updated.data.queue.message_size = 2;
    assert(!object_same(&queue, &updated));
    updated = queue;
    updated.kind = OBJECT_EVENT;
    assert(!object_same(&queue, &updated));
    assert(!object_same(NULL, &queue));

    Object event;
    memset(&event, 0, sizeof(event));
    event.kind = OBJECT_EVENT;
    event.address = 0x11006000;
    memcpy(event.name, "12345678", TASK_NAME_SIZE);
    updated = event;
    updated.data.event.flags = 0xffffffffu;
    updated.data.event.waiting = 9;
    assert(object_same(&event, &updated));
    assert(!object_blocked(&event, 1, 2));
    updated.name[7] = 'x';
    assert(!object_same(&event, &updated));
    updated = event;
    updated.address += 4;
    assert(!object_same(&event, &updated));
    return 0;
}
