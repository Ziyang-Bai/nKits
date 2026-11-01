/* nClock, GPL-3.0-or-later. CX / CX II display controls. */
#include "ui.h"
#include "resident.h"
#include "diagnose.h"

#include <stdint.h>

static unsigned brightness, maximum;
static bool ready, powered_off, cx2;

static bool entry(uintptr_t address, uint32_t first) {
    return *(const volatile uint32_t *)address == first;
}

bool clock_power_open(void) {
    unsigned value;
    ready = false;
    /* Cache SDK model queries here, never in a resident event callback.
       Subtype 1 is CM, not the original CX HDQ backlight. */
    if (hwtype() != 1) return false;
    cx2 = is_cx2;
    if (!cx2 && nl_hwsubtype() != 0) return false;
    if (cx2) {
        maximum = 255;
        /* CX II PWM is write-only. Use the checked OS shadow getter, not
           the original CX HDQ register (only mirrored by Firebird). */
        if (!entry(0x10004b20u, 0xe92d4010u) ||
            *(const volatile uint32_t *)0x10004b24u != 0xe24dd028u ||
            *(const volatile uint32_t *)0x10004b28u != 0xe3a04000u ||
            *(const volatile uint32_t *)0x10004b2cu != 0xe3a03ffbu ||
            !entry(0x10004c58u, 0xe59f301cu) ||
            *(const volatile uint32_t *)0x10004c5cu != 0xe3520000u ||
            *(const volatile uint32_t *)0x10004c60u != 0xe3a02000u ||
            !entry(0x10004bb8u, 0xe59f101cu) ||
            !entry(0x10004be0u, 0xe59f101cu) ||
            *(const volatile uint32_t *)0x10004bdcu != 0x3eau ||
            *(const volatile uint32_t *)0x10004c04u != 0x3e9u) {
            clock_diag("power.entries.rejected", 0);
            return false;
        }
        clock_diag("power.cx2.get.before", 0);
        value = ((unsigned (*)(void))0x10004b20u)();
        clock_diag("power.cx2.get.after", value);
        if (value > 255) return false;
        brightness = 255 - value;
    } else {
        uint32_t manuf[30] = {0};
        unsigned minimum, level;
        /* Logical NAND offsets, excluding page ECC; Firebird flash.cpp's
           manuf_data_804.ext starts at 0x818, light limits at 0x888. */
        clock_diag("power.nand.before", 0x818);
        read_nand(manuf, sizeof(manuf), 0x818, 0, 0, NULL);
        clock_diag("power.nand.after", manuf[0]);
        clock_diag("power.nand.limits", manuf[28]);
        minimum = manuf[28] & 0xffff;
        maximum = manuf[28] >> 16;
        level = manuf[29] & 0xffff;
        if (manuf[0] != 0x4c9e5f91u || minimum >= maximum ||
            maximum == 0xffff || level < minimum || level > maximum) return false;
        /* Original nClock uses a full-word HDQ +0x20 value, including values
           above 255; Firebird's uint8_t HDQ shadow truncates these. */
        clock_diag("power.hdq.read.before", 0);
        value = *(const volatile uint32_t *)0x900f0020u;
        clock_diag("power.hdq.read.after", value);
        if (value > clock_ui_max_brightness()) return false;
        brightness = value;
    }
    powered_off = false;
    ready = true;
    return true;
}

unsigned clock_ui_brightness(void) {
    return brightness;
}

unsigned clock_ui_max_brightness(void) {
    return maximum;
}

void clock_ui_set_brightness(unsigned value) {
    if (!ready) return;
    if (value > clock_ui_max_brightness()) value = clock_ui_max_brightness();
    clock_diag("brightness.before", value);
    if (cx2) {
        /* Firebird core/cx2.cpp: inverse duty, OS period 255, enable 1. */
        ((void (*)(unsigned, unsigned, unsigned))0x10004c58u)(255 - value, 255, 1);
    } else if (!powered_off) {
        *(volatile uint32_t *)0x900f0020u = value;
    }
    brightness = value;
    clock_diag("brightness.after", value);
}

void clock_power_off(void) {
    if (ready && !powered_off) {
        clock_diag("power.off.before", brightness);
        if (cx2) ((void (*)(void))0x10004be0u)();
        else *(volatile uint32_t *)0x900f0020u = 0;
        powered_off = true;
        clock_diag("power.off.after", brightness);
    }
}

void clock_power_on(void) {
    if (ready && powered_off) {
        clock_diag("power.on.before", brightness);
        if (cx2) ((void (*)(void))0x10004bb8u)();
        else *(volatile uint32_t *)0x900f0020u = brightness;
        powered_off = false;
        clock_diag("power.on.after", brightness);
    }
}

void clock_reboot(void) {
#ifdef NCLOCK_DIAGNOSTIC
    clock_diag("reboot.request", ready);
    clock_diag_abort("A reboot was requested. See reboot.request in the diagnostic report.");
#else
    /* SP805 watchdog is shared by CX and CX II (Firebird core/mem.c).
       Mask IRQs so the OS handler cannot clear the first expiry. */
    if (!ready) return;
    resident_lock();
    *(volatile uint32_t *)0x90060c00u = 0x1acce551u;
    *(volatile uint32_t *)0x90060008u = 0;
    *(volatile uint32_t *)0x9006000cu = 1;
    *(volatile uint32_t *)0x90060000u = 1;
    *(volatile uint32_t *)0x90060008u = 3;
    for (;;) __asm__ volatile("nop");
#endif
}
