#include <libndls.h>
#include <stdio.h>
#include "ui.h"

static unsigned irq_mask(void) {
    unsigned cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    return cpsr & 0x80u;
}

static bool run_case(int mask, const char *title) {
    const char *items[] = {"Finish test"};
    Clock clock;
    char result[160];
    int original = TCT_Local_Control_Interrupts(mask);
    unsigned before = irq_mask();
    clock_defaults(&clock);
    if (!clock_ui_open(NULL)) {
        TCT_Local_Control_Interrupts(original);
        show_msgbox("FAIL", "clock_ui_open failed");
        return false;
    }
    int choice = clock_ui_menu(&clock, title, items, 1, true);
    clock_ui_close();
    unsigned after = irq_mask();
    TCT_Local_Control_Interrupts(original);
    bool passed = choice == -1 && before == after;
    snprintf(result, sizeof(result), "Ndless r%lu\nExit %d; IRQ mask: before %02x, after %02x",
             (unsigned long)nl_ndless_rev(), choice, before, after);
    show_msgbox(passed ? "PASS: UI wait and IRQ restore" : "FAIL: UI wait and IRQ restore", result);
    return passed;
}

int main(void) {
    if (!run_case(0, "IRQ-enabled entry: press Esc")) return 1;
    return run_case(-1, "IRQ-masked entry: press Esc") ? 0 : 1;
}
