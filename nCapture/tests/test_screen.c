#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include "../../common/resident.c"

static unsigned interrupt_mask = 0x80u;

unsigned resident_lock(void) {
    unsigned mask = interrupt_mask;
    interrupt_mask = 0xc0u;
    return mask;
}

void resident_unlock(unsigned mask) { interrupt_mask = mask; }

static void *map(uintptr_t address, size_t size) {
    void *p = mmap((void *)address, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(p == (void *)address);
    return p;
}

int main(void) {
    const size_t bytes = 320 * 240 * sizeof(uint16_t);
    uint16_t *logical = map(RAM_START, bytes);
    uint16_t *physical = map(0xa8000000u, bytes);
    volatile uint32_t *lcd = map(0xc0000000u, 4096);
    uint32_t context[2] = {0, RAM_START};
    static uint16_t pixels[320 * 240];
    root = &storage;
    root->platform = &platforms[2];
    root->physical = 0xa8000000u;
    root->display_context = context;
    lcd[4] = root->physical;
    lcd[6] = 6u << 1;

    /* Model a stable OS page while LCD memory still holds a transition frame. */
    for (unsigned y = 0; y < 240; ++y) {
        for (unsigned x = 0; x < 320; ++x) {
            logical[y * 320 + x] = (uint16_t)((x << 5) ^ y);
            physical[y * 320 + x] = 0xf81f;
        }
    }
    for (unsigned portrait = 0; portrait < 2; ++portrait) {
        root->portrait = portrait;
        assert(resident_copy_screen(pixels));
        assert(!memcmp(pixels, logical, bytes));
        assert(interrupt_mask == 0x80u);
    }

    context[1] = RAM_END;
    memset(pixels, 0xab, bytes);
    assert(!resident_copy_screen(pixels));
    for (unsigned i = 0; i < 320 * 240; ++i) assert(pixels[i] == 0xabab);
    assert(interrupt_mask == 0x80u);
    context[1] = RAM_START;

    root->platform = &platforms[0];
    root->portrait = false;
    memcpy(physical, logical, bytes);
    assert(resident_copy_screen(pixels));
    assert(!memcmp(pixels, logical, bytes));
    root->portrait = true;
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
            physical[x * 240 + y] = logical[y * 320 + x];
    assert(resident_copy_screen(pixels));
    assert(!memcmp(pixels, logical, bytes));

    lcd[4] = RAM_START;
    assert(!resident_copy_screen(pixels));
    lcd[4] = root->physical;
    lcd[6] = 3u << 1;
    assert(!resident_copy_screen(pixels));
    assert(interrupt_mask == 0x80u);

    assert(munmap(logical, bytes) == 0);
    assert(munmap(physical, bytes) == 0);
    assert(munmap((void *)lcd, 4096) == 0);
    puts("screen tests passed");
    return 0;
}
