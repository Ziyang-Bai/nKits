#include "input.h"

#include <libndls.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const t_key *key;
    uint8_t normal;
    uint8_t shifted;
} CharacterKey;
static bool control_latched;
static bool shift_latched;
static bool pending_ready;
static InputEvent pending_event;

static bool pressed(const t_key *key) {
    return (isKeyPressed)(key);
}

static InputEvent event(InputAction action, uint8_t character) {
    InputEvent result;
    result.action = action;
    result.character = character;
    return result;
}

static InputEvent read_character(bool shift) {
    static const CharacterKey keys[] = {
        {&KEY_NSPIRE_A, 'a', 'A'}, {&KEY_NSPIRE_B, 'b', 'B'},
        {&KEY_NSPIRE_C, 'c', 'C'}, {&KEY_NSPIRE_D, 'd', 'D'},
        {&KEY_NSPIRE_E, 'e', 'E'}, {&KEY_NSPIRE_F, 'f', 'F'},
        {&KEY_NSPIRE_G, 'g', 'G'}, {&KEY_NSPIRE_H, 'h', 'H'},
        {&KEY_NSPIRE_I, 'i', 'I'}, {&KEY_NSPIRE_J, 'j', 'J'},
        {&KEY_NSPIRE_K, 'k', 'K'}, {&KEY_NSPIRE_L, 'l', 'L'},
        {&KEY_NSPIRE_M, 'm', 'M'}, {&KEY_NSPIRE_N, 'n', 'N'},
        {&KEY_NSPIRE_O, 'o', 'O'}, {&KEY_NSPIRE_P, 'p', 'P'},
        {&KEY_NSPIRE_Q, 'q', 'Q'}, {&KEY_NSPIRE_R, 'r', 'R'},
        {&KEY_NSPIRE_S, 's', 'S'}, {&KEY_NSPIRE_T, 't', 'T'},
        {&KEY_NSPIRE_U, 'u', 'U'}, {&KEY_NSPIRE_V, 'v', 'V'},
        {&KEY_NSPIRE_W, 'w', 'W'}, {&KEY_NSPIRE_X, 'x', 'X'},
        {&KEY_NSPIRE_Y, 'y', 'Y'}, {&KEY_NSPIRE_Z, 'z', 'Z'},
        {&KEY_NSPIRE_0, '0', ')'}, {&KEY_NSPIRE_1, '1', '!'},
        {&KEY_NSPIRE_2, '2', '@'}, {&KEY_NSPIRE_3, '3', '#'},
        {&KEY_NSPIRE_4, '4', '$'}, {&KEY_NSPIRE_5, '5', '%'},
        {&KEY_NSPIRE_6, '6', '^'}, {&KEY_NSPIRE_7, '7', '&'},
        {&KEY_NSPIRE_8, '8', '*'}, {&KEY_NSPIRE_9, '9', '('},
        {&KEY_NSPIRE_SPACE, ' ', ' '}, {&KEY_NSPIRE_PERIOD, '.', '>'},
        {&KEY_NSPIRE_COMMA, ',', '<'}, {&KEY_NSPIRE_PLUS, '+', '+'},
        {&KEY_NSPIRE_MINUS, '-', '_'}, {&KEY_NSPIRE_MULTIPLY, '*', '~'},
        {&KEY_NSPIRE_DIVIDE, '/', '\\'}, {&KEY_NSPIRE_LP, '[', '{'},
        {&KEY_NSPIRE_RP, ']', '}'}, {&KEY_NSPIRE_EQU, '=', '`'},
        {&KEY_NSPIRE_APOSTROPHE, '\'', '"'}, {&KEY_NSPIRE_COLON, ':', ';'},
        {&KEY_NSPIRE_BAR, '|', '\\'}, {&KEY_NSPIRE_QUES, '?', '!'}
    };
    size_t i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (pressed(keys[i].key)) {
            return event(INPUT_CHARACTER, shift ? keys[i].shifted : keys[i].normal);
        }
    }
    return event(INPUT_NONE, 0);
}

static InputEvent read_unmodified(bool shift) {
    if (pressed(&KEY_NSPIRE_LEFT)) return event(INPUT_LEFT, 0);
    if (pressed(&KEY_NSPIRE_RIGHT)) return event(INPUT_RIGHT, 0);
    if (pressed(&KEY_NSPIRE_UP)) return event(INPUT_UP, 0);
    if (pressed(&KEY_NSPIRE_DOWN)) return event(INPUT_DOWN, 0);
    if (pressed(&KEY_NSPIRE_HOME)) return event(INPUT_HOME, 0);
    if (pressed(&KEY_NSPIRE_TAB)) return event(INPUT_TOGGLE_MODE, 0);
    if (pressed(&KEY_NSPIRE_MENU)) return event(INPUT_MENU, 0);
    if (pressed(&KEY_NSPIRE_ESC)) return event(INPUT_EXIT, 0);
    return read_character(shift);
}

static InputEvent complete_event(InputEvent result, bool *complete) {
    control_latched = false;
    shift_latched = false;
    *complete = true;
    return result;
}

static InputEvent scan_input(bool *complete) {
    bool control_now = pressed(&KEY_NSPIRE_CTRL);
    bool shift_now = pressed(&KEY_NSPIRE_SHIFT);
    bool control;
    bool shift;
    InputEvent plain;

    *complete = false;
    control_latched = control_latched || control_now;
    shift_latched = shift_latched || shift_now;
    control = control_latched;
    shift = shift_latched;

    if (control) {
        if (pressed(&KEY_NSPIRE_G)) return complete_event(event(INPUT_GOTO, 0), complete);
        if (pressed(&KEY_NSPIRE_N)) return complete_event(event(INPUT_NEW, 0), complete);
        if (pressed(&KEY_NSPIRE_E)) return complete_event(event(INPUT_EXTEND, 0), complete);
        if (pressed(&KEY_NSPIRE_O)) return complete_event(event(INPUT_OPEN, 0), complete);
        if (pressed(&KEY_NSPIRE_S)) {
            return complete_event(event(shift ? INPUT_SAVE_AS : INPUT_SAVE, 0), complete);
        }
        if (pressed(&KEY_NSPIRE_R)) return complete_event(event(INPUT_RELOAD, 0), complete);
        if (pressed(&KEY_NSPIRE_LEFT)) return complete_event(event(INPUT_HOME, 0), complete);
        if (pressed(&KEY_NSPIRE_RIGHT)) return complete_event(event(INPUT_END, 0), complete);

        plain = read_unmodified(shift);
        if (plain.action != INPUT_NONE) {
            return complete_event(event(INPUT_NONE, 0), complete);
        }
        return event(INPUT_NONE, 0);
    }

    if (shift && pressed(&KEY_NSPIRE_UP)) {
        return complete_event(event(INPUT_PAGE_UP, 0), complete);
    }
    if (shift && pressed(&KEY_NSPIRE_DOWN)) {
        return complete_event(event(INPUT_PAGE_DOWN, 0), complete);
    }

    plain = read_unmodified(shift);
    if (plain.action != INPUT_NONE) {
        return complete_event(plain, complete);
    }
    return event(INPUT_NONE, 0);
}

InputEvent input_read(void) {
    InputEvent result;
    if (!pending_ready) input_wait();
    result = pending_event;
    pending_ready = false;
    return result;
}

void input_wait(void) {
    while (!pending_ready) {
        bool complete;
        InputEvent result;
        while (!any_key_pressed()) msleep(10);
        result = scan_input(&complete);
        if (complete) {
            pending_event = result;
            pending_ready = true;
        } else {
            msleep(10);
        }
    }
}
