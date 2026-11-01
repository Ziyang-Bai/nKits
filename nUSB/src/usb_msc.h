#ifndef NUSB_USB_MSC_H
#define NUSB_USB_MSC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MSC_OK, MSC_NO_DEVICE, MSC_UNSUPPORTED, MSC_TIMEOUT, MSC_IO_ERROR,
    MSC_PROTOCOL_ERROR, MSC_CANCELLED, MSC_BUSY, MSC_STALLED, MSC_COMMAND_FAILED,
    MSC_WRITE_PROTECTED
} MscResult;

typedef struct {
    bool connected;
    bool ready;
    uint32_t generation;
    uint32_t sector_size;
    uint64_t sector_count;
} MscInfo;

typedef struct {
    bool (*pump)(void *ctx);
    void *ctx;
} MscHooks;

MscResult msc_open(MscInfo *info, const MscHooks *hooks);
MscResult msc_read(uint32_t lba, uint32_t count, void *buf);
MscResult msc_write(uint32_t lba, uint32_t count, const void *buf);
MscResult msc_sync(void);
void msc_close(void);
void msc_poll(MscInfo *info);
/* Wait about 10ms with OS interrupts enabled; return elapsed 32768Hz ticks. */
unsigned msc_wait(void);

#endif
