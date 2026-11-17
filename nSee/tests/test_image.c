#include "image.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char root[1024];

static void path(char *out, size_t size, const char *name) {
    int n = snprintf(out, size, "%s/%s", root, name);
    assert(n > 0 && (size_t)n < size);
}

static unsigned read32(FILE *file) {
    unsigned char p[4];
    assert(fread(p, 1, 4, file) == 4);
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static void rejected(const char *name) {
    Image image = {0};
    const char *error = NULL;
    assert(!image_load(&image, name, &error));
    assert(error && *error);
    assert(!image.pixels && !image.width && !image.height);
    image_free(&image);
}

static void accepted(const char *name) {
    Image image = {0};
    const char *error = "unchanged";
    assert(image_load(&image, name, &error));
    assert(!error && image.pixels);
    char expected[1200];
    int n = snprintf(expected, sizeof(expected), "%s.rgb565", name);
    assert(n > 0 && (size_t)n < sizeof(expected));
    FILE *file = fopen(expected, "rb");
    assert(file);
    unsigned width = read32(file), height = read32(file);
    assert((unsigned)image.width == width && (unsigned)image.height == height);
    for (size_t i = 0; i < (size_t)width * height; ++i) {
        unsigned char p[2];
        assert(fread(p, 1, 2, file) == 2);
        assert(image.pixels[i] == (unsigned)(p[0] | p[1] << 8));
    }
    assert(fgetc(file) == EOF);
    fclose(file);
    image_free(&image);
    assert(!image.pixels && !image.width && !image.height);
    image_free(&image);
}

static void truncated(const char *name) {
    FILE *file = fopen(name, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0 && !fseek(file, 0, SEEK_SET));
    unsigned char *data = malloc((size_t)length);
    assert(data && fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    char temporary[1200];
    path(temporary, sizeof(temporary), "truncated.tmp");
    for (size_t i = 0; i < (size_t)length; ++i) {
        file = fopen(temporary, "wb");
        assert(file && fwrite(data, 1, i, file) == i);
        assert(!fclose(file));
        rejected(temporary);
    }
    assert(!remove(temporary));
    free(data);
}

int main(int argc, char **argv) {
    assert(argc == 2 && strlen(argv[1]) < sizeof(root));
    strcpy(root, argv[1]);
    char filename[1200];
    path(filename, sizeof(filename), "cases.txt");
    FILE *cases = fopen(filename, "r");
    assert(cases);
    char kind[8], name[256];
    unsigned count = 0;
    while (fscanf(cases, "%7s %255s", kind, name) == 2) {
        path(filename, sizeof(filename), name);
        if (!strcmp(kind, "ok")) {
            accepted(filename);
            truncated(filename);
        } else {
            assert(!strcmp(kind, "bad"));
            for (int i = 0; i < 4; ++i) rejected(filename);
        }
        ++count;
    }
    assert(feof(cases));
    fclose(cases);
    path(filename, sizeof(filename), "does-not-exist.bin");
    rejected(filename);
    printf("image tests passed (%u cases)\n", count);
    return 0;
}
