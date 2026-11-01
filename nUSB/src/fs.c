#include "fs.h"
#include "ff.h"
#include "diskio.h"

#include <stdlib.h>
#include <string.h>

PARTITION VolToPart[FF_VOLUMES] = {{0, 0}};

struct FsDir {
    DIR directory;
    uint64_t epoch;
};

struct FsFile {
    FIL file;
    uint64_t epoch;
    uint64_t position;
    unsigned flags;
};

static BlockDevice device;
static FATFS volume;
static FILINFO info;
static uint8_t sector0[FF_MAX_SS];
static uint32_t generation;
static uint64_t epoch, revision;
static uint64_t first_sector, end_sector;
static FsResult io_result;
static bool mounted, invalidated, volume_readonly;
static bool synced;
static FsVolumeInfo volume_info;
static const char *mount_error;

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t read64(const uint8_t *p) {
    return (uint64_t)read32(p) | (uint64_t)read32(p + 4) << 32;
}

static bool disk_present(void) {
    if (!device.read || invalidated) return false;
    if (device.generation(device.ctx) != generation) {
        invalidated = true;
        io_result = FS_NO_DEVICE;
        return false;
    }
    return true;
}

DWORD get_fattime(void) {
    if (!mounted || !disk_present() || !device.fattime) return 0;
    return device.fattime(device.ctx);
}

static FsResult msc_result(MscResult result) {
    switch (result) {
        case MSC_OK: return FS_OK;
        case MSC_NO_DEVICE: return FS_NO_DEVICE;
        case MSC_UNSUPPORTED: return FS_UNSUPPORTED;
        case MSC_CANCELLED: return FS_CANCELLED;
        case MSC_WRITE_PROTECTED: return FS_WRITE_PROTECTED;
        default: return FS_IO_ERROR;
    }
}

static FsResult fs_result(FRESULT result) {
    if (io_result != FS_OK) return io_result;
    if (!disk_present()) return FS_NO_DEVICE;
    switch (result) {
        case FR_OK: return FS_OK;
        case FR_NOT_READY: return FS_NO_DEVICE;
        case FR_NO_FILESYSTEM: return FS_UNSUPPORTED;
        case FR_NOT_ENOUGH_CORE: return FS_NO_MEMORY;
        case FR_WRITE_PROTECTED: return FS_WRITE_PROTECTED;
        case FR_DENIED: return FS_DENIED;
        case FR_EXIST: return FS_EXISTS;
        case FR_LOCKED:
        case FR_TOO_MANY_OPEN_FILES: return FS_BUSY;
        case FR_INVALID_PARAMETER: return FS_INVALID_ARGUMENT;
        case FR_NO_FILE:
        case FR_NO_PATH:
        case FR_INVALID_NAME: return FS_NOT_FOUND;
        default: return FS_IO_ERROR;
    }
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv || !disk_present()) return STA_NOINIT | STA_NODISK;
    return !volume_readonly && device.write && device.sync ? 0 : STA_PROTECT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buf, LBA_t lba, UINT count) {
    MscResult result;
    uint64_t end = (uint64_t)lba + count;
    if (pdrv || !buf || !count) return RES_PARERR;
    if (!disk_present()) return RES_NOTRDY;
    if (end > device.sector_count ||
        (!(!mounted && lba == 0 && count == 1) &&
         ((uint64_t)lba < first_sector || end > end_sector))) {
        io_result = FS_IO_ERROR;
        return RES_ERROR;
    }
    result = device.read(device.ctx, lba, count, buf);
    if (!disk_present()) return RES_NOTRDY;
    io_result = msc_result(result);
    if (result == MSC_NO_DEVICE) invalidated = true;
    return result == MSC_OK ? RES_OK : result == MSC_NO_DEVICE ? RES_NOTRDY : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buf, LBA_t lba, UINT count) {
    MscResult result;
    uint64_t end = (uint64_t)lba + count;
    if (pdrv || !buf || !count) return RES_PARERR;
    if (!disk_present()) return RES_NOTRDY;
    if (volume_readonly || !device.write || !device.sync) return RES_WRPRT;
    if (!mounted || (uint64_t)lba < first_sector ||
        end > end_sector || end > device.sector_count) {
        io_result = FS_IO_ERROR;
        invalidated = true;
        return RES_ERROR;
    }
    result = device.write(device.ctx, lba, count, buf);
    io_result = result == MSC_UNSUPPORTED ? FS_IO_ERROR : msc_result(result);
    if (result != MSC_OK) invalidated = true;
    else if (!disk_present()) return RES_NOTRDY;
    return result == MSC_OK ? RES_OK :
           result == MSC_WRITE_PROTECTED ? RES_WRPRT :
           result == MSC_NO_DEVICE ? RES_NOTRDY : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buf) {
    if (pdrv || (cmd != CTRL_SYNC && !buf)) return RES_PARERR;
    if (!disk_present()) return RES_NOTRDY;
    switch (cmd) {
        case CTRL_SYNC: {
            MscResult result;
            if (!device.write || !device.sync) return RES_WRPRT;
            result = device.sync(device.ctx);
            io_result = result == MSC_UNSUPPORTED ? FS_IO_ERROR : msc_result(result);
            if (result != MSC_OK) invalidated = true;
            else if (!disk_present()) return RES_NOTRDY;
            if (result == MSC_OK) synced = true;
            return result == MSC_OK ? RES_OK :
                   result == MSC_NO_DEVICE ? RES_NOTRDY : RES_ERROR;
        }
        case GET_SECTOR_SIZE:
            *(WORD *)buf = (WORD)device.sector_size;
            break;
        case GET_SECTOR_COUNT:
            if (device.sector_count > UINT32_MAX) return RES_PARERR;
            *(LBA_t *)buf = (LBA_t)device.sector_count;
            break;
        default: return RES_PARERR;
    }
    return disk_present() ? RES_OK : RES_NOTRDY;
}

void fs_unmount(void) {
    ++epoch;
    f_mount(NULL, "0:", 0);
    memset(&device, 0, sizeof(device));
    mounted = false;
    invalidated = false;
    volume_readonly = false;
    memset(&volume_info, 0, sizeof(volume_info));
    ++revision;
    io_result = FS_OK;
}

static FsResult read_sector(uint64_t sector) {
    if (disk_read(0, volume.win, (LBA_t)sector, 1) == RES_OK) return FS_OK;
    return io_result == FS_OK ? FS_IO_ERROR : io_result;
}

static FsResult exfat_boot(uint64_t start, uint64_t count) {
    const uint8_t *b = volume.win;
    uint64_t length = read64(b + 72), heap_end, fat_end;
    uint32_t fat = read32(b + 80), fat_size = read32(b + 84);
    uint32_t heap = read32(b + 88), clusters = read32(b + 92);
    uint32_t root = read32(b + 96), checksum = 0;
    unsigned sector_shift = b[108], cluster_shift = b[109], i, j;
    uint16_t flags = (uint16_t)b[106] | (uint16_t)b[107] << 8;
    FsResult result;
    mount_error = "Invalid exFAT geometry";
    for (i = 11; i < 64; ++i) if (b[i]) return FS_IO_ERROR;
    for (i = 113; i < 120; ++i) if (b[i]) return FS_IO_ERROR;
    if (memcmp(b, "\xeb\x76\x90", 3) ||
        b[104] != 0 || b[105] != 1 || b[110] != 1 ||
        (flags & 1) || sector_shift < 9 || sector_shift > 12 ||
        (1U << sector_shift) != device.sector_size || cluster_shift > 15 ||
        clusters < 0x100 || clusters > 0x7ffffffd ||
        root < 2 || root >= (uint64_t)clusters + 2 ||
        fat < 24 || !fat_size || !length || length > count ||
        start + length >= ((uint64_t)1 << 32))
        return FS_IO_ERROR;
    fat_end = (uint64_t)fat + fat_size;
    heap_end = (uint64_t)heap + ((uint64_t)clusters << cluster_shift);
    if ((uint64_t)fat_size * device.sector_size < ((uint64_t)clusters + 2) * 4 ||
        fat_end > heap || heap_end > length) return FS_IO_ERROR;
    end_sector = start + length;
    volume_info.format = FS_FORMAT_EXFAT;
    volume_info.dirty = (flags & 2) != 0;
    volume_info.media_failure = (flags & 4) != 0;
    volume_readonly = volume_info.dirty || volume_info.media_failure;
    volume_info.volume_bytes = length * device.sector_size;
    volume_info.max_file_size = (uint64_t)clusters *
                                ((uint64_t)1 << cluster_shift) * device.sector_size;
    mount_error = "Invalid exFAT boot checksum";
    for (i = 0; i < 11; ++i) {
        result = read_sector(start + i);
        if (result != FS_OK) return result;
        for (j = 0; j < device.sector_size; ++j) {
            if (i == 0 && (j == 106 || j == 107 || j == 112)) continue;
            checksum = (checksum >> 1 | checksum << 31) + volume.win[j];
        }
    }
    result = read_sector(start + 11);
    if (result != FS_OK) return result;
    for (i = 0; i < device.sector_size; i += 4)
        if (read32(volume.win + i) != checksum) return FS_IO_ERROR;
    return FS_OK;
}

static FsResult exfat_bitmap(uint64_t start, uint32_t fat, uint32_t heap,
                             uint32_t clusters, uint32_t root, uint32_t csize) {
    uint64_t root_sector = start + heap + (uint64_t)(root - 2) * csize;
    uint64_t length = 0, needed, cluster_bytes = (uint64_t)csize * device.sector_size;
    uint32_t first = 0, cluster, next;
    unsigned i, j;
    bool found = false;
    FsResult result;
    mount_error = "Unsupported exFAT bitmap layout";
    for (i = 0; i < csize; ++i) {
        result = read_sector(root_sector + i);
        if (result != FS_OK) return result;
        for (j = 0; j < device.sector_size; j += 32) {
            const uint8_t *entry = volume.win + j;
            if (!entry[0]) goto scanned;
            if (entry[0] != 0x81) continue;
            if (found || entry[1] != 0) return FS_UNSUPPORTED;
            found = true;
            first = read32(entry + 20);
            length = read64(entry + 24);
        }
    }
scanned:
    if (!found || length < ((uint64_t)clusters + 7) / 8 ||
        first < 2 || first >= (uint64_t)clusters + 2 ||
        length > (uint64_t)clusters * cluster_bytes) return FS_UNSUPPORTED;
    needed = (length + cluster_bytes - 1) / cluster_bytes;
    if (needed > (uint64_t)clusters + 2 - first) return FS_UNSUPPORTED;
    cluster = first;
    for (i = 0; i < clusters; ++i, ++cluster) {
        uint64_t offset = (uint64_t)cluster * 4;
        if (cluster >= (uint64_t)clusters + 2) return FS_UNSUPPORTED;
        result = read_sector(start + fat + offset / device.sector_size);
        if (result != FS_OK) return result;
        next = read32(volume.win + offset % device.sector_size);
        if (next >= 0xfffffff8)
            return (uint64_t)i + 1 >= needed ? FS_OK : FS_UNSUPPORTED;
        if (next != cluster + 1) return FS_UNSUPPORTED;
    }
    return FS_UNSUPPORTED;
}

static FsResult fat32_boot(uint64_t start, uint64_t count) {
    const uint8_t *b = volume.win;
    uint32_t sectors = (uint32_t)b[19] | (uint32_t)b[20] << 8;
    uint32_t reserved = (uint32_t)b[14] | (uint32_t)b[15] << 8;
    uint32_t fat_size = read32(b + 36), root = read32(b + 44);
    uint32_t sector_size = (uint32_t)b[11] | (uint32_t)b[12] << 8;
    uint64_t data, clusters;
    if (!sectors) sectors = read32(b + 32);
    if ((b[0] != 0xeb && b[0] != 0xe9 && b[0] != 0xe8) ||
        memcmp(b + 82, "FAT32   ", 8) ||
        sector_size != device.sector_size || !b[13] ||
        (b[13] & (b[13] - 1)) || !reserved || !b[16] || b[16] > 2 ||
        b[17] || b[18] || b[22] || b[23] || !fat_size)
        return FS_UNSUPPORTED;
    mount_error = "Invalid FAT32 geometry";
    data = (uint64_t)reserved + (uint64_t)b[16] * fat_size;
    if (sectors > count || sectors <= data ||
        start + sectors >= ((uint64_t)1 << 32)) return FS_IO_ERROR;
    clusters = (sectors - data) / b[13];
    if (clusters < 65525 || clusters > 0x0ffffff5 ||
        (uint64_t)fat_size * device.sector_size < (clusters + 2) * 4 ||
        root < 2 || root >= clusters + 2) return FS_IO_ERROR;
    end_sector = start + sectors;
    volume_info.format = FS_FORMAT_FAT32;
    volume_info.max_file_size = UINT32_MAX;
    volume_info.volume_bytes = (uint64_t)sectors * device.sector_size;
    return FS_OK;
}

static FsResult mount_partition(BYTE part, uint64_t start, uint64_t count) {
    FsResult result;
    FRESULT mounted_result;
    bool exfat;
    uint32_t fat, heap, clusters, root, csize;
    f_mount(NULL, "0:", 0);
    mounted = false;
    volume_readonly = false;
    memset(&volume_info, 0, sizeof(volume_info));
    VolToPart[0].pt = part;
    first_sector = start;
    end_sector = start + count;
    io_result = FS_OK;
    result = read_sector(start);
    if (result != FS_OK) return result;
    if (volume.win[510] != 0x55 || volume.win[511] != 0xaa) return FS_UNSUPPORTED;
    exfat = !memcmp(volume.win + 3, "EXFAT   ", 8);
    if (exfat) {
        fat = read32(volume.win + 80);
        heap = read32(volume.win + 88);
        clusters = read32(volume.win + 92);
        root = read32(volume.win + 96);
        csize = volume.win[109] <= 15 ? 1U << volume.win[109] : 0;
        result = exfat_boot(start, count);
        if (result == FS_OK)
            result = exfat_bitmap(start, fat, heap, clusters, root, csize);
    } else {
        result = fat32_boot(start, count);
    }
    if (result != FS_OK) return result;
    mounted_result = f_mount(&volume, "0:", 1);
    if (mounted_result != FR_OK) return fs_result(mounted_result);
    if (!disk_present()) return FS_NO_DEVICE;
    if (volume.volbase != start ||
        volume.fs_type != (exfat ? FS_EXFAT : FS_FAT32)) return FS_UNSUPPORTED;
    volume_info.writable = !volume_readonly && device.write && device.sync;
    volume_info.max_path_bytes = 4095;
    volume_info.max_name_bytes = 765;
    volume_info.max_name_units = 255;
    mount_error = NULL;
    mounted = true;
    return FS_OK;
}

FsResult fs_mount(const BlockDevice *disk) {
    FsResult result;
    unsigned i;
    fs_unmount();
    mount_error = NULL;
    if (!disk || !disk->read || !disk->generation) return FS_NO_DEVICE;
    if ((disk->sector_size != 512 && disk->sector_size != 1024 &&
         disk->sector_size != 2048 && disk->sector_size != 4096) ||
        !disk->sector_count || disk->sector_count > UINT32_MAX)
        return FS_UNSUPPORTED;
    device = *disk;
    generation = device.generation(device.ctx);
    first_sector = 0;
    end_sector = device.sector_count;
    if (disk_read(0, sector0, 0, 1) != RES_OK) {
        result = io_result == FS_OK ? FS_NO_DEVICE : io_result;
        goto failed;
    }
    result = mount_partition(0, 0, device.sector_count);
    if (result == FS_OK) return result;
    if (io_result != FS_OK || result == FS_NO_DEVICE) goto failed;
    if (!memcmp(sector0 + 3, "EXFAT   ", 8)) goto failed;
    if (sector0[510] != 0x55 || sector0[511] != 0xaa) goto failed;
    for (i = 0; i < 4; ++i)
        if (sector0[446 + i * 16 + 4] == 0xee) {
            result = FS_UNSUPPORTED;
            goto failed;
        }
    for (i = 0; i < 4; ++i) {
        const uint8_t *part = sector0 + 446 + i * 16;
        uint32_t start = read32(part + 8), count = read32(part + 12);
        if (part[4] != 0x07 && part[4] != 0x0b && part[4] != 0x0c &&
            part[4] != 0x1b && part[4] != 0x1c) continue;
        if (!start || !count || start >= device.sector_count ||
            count > device.sector_count - start) continue;
        result = mount_partition((BYTE)(i + 1), start, count);
        if (result == FS_OK) return result;
        if (io_result != FS_OK || result == FS_NO_DEVICE) goto failed;
    }
failed:
    fs_unmount();
    return result;
}

FsResult fs_volume_info(FsVolumeInfo *out) {
    if (!out) return FS_INVALID_ARGUMENT;
    if (!mounted || !disk_present()) return FS_NO_DEVICE;
    *out = volume_info;
    return FS_OK;
}

uint64_t fs_revision(void) {
    return revision;
}

const char *fs_mount_error(void) {
    return mount_error;
}

static FsResult check_path(const char *path) {
    if (!mounted || !disk_present()) return FS_NO_DEVICE;
    if (!path) return FS_NOT_FOUND;
    if (strlen(path) >= 4096) return FS_PATH_TOO_LONG;
    if (strchr(path, ':') && (path[0] != '0' || path[1] != ':'))
        return FS_INVALID_ARGUMENT;
    return FS_OK;
}

static void copy_entry(FsEntry *entry) {
    memcpy(entry->name, info.fname, strlen(info.fname) + 1);
    entry->size = info.fsize;
    entry->is_directory = (info.fattrib & AM_DIR) != 0;
    entry->date = info.fdate;
    entry->time = info.ftime;
    entry->attributes = info.fattrib;
    entry->access_date = info.acdate;
    entry->created_date = info.crdate;
    entry->created_time = info.crtime;
    entry->identity.parent_cluster = info.parent_cluster;
    entry->identity.entry_offset = info.entry_offset;
    entry->identity.directory_cluster = info.directory_cluster;
}

FsResult fs_opendir(const char *path, FsDir **dir) {
    FsDir *opened;
    FsResult result;
    if (!dir) return FS_IO_ERROR;
    *dir = NULL;
    result = check_path(path);
    if (result != FS_OK) return result;
    opened = malloc(sizeof(*opened));
    if (!opened) return FS_NO_MEMORY;
    io_result = FS_OK;
    result = fs_result(f_opendir(&opened->directory, path));
    if (result != FS_OK) {
        free(opened);
        return result;
    }
    opened->epoch = epoch;
    *dir = opened;
    return FS_OK;
}

FsResult fs_next(FsDir *dir, FsEntry *entry, bool *end) {
    FsResult result;
    if (end) *end = false;
    if (!mounted || !disk_present()) return FS_NO_DEVICE;
    if (!dir || !entry || !end) return FS_IO_ERROR;
    if (dir->epoch != epoch) return FS_NO_DEVICE;
    do {
        io_result = FS_OK;
        result = fs_result(f_readdir(&dir->directory, &info));
        if (result != FS_OK) return result;
        if (!info.fname[0]) {
            *end = true;
            return FS_OK;
        }
    } while (!strcmp(info.fname, ".") || !strcmp(info.fname, ".."));
    copy_entry(entry);
    return FS_OK;
}

void fs_closedir(FsDir *dir) {
    if (!dir) return;
    if (mounted && dir->epoch == epoch && disk_present())
        f_closedir(&dir->directory);
    free(dir);
}

FsResult fs_stat(const char *path, FsEntry *entry) {
    FsResult result;
    if (!entry) return FS_IO_ERROR;
    memset(entry, 0, sizeof(*entry));
    result = check_path(path);
    if (result != FS_OK) return result;
    if (!strcmp(path, "0:/") || !strcmp(path, "0:")) {
        strcpy(entry->name, "/");
        entry->is_directory = true;
        entry->attributes = AM_DIR;
        return FS_OK;
    }
    io_result = FS_OK;
    result = fs_result(f_stat(path, &info));
    if (result == FS_OK) copy_entry(entry);
    return result;
}

static FsResult writable_path(const char *path) {
    FsResult result = check_path(path);
    if (result != FS_OK) return result;
    if (volume_readonly || !device.write || !device.sync) return FS_WRITE_PROTECTED;
    return FS_OK;
}

static FsResult check_file(FsFile *file) {
    if (!mounted || !disk_present()) return FS_NO_DEVICE;
    if (!file) return FS_INVALID_ARGUMENT;
    if (file->epoch != epoch) return FS_NO_DEVICE;
    return FS_OK;
}

FsResult fs_open(const char *path, unsigned flags, FsFile **file) {
    FsFile *opened;
    FsResult result;
    BYTE mode = 0;
    bool created = false;
    if (!file) return FS_INVALID_ARGUMENT;
    *file = NULL;
    if (!(flags & (FS_OPEN_READ | FS_OPEN_WRITE)) ||
        (flags & ~(FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE |
                   FS_OPEN_EXCLUSIVE | FS_OPEN_TRUNCATE | FS_OPEN_APPEND)) ||
        ((flags & (FS_OPEN_CREATE | FS_OPEN_TRUNCATE | FS_OPEN_APPEND)) &&
         !(flags & FS_OPEN_WRITE)) ||
        ((flags & FS_OPEN_EXCLUSIVE) && !(flags & FS_OPEN_CREATE)))
        return FS_INVALID_ARGUMENT;
    result = flags & FS_OPEN_WRITE ? writable_path(path) : check_path(path);
    if (result != FS_OK) return result;
    if (flags & FS_OPEN_CREATE) {
        created = (flags & FS_OPEN_EXCLUSIVE) != 0;
        if (!created) {
            io_result = FS_OK;
            result = fs_result(f_stat(path, &info));
            if (result != FS_OK && result != FS_NOT_FOUND) return result;
            created = result == FS_NOT_FOUND;
        }
    }
    if (flags & FS_OPEN_READ) mode |= FA_READ;
    if (flags & FS_OPEN_WRITE) mode |= FA_WRITE;
    if (flags & FS_OPEN_CREATE)
        mode |= flags & FS_OPEN_EXCLUSIVE ? FA_CREATE_NEW : FA_OPEN_ALWAYS;
    opened = malloc(sizeof(*opened));
    if (!opened) return FS_NO_MEMORY;
    io_result = FS_OK;
    result = fs_result(f_open(&opened->file, path, mode));
    if (result != FS_OK) {
        free(opened);
        return result;
    }
    if (created) ++revision;
    opened->epoch = epoch;
    opened->flags = flags;
    opened->position = flags & FS_OPEN_APPEND ? f_size(&opened->file) : 0;
    if (flags & FS_OPEN_TRUNCATE) {
        result = fs_truncate(opened, 0);
        if (result != FS_OK) {
            fs_close(opened);
            return result;
        }
        opened->position = 0;
    }
    *file = opened;
    return FS_OK;
}

FsResult fs_read(FsFile *file, void *buf, uint32_t count, uint32_t *read) {
    FsResult result = check_file(file);
    UINT actual = 0;
    if (read) *read = 0;
    if (result != FS_OK) return result;
    if (!read || (!buf && count)) return FS_INVALID_ARGUMENT;
    if (!(file->flags & FS_OPEN_READ)) return FS_DENIED;
    if (count > UINT64_MAX - file->position) return FS_INVALID_ARGUMENT;
    if (!count || file->position >= f_size(&file->file)) return FS_OK;
    io_result = FS_OK;
    result = fs_result(f_lseek(&file->file, file->position));
    if (result == FS_OK) result = fs_result(f_read(&file->file, buf, count, &actual));
    file->position += actual;
    *read = actual;
    return result;
}

FsResult fs_write(FsFile *file, const void *buf, uint32_t count, uint32_t *written) {
    FsResult result = check_file(file);
    UINT actual = 0;
    if (written) *written = 0;
    if (result != FS_OK) return result;
    if (!written || (!buf && count)) return FS_INVALID_ARGUMENT;
    if (!(file->flags & FS_OPEN_WRITE)) return FS_DENIED;
    if (!count) return FS_OK;
    if (file->flags & FS_OPEN_APPEND) file->position = f_size(&file->file);
    if (count > UINT64_MAX - file->position) return FS_INVALID_ARGUMENT;
    if (file->position + count > volume_info.max_file_size) return FS_TOO_LARGE;
    if (file->position > f_size(&file->file)) {
        result = fs_truncate(file, file->position);
        if (result != FS_OK) return result;
    }
    io_result = FS_OK;
    result = fs_result(f_lseek(&file->file, file->position));
    if (result == FS_OK) result = fs_result(f_write(&file->file, buf, count, &actual));
    file->position += actual;
    *written = actual;
    if (result != FS_OK) invalidated = true;
    if (result == FS_OK && actual != count) return FS_NO_SPACE;
    return result;
}

FsResult fs_sync(FsFile *file) {
    FsResult result = check_file(file);
    if (result != FS_OK) return result;
    io_result = FS_OK;
    synced = false;
    result = fs_result(f_sync(&file->file));
    /* f_sync skips CTRL_SYNC when this file has no modified metadata. */
    if (result == FS_OK && !synced && (file->flags & FS_OPEN_WRITE))
        result = fs_result(disk_ioctl(0, CTRL_SYNC, NULL) == RES_OK ? FR_OK : FR_DISK_ERR);
    if (result != FS_OK) invalidated = true;
    return result;
}

FsResult fs_truncate(FsFile *file, uint64_t size) {
    FsResult result = check_file(file);
    if (result != FS_OK) return result;
    if (!(file->flags & FS_OPEN_WRITE)) return FS_DENIED;
    if (size > volume_info.max_file_size) return FS_TOO_LARGE;
    io_result = FS_OK;
    if (size <= f_size(&file->file)) {
        result = fs_result(f_lseek(&file->file, size));
        if (result == FS_OK) result = fs_result(f_truncate(&file->file));
    } else {
        uint8_t *zeroes = calloc(1, 32768);
        if (!zeroes) return FS_NO_MEMORY;
        result = fs_result(f_lseek(&file->file, f_size(&file->file)));
        while (result == FS_OK && f_size(&file->file) < size) {
            UINT actual = 0;
            uint64_t remaining = size - f_size(&file->file);
            UINT count = remaining > 32768 ? 32768 : (UINT)remaining;
            result = fs_result(f_write(&file->file, zeroes, count, &actual));
            if (result == FS_OK && actual != count) result = FS_NO_SPACE;
        }
        free(zeroes);
    }
    if (result != FS_OK && result != FS_NO_SPACE) invalidated = true;
    return result;
}

FsResult fs_seek(FsFile *file, uint64_t absolute_offset) {
    FsResult result = check_file(file);
    if (result != FS_OK) return result;
    if (absolute_offset > f_size(&file->file)) absolute_offset = f_size(&file->file);
    file->position = absolute_offset;
    return FS_OK;
}

uint64_t fs_tell(FsFile *file) {
    return check_file(file) == FS_OK ? file->position : 0;
}

uint64_t fs_size(FsFile *file) {
    return check_file(file) == FS_OK ? f_size(&file->file) : 0;
}

FsResult fs_close(FsFile *file) {
    FsResult result;
    if (!file) return FS_OK;
    result = fs_sync(file);
    if (result == FS_OK) {
        io_result = FS_OK;
        result = fs_result(f_close(&file->file));
    }
    /* A failed close cannot leave a live FatFs lock or dirty cache behind. */
    if (result != FS_OK && file->epoch == epoch) invalidated = true;
    free(file);
    return result;
}

FsResult fs_mkdir(const char *path) {
    FsResult result = writable_path(path);
    if (result != FS_OK) return result;
    io_result = FS_OK;
    result = fs_result(f_mkdir(path));
    if (result == FS_OK) ++revision;
    return result;
}

FsResult fs_unlink(const char *path) {
    FsResult result = writable_path(path);
    if (result != FS_OK) return result;
    io_result = FS_OK;
    result = fs_result(f_unlink(path));
    if (result == FS_OK) ++revision;
    return result;
}

FsResult fs_rename(const char *from, const char *to) {
    FsResult result = writable_path(from);
    if (result == FS_OK) result = check_path(to);
    if (result != FS_OK) return result;
    io_result = FS_OK;
    result = fs_result(f_rename(from, to));
    if (result == FS_OK) ++revision;
    return result;
}

FsResult fs_chmod(const char *path, uint8_t attributes) {
    FsResult result = writable_path(path);
    if (result != FS_OK) return result;
    io_result = FS_OK;
    return fs_result(f_chmod(path, attributes, AM_RDO | AM_HID | AM_SYS | AM_ARC));
}

FsResult fs_utime(const char *path, const FsTimes *times) {
    FsResult result = writable_path(path);
    if (result != FS_OK) return result;
    if (!times) return FS_INVALID_ARGUMENT;
    memset(&info, 0, sizeof(info));
    info.acdate = times->access_date;
    info.fdate = times->modified_date;
    info.ftime = times->modified_time;
    info.crdate = times->created_date;
    info.crtime = times->created_time;
    io_result = FS_OK;
    return fs_result(f_settimes(path, &info));
}
