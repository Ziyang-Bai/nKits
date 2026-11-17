#include "tasks.h"

#include <libndls.h>
#include <stdlib.h>
#include <string.h>

#define RAM_START 0x10000000u
#define RAM_END 0x14000000u
#define CODE_END 0x10500000u
#define TASK_MAGIC 0x5441534bu
#define POOL_MAGIC 0x44594e41u
#define EVENT_MAGIC 0x45564e54u
#define QUEUE_MAGIC 0x51554555u

typedef struct {
    uintptr_t info;
    unsigned (*pointers)(void **, unsigned);
    unsigned (*count)(void);
    int (*remove)(void *);
} ObjectApi;

typedef int (*TaskInfo)(void *, char *, uint8_t *, unsigned *, uint8_t *,
                        uint8_t *, unsigned *, void **, unsigned *, unsigned *);
typedef int (*PoolInfo)(void *, char *, void **, unsigned *, unsigned *,
                        unsigned *, uint8_t *, unsigned *, void **);
typedef int (*EventInfo)(void *, char *, unsigned *, unsigned *, void **);
typedef int (*QueueInfo)(void *, char *, void **, unsigned *, unsigned *,
                         unsigned *, uint8_t *, unsigned *, uint8_t *,
                         unsigned *, void **);

static ObjectApi api[4];
static void *(*current_task)(void);
static uintptr_t owner;

static bool ram_range(uintptr_t address, unsigned size) {
    return !(address & 3u) && address >= RAM_START && address < RAM_END &&
        size <= RAM_END - address;
}

static bool words(const uint32_t *address, const uint32_t *expected, unsigned count) {
    return ram_range((uintptr_t)address, count * 4) &&
        !memcmp(address, expected, count * 4);
}

static bool registry(ObjectKind kind, uintptr_t information, uintptr_t count,
                      uintptr_t pointers, uintptr_t remove) {
    static const uint32_t pointer_code[] = {
        0xe92d4070u, 0xe1a04001u, 0xe1a05000u
    };
    const uint32_t *num = (const uint32_t *)count;
    const uint32_t *ptr = (const uint32_t *)pointers;
    if (!ram_range(count, 24) || !words(ptr, pointer_code, 3) ||
        !ram_range(pointers, 96) || ptr[11] != 0xe5933004u ||
        ptr[21] != 0xe8bd8070u || num[0] != 0xe92d4008u ||
        num[2] != 0xe59f3004u || num[3] != 0xe5930000u ||
        !ram_range(num[5], 4) || !ram_range(ptr[23], 4)) return false;
    api[kind].info = information;
    api[kind].count = (unsigned (*)(void))count;
    api[kind].pointers = (unsigned (*)(void **, unsigned))pointers;
    api[kind].remove = (int (*)(void *))remove;
    return true;
}

static bool discover_objects(void) {
    uintptr_t information[4] = {0}, remove[4] = {0};
    /* These are native Nucleus function bodies, not writable control-block
       guesses. Signatures include the object ID and its field-access ABI. */
    for (uintptr_t address = RAM_START; address < CODE_END - 0x200; address += 4) {
        const uint32_t *p = (const uint32_t *)address;
        ObjectKind kind;
        if (p[0] == 0xe92d45f8u && p[1] == 0xe59d8028u &&
            p[2] == 0xe1a04000u && p[3] == 0xe1a05001u &&
            p[66] == POOL_MAGIC && p[45] == 0xe5942020u) {
            kind = OBJECT_POOL;
        } else if (p[0] == 0xe92d41f0u && p[1] == 0xe59d8018u &&
            p[2] == 0xe1a04000u && p[3] == 0xe1a05001u &&
            p[50] == EVENT_MAGIC && p[38] == 0xe594c018u) {
            kind = OBJECT_EVENT;
        } else if (p[0] == 0xe92d47f0u && p[1] == 0xe59d8028u &&
            p[2] == 0xe1a04000u && p[3] == 0xe1a05001u &&
            p[4] == 0xe1a06002u && p[5] == 0xe1a07003u &&
            p[6] == 0xe59da030u && p[7] == 0xe59d9038u &&
            p[76] == QUEUE_MAGIC && p[52] == 0xe594002cu) {
            kind = OBJECT_QUEUE;
        } else {
            if (p[0] == 0xe92d41f0u && p[1] == 0xe1a06000u &&
                p[2] == 0xe3a05000u && p[4] == 0xe286000cu &&
                p[6] == 0xe5865014u) {
                if (remove[OBJECT_POOL]) return false;
                remove[OBJECT_POOL] = address;
            }
            if (p[0] == 0xe92d40f8u && p[1] == 0xe1a06000u &&
                p[2] == 0xe3a05000u && p[6] == 0xe586500cu &&
                p[17] == 0xe5964020u) {
                if (remove[OBJECT_EVENT]) return false;
                remove[OBJECT_EVENT] = address;
            }
            continue;
        }
        if (information[kind]) return false;
        information[kind] = address;
    }
    static const uint32_t queue_remove_code[] = {
        0xe92d40f8u, 0xe1a06000u, 0xe3a04000u
    };
    /* Pipe and queue deletion share their prefix; QUEU information identifies
       the adjacent queue routine. */
    const uint32_t *queue_remove = (const uint32_t *)(information[OBJECT_QUEUE] - 0x158u);
    if (!ram_range((uintptr_t)queue_remove, 72) ||
        !words(queue_remove, queue_remove_code, 3) ||
        queue_remove[6] != 0xe586400cu || queue_remove[17] != 0xe5965044u)
        return false;
    remove[OBJECT_QUEUE] = (uintptr_t)queue_remove;
    static const unsigned pointer_offsets[] = {0, 0x10c, 0xd0, 0x138};
    for (unsigned kind = OBJECT_POOL; kind <= OBJECT_QUEUE; ++kind) {
        if (!information[kind] || !remove[kind] ||
            !registry((ObjectKind)kind, information[kind], information[kind] - 24,
                information[kind] + pointer_offsets[kind], remove[kind])) return false;
    }
    return true;
}

bool native_open(const char **error) {
    static const uint32_t current_code[] = {
        0xe59f3028u, 0xe5930000u, 0xe3500000u, 0x1a000001u,
        0xe3a00000u, 0xe12fff1eu, 0xe590200cu, 0xe59f1010u,
        0xe1520001u, 0x012fff1eu, 0xe3a00000u, 0xe12fff1eu
    };
    static const uint32_t info_code[] = {
        0xe92d41f0u, 0xe59d801cu, 0xe1a04000u, 0xe1a05001u,
        0xe1a06002u, 0xe1a07003u
    };
    uint32_t marker = *(volatile uint32_t *)0x10000020u;
    *error = "CX 4.5.5.79 / CX II 6.4.0.74 and Ndless r2023 required";
    if (hwtype() != 1 || nl_hwsubtype() == 1 || nl_ndless_rev() < 2023 ||
        (marker != 0x103b18e0u && marker != 0x103b1f80u &&
         marker != 0x10429ec0u && marker != 0x1042aee0u)) return false;

    uintptr_t address = *(volatile uint32_t *)0x10000028u;
    *error = "Unsupported Ndless native dispatcher";
    if (!ram_range(address - 8, 0xf0)) return false;
    const uint32_t *dispatcher = (const uint32_t *)address;
    if (dispatcher[-2] != 0x4e455854u || dispatcher[0] != 0xe92d000fu ||
        dispatcher[26] != 0x159f2070u || dispatcher[27] != 0xe7920100u)
        return false;
    const uint32_t *table = (const uint32_t *)dispatcher[56];
    if (!ram_range((uintptr_t)table, (__SYSCALLS_LAST + 1) * 4)) return false;
    uintptr_t base = table[e_TCC_Terminate_Task];
    const uint32_t *cur = (const uint32_t *)table[e_TCC_Current_Task_Pointer];
    const uint32_t *inf = (const uint32_t *)(base + 0x650);
    *error = "Unsupported Nucleus task routines";
    if (!ram_range(base, 0xa00) || base >= CODE_END ||
        !words(cur, current_code, 12) || cur[13] != TASK_MAGIC ||
        !words(inf, info_code, 6) || inf[66] != TASK_MAGIC ||
        inf[44] != 0xe5d41018u || inf[49] != 0xe5d4201au ||
        inf[52] != 0xe5941040u ||
        *(const uint32_t *)base != 0xe92d47f0u ||
        !registry(OBJECT_TASK, (uintptr_t)inf, base + 0x55c, base + 0x7c0, base))
        return false;
    current_task = (void *(*)(void))cur;
    owner = (uintptr_t)current_task();
    if (!ram_range(owner, 0x98) || *(const uint32_t *)(owner + 12) != TASK_MAGIC)
        return false;
    *error = "Unsupported Nucleus pool/event/queue routines";
    if (!discover_objects()) return false;
    *error = NULL;
    return true;
}

uintptr_t native_current(void) {
    return (uintptr_t)current_task();
}

int native_interrupts(int mask) {
    unsigned previous, next;
    unsigned bits = (unsigned)mask & 0xc0u;
    __asm__ volatile(
        "mrs %0, cpsr\n\t"
        "bic %1, %0, #0xc0\n\t"
        "orr %1, %1, %2\n\t"
        "msr cpsr_c, %1"
        : "=&r"(previous), "=&r"(next) : "r"(bits) : "memory");
    return (int)(previous & 0xc0u);
}

int native_info(ObjectKind kind, uintptr_t address, Object *object) {
    static const unsigned sizes[] = {0x98, 0x44, 0x24, 0x48};
    if ((unsigned)kind > OBJECT_QUEUE || !ram_range(address, sizes[kind]))
        return -1003;
    memset(object, 0, sizeof(*object));
    object->address = address;
    object->kind = kind;
    void *start = NULL, *first = NULL;
    int mask = native_interrupts(-1), result;
    switch (kind) {
    case OBJECT_TASK:
        result = ((TaskInfo)api[kind].info)((void *)address, object->name,
            &object->data.task.status, &object->data.task.scheduled,
            &object->data.task.priority, &object->data.task.preempt,
            &object->data.task.time_slice, &start, &object->data.task.stack_size,
            &object->data.task.minimum_stack);
        object->data.task.stack_base = (uintptr_t)start;
        if (!result) object->data.task.entry = *(const volatile uint32_t *)(address + 0x5c);
        break;
    case OBJECT_POOL:
        result = ((PoolInfo)api[kind].info)((void *)address, object->name,
            &start, &object->data.pool.size, &object->data.pool.minimum,
            &object->data.pool.available, &object->data.pool.suspend,
            &object->data.pool.waiting, &first);
        object->data.pool.start = (uintptr_t)start;
        object->data.pool.first_task = (uintptr_t)first;
        break;
    case OBJECT_EVENT:
        result = ((EventInfo)api[kind].info)((void *)address, object->name,
            &object->data.event.flags, &object->data.event.waiting, &first);
        object->data.event.first_task = (uintptr_t)first;
        break;
    case OBJECT_QUEUE:
        result = ((QueueInfo)api[kind].info)((void *)address, object->name,
            &start, &object->data.queue.size, &object->data.queue.available,
            &object->data.queue.messages, &object->data.queue.message_type,
            &object->data.queue.message_size, &object->data.queue.suspend,
            &object->data.queue.waiting, &first);
        object->data.queue.start = (uintptr_t)start;
        object->data.queue.first_task = (uintptr_t)first;
        break;
    default:
        result = -1003;
    }
    native_interrupts(mask);
    object->name[TASK_NAME_SIZE] = '\0';
    return result;
}

int native_list(ObjectKind kind, Object **objects, unsigned *count) {
    unsigned capacity = api[kind].count() + 8;
    if (capacity < 8 || capacity > 65536) return -1000;
    void **addresses = malloc(capacity * sizeof(*addresses));
    Object *list = malloc(capacity * sizeof(*list));
    if (!addresses || !list) {
        free(addresses);
        free(list);
        return -1001;
    }
    int mask = native_interrupts(-1);
    unsigned found = api[kind].pointers(addresses, capacity);
    int result = found >= capacity ? -1002 : 0;
    for (unsigned i = 0; !result && i < found; ++i)
        result = native_info(kind, (uintptr_t)addresses[i], list + i);
    native_interrupts(mask);
    free(addresses);
    if (result) {
        free(list);
        return result;
    }
    free(*objects);
    *objects = list;
    *count = found;
    return 0;
}

int native_action(const Object *object) {
    Object live;
    /* Keep the identity check and raw Nucleus mutation in one non-preemptible
       interval. The native APIs supply their own scheduler/list protection. */
    int mask = native_interrupts(-1);
    int result = native_info(object->kind, object->address, &live);
    if (!result && !object_same(object, &live)) result = -1003;
    if (!result && object_blocked(&live, owner, native_current())) result = -1004;
    if (!result) result = api[object->kind].remove((void *)object->address);
    native_interrupts(mask);
    return result;
}
