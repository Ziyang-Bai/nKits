#ifndef NUSB_NATIVE_PATH_H
#define NUSB_NATIVE_PATH_H

#include <stdbool.h>

#define NATIVE_PATH_MAX 260

/* Canonical A: path; cwd uses the OS's leading-backslash form. */
int native_path(const char *path, const char *cwd,
                char result[NATIVE_PATH_MAX], bool *usb);

#endif
