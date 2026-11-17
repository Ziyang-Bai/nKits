#ifndef NCAPTURE_TEST_LIBNDLS_H
#define NCAPTURE_TEST_LIBNDLS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int row, tpad_row;
    unsigned col, tpad_col, tpad_arrow;
} t_key;

enum { TPAD_ARROW_NONE };
enum { SCR_240x320_565 };
enum { e_NU_Get_First, e_get_event, e__show_msgbox_2b, __SYSCALLS_LAST };

struct s_ns_event {
    unsigned timestamp, type, ascii, key, modifiers;
};

typedef struct { void *unknown6; } DSTAT;

unsigned hwtype(void);
unsigned nl_hwsubtype(void);
unsigned nl_ndless_rev(void);
void nl_set_resident(void);
bool _is_touchpad(void);
#define is_touchpad _is_touchpad()
int lcd_type(void);
int NU_Get_First(DSTAT *stat, const char *path);
void NU_Done(DSTAT *stat);

#endif
