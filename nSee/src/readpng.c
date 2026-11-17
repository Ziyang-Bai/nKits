/* Adapted from mViewer CX 4.5.1 readpng.c.
 * Copyright (c) 2013-2019 Xavier Andréani, JayTe, Excale
 * GPL-3.0-only. Original libpng transforms and RGB565 conversion retained.
 */
#include "readpng.h"
#include "png.h"

#include <limits.h>
#include <stdlib.h>

static void PNGAPI custom_error(png_structp png, png_const_charp message) {
    (void)message;
    longjmp(png_jmpbuf(png), 1);
}

static void PNGAPI custom_warning(png_structp png, png_const_charp message) {
    (void)png;
    (void)message;
}

bool read_png(Image *image, FILE *file, const char **error) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
        custom_error, custom_warning);
    if (!png) { *error = "Not enough memory"; return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        *error = "Not enough memory";
        return false;
    }
    /* Only this local changes between setjmp and an error longjmp. */
    unsigned char *volatile buffer = NULL;
    if (setjmp(png_jmpbuf(png))) {
        free(buffer);
        png_destroy_read_struct(&png, &info, NULL);
        return false;
    }
    png_init_io(png, file);
    png_set_user_limits(png, INT_MAX, INT_MAX);
    png_set_crc_action(png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_read_info(png, info);
    png_uint_32 width = png_get_image_width(png, info);
    png_uint_32 height = png_get_image_height(png, info);
    if (!width || !height || width > INT_MAX || height > INT_MAX ||
        (size_t)width > SIZE_MAX / sizeof(uint16_t) / height ||
        (size_t)width > SIZE_MAX / 3) png_error(png, "Invalid dimensions");

    int type = png_get_color_type(png, info);
    int depth = png_get_bit_depth(png, info);
    if (!(type & PNG_COLOR_MASK_COLOR)) png_set_gray_to_rgb(png);
    else if (type & PNG_COLOR_MASK_PALETTE) png_set_palette_to_rgb(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
        png_set_strip_alpha(png);
    } else if (type & PNG_COLOR_MASK_ALPHA) png_set_strip_alpha(png);
    if (depth < 8) {
        if (!(type & PNG_COLOR_MASK_COLOR)) png_set_expand_gray_1_2_4_to_8(png);
        else png_set_packing(png);
    } else if (depth >= 16) png_set_strip_16(png);
    int passes = png_set_interlace_handling(png);
    png_read_update_info(png, info);
    size_t stride = (size_t)width * 3;
    if (png_get_bit_depth(png, info) != 8 || png_get_channels(png, info) != 3 ||
        png_get_rowbytes(png, info) != stride) png_error(png, "Invalid row size");
    size_t rows = passes > 1 ? height : 1;
    if (stride > SIZE_MAX / rows) png_error(png, "Invalid dimensions");
    if (passes > 1) {
        /* Adam7 needs earlier passes intact. Convert in place on the last pass. */
        image->pixels = calloc(rows, stride);
    } else {
        buffer = malloc(stride);
        image->pixels = malloc((size_t)width * height * sizeof(uint16_t));
    }
    if ((!buffer && passes == 1) || !image->pixels) {
        *error = "Not enough memory";
        png_error(png, "Out of memory");
    }
    image->width = (int)width;
    image->height = (int)height;
    for (int pass = 0; pass < passes; ++pass) {
        for (png_uint_32 y = 0; y < height; ++y) {
            unsigned char *row = passes > 1 ?
                (unsigned char *)image->pixels + (size_t)y * stride : buffer;
            png_read_row(png, row, NULL);
            if (pass != passes - 1) continue;
            for (png_uint_32 x = 0; x < width; ++x) {
                unsigned red = row[(size_t)x * 3] >> 3;
                unsigned green = row[(size_t)x * 3 + 1] >> 2;
                unsigned blue = row[(size_t)x * 3 + 2] >> 3;
                image->pixels[(size_t)y * width + x] =
                    (uint16_t)((red << 11) | (green << 5) | blue);
            }
        }
    }
    png_read_end(png, NULL);
    png_destroy_read_struct(&png, &info, NULL);
    free(buffer);
    return true;
}
