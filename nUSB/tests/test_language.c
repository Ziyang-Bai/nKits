#define _POSIX_C_SOURCE 200809L
#include "language.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void preference(const char *path, const void *value, size_t length) {
    FILE *file = fopen(path, "wb");
    CHECK(file);
    CHECK(fwrite(value, 1, length, file) == length);
    CHECK(fclose(file) == 0);
}

static void test_persistence(const char *path) {
    char copy[260];
    unsigned i;
    language_init(path);
    CHECK(language_current() == LANGUAGE_ENGLISH);
    for (i = 0; i < LANGUAGE_COUNT; ++i) {
        CHECK(language_select((Language)i));
        CHECK(language_current() == (Language)i);
        language_init(path);
        CHECK(language_current() == (Language)i);
    }
    strcpy(copy, path);
    language_init(copy);
    copy[0] = '\0';
    CHECK(language_select(LANGUAGE_CHINESE));
    language_init(path);
    CHECK(language_current() == LANGUAGE_CHINESE);
    CHECK(!language_select((Language)-1));
    CHECK(language_current() == LANGUAGE_CHINESE);
    CHECK(!language_select(LANGUAGE_COUNT));
    CHECK(language_current() == LANGUAGE_CHINESE);
    language_init(path);
    CHECK(language_current() == LANGUAGE_CHINESE);
}

static void test_input(const char *path) {
    static const struct {
        const char *value;
        size_t length;
        Language language;
    } cases[] = {
        {"en", 2, LANGUAGE_ENGLISH},
        {"zh", 2, LANGUAGE_CHINESE},
        {"fr", 2, LANGUAGE_FRENCH},
        {"zh\n", 3, LANGUAGE_CHINESE},
        {"fr\r\n", 4, LANGUAGE_FRENCH},
        {"", 0, LANGUAGE_ENGLISH},
        {"z", 1, LANGUAGE_ENGLISH},
        {"f", 1, LANGUAGE_ENGLISH},
        {"fr\r", 3, LANGUAGE_ENGLISH},
        {"de\n", 3, LANGUAGE_ENGLISH},
        {"ZH\n", 3, LANGUAGE_ENGLISH},
        {" zh", 3, LANGUAGE_ENGLISH},
        {"zh ", 3, LANGUAGE_ENGLISH},
        {"zh\0", 3, LANGUAGE_ENGLISH},
        {"zh\nfr\n", 6, LANGUAGE_ENGLISH},
        {"fr\n\n", 4, LANGUAGE_ENGLISH},
        {"fr\r\nX", 5, LANGUAGE_ENGLISH},
        {"\xff\xfe", 2, LANGUAGE_ENGLISH}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(language_select(LANGUAGE_FRENCH));
        preference(path, cases[i].value, cases[i].length);
        language_init(path);
        CHECK(language_current() == cases[i].language);
    }
}

static void test_failed_save(const char *directory, const char *path) {
    char missing[300], long_path[261];
    snprintf(missing, sizeof(missing), "%s/missing/settings", directory);
    language_init(missing);
    CHECK(language_current() == LANGUAGE_ENGLISH);
    CHECK(!language_select(LANGUAGE_FRENCH));
    CHECK(language_current() == LANGUAGE_FRENCH);
    language_init(directory);
    CHECK(language_current() == LANGUAGE_ENGLISH);
    CHECK(!language_select(LANGUAGE_CHINESE));
    CHECK(language_current() == LANGUAGE_CHINESE);

    language_init(NULL);
    CHECK(language_current() == LANGUAGE_ENGLISH);
    CHECK(!language_select(LANGUAGE_FRENCH));
    CHECK(language_current() == LANGUAGE_FRENCH);
    language_init("");
    CHECK(language_current() == LANGUAGE_ENGLISH);
    CHECK(!language_select(LANGUAGE_CHINESE));
    CHECK(language_current() == LANGUAGE_CHINESE);

    CHECK(strlen(path) < sizeof(long_path));
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    language_init(path);
    CHECK(language_select(LANGUAGE_CHINESE));
    language_init(long_path);
    CHECK(language_current() == LANGUAGE_ENGLISH);
    CHECK(!language_select(LANGUAGE_FRENCH));
    CHECK(language_current() == LANGUAGE_FRENCH);
    language_init(path);
    CHECK(language_current() == LANGUAGE_CHINESE);
}

int main(void) {
    char directory[] = "/tmp/nusb-language-XXXXXX";
    char path[260];
    CHECK(mkdtemp(directory));
    CHECK(snprintf(path, sizeof(path), "%s/nusb.cfg", directory) < (int)sizeof(path));
    test_persistence(path);
    test_input(path);
    test_failed_save(directory, path);
    CHECK(unlink(path) == 0);
    language_init(path);
    CHECK(language_current() == LANGUAGE_ENGLISH);
    CHECK(rmdir(directory) == 0);
    puts("language tests passed");
    return 0;
}
