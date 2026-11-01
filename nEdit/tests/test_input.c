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

static void require_character(const char *name, InputEvent event, uint32_t expected) {
    require_action(name, event, INPUT_CHARACTER);
    if (event.character != expected) {
        fprintf(stderr, "FAIL %s: character %u, expected %u\n", name,
                (unsigned)event.character, (unsigned)expected);
        exit(1);
    }
}

static void test_character_keys(void) {
    static const struct {
        const t_key *key;
        uint32_t normal;
        uint32_t shifted;
    } cases[] = {
        {&KEY_NSPIRE_0, '0', ')'}, {&KEY_NSPIRE_1, '1', '!'},
        {&KEY_NSPIRE_2, '2', '@'}, {&KEY_NSPIRE_3, '3', '#'},
        {&KEY_NSPIRE_4, '4', '$'}, {&KEY_NSPIRE_5, '5', '%'},
        {&KEY_NSPIRE_6, '6', '^'}, {&KEY_NSPIRE_7, '7', '&'},
        {&KEY_NSPIRE_8, '8', '*'}, {&KEY_NSPIRE_9, '9', '('},
        {&KEY_NSPIRE_SPACE, ' ', ' '}, {&KEY_NSPIRE_PERIOD, '.', '>'},
        {&KEY_NSPIRE_COMMA, ',', '<'}, {&KEY_NSPIRE_PLUS, '+', '+'},
        {&KEY_NSPIRE_MINUS, '-', '_'}, {&KEY_NSPIRE_MULTIPLY, '*', '~'},
        {&KEY_NSPIRE_DIVIDE, '/', '\\'}, {&KEY_NSPIRE_LP, '(', '{'},
        {&KEY_NSPIRE_RP, ')', '}'}, {&KEY_NSPIRE_EQU, '=', '`'},
        {&KEY_NSPIRE_APOSTROPHE, '\'', '"'}, {&KEY_NSPIRE_COLON, ':', ';'},
        {&KEY_NSPIRE_BAR, '|', '\\'}, {&KEY_NSPIRE_QUES, '?', '!'},
        {&KEY_NSPIRE_QUESEXCL, '?', '!'}, {&KEY_NSPIRE_QUOTE, '"', '\''},
        {&KEY_NSPIRE_PI, 0x03c0, 0x03a0}, {&KEY_NSPIRE_EE, 'e', 'E'},
        {&KEY_NSPIRE_NEGATIVE, '-', '_'}, {&KEY_NSPIRE_EXP, '^', '^'},
        {&KEY_NSPIRE_SQU, 0x00b2, 0x221a}, {&KEY_NSPIRE_FRAC, '/', 0x00f7},
        {&KEY_NSPIRE_THETA, 0x03b8, 0x0398}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const KeyFrame normal[] = {{{cases[i].key->id}, 1}};
        const KeyFrame shifted[] = {
            {{KEY_NSPIRE_SHIFT.id, cases[i].key->id}, 2}
        };
        require_character("native key", read_frames(normal, 1), cases[i].normal);
        require_character("shifted native key", read_frames(shifted, 1),
                          cases[i].shifted);
    }
}

static void test_actions(void) {
    const struct {
        KeyFrame frame;
        InputAction action;
    } cases[] = {
        {{{KEY_NSPIRE_CAT.id}, 1}, INPUT_SYMBOLS},
        {{{KEY_NSPIRE_TRIG.id}, 1}, INPUT_SYMBOLS},
        {{{KEY_NSPIRE_VAR.id}, 1}, INPUT_SYMBOLS},
        {{{KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_CAT.id}, 2}, INPUT_SYMBOLS},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_CAT.id}, 2}, INPUT_NONE},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_PI.id}, 2}, INPUT_NONE},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_S.id, KEY_NSPIRE_CAT.id}, 3}, INPUT_SAVE},
        {{{KEY_NSPIRE_DEL.id}, 1}, INPUT_BACKSPACE},
        {{{KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_DEL.id}, 2}, INPUT_BACKSPACE},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_DEL.id}, 2}, INPUT_DELETE},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_DEL.id}, 3}, INPUT_DELETE},
        {{{KEY_NSPIRE_UP.id}, 1}, INPUT_UP},
        {{{KEY_NSPIRE_DOWN.id}, 1}, INPUT_DOWN},
        {{{KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_UP.id}, 2}, INPUT_PAGE_UP},
        {{{KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_DOWN.id}, 2}, INPUT_PAGE_DOWN},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_UP.id}, 2}, INPUT_DOCUMENT_HOME},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_DOWN.id}, 2}, INPUT_DOCUMENT_END},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_UP.id}, 3}, INPUT_DOCUMENT_HOME},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_DOWN.id}, 3}, INPUT_DOCUMENT_END},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_LEFT.id}, 2}, INPUT_HOME},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_RIGHT.id}, 2}, INPUT_END},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_G.id}, 2}, INPUT_GOTO},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_N.id}, 2}, INPUT_NEW},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_O.id}, 2}, INPUT_OPEN},
        {{{KEY_NSPIRE_CTRL.id, KEY_NSPIRE_R.id}, 2}, INPUT_RELOAD},
        {{{KEY_NSPIRE_MENU.id}, 1}, INPUT_MENU},
        {{{KEY_NSPIRE_ESC.id}, 1}, INPUT_EXIT}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        require_action("native action or shortcut", read_frames(&cases[i].frame, 1),
                       cases[i].action);
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
    const KeyFrame plain_pi[] = {{{KEY_NSPIRE_PI.id}, 1}};
    const KeyFrame shifted_pi[] = {
        {{KEY_NSPIRE_SHIFT.id, KEY_NSPIRE_PI.id}, 2}
    };
    const KeyFrame plain_ee[] = {{{KEY_NSPIRE_EE.id}, 1}};
    const KeyFrame sticky_root[] = {
        {{KEY_NSPIRE_SHIFT.id}, 1}, {{0}, 0}, {{KEY_NSPIRE_SQU.id}, 1}
    };
    const KeyFrame plain_square[] = {{{KEY_NSPIRE_SQU.id}, 1}};
    const KeyFrame sticky_delete[] = {
        {{KEY_NSPIRE_CTRL.id}, 1}, {{0}, 0}, {{KEY_NSPIRE_DEL.id}, 1}
    };
    const KeyFrame sticky_document_home[] = {
        {{KEY_NSPIRE_CTRL.id}, 1}, {{0}, 0}, {{KEY_NSPIRE_UP.id}, 1}
    };
    const KeyFrame plain_catalog[] = {{{KEY_NSPIRE_CAT.id}, 1}};
    InputEvent result;

    test_character_keys();
    test_actions();

    require_action("held Ctrl+S", read_frames(held_save, 2), INPUT_SAVE);
    require_action("Ctrl+Shift+S", read_frames(save_as, 3), INPUT_SAVE_AS);
    require_action("Shift+Down", read_frames(shifted_page, 2), INPUT_PAGE_DOWN);

    result = read_frames(caps_toggle, 3);
    require_action("Ctrl+Shift enables Caps Lock", result, INPUT_CAPS_LOCK);
    if (result.character != 1) {
        fputs("FAIL Caps Lock did not report enabled\n", stderr);
        return 1;
    }
    require_character("Caps Lock preserves pi", read_frames(plain_pi, 1), 0x03c0);
    require_character("Caps Lock preserves shifted pi", read_frames(shifted_pi, 1), 0x03a0);
    require_character("Caps Lock on EE", read_frames(plain_ee, 1), 'E');
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
    require_action("sticky Ctrl then Del", read_frames(sticky_delete, 3), INPUT_DELETE);
    require_action("sticky Ctrl then Up", read_frames(sticky_document_home, 3),
                   INPUT_DOCUMENT_HOME);
    require_character("sticky Shift then square", read_frames(sticky_root, 3), 0x221a);
    require_character("sticky Shift consumed", read_frames(plain_square, 1), 0x00b2);
    require_action("Catalog after sticky shortcut", read_frames(plain_catalog, 1),
                   INPUT_SYMBOLS);
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

    puts("PASS nEdit input: native keys, Unicode, shortcuts, Caps Lock, and Sticky Keys");
    return 0;
}
