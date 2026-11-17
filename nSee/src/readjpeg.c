/* Adapted from mViewer CX 4.5.1 readjpeg.c.
 * Copyright (c) 2013-2019 Xavier Andréani, JayTe, Excale
 * GPL-3.0-only. Original libjpeg scanline decode and RGB565 conversion retained.
 */
#include "readjpeg.h"
#include "jpeglib.h"

#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>

struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

struct jpeg_reader {
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
};

static void my_error_exit(j_common_ptr cinfo) {
    struct my_error_mgr *myerr = (struct my_error_mgr *)cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

static void my_emit_message(j_common_ptr cinfo, int level) {
    /* libjpeg normally substitutes an EOI marker for truncated input. */
    if (level < 0) my_error_exit(cinfo);
}

bool read_jpeg(Image *image, FILE *file, const char **error) {
    /* Decoder state is modified by libjpeg, so keep it out of setjmp locals. */
    struct jpeg_reader *reader = calloc(1, sizeof(*reader));
    if (!reader) { *error = "Not enough memory"; return false; }
    struct jpeg_decompress_struct *cinfo = &reader->cinfo;
    cinfo->err = jpeg_std_error(&reader->jerr.pub);
    reader->jerr.pub.error_exit = my_error_exit;
    reader->jerr.pub.emit_message = my_emit_message;
    if (setjmp(reader->jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(cinfo);
        free(reader);
        return false;
    }
    jpeg_create_decompress(cinfo);
    jpeg_stdio_src(cinfo, file);
    jpeg_read_header(cinfo, TRUE);
    if (!cinfo->image_width || !cinfo->image_height ||
        cinfo->image_width > INT_MAX || cinfo->image_height > INT_MAX ||
        (size_t)cinfo->image_width > SIZE_MAX / sizeof(uint16_t) / cinfo->image_height ||
        (size_t)cinfo->image_width > SIZE_MAX / 3) {
        my_error_exit((j_common_ptr)cinfo);
    }
    cinfo->out_color_space = JCS_RGB;
    jpeg_start_decompress(cinfo);
    if (cinfo->output_components != 3) my_error_exit((j_common_ptr)cinfo);
    image->pixels = malloc((size_t)cinfo->output_width * cinfo->output_height * sizeof(uint16_t));
    if (!image->pixels) {
        *error = "Not enough memory";
        my_error_exit((j_common_ptr)cinfo);
    }
    image->width = (int)cinfo->output_width;
    image->height = (int)cinfo->output_height;
    JSAMPARRAY buffer = (*cinfo->mem->alloc_sarray)((j_common_ptr)cinfo,
        JPOOL_IMAGE, cinfo->output_width * 3, 1);
    while (cinfo->output_scanline < cinfo->output_height) {
        jpeg_read_scanlines(cinfo, buffer, 1);
        for (JDIMENSION x = 0; x < cinfo->output_width; ++x) {
            unsigned red = buffer[0][(size_t)x * 3] >> 3;
            unsigned green = buffer[0][(size_t)x * 3 + 1] >> 2;
            unsigned blue = buffer[0][(size_t)x * 3 + 2] >> 3;
            image->pixels[(size_t)(cinfo->output_scanline - 1) * cinfo->output_width + x] =
                (uint16_t)((red << 11) | (green << 5) | blue);
        }
    }
    jpeg_finish_decompress(cinfo);
    jpeg_destroy_decompress(cinfo);
    free(reader);
    return true;
}
