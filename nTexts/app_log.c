#include "app_log.h"
#include <nspireio/nspireio.h>
#include <stdarg.h>
#include <stdio.h>

void app_log(const char *tag, const char *fmt, ...) {
    va_list ap;
    char line[256];
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt ? fmt : "", ap);
    va_end(ap);
    if (n < 0) {
        line[0] = 0;
    } else {
        line[sizeof(line) - 1] = 0;
    }

    uart_printf((char *)"[nTexts][%s] %s\n", tag ? tag : "log", line);
}
