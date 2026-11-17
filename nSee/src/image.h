#ifndef NSEE_IMAGE_H
#define NSEE_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t *pixels;
    int width, height;
} Image;

/* Start with a zeroed Image. Failure leaves it zeroed; error is static text. */
bool image_load(Image *image, const char *path, const char **error);
void image_free(Image *image);

#endif
