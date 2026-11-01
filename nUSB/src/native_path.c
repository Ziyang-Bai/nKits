#include "native_path.h"

#include <stddef.h>
#include <string.h>

static bool slash(char c) {
    return c == '/' || c == '\\';
}

static bool same(char a, char b) {
    if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
    if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
    return a == b;
}

static int append(char *out, size_t *length, const char *path) {
    while (*path) {
        while (slash(*path)) ++path;
        const char *start = path;
        while (*path && !slash(*path)) ++path;
        size_t n = (size_t)(path - start);
        if (!n || (n == 1 && start[0] == '.')) continue;
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            while (*length > 3 && out[*length - 1] != '\\') --*length;
            if (*length > 3) --*length;
            out[*length] = '\0';
            continue;
        }
        if (*length + (*length > 3) + n >= NATIVE_PATH_MAX) return -1;
        if (*length > 3) out[(*length)++] = '\\';
        memcpy(out + *length, start, n);
        *length += n;
        out[*length] = '\0';
    }
    return 0;
}

int native_path(const char *path, const char *cwd,
                char result[NATIVE_PATH_MAX], bool *usb) {
    static const char prefix[] = "A:\\documents\\USB";
    size_t length = 3;
    *usb = false;
    if (!path || !*path) return -1;
    if (path[1] == ':') {
        if (!same(path[0], 'A')) return -1;
        path += 2;
    }
    memcpy(result, "A:\\", 4);
    if (!slash(*path)) {
        if (!cwd || append(result, &length, cwd)) return -1;
    }
    if (append(result, &length, path)) return -1;
    size_t i;
    for (i = 0; prefix[i]; ++i) {
        if (!same(result[i], prefix[i])) return 0;
    }
    *usb = !result[i] || result[i] == '\\';
    return 0;
}
