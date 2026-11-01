#ifndef NUSB_IMAGE_DISK_H
#define NUSB_IMAGE_DISK_H

#include "fs.h"
#include <stdio.h>

typedef struct {
    BlockDevice device;
    FILE *file;
    uint32_t generation;
    uint64_t reads;
    uint64_t sectors_read;
    uint64_t fail_at;
    uint64_t detach_at;
    MscResult failure;
    uint64_t writes, sectors_written, syncs;
    uint64_t fail_write_at, fail_sync_at, detach_write_at;
    bool connected;
    bool detach_on_failure;
} ImageDisk;

bool image_disk_open(ImageDisk *disk, const char *path, uint32_t sector_size);
/* Opens an explicitly selected working image, never a fixture copy. */
bool image_disk_open_writable(ImageDisk *disk, const char *path, uint32_t sector_size);
/* Copies a fixture into an anonymous temporary file; the source stays read-only. */
bool image_disk_copy(ImageDisk *disk, const char *path, uint32_t sector_size);
void image_disk_close(ImageDisk *disk);
void image_disk_detach(ImageDisk *disk);

#endif
