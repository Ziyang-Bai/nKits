#include <ngc.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>

int host_rename_calls;
int host_rename_errno = ENOSYS;
int host_char_width = 8;
const char *host_documents_dir = ".";

int rename(const char *old_path, const char *new_path) {
    (void)old_path;
    (void)new_path;
    ++host_rename_calls;
    errno = host_rename_errno;
    return -1;
}
void app_log(const char *tag, const char *fmt, ...) {
    (void)tag;
    (void)fmt;
}

int gui_gc_getCharWidth(Gc gc, gui_gc_Font font, short value) {
    (void)gc;
    (void)font;
    if (host_char_width == -2)
        return value == 0x4e00 ? 200 : value == 0x4f00 ? 80 : 8;
    return host_char_width;
}

const char *get_documents_dir(void) {
    return host_documents_dir;
}
