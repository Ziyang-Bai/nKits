#ifndef NSEE_READPNG_H
#define NSEE_READPNG_H

#include "image.h"
#include <stdio.h>

bool read_png(Image *image, FILE *file, const char **error);

#endif
