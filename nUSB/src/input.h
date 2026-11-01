#ifndef NUSB_INPUT_H
#define NUSB_INPUT_H

#include <stdint.h>

typedef enum {
    INPUT_NONE, INPUT_UP, INPUT_DOWN, INPUT_PAGE_UP, INPUT_PAGE_DOWN,
    INPUT_ENTER, INPUT_BACK, INPUT_REFRESH, INPUT_MENU,
    INPUT_LEFT, INPUT_RIGHT, INPUT_HOME, INPUT_END,
    INPUT_DELETE, INPUT_BACKSPACE, INPUT_CATALOG, INPUT_CHARACTER
} InputAction;

typedef struct {
    InputAction action;
    uint32_t character;
} InputEvent;

void input_reset(void);
/* Poll once; only INPUT_CHARACTER carries a Unicode scalar. Held keys do not repeat. */
InputEvent input_read(void);

#endif
