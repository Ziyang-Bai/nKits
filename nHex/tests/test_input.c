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
    const KeyFrame sticky_extend[] = {
        {{KEY_NSPIRE_CTRL.id}, 1},
        {{0}, 0},
        {{KEY_NSPIRE_E.id}, 1}
    };
    const KeyFrame shifted_page[] = {
        {{KEY_NSPIRE_SHIFT.id}, 1},
        {{KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_DOWN.id}, 2}
    };

    require_action("held Ctrl+S", read_frames(held_save, 2), INPUT_SAVE);
    require_action("sticky Ctrl then O", read_frames(sticky_open, 3), INPUT_OPEN);
    require_action("Ctrl+Shift+S", read_frames(save_as, 3), INPUT_SAVE_AS);
    require_action("sticky Ctrl then E", read_frames(sticky_extend, 3), INPUT_EXTEND);
    require_action("Shift+Down", read_frames(shifted_page, 2), INPUT_PAGE_DOWN);

    puts("PASS nHex input: held and sequential modifiers");
    return 0;
}
