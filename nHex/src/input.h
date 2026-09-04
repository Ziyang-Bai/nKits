#ifndef NHEX_INPUT_H
#define NHEX_INPUT_H

#include <stdint.h>

typedef enum {
    INPUT_NONE,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_PAGE_UP,
    INPUT_PAGE_DOWN,
    INPUT_HOME,
    INPUT_END,
    INPUT_TOGGLE_MODE,
    INPUT_GOTO,
    INPUT_NEW,
    INPUT_EXTEND,
    INPUT_OPEN,
    INPUT_SAVE,
    INPUT_SAVE_AS,
    INPUT_RELOAD,
    INPUT_MENU,
    INPUT_EXIT,
    INPUT_CHARACTER
} InputAction;

typedef struct {
    InputAction action;
    uint8_t character;
} InputEvent;

InputEvent input_read(void);
void input_wait(void);

#endif
