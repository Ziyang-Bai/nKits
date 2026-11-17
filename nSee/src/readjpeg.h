#ifndef NSEE_READJPEG_H
#define NSEE_READJPEG_H

#include "image.h"
#include <stdio.h>

bool read_jpeg(Image *image, FILE *file, const char **error);

#endif
