#ifndef NCAPTURE_CAPTURE_H
#define NCAPTURE_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#define CAPTURE_WIDTH 320
#define CAPTURE_HEIGHT 240
#define CAPTURE_DIALOG_MAX 256
#define CAPTURE_PATH_MAX 544
#define CAPTURE_PREVIEW_WIDTH 200
#define CAPTURE_PREVIEW_HEIGHT 150
/* OS 6.4 get_event reports Ctrl+. as key 0xfc with ascii=0. */
#define CAPTURE_PERIOD_KEY 0xfcu

typedef struct {
    int held;
    unsigned key;
} CaptureHotkey;

int capture_hotkey(CaptureHotkey *hotkey, unsigned type, unsigned ascii,
                   unsigned key, unsigned modifiers);
#define CAPTURE_LAST_NUMBER 999999u

typedef struct {
    int (*open)(const char *path, int flags, int mode);
    int (*write)(int fd, const void *data, unsigned size);
    int (*close)(int fd);
    int (*remove)(const char *path);
    int (*error)(void);
    int (*rename)(const char *from, const char *to);
} CaptureIo;

typedef enum {
    CAPTURE_OK,
    CAPTURE_BAD_PATH,
    CAPTURE_NO_NAMES,
    CAPTURE_OPEN_ERROR,
    CAPTURE_WRITE_ERROR,
    CAPTURE_CLOSE_ERROR,
    CAPTURE_RENAME_ERROR,
    CAPTURE_BACKUP_ERROR,
    CAPTURE_DIALOG_ERROR
} CaptureCode;

typedef struct {
    CaptureCode code;
    int error;
    int cleanup_error;
    int backup_error;
} CaptureResult;

int capture_bmp(const CaptureIo *io, int fd, const uint16_t *pixels,
                unsigned width, unsigned height);
int capture_dialog_path(const char *folder, const char *name,
                        char *path, size_t size);
/* Replacement permission comes from an explicit user confirmation. */
CaptureResult capture_save_path(const CaptureIo *io, const char *path, int replace,
                                const uint16_t *pixels, unsigned width, unsigned height,
                                char *temporary, char *backup, size_t path_size);
void capture_preview(const uint16_t *shot, const uint16_t *frame,
                     volatile uint16_t *screen);

#endif
