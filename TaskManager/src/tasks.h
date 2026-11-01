#ifndef TASKMANAGER_TASKS_H
#define TASKMANAGER_TASKS_H

#include <stdbool.h>
#include <stdint.h>

#define TASK_NAME_SIZE 8u
#define TASKMANAGER_ID 0x6e544153u

typedef enum { OBJECT_TASK, OBJECT_POOL, OBJECT_EVENT, OBJECT_QUEUE } ObjectKind;

typedef struct {
    uintptr_t address;
    char name[TASK_NAME_SIZE + 1];
    ObjectKind kind;
    union {
        struct {
            uintptr_t entry, stack_base;
            uint8_t status, priority, preempt;
            unsigned scheduled, time_slice, stack_size, minimum_stack;
        } task;
        struct {
            uintptr_t start, first_task;
            unsigned size, minimum, available, waiting;
            uint8_t suspend;
        } pool;
        struct {
            uintptr_t first_task;
            unsigned flags, waiting;
        } event;
        struct {
            uintptr_t start, first_task;
            unsigned size, available, messages, message_size, waiting;
            uint8_t message_type, suspend;
        } queue;
    } data;
} Object;

bool object_same(const Object *a, const Object *b);
int object_find(const Object *objects, unsigned count, const Object *selected);
const char *task_status(unsigned status);
const char *object_blocked(const Object *object, uintptr_t owner, uintptr_t current);
const char *native_error(int status);

bool native_open(const char **error);
int native_list(ObjectKind kind, Object **objects, unsigned *count);
int native_info(ObjectKind kind, uintptr_t address, Object *object);
uintptr_t native_current(void);
int native_action(const Object *object);
/* Set the independent IRQ/FIQ mask bits and return their previous values. */
int native_interrupts(int mask);
int task_ui(void);

#endif
