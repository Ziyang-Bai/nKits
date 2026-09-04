#include "input.h"

#include <libndls.h>

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int keys[3];
    size_t count;
} KeyFrame;

static const KeyFrame *frames;
static size_t frame_count;
static size_t frame_index;

bool isKeyPressed(const t_key *key) {
    size_t i;
    for (i = 0; i < frames[frame_index].count; ++i) {
        if (frames[frame_index].keys[i] == key->id) return true;
    }
    return false;
}

bool any_key_pressed(void) {
    return frames[frame_index].count != 0;
}

void msleep(unsigned milliseconds) {
    (void)milliseconds;
    if (frame_index + 1 < frame_count) ++frame_index;
}

static InputEvent read_frames(const KeyFrame *sequence, size_t count) {
    frames = sequence;
    frame_count = count;
    frame_index = 0;
    input_wait();
    return input_read();
}

static void require_action(const char *name, InputEvent event, InputAction expected) {
    if (event.action != expected) {
        fprintf(stderr, "FAIL %s: action %d, expected %d\n", name,
                (int)event.action, (int)expected);
        exit(1);
    }
}

int main(void) {
    const KeyFrame held_save[] = {
        {{KEY_NSPIRE_CTRL.id}, 1},
        {{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_S.id}, 2}
    };
    const KeyFrame sticky_open[] = {
        {{KEY_NSPIRE_CTRL.id}, 1},
        {{0}, 0},
        {{KEY_NSPIRE_O.id}, 1}
    };
    const KeyFrame save_as[] = {
        {{KEY_NSPIRE_CTRL.id}, 1},
        {{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_SHIFT.id}, 2},
        {{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_S.id}, 3}
    };
    const KeyFrame shifted_letter[] = {
        {{KEY_NSPIRE_SHIFT.id}, 1},
        {{0}, 0},
        {{KEY_NSPIRE_A.id}, 1}
    };
    const KeyFrame shifted_page[] = {
        {{KEY_NSPIRE_SHIFT.id}, 1},
        {{KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_DOWN.id}, 2}
    };
    const KeyFrame caps_toggle[] = {
        {{KEY_NSPIRE_CTRL.id}, 1},
        {{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_SHIFT.id}, 2},
        {{0}, 0}
    };
    const KeyFrame sticky_toggle[] = {
        {{KEY_NSPIRE_CTRL.id}, 1}, {{0}, 0},
        {{KEY_NSPIRE_CTRL.id}, 1}, {{0}, 0},
        {{KEY_NSPIRE_CTRL.id}, 1}, {{0}, 0},
        {{KEY_NSPIRE_CTRL.id}, 1}, {{0}, 0},
        {{KEY_NSPIRE_CTRL.id}, 1}, {{0}, 0}
    };
    const KeyFrame plain_letter[] = {
        {{KEY_NSPIRE_A.id}, 1}
    };
    const KeyFrame caps_shift_letter[] = {
        {{KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_A.id}, 2}
    };
    InputEvent result;

    require_action("held Ctrl+S", read_frames(held_save, 2), INPUT_SAVE);
    require_action("Ctrl+Shift+S", read_frames(save_as, 3), INPUT_SAVE_AS);
    require_action("Shift+Down", read_frames(shifted_page, 2), INPUT_PAGE_DOWN);

    result = read_frames(caps_toggle, 3);
    require_action("Ctrl+Shift enables Caps Lock", result, INPUT_CAPS_LOCK);
    if (result.character != 1) {
        fputs("FAIL Caps Lock did not report enabled\n", stderr);
        return 1;
    }
    result = read_frames(plain_letter, 1);
    require_action("Caps Lock letter", result, INPUT_CHARACTER);
    if (result.character != 'A') {
        fprintf(stderr, "FAIL Caps Lock letter: character %u\n", result.character);
        return 1;
    }
    result = read_frames(caps_shift_letter, 1);
    require_action("Caps Lock with Shift", result, INPUT_CHARACTER);
    if (result.character != 'a') {
        fprintf(stderr, "FAIL Caps Lock with Shift: character %u\n", result.character);
        return 1;
    }
    result = read_frames(caps_toggle, 3);
    require_action("Ctrl+Shift disables Caps Lock", result, INPUT_CAPS_LOCK);
    if (result.character != 0) {
        fputs("FAIL Caps Lock did not report disabled\n", stderr);
        return 1;
    }

    result = read_frames(sticky_toggle, 10);
    require_action("five Ctrl taps enable Sticky Keys", result, INPUT_STICKY_KEYS);
    if (result.character != 1) {
        fputs("FAIL Sticky Keys did not report enabled\n", stderr);
        return 1;
    }
    require_action("sticky Ctrl then O", read_frames(sticky_open, 3), INPUT_OPEN);
    result = read_frames(shifted_letter, 3);
    require_action("sticky Shift then A", result, INPUT_CHARACTER);
    if (result.character != 'A') {
        fprintf(stderr, "FAIL sticky Shift then A: character %u\n", result.character);
        return 1;
    }

    result = read_frames(sticky_toggle, 10);
    require_action("five Ctrl taps disable Sticky Keys", result, INPUT_STICKY_KEYS);
    if (result.character != 0) {
        fputs("FAIL Sticky Keys did not report disabled\n", stderr);
        return 1;
    }
    result = read_frames(sticky_open, 3);
    require_action("released Ctrl is not sticky", result, INPUT_CHARACTER);
    if (result.character != 'o') {
        fprintf(stderr, "FAIL released Ctrl inserted character %u\n", result.character);
        return 1;
    }

    puts("PASS nEdit input: modifiers, Caps Lock, and Sticky Keys");
    return 0;
}
