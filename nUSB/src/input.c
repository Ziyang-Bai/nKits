#include "input.h"

#include <libndls.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const t_key *key;
    uint32_t normal;
    uint32_t shifted;
} CharacterKey;

static const CharacterKey characters[] = {
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

static uint32_t previous;
static bool previous_characters[sizeof(characters) / sizeof(characters[0])];

static InputEvent event(InputAction action, uint32_t character) {
    InputEvent result;
    result.action = action;
    result.character = character;
    return result;
}

void input_reset(void) {
    previous = 0;
    memset(previous_characters, 0, sizeof(previous_characters));
}

InputEvent input_read(void) {
    uint32_t keys = 0, edges;
    bool shift = isKeyPressed(KEY_NSPIRE_SHIFT);
    bool control = isKeyPressed(KEY_NSPIRE_CTRL);
    InputEvent character = event(INPUT_NONE, 0);
    size_t index;
    if (isKeyPressed(KEY_NSPIRE_UP)) keys |= 1u;
    if (isKeyPressed(KEY_NSPIRE_DOWN)) keys |= 2u;
    if (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) ||
        isKeyPressed(KEY_NSPIRE_CLICK)) keys |= 4u;
    if (isKeyPressed(KEY_NSPIRE_ESC)) keys |= 8u;
    if (control && isKeyPressed(KEY_NSPIRE_R)) keys |= 16u;
    if (isKeyPressed(KEY_NSPIRE_MENU)) keys |= 32u;
    if (isKeyPressed(KEY_NSPIRE_LEFT)) keys |= 64u;
    if (isKeyPressed(KEY_NSPIRE_RIGHT)) keys |= 128u;
    if (isKeyPressed(KEY_NSPIRE_HOME)) keys |= 256u;
    if (isKeyPressed(KEY_NSPIRE_DEL)) keys |= 512u;
    if (isKeyPressed(KEY_NSPIRE_CAT)) keys |= 1024u;
    edges = keys & ~previous;
    previous = keys;
    /* Record every key, including characters consumed by a shortcut. */
    for (index = 0; index < sizeof(characters) / sizeof(characters[0]); ++index) {
        bool down = (isKeyPressed)(characters[index].key);
        if (down && !previous_characters[index] && !control &&
            character.action == INPUT_NONE) {
            character = event(INPUT_CHARACTER,
                              shift ? characters[index].shifted : characters[index].normal);
        }
        previous_characters[index] = down;
    }
    if (edges & 8u) return event(INPUT_BACK, 0);
    if (edges & 16u) return event(INPUT_REFRESH, 0);
    if (edges & 32u) return event(INPUT_MENU, 0);
    if (edges & 1u) return event(shift ? INPUT_PAGE_UP : INPUT_UP, 0);
    if (edges & 2u) return event(shift ? INPUT_PAGE_DOWN : INPUT_DOWN, 0);
    if (edges & 4u) return event(INPUT_ENTER, 0);
    if (edges & 64u) return event(control ? INPUT_HOME : INPUT_LEFT, 0);
    if (edges & 128u) return event(control ? INPUT_END : INPUT_RIGHT, 0);
    if (edges & 256u) return event(INPUT_HOME, 0);
    if (edges & 512u) return event(control ? INPUT_BACKSPACE : INPUT_DELETE, 0);
    if (edges & 1024u) return event(INPUT_CATALOG, 0);
    return character;
}
