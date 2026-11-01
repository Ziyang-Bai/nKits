#ifndef NUSB_BOT_H
#define NUSB_BOT_H

#include "usb_msc.h"

#define BOT_TRANSFER_LIMIT 32768u
#define BOT_TIMEOUT_MS 5000u

typedef struct {
    uint8_t type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} UsbRequest;

typedef struct {
    void *ctx;
    /* OUT buffers are borrowed read-only; only IN transfers may modify buf. */
    MscResult (*bulk)(void *ctx, uint8_t endpoint, void *buf, uint32_t len,
                      uint32_t *actual);
    MscResult (*control)(void *ctx, const UsbRequest *req, void *data,
                         uint32_t *actual);
    MscResult (*clear_halt)(void *ctx, uint8_t endpoint);
} UsbIo;

typedef struct {
    uint8_t key;
    uint8_t asc;
    uint8_t ascq;
} BotSense;

typedef struct {
    UsbIo io;
    uint8_t interface_number;
    uint8_t ep_in;
    uint8_t ep_out;
    uint8_t max_lun;
    uint32_t tag;
    uint32_t transferred;
    uint32_t residue;
    uint32_t sector_size;
    uint64_t sector_count;
    bool needs_reset;
    bool sync_optional;
    uint8_t cbw[31];
    uint8_t csw[13];
} Bot;

void bot_init(Bot *bot, const UsbIo *io, uint8_t interface_number,
              uint8_t ep_in, uint8_t ep_out);
MscResult bot_exec(Bot *bot, const uint8_t *cdb, uint8_t cdb_len,
                   void *data, uint32_t len);
MscResult bot_sense(Bot *bot, BotSense *sense);
MscResult bot_capacity(Bot *bot, MscInfo *info);
MscResult bot_probe(Bot *bot, MscInfo *info);
MscResult bot_read(Bot *bot, uint32_t lba, uint32_t count, void *buf);
MscResult bot_write(Bot *bot, uint32_t lba, uint32_t count, const void *buf);
MscResult bot_sync(Bot *bot);

#endif
