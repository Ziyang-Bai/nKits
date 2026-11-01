#include "input.h"
#include <libndls.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static bool keys[TEST_KEY_COUNT];
bool (isKeyPressed)(const t_key *key) { return keys[key->id]; }

static void expect(InputAction action, uint32_t character) {
    InputEvent result = input_read();
    CHECK(result.action == action);
    CHECK(result.character == character);
}

static void release(void) {
    memset(keys, 0, sizeof(keys));
    expect(INPUT_NONE, 0);
}

static void test_actions(void) {
    static const struct {
        const t_key *key;
        bool control;
        bool shift;
        InputAction action;
    } cases[] = {
        {&KEY_NSPIRE_UP, false, false, INPUT_UP},
        {&KEY_NSPIRE_DOWN, false, false, INPUT_DOWN},
        {&KEY_NSPIRE_UP, false, true, INPUT_PAGE_UP},
        {&KEY_NSPIRE_DOWN, false, true, INPUT_PAGE_DOWN},
        {&KEY_NSPIRE_ENTER, false, false, INPUT_ENTER},
        {&KEY_NSPIRE_RET, false, false, INPUT_ENTER},
        {&KEY_NSPIRE_CLICK, false, false, INPUT_ENTER},
        {&KEY_NSPIRE_ESC, false, false, INPUT_BACK},
        {&KEY_NSPIRE_MENU, false, false, INPUT_MENU},
        {&KEY_NSPIRE_R, true, false, INPUT_REFRESH},
        {&KEY_NSPIRE_LEFT, false, false, INPUT_LEFT},
        {&KEY_NSPIRE_RIGHT, false, false, INPUT_RIGHT},
        {&KEY_NSPIRE_LEFT, true, false, INPUT_HOME},
        {&KEY_NSPIRE_RIGHT, true, false, INPUT_END},
        {&KEY_NSPIRE_HOME, false, false, INPUT_HOME},
        {&KEY_NSPIRE_HOME, true, false, INPUT_HOME},
        {&KEY_NSPIRE_DEL, false, false, INPUT_DELETE},
        {&KEY_NSPIRE_DEL, true, false, INPUT_BACKSPACE},
        {&KEY_NSPIRE_CAT, false, false, INPUT_CATALOG}
    };
    size_t i;
    unsigned held;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        release();
        keys[KEY_NSPIRE_CTRL.id] = cases[i].control;
        keys[KEY_NSPIRE_SHIFT.id] = cases[i].shift;
        keys[cases[i].key->id] = true;
        expect(cases[i].action, 0);
        for (held = 0; held < 1000; ++held) expect(INPUT_NONE, 0);
        release();
    }
}

static void test_characters(void) {
    static const struct {
        const t_key *key;
        uint32_t normal;
        uint32_t shifted;
    } cases[] = {
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
    unsigned held;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        release();
        keys[cases[i].key->id] = true;
        expect(INPUT_CHARACTER, cases[i].normal);
        for (held = 0; held < 1000; ++held) expect(INPUT_NONE, 0);
        keys[KEY_NSPIRE_SHIFT.id] = true;
        expect(INPUT_NONE, 0);
        keys[cases[i].key->id] = false;
        expect(INPUT_NONE, 0);
        keys[cases[i].key->id] = true;
        expect(INPUT_CHARACTER, cases[i].shifted);
        expect(INPUT_NONE, 0);
        release();
    }
}

static void test_edges(void) {
    release();
    keys[KEY_NSPIRE_UP.id] = true;
    expect(INPUT_UP, 0);
    keys[KEY_NSPIRE_ESC.id] = true;
    expect(INPUT_BACK, 0);
    expect(INPUT_NONE, 0);
    release();

    keys[KEY_NSPIRE_R.id] = true;
    expect(INPUT_CHARACTER, 'r');
    keys[KEY_NSPIRE_CTRL.id] = true;
    expect(INPUT_REFRESH, 0);
    expect(INPUT_NONE, 0);
    keys[KEY_NSPIRE_CTRL.id] = false;
    expect(INPUT_NONE, 0);
    release();

    keys[KEY_NSPIRE_CTRL.id] = true;
    keys[KEY_NSPIRE_A.id] = true;
    expect(INPUT_NONE, 0);
    keys[KEY_NSPIRE_CTRL.id] = false;
    expect(INPUT_NONE, 0);
    keys[KEY_NSPIRE_A.id] = false;
    expect(INPUT_NONE, 0);
    keys[KEY_NSPIRE_A.id] = true;
    expect(INPUT_CHARACTER, 'a');
    keys[KEY_NSPIRE_B.id] = true;
    expect(INPUT_CHARACTER, 'b');
    expect(INPUT_NONE, 0);
    keys[KEY_NSPIRE_B.id] = false;
    expect(INPUT_NONE, 0);
    keys[KEY_NSPIRE_B.id] = true;
    expect(INPUT_CHARACTER, 'b');
    release();

    keys[KEY_NSPIRE_CAT.id] = true;
    keys[KEY_NSPIRE_A.id] = true;
    expect(INPUT_CATALOG, 0);
    expect(INPUT_NONE, 0);
    keys[KEY_NSPIRE_CAT.id] = false;
    expect(INPUT_NONE, 0);
    release();

    keys[KEY_NSPIRE_UP.id] = true;
    expect(INPUT_UP, 0);
    keys[KEY_NSPIRE_SHIFT.id] = true;
    expect(INPUT_NONE, 0);
    release();

    keys[KEY_NSPIRE_DEL.id] = true;
    expect(INPUT_DELETE, 0);
    keys[KEY_NSPIRE_CTRL.id] = true;
    expect(INPUT_NONE, 0);
    release();

    keys[KEY_NSPIRE_A.id] = true;
    expect(INPUT_CHARACTER, 'a');
    input_reset();
    expect(INPUT_CHARACTER, 'a');
    release();
    keys[KEY_NSPIRE_LEFT.id] = true;
    expect(INPUT_LEFT, 0);
    input_reset();
    expect(INPUT_LEFT, 0);
    release();
}

int main(void) {
    input_reset();
    expect(INPUT_NONE, 0);
    test_actions();
    test_characters();
    test_edges();
    puts("input tests passed");
    return 0;
}
