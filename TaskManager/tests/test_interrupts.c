#include "tasks.h"

#include <libndls.h>
#include <stdio.h>

static unsigned interrupt_mask(void) {
    unsigned value;
    __asm__ volatile("mrs %0, cpsr" : "=r"(value));
    return value & 0xc0u;
}

int main(void) {
    const char *error;
    if (!native_open(&error)) { show_msgbox("TaskManager IRQ test", error); return 1; }
    unsigned original = interrupt_mask();
    unsigned states[] = {0x80u, 0xc0u};
    /* Keep IRQs masked while exercising both FIQ states in SDK context. */
    unsigned failures = 0;
    for (unsigned i = 0; i < sizeof(states) / sizeof(*states); ++i) {
        native_interrupts((int)states[i]);
        int saved = native_interrupts(-1);
        unsigned disabled = interrupt_mask();
        int nested = native_interrupts(-1);
        native_interrupts(nested);
        unsigned nested_restored = interrupt_mask();
        native_interrupts(saved);
        unsigned restored = interrupt_mask();
        if (disabled != 0xc0u || nested_restored != 0xc0u || restored != states[i]) ++failures;
    }
    native_interrupts((int)original);
    unsigned final = interrupt_mask();
    if (final != original) ++failures;
    char result[128];
    snprintf(result, sizeof(result), "%s: nested IRQ/FIQ restoration. Initial %02X, final %02X, failures %u.",
             failures ? "FAIL" : "PASS", original, final, failures);
    show_msgbox("TaskManager IRQ test", result);
    wait_no_key_pressed();
    refresh_osscr();
    return failures ? 1 : 0;
}
