#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "image_disk.h"

#include <string.h>
#include <unistd.h>

static uint32_t image_generation(void *ctx) {
    return ((ImageDisk *)ctx)->generation;
}

static uint32_t image_fattime(void *ctx) {
    (void)ctx;
    return (uint32_t)0x5d21 << 16 | 0x645c;
}

static MscResult image_read(void *ctx, uint32_t lba, uint32_t count, void *buf) {
    ImageDisk *disk = ctx;
    if (!disk->connected) return MSC_NO_DEVICE;
    ++disk->reads;
    if (disk->fail_at && disk->reads >= disk->fail_at) return disk->failure;
    if (!count || (uint64_t)lba + count > disk->device.sector_count)
        return MSC_IO_ERROR;
    if (fseeko(disk->file, (off_t)((uint64_t)lba * disk->device.sector_size), SEEK_SET))
        return MSC_IO_ERROR;
    if (fread(buf, disk->device.sector_size, count, disk->file) != count)
        return MSC_IO_ERROR;
    disk->sectors_read += count;
    if (disk->detach_at && disk->reads >= disk->detach_at) image_disk_detach(disk);
    return MSC_OK;
}

static MscResult image_write(void *ctx, uint32_t lba, uint32_t count, const void *buf) {
    ImageDisk *disk = ctx;
    if (!disk->connected) return MSC_NO_DEVICE;
    ++disk->writes;
    if (disk->fail_write_at && disk->writes >= disk->fail_write_at) {
        if (disk->detach_on_failure) image_disk_detach(disk);
        return disk->failure;
    }
    if (!count || (uint64_t)lba + count > disk->device.sector_count)
        return MSC_IO_ERROR;
    if (fseeko(disk->file, (off_t)((uint64_t)lba * disk->device.sector_size), SEEK_SET))
        return MSC_IO_ERROR;
    if (fwrite(buf, disk->device.sector_size, count, disk->file) != count)
        return MSC_IO_ERROR;
    disk->sectors_written += count;
    if (disk->detach_write_at && disk->writes >= disk->detach_write_at)
        image_disk_detach(disk);
    return MSC_OK;
}

static MscResult image_sync(void *ctx) {
    ImageDisk *disk = ctx;
    if (!disk->connected) return MSC_NO_DEVICE;
    ++disk->syncs;
    if (disk->fail_sync_at && disk->syncs >= disk->fail_sync_at) {
        if (disk->detach_on_failure) image_disk_detach(disk);
        return disk->failure;
    }
    return fflush(disk->file) ? MSC_IO_ERROR : MSC_OK;
}

bool image_disk_open(ImageDisk *disk, const char *path, uint32_t sector_size) {
    off_t size;
    memset(disk, 0, sizeof(*disk));
    disk->file = fopen(path, "rb");
    if (!disk->file) return false;
    if (fseeko(disk->file, 0, SEEK_END)) goto failed;
    size = ftello(disk->file);
    if (size <= 0 || !sector_size || (uint64_t)size % sector_size) goto failed;
    disk->device.ctx = disk;
    disk->device.read = image_read;
    disk->device.generation = image_generation;
    disk->device.sector_size = sector_size;
    disk->device.sector_count = (uint64_t)size / sector_size;
    disk->device.fattime = image_fattime;
    disk->generation = 1;
    disk->connected = true;
    disk->failure = MSC_IO_ERROR;
    return true;
failed:
    fclose(disk->file);
    disk->file = NULL;
    return false;
}

bool image_disk_open_writable(ImageDisk *disk, const char *path, uint32_t sector_size) {
    FILE *file;
    if (!image_disk_open(disk, path, sector_size)) return false;
    file = fopen(path, "r+b");
    if (!file) {
        image_disk_close(disk);
        return false;
    }
    fclose(disk->file);
    disk->file = file;
    disk->device.write = image_write;
    disk->device.sync = image_sync;
    return true;
}

bool image_disk_copy(ImageDisk *disk, const char *path, uint32_t sector_size) {
    unsigned char buf[32768];
    size_t count;
    FILE *copy;
    if (!image_disk_open(disk, path, sector_size)) return false;
    copy = tmpfile();
    if (!copy) goto failed;
    rewind(disk->file);
    while ((count = fread(buf, 1, sizeof(buf), disk->file)) != 0) {
        size_t i;
        for (i = 0; i < count && !buf[i]; ++i) {}
        if (i == count) {
            if (fseeko(copy, (off_t)count, SEEK_CUR)) goto failed;
        } else if (fwrite(buf, 1, count, copy) != count) goto failed;
    }
    if (ferror(disk->file) || fflush(copy) ||
        ftruncate(fileno(copy), (off_t)(disk->device.sector_count * sector_size)))
        goto failed;
    fclose(disk->file);
    disk->file = copy;
    disk->device.write = image_write;
    disk->device.sync = image_sync;
    return true;
failed:
    if (copy) fclose(copy);
    image_disk_close(disk);
    return false;
}

void image_disk_detach(ImageDisk *disk) {
    disk->connected = false;
    ++disk->generation;
}

void image_disk_close(ImageDisk *disk) {
    image_disk_detach(disk);
    if (disk->file) fclose(disk->file);
    disk->file = NULL;
}
