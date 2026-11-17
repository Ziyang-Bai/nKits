/* Adapted from mViewer CX 4.5.1 image.c.
 * Copyright (c) 2013-2019 Xavier Andréani, JayTe, Excale
 * GPL-3.0-only. BMP palette, mask, raw-row and RLE decoding retained;
 * console, progress and monochrome display dependencies removed.
 */
#include "image.h"
#include "readpng.h"
#include "readjpeg.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int width, height, bits, invertedlines;
    uint32_t compression, indexedcolors;
    uint32_t redmask, greenmask, bluemask;
    unsigned redshift, greenshift, blueshift, redbits, greenbits, bluebits;
    size_t rawlinesize, sourcedatasize;
} dataheader;

static uint32_t readhexint(const unsigned char *data, unsigned bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint32_t)data[i] << (i * 8);
    return value;
}

static bool dimensions(uint32_t width, uint32_t height) {
    return width && height && width <= INT_MAX && height <= INT_MAX &&
        (size_t)width <= SIZE_MAX / sizeof(uint16_t) / height;
}

static bool validmask(uint32_t mask, unsigned bits) {
    if (!mask || (bits < 32 && (mask >> bits))) return false;
    while (!(mask & 1)) mask >>= 1;
    return !(mask & (mask + 1));
}

static void maskinfo(uint32_t mask, unsigned *shift, unsigned *bits) {
    *shift = *bits = 0;
    while (!(mask & 1)) { mask >>= 1; ++*shift; }
    while (mask) { mask >>= 1; ++*bits; }
}

static unsigned maskright(uint32_t color, uint32_t mask, unsigned shift,
                          unsigned width, unsigned bits) {
    color = (color & mask) >> shift;
    if (width >= bits) return color >> (width - bits);
    return (unsigned)((uint64_t)color * ((1u << bits) - 1) / (mask >> shift));
}

static uint16_t makeData(uint32_t color, const dataheader *header) {
    unsigned r = maskright(color, header->redmask, header->redshift, header->redbits, 5);
    unsigned g = maskright(color, header->greenmask, header->greenshift, header->greenbits, 6);
    unsigned b = maskright(color, header->bluemask, header->blueshift, header->bluebits, 5);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void setImgPixel(Image *image, int y, int x, uint16_t color,
                        const dataheader *header) {
    if (header->invertedlines) y = header->height - 1 - y;
    image->pixels[(size_t)y * header->width + x] = color;
}

static bool readRAWImage(Image *image, const dataheader *header,
                         const uint16_t *palette, FILE *file, const char **error) {
    unsigned char *row = malloc(header->rawlinesize);
    if (!row) { *error = "Not enough memory"; return false; }
    bool ok = false;
    for (int y = 0; y < header->height; ++y) {
        if (fread(row, 1, header->rawlinesize, file) != header->rawlinesize) goto done;
        for (int x = 0; x < header->width; ++x) {
            uint32_t color;
            if (header->bits < 8) {
                size_t bit = (size_t)x * header->bits;
                color = (row[bit / 8] >> (8 - header->bits - bit % 8)) &
                    ((1u << header->bits) - 1);
            } else {
                color = readhexint(row + (size_t)x * (header->bits / 8), header->bits / 8);
            }
            if (header->indexedcolors) {
                if (color >= header->indexedcolors) goto done;
                setImgPixel(image, y, x, palette[color], header);
            } else {
                setImgPixel(image, y, x, makeData(color, header), header);
            }
        }
    }
    ok = true;
done:
    free(row);
    return ok;
}

/* Read no farther than the BMP's declared RLE data (or the remaining file). */
static int rlebyte(FILE *file, size_t *remaining) {
    if (!*remaining) return EOF;
    --*remaining;
    return fgetc(file);
}

static bool readRLEImage(Image *image, const dataheader *header,
                         const uint16_t *palette, FILE *file) {
    size_t remaining = header->sourcedatasize;
    int x = 0, y = 0;
    /* Delta and early-EOB pixels use palette entry zero, not RGB black. */
    for (size_t i = 0, n = (size_t)image->width * image->height; i < n; ++i)
        image->pixels[i] = palette[0];
    for (;;) {
        int count = rlebyte(file, &remaining);
        int code = rlebyte(file, &remaining);
        if (count == EOF || code == EOF) return false;
        if (!count) {
            if (code == 1) return true;
            if (code == 0) {
                if (y >= header->height) return false;
                x = 0;
                ++y;
                continue;
            }
            if (code == 2) {
                int dx = rlebyte(file, &remaining), dy = rlebyte(file, &remaining);
                if (dx == EOF || dy == EOF || y >= header->height ||
                    dx > header->width - x || dy >= header->height - y) return false;
                x += dx;
                y += dy;
                continue;
            }
            count = code;
            if (y >= header->height || count > header->width - x) return false;
            int packed = 0;
            for (int j = 0; j < count; ++j) {
                if (header->bits == 8 || !(j & 1)) packed = rlebyte(file, &remaining);
                if (packed == EOF) return false;
                unsigned color = header->bits == 8 ? (unsigned)packed :
                    (unsigned)((j & 1) ? packed & 15 : packed >> 4);
                if (color >= header->indexedcolors) return false;
                setImgPixel(image, y, x++, palette[color], header);
            }
            int bytes = header->bits == 8 ? count : (count + 1) / 2;
            if ((bytes & 1) && rlebyte(file, &remaining) == EOF) return false;
        } else {
            if (y >= header->height || count > header->width - x) return false;
            for (int j = 0; j < count; ++j) {
                unsigned color = header->bits == 8 ? (unsigned)code :
                    (unsigned)((j & 1) ? code & 15 : code >> 4);
                if (color >= header->indexedcolors) return false;
                setImgPixel(image, y, x++, palette[color], header);
            }
        }
    }
}

static bool read_bmp(Image *image, FILE *file, bool dib, const char **error) {
    unsigned char data[124], fileheader[14];
    dataheader header = {0};
    uint16_t palette[256];
    uint32_t offset = 0;
    if (fseek(file, 0, SEEK_END)) return false;
    long end = ftell(file);
    if (end < 0 || fseek(file, 0, SEEK_SET)) return false;
    if (!dib) {
        if (fread(fileheader, 1, 14, file) != 14) return false;
        offset = readhexint(fileheader + 10, 4);
    }
    if (fread(data, 1, 4, file) != 4) return false;
    uint32_t size = readhexint(data, 4);
    if (size != 12 && size != 40 && size != 52 && size != 56 && size != 64 &&
        size != 108 && size != 124) return false;
    if (fread(data + 4, 1, size - 4, file) != size - 4) return false;
    uint32_t width, height;
    if (size == 12) {
        width = readhexint(data + 4, 2);
        height = readhexint(data + 6, 2);
        if (readhexint(data + 8, 2) != 1) return false;
        header.bits = readhexint(data + 10, 2);
        header.invertedlines = 1;
    } else {
        width = readhexint(data + 4, 4);
        uint32_t signedheight = readhexint(data + 8, 4);
        header.invertedlines = !(signedheight & 0x80000000u);
        height = header.invertedlines ? signedheight : 0u - signedheight;
        if (readhexint(data + 12, 2) != 1) return false;
        header.bits = readhexint(data + 14, 2);
        header.compression = readhexint(data + 16, 4);
        header.sourcedatasize = readhexint(data + 20, 4);
        header.indexedcolors = readhexint(data + 32, 4);
    }
    if (!dimensions(width, height)) return false;
    header.width = (int)width;
    header.height = (int)height;
    if (header.bits != 1 && header.bits != 4 && header.bits != 8 &&
        header.bits != 16 && header.bits != 24 && header.bits != 32) return false;
    if (header.compression == 1 || header.compression == 2) {
        if ((header.compression == 1 && header.bits != 8) ||
            (header.compression == 2 && header.bits != 4) || !header.invertedlines) return false;
    } else if (header.compression == 3 || header.compression == 6) {
        if ((header.bits != 16 && header.bits != 32) || size == 64) return false;
    } else if (header.compression) return false;
    if ((size_t)width > (SIZE_MAX - 31) / header.bits) return false;
    header.rawlinesize = (((size_t)width * header.bits + 31) / 32) * 4;
    header.redmask = header.bits == 16 ? 0x7c00 : 0xff0000;
    header.greenmask = header.bits == 16 ? 0x03e0 : 0x00ff00;
    header.bluemask = header.bits == 16 ? 0x001f : 0x0000ff;
    if (header.compression == 3 || header.compression == 6) {
        if (size >= 52) {
            header.redmask = readhexint(data + 40, 4);
            header.greenmask = readhexint(data + 44, 4);
            header.bluemask = readhexint(data + 48, 4);
            if (header.compression == 6 && size < 56) return false;
        } else {
            unsigned bytes = header.compression == 6 ? 16 : 12;
            if (fread(data, 1, bytes, file) != bytes) return false;
            header.redmask = readhexint(data, 4);
            header.greenmask = readhexint(data + 4, 4);
            header.bluemask = readhexint(data + 8, 4);
        }
        if (!validmask(header.redmask, header.bits) ||
            !validmask(header.greenmask, header.bits) ||
            !validmask(header.bluemask, header.bits) ||
            (header.redmask & header.greenmask) || (header.redmask & header.bluemask) ||
            (header.greenmask & header.bluemask)) return false;
    }
    maskinfo(header.redmask, &header.redshift, &header.redbits);
    maskinfo(header.greenmask, &header.greenshift, &header.greenbits);
    maskinfo(header.bluemask, &header.blueshift, &header.bluebits);
    if (header.bits <= 8) {
        uint32_t maxcolors = 1u << header.bits;
        if (!header.indexedcolors) header.indexedcolors = maxcolors;
        if (header.indexedcolors > maxcolors) return false;
        unsigned colorsize = size == 12 ? 3 : 4;
        for (uint32_t i = 0; i < header.indexedcolors; ++i) {
            if (fread(data, 1, colorsize, file) != colorsize) return false;
            palette[i] = (uint16_t)(((data[2] >> 3) << 11) |
                ((data[1] >> 2) << 5) | (data[0] >> 3));
        }
    } else {
        /* Truecolor clrUsed entries are optional optimization palettes. */
        if (header.indexedcolors) {
            long pos = ftell(file);
            if (pos < 0 || (uint64_t)header.indexedcolors * 4 > (uint64_t)(end - pos)) return false;
            if (fseek(file, (long)((uint64_t)header.indexedcolors * 4), SEEK_CUR)) return false;
        }
        header.indexedcolors = 0;
    }
    long start = ftell(file);
    if (start < 0 || start > end) return false;
    if (!dib) {
        if (offset < (unsigned long)start || offset > (unsigned long)end ||
            offset > LONG_MAX || fseek(file, (long)offset, SEEK_SET)) return false;
        start = (long)offset;
    }
    size_t available = (size_t)(end - start);
    bool rle = header.compression == 1 || header.compression == 2;
    if (rle) {
        if (!header.sourcedatasize) header.sourcedatasize = available;
        if (header.sourcedatasize > available) return false;
    } else if (header.rawlinesize > available / height) return false;
    image->pixels = malloc((size_t)width * height * sizeof(uint16_t));
    if (!image->pixels) { *error = "Not enough memory"; return false; }
    image->width = (int)width;
    image->height = (int)height;
    return rle ? readRLEImage(image, &header, palette, file) :
        readRAWImage(image, &header, palette, file, error);
}

bool image_load(Image *image, const char *path, const char **error) {
    const char *message = "Invalid or truncated image";
    FILE *file = fopen(path, "rb");
    if (error) *error = NULL;
    if (!file) {
        if (error) *error = "Cannot open image";
        return false;
    }
    unsigned char magic[8] = {0};
    size_t count = fread(magic, 1, sizeof(magic), file);
    bool ok = false;
    if (fseek(file, 0, SEEK_SET)) goto done;
    if (count >= 2 && magic[0] == 'B' && magic[1] == 'M') {
        ok = read_bmp(image, file, false, &message);
    } else if (count == 8 && !memcmp(magic, "\x89PNG\r\n\x1a\n", 8)) {
        ok = read_png(image, file, &message);
    } else if (count >= 2 && magic[0] == 0xff && magic[1] == 0xd8) {
        ok = read_jpeg(image, file, &message);
    } else if (count >= 4) {
        uint32_t size = readhexint(magic, 4);
        if (size == 12 || size == 40 || size == 52 || size == 56 || size == 64 || size == 108 || size == 124)
            ok = read_bmp(image, file, true, &message);
        else message = "Unsupported image format";
    } else message = "Unsupported image format";
done:
    fclose(file);
    if (!ok) {
        image_free(image);
        if (error) *error = message;
    }
    return ok;
}

void image_free(Image *image) {
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}
