#include "native_path.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *path, const char *cwd, const char *expected, bool owned) {
    char full[NATIVE_PATH_MAX];
    bool usb;
    assert(native_path(path, cwd, full, &usb) == 0);
    assert(!strcmp(full, expected));
    assert(usb == owned);
}

int main(void) {
    check("A:\\documents\\USB", NULL, "A:\\documents\\USB", true);
    check("/DOCUMENTS/uSb//资料/./test.tns", NULL,
          "A:\\DOCUMENTS\\uSb\\资料\\test.tns", true);
    check("test.tns", "\\documents\\USB\\资料\\",
          "A:\\documents\\USB\\资料\\test.tns", true);
    check("A:test.tns", "\\documents\\USB\\",
          "A:\\documents\\USB\\test.tns", true);
    check("A:", "\\documents\\USB\\", "A:\\documents\\USB", true);
    check("..\\..\\MyWidgets\\test.tns", "\\documents\\USB\\docs\\",
          "A:\\documents\\MyWidgets\\test.tns", false);
    check("A:\\documents\\USB\\..\\nusb.tns", NULL,
          "A:\\documents\\nusb.tns", false);
    check("A:\\documents\\USB-backup\\test.tns", NULL,
          "A:\\documents\\USB-backup\\test.tns", false);
    check("..\\USB\\test.tns", "\\documents\\MyWidgets\\",
          "A:\\documents\\USB\\test.tns", true);
    check("/../../documents/USB", NULL, "A:\\documents\\USB", true);
    check("/", NULL, "A:\\", false);

    char full[NATIVE_PATH_MAX], long_path[NATIVE_PATH_MAX + 1];
    bool usb = true;
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    assert(native_path(long_path, "\\documents\\USB", full, &usb) == -1);
    assert(!usb);
    assert(native_path("", "\\documents", full, &usb) == -1);
    assert(native_path("B:\\documents\\USB", NULL, full, &usb) == -1);
    puts("native path tests passed");
    return 0;
}
