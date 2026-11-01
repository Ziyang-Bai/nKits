#include "native_namespace.h"
#include "image_disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#define USB "A:\\documents\\USB"
static const char *fixtures = "build/fixtures";
static ImageDisk disk;
static NativeResolved resolved, other;

static void open_image(const char *name) {
    char path[1024];
    CHECK(snprintf(path, sizeof(path), "%s/%s", fixtures, name) < (int)sizeof(path));
    CHECK(image_disk_copy(&disk, path, 512));
    CHECK(fs_mount(&disk.device) == FS_OK);
    native_namespace_reset();
}

static void close_image(void) {
    fs_unmount();
    image_disk_close(&disk);
}

static void create_file(const char *path) {
    FsFile *file;
    CHECK(fs_open(path, FS_OPEN_CREATE | FS_OPEN_WRITE | FS_OPEN_EXCLUSIVE, &file) == FS_OK);
    CHECK(fs_close(file) == FS_OK);
}

static void test_names(void) {
    FsEntry entry, parent;
    char name[256], alias[256], path[4096], native[260];
    unsigned i;
    uint64_t reads;
    CHECK(native_namespace_reserved("~NuSb-PaThS"));
    CHECK(native_namespace_reserved("~d_anything"));
    CHECK(native_namespace_reserved("~f_anything"));
    CHECK(!native_namespace_reserved("~nUSB-paths.txt"));
    CHECK(fs_stat("0:/README.TXT", &entry) == FS_OK);
    CHECK(native_namespace_project(&entry, strlen(USB), false, name) == FS_OK);
    CHECK(!strcmp(name, "README.TXT"));
    CHECK(native_namespace_project(&entry, strlen(USB), true, alias) == FS_OK);
    CHECK(strlen(alias) == 45 && !strncmp(alias, "~F_", 3));
    CHECK(native_namespace_project(&entry, strlen(USB), true, name) == FS_OK);
    CHECK(!strcmp(name, alias));
    CHECK(snprintf(native, sizeof(native), "%s\\%s", USB, alias) < (int)sizeof(native));
    CHECK(native_namespace_resolve(native, true, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/README.TXT") && resolved.entry.size == 12);
    CHECK(snprintf(native, sizeof(native), "%s\\%s-hint", USB, alias) < (int)sizeof(native));
    CHECK(native_namespace_resolve(native, false, &resolved) == FS_NOT_FOUND);
    CHECK(native_namespace_resolve(USB "\\new.txt", true, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/new.txt") && !resolved.entry.size);
    CHECK(native_namespace_resolve(USB "\\~F_missing", true, &resolved) == FS_DENIED);
    CHECK(native_namespace_resolve(USB "\\~nUSB-paths", false, &resolved) == FS_OK);
    CHECK(resolved.synthetic && resolved.portal && resolved.entry.is_directory);
    CHECK(native_namespace_resolve(USB "\\..", false, &resolved) == FS_INVALID_ARGUMENT);

    memcpy(path, "0:/", 3);
    for (i = 0; i < 251; ++i) memcpy(path + 3 + i * 3, "界", 3);
    memcpy(path + 3 + 251 * 3, ".txt", 5);
    CHECK(fs_stat(path, &entry) == FS_OK);
    CHECK(native_namespace_project(&entry, strlen(USB), false, name) == FS_OK);
    CHECK(!strncmp(name, "~F_", 3));
    CHECK(snprintf(native, sizeof(native), "%s\\%s", USB, name) < (int)sizeof(native));
    CHECK(native_namespace_resolve(native, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, path));

    create_file("0:/~f_real.TnS");
    CHECK(fs_stat("0:/~f_real.TnS", &entry) == FS_OK);
    CHECK(native_namespace_project(&entry, strlen(USB), false, name) == FS_OK);
    CHECK(strlen(name) == 49 && !strcmp(name + 45, ".tns"));
    CHECK(snprintf(native, sizeof(native), "%s\\%s", USB, name) < (int)sizeof(native));
    CHECK(native_namespace_resolve(native, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/~f_real.TnS"));
    CHECK(fs_mkdir("0:/~NuSb-paths") == FS_OK);
    CHECK(fs_stat("0:/~NuSb-paths", &entry) == FS_OK);
    CHECK(native_namespace_project(&entry, strlen(USB), false, name) == FS_OK);
    CHECK(!strncmp(name, "~D_", 3));
    CHECK(snprintf(native, sizeof(native), "%s\\%s", USB, name) < (int)sizeof(native));
    CHECK(native_namespace_resolve(native, false, &resolved) == FS_OK);
    CHECK(!resolved.synthetic && !strcmp(resolved.path, "0:/~NuSb-paths"));
    CHECK(snprintf(native, sizeof(native), "%s\\%s-hint", USB, name) < (int)sizeof(native));
    CHECK(native_namespace_resolve(native, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/~NuSb-paths"));

    memset(path, 'q', 240);
    path[240] = '\0';
    memcpy(entry.name, path, 241);
    CHECK(native_namespace_project(&entry, 18, false, name) == FS_OK && !strcmp(name, path));
    CHECK(native_namespace_project(&entry, 19, false, name) == FS_OK && !strncmp(name, "~D_", 3));
    CHECK(native_namespace_project(&entry, 214, true, name) == FS_PATH_TOO_LONG);

    CHECK(fs_stat("0:/", &parent) == FS_OK);
    CHECK(fs_stat("0:/README.TXT", &entry) == FS_OK);
    reads = disk.reads;
    CHECK(native_namespace_portal_path(&entry, &parent.identity, native, sizeof(native)) == FS_OK);
    CHECK(!strcmp(native, USB "\\README.TXT"));
    CHECK(native_namespace_portal_path(&parent, NULL, native, sizeof(native)) == FS_OK);
    CHECK(!strcmp(native, USB) && disk.reads == reads);
}

static void test_portal(void) {
    NativePortal *portal;
    FsEntry entry, parent;
    FsDir *held;
    char name[256], path[260], token[260], target[4096];
    bool end;
    unsigned count = 0;
    uint64_t reads, scan_reads;
    CHECK(fs_mkdir("0:/docs/child") == FS_OK);
    CHECK(fs_opendir("0:/", &held) == FS_OK);
    CHECK(native_portal_open(&portal) == FS_OK);
    while (native_portal_next(portal, &resolved, name, &end) == FS_OK && !end) {
        CHECK(resolved.portal && !resolved.synthetic && resolved.entry.is_directory);
        CHECK(strcmp(resolved.path, "0:/") && !strncmp(name, "~D_", 3));
        ++count;
    }
    CHECK(end && count == 5); /* docs, child, empty, 资料, reserved directory */
    CHECK(native_portal_next(portal, &resolved, name, &end) == FS_OK && end);
    native_portal_close(portal);
    fs_closedir(held);
    CHECK(fs_stat("0:/docs", &entry) == FS_OK);
    reads = disk.reads;
    CHECK(native_namespace_portal_path(&entry, NULL, token, sizeof(token)) == FS_OK);
    CHECK(disk.reads == reads);
    reads = disk.reads;
    CHECK(native_namespace_resolve(token, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/docs") && resolved.portal);
    scan_reads = disk.reads - reads;
    fs_unmount();
    CHECK(fs_mount(&disk.device) == FS_OK);
    reads = disk.reads;
    CHECK(native_namespace_resolve(token, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/docs") && resolved.portal);
    CHECK(disk.reads - reads < scan_reads / 2);
    CHECK(fs_chmod("0:/docs", 0x12) == FS_OK);
    CHECK(native_namespace_resolve(token, false, &resolved) == FS_OK);
    CHECK(resolved.entry.attributes & 0x02);
    CHECK(fs_chmod("0:/docs", 0x10) == FS_OK);
    CHECK(snprintf(path, sizeof(path), "%s-123456789012", token) < (int)sizeof(path));
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/docs"));
    CHECK(snprintf(path, sizeof(path), "%s-1234567890123", token) < (int)sizeof(path));
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_NOT_FOUND);
    CHECK(snprintf(path, sizeof(path), "%s-", token) < (int)sizeof(path));
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_NOT_FOUND);
    CHECK(snprintf(path, sizeof(path), "%s-资料", token) < (int)sizeof(path));
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_NOT_FOUND);
    CHECK(snprintf(path, sizeof(path), "%s\\nested.txt", token) < (int)sizeof(path));
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_OK && resolved.entry.size == 3);
    CHECK(snprintf(path, sizeof(path), "%s\\child", token) < (int)sizeof(path));
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_NOT_FOUND);
    CHECK(fs_stat("0:/docs", &parent) == FS_OK);
    CHECK(fs_stat("0:/docs/nested.txt", &entry) == FS_OK);
    reads = disk.reads;
    CHECK(native_namespace_portal_path(&entry, &parent.identity, path, sizeof(path)) == FS_OK);
    CHECK(disk.reads == reads);
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_OK && resolved.entry.size == 3);
    CHECK(native_namespace_resolve(token, false, &resolved) == FS_OK);
    CHECK(native_namespace_rename_target(&resolved, USB "\\~nUSB-paths\\moved", &other) == FS_OK);
    CHECK(!strcmp(other.path, "0:/moved"));
    strcpy(target, other.path);
    CHECK(native_portal_open(&portal) == FS_OK);
    CHECK(native_portal_next(portal, &other, name, &end) == FS_OK && !end);
    CHECK(fs_rename(resolved.path, target) == FS_OK);
    CHECK(native_portal_next(portal, &other, name, &end) == FS_IO_ERROR);
    native_portal_close(portal);
    CHECK(native_namespace_resolve(token, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/moved"));
    CHECK(native_portal_open(&portal) == FS_OK);
    native_namespace_invalidate();
    CHECK(native_portal_next(portal, &resolved, name, &end) == FS_IO_ERROR);
    native_portal_close(portal);
    CHECK(native_namespace_resolve(token, false, &resolved) == FS_NOT_FOUND);
}

static void test_retirement(void) {
    FsEntry entry;
    NativeRetirement retirement;
    char name[256], after[256], path[260];
    uint64_t before;
    create_file("0:/retire.tns");
    CHECK(fs_stat("0:/retire.tns", &entry) == FS_OK);
    CHECK(native_namespace_project(&entry, strlen(USB), true, name) == FS_OK);
    CHECK(native_namespace_prepare(&entry, &retirement) == FS_OK);
    native_namespace_finish(&retirement, FS_DENIED);
    CHECK(native_namespace_project(&entry, strlen(USB), true, after) == FS_OK);
    CHECK(!strcmp(name, after));
    CHECK(native_namespace_prepare(&entry, &retirement) == FS_OK);
    CHECK(fs_unlink("0:/retire.tns") == FS_OK);
    native_namespace_finish(&retirement, FS_OK);
    create_file("0:/retire.tns");
    CHECK(snprintf(path, sizeof(path), "%s\\%s", USB, name) < (int)sizeof(path));
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_NOT_FOUND);
    CHECK(fs_stat("0:/retire.tns", &entry) == FS_OK);
    CHECK(native_namespace_project(&entry, strlen(USB), true, name) == FS_OK);
    CHECK(native_namespace_prepare(&entry, &retirement) == FS_OK);
    CHECK(fs_rename("0:/retire.tns", "0:/renamed.tns") == FS_OK);
    native_namespace_finish(&retirement, FS_OK);
    CHECK(snprintf(path, sizeof(path), "%s\\%s", USB, name) < (int)sizeof(path));
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_NOT_FOUND);
    CHECK(fs_stat("0:/renamed.tns", &entry) == FS_OK);
    CHECK(native_namespace_prepare(&entry, &retirement) == FS_OK);
    before = native_namespace_epoch();
    native_namespace_finish(&retirement, FS_IO_ERROR);
    CHECK(native_namespace_epoch() != before);
}

static void test_table_limit(void) {
    FsEntry entry;
    NativeRetirement retirement;
    char path[64];
    unsigned i;
    uint64_t before;
    native_namespace_reset();
    CHECK(fs_mkdir("0:/slots") == FS_OK);
    for (i = 0; i < 2049; ++i) {
        snprintf(path, sizeof(path), "0:/slots/f%04u", i);
        create_file(path);
    }
    before = native_namespace_epoch();
    for (i = 0; i < 2048; ++i) {
        snprintf(path, sizeof(path), "0:/slots/f%04u", i);
        CHECK(fs_stat(path, &entry) == FS_OK);
        CHECK(native_namespace_prepare(&entry, &retirement) == FS_OK);
        CHECK(fs_unlink(path) == FS_OK);
        native_namespace_finish(&retirement, FS_OK);
    }
    CHECK(native_namespace_epoch() == before);
    CHECK(fs_stat("0:/slots/f2048", &entry) == FS_OK);
    CHECK(native_namespace_prepare(&entry, &retirement) == FS_NO_MEMORY);
    CHECK(native_namespace_epoch() == before);
    CHECK(fs_stat("0:/slots/f2048", &entry) == FS_OK);
    CHECK(native_namespace_prepare(&entry, &retirement) == FS_NO_MEMORY);
    CHECK(native_namespace_epoch() == before);
    native_namespace_reset();
    CHECK(native_namespace_epoch() != before);
    CHECK(native_namespace_prepare(&entry, &retirement) == FS_OK);
    native_namespace_finish(&retirement, FS_DENIED);
}

static void test_path_limit(void) {
    NativePortal *portal;
    FsResult status;
    char name[256];
    bool end;
    open_image("fat32-sfd.img");
    CHECK(native_portal_open(&portal) == FS_OK);
    do {
        status = native_portal_next(portal, &resolved, name, &end);
    } while (status == FS_OK && !end);
    CHECK(status == FS_PATH_TOO_LONG);
    CHECK(native_portal_next(portal, &resolved, name, &end) == FS_PATH_TOO_LONG);
    native_portal_close(portal);
    close_image();
}

static void test_long_portal(void) {
    FsEntry entry, parent;
    char path[4096] = "0:/", name[241], native[260];
    size_t length;
    unsigned i;
    memset(name, 'd', 240);
    name[240] = '\0';
    for (i = 0; i < 16; ++i) {
        length = strlen(path);
        if (length > 3) path[length++] = '/';
        memcpy(path + length, name, sizeof(name));
        CHECK(fs_mkdir(path) == FS_OK);
    }
    length = strlen(path);
    CHECK(length == 3858);
    path[length++] = '/';
    memset(path + length, 'e', 230);
    path[length + 230] = '\0';
    CHECK(fs_mkdir(path) == FS_OK);
    CHECK(fs_stat(path, &parent) == FS_OK);
    length = strlen(path);
    memcpy(path + length, "/x.tns", 7);
    CHECK(strlen(path) == 4095);
    create_file(path);
    CHECK(fs_stat(path, &entry) == FS_OK);
    CHECK(native_namespace_portal_path(&entry, &parent.identity, native, sizeof(native)) == FS_OK);
    CHECK(strlen(native) < 260);
    CHECK(native_namespace_resolve(native, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, path));
}

static void test_portal_capacity(void) {
    FsEntry entry;
    char path[64], token[260];
    unsigned i;
    CHECK(fs_mkdir("0:/many") == FS_OK);
    for (i = 0; i < 300; ++i) {
        snprintf(path, sizeof(path), "0:/many/d%03u", i);
        CHECK(fs_mkdir(path) == FS_OK);
    }
    CHECK(fs_stat("0:/many/d299", &entry) == FS_OK);
    CHECK(native_namespace_portal_path(&entry, NULL, token, sizeof(token)) == FS_OK);
    CHECK(native_namespace_resolve(token, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/many/d299"));
    CHECK(fs_rename("0:/many/d299", "0:/many/last") == FS_OK);
    CHECK(native_namespace_resolve(token, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/many/last"));
    CHECK(fs_unlink("0:/many/last") == FS_OK);
    for (i = 0; i < 299; ++i) {
        snprintf(path, sizeof(path), "0:/many/d%03u", i);
        CHECK(fs_unlink(path) == FS_OK);
    }
    CHECK(fs_unlink("0:/many") == FS_OK);
}

static void test_directory_graph(void) {
    NativePortal *portal;
    FsEntry entry;
    FsResult status;
    char name[256], path[260];
    bool end;
    open_image("fat32-namespace-cycle.img");
    CHECK(native_portal_open(&portal) == FS_OK);
    do {
        status = native_portal_next(portal, &resolved, name, &end);
    } while (status == FS_OK && !end);
    CHECK(status == FS_IO_ERROR);
    native_portal_close(portal);
    close_image();
    open_image("fat32-namespace-duplicate.img");
    CHECK(fs_stat("0:/empty", &entry) == FS_OK);
    CHECK(native_namespace_portal_path(&entry, NULL, path, sizeof(path)) == FS_OK);
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_OK);
    CHECK(!strcmp(resolved.path, "0:/empty"));
    CHECK(fs_stat("0:/docs", &entry) == FS_OK);
    CHECK(native_namespace_portal_path(&entry, NULL, path, sizeof(path)) == FS_OK);
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_IO_ERROR);
    CHECK(strlen(path) + 5 < sizeof(path));
    strcat(path, "-hint");
    CHECK(native_namespace_resolve(path, false, &resolved) == FS_IO_ERROR);
    close_image();
}

int main(int argc, char **argv) {
    static const char *images[] = {"fat32-namespace.img", "exfat-namespace.img"};
    unsigned i;
    if (argc > 1) fixtures = argv[1];
    for (i = 0; i < sizeof(images) / sizeof(images[0]); ++i) {
        open_image(images[i]);
        test_names();
        test_portal();
        test_retirement();
        test_portal_capacity();
        test_long_portal();
        test_table_limit();
        close_image();
    }
    test_path_limit();
    test_directory_graph();
    puts("Native namespace tests passed");
    return 0;
}
