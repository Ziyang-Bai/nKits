#include "bot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

typedef struct {
    Bot bot;
    unsigned phase;
    uint8_t cdb[16];
    uint32_t tag;
    uint32_t len;
    uint32_t residue;
    uint8_t status;
    uint32_t sector_size;
    uint32_t last_lba;
    uint8_t lun;
    uint8_t inquiry_type;
    BotSense sense;
    bool descriptor_sense;
    unsigned ready_failures;
    unsigned ready_count;
    unsigned sense_count;
    unsigned read_count;
    unsigned write_count;
    unsigned sync_count;
    uint32_t write_lba[16];
    uint32_t write_blocks[16];
    const uint8_t *write_data;
    uint32_t write_size;
    uint32_t write_bytes;
    unsigned command_count;
    uint32_t read_lba[16];
    uint32_t read_blocks[16];
    uint8_t fail_opcode;
    unsigned fail_count;
    MscResult cbw_result;
    MscResult data_result;
    uint8_t data_opcode;
    MscResult csw_result;
    MscResult control_result;
    MscResult clear_result;
    unsigned csw_stalls;
    bool sync_unsupported;
    bool mode6_unsupported;
    uint8_t mode6[192];
    uint8_t mode10[192];
    uint32_t mode6_size;
    uint32_t mode10_size;
    unsigned csw_count;
    bool short_cbw;
    bool short_csw;
    bool bad_tag;
    bool bad_signature;
    int status_override;
    uint32_t residue_override;
    uint32_t data_limit;
    bool data_overrun;
    uint32_t control_actual;
    unsigned now_ms;
    unsigned cancel_ms;
    char events[512];
    unsigned event_count;
} Device;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static void store_le32(uint8_t *p, uint32_t value) {
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static void store_be32(uint8_t *p, uint32_t value) {
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (24 - 8 * i));
}

static void event(Device *dev, char code) {
    CHECK(dev->event_count + 1 < sizeof(dev->events));
    dev->events[dev->event_count++] = code;
    dev->events[dev->event_count] = 0;
}

static MscResult timed_result(Device *dev, MscResult result) {
    unsigned elapsed;
    if (result != MSC_TIMEOUT && result != MSC_CANCELLED) return result;
    for (elapsed = 0; elapsed < BOT_TIMEOUT_MS; elapsed += 10) {
        dev->now_ms += 10;
        if (result == MSC_CANCELLED && elapsed + 10 >= dev->cancel_ms) {
            return MSC_CANCELLED;
        }
    }
    return MSC_TIMEOUT;
}

static MscResult bulk(void *ctx, uint8_t endpoint, void *buf, uint32_t len,
                      uint32_t *actual) {
    Device *dev = ctx;
    uint8_t *p = buf;
    unsigned i;
    *actual = 0;
    if (dev->phase == 0) {
        CHECK(endpoint == 0x03 && len == 31);
        CHECK(le32(p) == 0x43425355);
        CHECK(p[13] == 0);
        CHECK(p[14] == 6 || p[14] == 10);
        for (i = 15 + p[14]; i < 31; ++i) CHECK(p[i] == 0);
        dev->tag = le32(p + 4);
        dev->len = le32(p + 8);
        CHECK(dev->len <= BOT_TRANSFER_LIMIT);
        CHECK(p[12] == (dev->len && p[15] != 0x2a ? 0x80 : 0));
        memcpy(dev->cdb, p + 15, 16);
        event(dev, 'C');
        ++dev->command_count;
        if (dev->cbw_result != MSC_OK) return timed_result(dev, dev->cbw_result);
        dev->status = 0;
        if (dev->cdb[0] == 0x00) {
            ++dev->ready_count;
            if (dev->ready_failures) {
                --dev->ready_failures;
                dev->status = 1;
            }
        }
        if (dev->cdb[0] == dev->fail_opcode && dev->fail_count) {
            --dev->fail_count;
            dev->status = 1;
        }
        if ((dev->cdb[0] == 0x35 && dev->sync_unsupported) ||
            (dev->cdb[0] == 0x1a && dev->mode6_unsupported)) dev->status = 1;
        if (dev->cdb[0] == 0x28) {
            uint32_t blocks = (uint32_t)dev->cdb[7] << 8 | dev->cdb[8];
            CHECK(blocks != 0 && blocks * dev->sector_size == dev->len);
            CHECK(dev->read_count < 16);
            dev->read_lba[dev->read_count] = be32(dev->cdb + 2);
            dev->read_blocks[dev->read_count++] = blocks;
        }
        if (dev->cdb[0] == 0x2a) {
            uint32_t blocks = (uint32_t)dev->cdb[7] << 8 | dev->cdb[8];
            CHECK(p[14] == 10 && blocks != 0);
            CHECK(dev->cdb[1] == 0 && dev->cdb[6] == 0 && dev->cdb[9] == 0);
            CHECK(blocks * dev->sector_size == dev->len);
            CHECK(dev->write_count < 16);
            dev->write_lba[dev->write_count] = be32(dev->cdb + 2);
            dev->write_blocks[dev->write_count++] = blocks;
        }
        if (dev->cdb[0] == 0x35) {
            CHECK(p[14] == 10 && dev->len == 0);
            for (i = 1; i < 10; ++i) CHECK(dev->cdb[i] == 0);
            ++dev->sync_count;
        }
        dev->residue = dev->status ? dev->len : 0;
        dev->phase = dev->len ? 1 : 2;
        *actual = dev->short_cbw ? 30 : 31;
        return MSC_OK;
    }
    CHECK(endpoint == (dev->phase == 1 && dev->cdb[0] == 0x2a ? 0x03 : 0x85));
    if (dev->phase == 1) {
        uint32_t n = dev->len;
        CHECK(len == dev->len);
        event(dev, 'D');
        dev->phase = 2;
        if (dev->data_result != MSC_OK &&
            (!dev->data_opcode || dev->cdb[0] == dev->data_opcode)) {
            dev->residue = dev->len;
            return timed_result(dev, dev->data_result);
        }
        if (dev->status && dev->cdb[0] != 0x2a) n = 0;
        if (n > dev->data_limit) n = dev->data_limit;
        if (dev->cdb[0] == 0x2a) {
            CHECK(dev->write_data && dev->write_bytes + len <= dev->write_size);
            CHECK(memcmp(p, dev->write_data + dev->write_bytes, len) == 0);
            dev->write_bytes += n;
            *actual = dev->data_overrun ? len + 1 : n;
            dev->residue = dev->status ? len : len - n;
            return MSC_OK;
        }
        memset(p, 0, len);
        switch (dev->cdb[0]) {
            case 0x12:
                CHECK(len == 36 && dev->cdb[4] == 36);
                p[0] = dev->inquiry_type;
                p[2] = 5;
                p[3] = 2;
                p[4] = 31;
                memcpy(p + 8, "TESTDISK", 8);
                break;
            case 0x03:
                CHECK(len == 18 && dev->cdb[4] == 18);
                ++dev->sense_count;
                if (dev->descriptor_sense) {
                    p[0] = 0x72;
                    p[1] = dev->sense.key;
                    p[2] = dev->sense.asc;
                    p[3] = dev->sense.ascq;
                } else {
                    p[0] = 0x70;
                    p[2] = dev->sense.key;
                    p[7] = 10;
                    p[12] = dev->sense.asc;
                    p[13] = dev->sense.ascq;
                }
                break;
            case 0x25:
                CHECK(len == 8);
                store_be32(p, dev->last_lba);
                store_be32(p + 4, dev->sector_size);
                break;
            case 0x1a:
                CHECK(dev->cdb[2] == 8 && dev->cdb[1] == 8);
                n = dev->mode6_size;
                if (n > len) n = len;
                memcpy(p, dev->mode6, n);
                break;
            case 0x5a:
                CHECK(dev->cdb[2] == 8 && dev->cdb[1] == 8);
                n = dev->mode10_size;
                if (n > len) n = len;
                memcpy(p, dev->mode10, n);
                break;
            case 0x28:
                for (i = 0; i < n; ++i) {
                    p[i] = (uint8_t)(be32(dev->cdb + 2) +
                                     i / dev->sector_size + i % dev->sector_size);
                }
                break;
            default: CHECK(false);
        }
        *actual = n;
        dev->residue = len - n;
        return MSC_OK;
    }
    CHECK(dev->phase == 2 && len == 13);
    event(dev, 'S');
    ++dev->csw_count;
    if (dev->csw_stalls) {
        --dev->csw_stalls;
        return MSC_STALLED;
    }
    if (dev->csw_result != MSC_OK) return timed_result(dev, dev->csw_result);
    memset(p, 0, len);
    store_le32(p, dev->bad_signature ? 0 : 0x53425355);
    store_le32(p + 4, dev->bad_tag ? dev->tag + 1 : dev->tag);
    store_le32(p + 8, dev->residue_override == UINT32_MAX ?
               dev->residue : dev->residue_override);
    p[12] = dev->status_override < 0 ? dev->status : (uint8_t)dev->status_override;
    dev->phase = 0;
    *actual = dev->short_csw ? 12 : 13;
    return MSC_OK;
}

static MscResult control(void *ctx, const UsbRequest *req, void *buf,
                         uint32_t *actual) {
    Device *dev = ctx;
    CHECK(req->value == 0 && req->index == 4);
    *actual = 0;
    if (req->request == 0xfe) {
        CHECK(req->type == 0xa1 && req->length == 1);
        event(dev, 'Q');
        if (dev->control_result != MSC_OK) return timed_result(dev, dev->control_result);
        *(uint8_t *)buf = dev->lun;
        *actual = dev->control_actual;
    } else {
        CHECK(req->request == 0xff && req->type == 0x21 && req->length == 0);
        CHECK(buf == NULL);
        event(dev, 'R');
        dev->phase = 0;
    }
    return MSC_OK;
}

static MscResult clear_halt(void *ctx, uint8_t endpoint) {
    Device *dev = ctx;
    CHECK(endpoint == 0x85 || endpoint == 0x03);
    event(dev, endpoint == 0x85 ? 'I' : 'O');
    return dev->clear_result;
}

static void init(Device *dev) {
    UsbIo io = {dev, bulk, control, clear_halt};
    memset(dev, 0, sizeof(*dev));
    dev->sector_size = 512;
    dev->last_lba = 131071;
    dev->sense.key = 2;
    dev->sense.asc = 4;
    dev->status_override = -1;
    dev->residue_override = UINT32_MAX;
    dev->data_limit = UINT32_MAX;
    dev->control_actual = 1;
    dev->cancel_ms = 30;
    bot_init(&dev->bot, &io, 4, 0x85, 0x03);
}

static MscResult inquiry(Device *dev) {
    const uint8_t cdb[6] = {0x12, 0, 0, 0, 36, 0};
    uint8_t data[36];
    return bot_exec(&dev->bot, cdb, sizeof(cdb), data, sizeof(data));
}

static void test_probe(void) {
    Device dev;
    MscInfo info = {true, false, 17, 0, 0};
    unsigned i;
    for (i = 512; i <= 4096; i *= 2) {
        init(&dev);
        dev.sector_size = i;
        dev.lun = 3;
        CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
        CHECK(info.ready && info.connected && info.generation == 17);
        CHECK(info.sector_size == i && info.sector_count == 131072);
        CHECK(dev.bot.max_lun == 3 && dev.bot.tag == 3);
        CHECK(strcmp(dev.events, "QCDSCSCDS") == 0);
    }
    init(&dev);
    dev.control_result = MSC_STALLED;
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    CHECK(dev.bot.max_lun == 0);
    CHECK(strchr(dev.events, 'I') == NULL);
    init(&dev);
    dev.control_result = MSC_IO_ERROR;
    CHECK(bot_probe(&dev.bot, &info) == MSC_IO_ERROR);
    CHECK(dev.command_count == 0);
    init(&dev);
    dev.control_actual = 0;
    CHECK(bot_probe(&dev.bot, &info) == MSC_PROTOCOL_ERROR);
    init(&dev);
    dev.lun = 16;
    CHECK(bot_probe(&dev.bot, &info) == MSC_PROTOCOL_ERROR);
    init(&dev);
    dev.inquiry_type = 5;
    CHECK(bot_probe(&dev.bot, &info) == MSC_UNSUPPORTED);
    init(&dev);
    dev.sector_size = 520;
    CHECK(bot_probe(&dev.bot, &info) == MSC_UNSUPPORTED);
    CHECK(!info.ready && !dev.bot.sector_count);
    init(&dev);
    dev.last_lba = UINT32_MAX;
    CHECK(bot_probe(&dev.bot, &info) == MSC_UNSUPPORTED);
    init(&dev);
    dev.last_lba = UINT32_MAX - 1;
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    CHECK(info.sector_count == (uint64_t)UINT32_MAX);
}

static void test_sense(void) {
    Device dev;
    MscInfo info = {0};
    BotSense sense;
    init(&dev);
    dev.ready_failures = 2;
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    CHECK(dev.ready_count == 3 && dev.sense_count == 2);
    init(&dev);
    dev.ready_failures = 4;
    dev.sense.key = 6;
    CHECK(bot_probe(&dev.bot, &info) == MSC_BUSY);
    CHECK(dev.ready_count == 3 && dev.sense_count == 3 && !info.ready);
    init(&dev);
    dev.ready_failures = 1;
    dev.sense.asc = 0x3a;
    CHECK(bot_probe(&dev.bot, &info) == MSC_NO_DEVICE);
    CHECK(dev.ready_count == 1);
    init(&dev);
    dev.fail_opcode = 0x12;
    dev.fail_count = 1;
    dev.sense.key = 5;
    CHECK(bot_probe(&dev.bot, &info) == MSC_UNSUPPORTED);
    init(&dev);
    dev.descriptor_sense = true;
    dev.sense.key = 6;
    dev.sense.asc = 0x29;
    dev.sense.ascq = 1;
    CHECK(bot_sense(&dev.bot, &sense) == MSC_OK);
    CHECK(sense.key == 6 && sense.asc == 0x29 && sense.ascq == 1);
    init(&dev);
    dev.fail_opcode = 0x03;
    dev.fail_count = 1;
    CHECK(bot_sense(&dev.bot, &sense) == MSC_COMMAND_FAILED);
    CHECK(dev.sense_count == 1);
    init(&dev);
    dev.data_limit = 7;
    CHECK(bot_sense(&dev.bot, &sense) == MSC_PROTOCOL_ERROR);
}

static void test_reads(void) {
    Device dev;
    MscInfo info = {0};
    uint8_t data[BOT_TRANSFER_LIMIT + 4096];
    uint32_t size, i, blocks;
    for (size = 512; size <= 4096; size *= 2) {
        init(&dev);
        dev.sector_size = size;
        CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
        blocks = BOT_TRANSFER_LIMIT / size + 1;
        CHECK(bot_read(&dev.bot, 7, blocks, data) == MSC_OK);
        CHECK(dev.read_count == 2);
        CHECK(dev.read_lba[0] == 7 && dev.read_blocks[0] == blocks - 1);
        CHECK(dev.read_lba[1] == 7 + blocks - 1 && dev.read_blocks[1] == 1);
        for (i = 0; i < blocks * size; ++i) {
            CHECK(data[i] == (uint8_t)(7 + i / size + i % size));
        }
        CHECK(bot_read(&dev.bot, 0, 0, NULL) == MSC_OK);
        CHECK(bot_read(&dev.bot, dev.last_lba, 2, data) == MSC_UNSUPPORTED);
        CHECK(bot_read(&dev.bot, UINT32_MAX, 2, data) == MSC_UNSUPPORTED);
        CHECK(bot_read(&dev.bot, 0, 1, NULL) == MSC_UNSUPPORTED);
        CHECK(dev.read_count == 2);
    }
    init(&dev);
    CHECK(bot_read(&dev.bot, 0, 1, data) == MSC_NO_DEVICE);
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    dev.fail_opcode = 0x28;
    dev.fail_count = 1;
    dev.sense.key = 3;
    CHECK(bot_read(&dev.bot, 0, 1, data) == MSC_COMMAND_FAILED);
    dev.data_limit = 256;
    CHECK(bot_read(&dev.bot, 0, 1, data) == MSC_PROTOCOL_ERROR);
}

static void test_recovery(void) {
    Device dev;
    unsigned i;
    for (i = 0; i < 7; ++i) {
        init(&dev);
        switch (i) {
            case 0: dev.short_csw = true; break;
            case 1: dev.bad_tag = true; break;
            case 2: dev.bad_signature = true; break;
            case 3: dev.status_override = 2; break;
            case 4: dev.status_override = 3; break;
            case 5: dev.residue_override = 37; break;
            case 6: dev.short_cbw = true; break;
        }
        CHECK(inquiry(&dev) == MSC_PROTOCOL_ERROR);
        CHECK(strcmp(dev.events + strlen(dev.events) - 3, "RIO") == 0);
        CHECK(!dev.bot.needs_reset);
    }
    init(&dev);
    dev.data_result = MSC_STALLED;
    dev.status_override = 1;
    CHECK(inquiry(&dev) == MSC_COMMAND_FAILED);
    CHECK(strcmp(dev.events, "CDIS") == 0);
    CHECK(!dev.bot.needs_reset);
    init(&dev);
    dev.csw_stalls = 1;
    CHECK(inquiry(&dev) == MSC_OK);
    CHECK(strcmp(dev.events, "CDSIS") == 0 && dev.csw_count == 2);
    init(&dev);
    dev.csw_stalls = 2;
    CHECK(inquiry(&dev) == MSC_PROTOCOL_ERROR);
    CHECK(strcmp(dev.events, "CDSISRIO") == 0 && dev.csw_count == 2);
    init(&dev);
    dev.cbw_result = MSC_STALLED;
    CHECK(inquiry(&dev) == MSC_STALLED);
    CHECK(strcmp(dev.events, "CRIO") == 0);
    init(&dev);
    dev.data_result = MSC_STALLED;
    dev.clear_result = MSC_STALLED;
    CHECK(inquiry(&dev) == MSC_STALLED);
    CHECK(strcmp(dev.events, "CDI") == 0 && dev.bot.needs_reset);
    init(&dev);
    dev.data_limit = 8;
    dev.residue_override = 0;
    CHECK(inquiry(&dev) == MSC_PROTOCOL_ERROR);
    CHECK(strcmp(dev.events, "CDSRIO") == 0);
    init(&dev);
    dev.data_limit = 8;
    CHECK(inquiry(&dev) == MSC_OK);
    CHECK(dev.bot.transferred == 8 && dev.bot.residue == 28);
}

static void test_interruption(void) {
    const MscResult errors[] = {MSC_TIMEOUT, MSC_CANCELLED, MSC_NO_DEVICE, MSC_IO_ERROR};
    Device dev;
    MscInfo info = {0};
    unsigned i, phase;
    for (phase = 0; phase < 3; ++phase) {
        for (i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            init(&dev);
            if (phase == 0) dev.cbw_result = errors[i];
            if (phase == 1) dev.data_result = errors[i];
            if (phase == 2) dev.csw_result = errors[i];
            CHECK(inquiry(&dev) == errors[i]);
            CHECK(dev.command_count == 1 && dev.bot.needs_reset);
            CHECK(strchr(dev.events, 'R') == NULL);
            if (errors[i] == MSC_TIMEOUT) CHECK(dev.now_ms == 5000);
            if (errors[i] == MSC_CANCELLED) CHECK(dev.now_ms == 30);
            dev.cbw_result = dev.data_result = dev.csw_result = MSC_OK;
            dev.event_count = 0;
            CHECK(inquiry(&dev) == MSC_OK);
            CHECK(strcmp(dev.events, "RIOCDS") == 0);
        }
    }
    init(&dev);
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    dev.data_result = MSC_NO_DEVICE;
    CHECK(inquiry(&dev) == MSC_NO_DEVICE);
    /* A newly attached device starts its own BOT session. */
    init(&dev);
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    CHECK(strchr(dev.events, 'R') == NULL);
}

static void test_writes(void) {
    Device dev;
    MscInfo info = {0};
    uint8_t data[BOT_TRANSFER_LIMIT + 4096];
    uint32_t size, i, blocks;
    for (i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 17 + 3);
    for (size = 512; size <= 4096; size *= 2) {
        init(&dev);
        dev.sector_size = size;
        CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
        dev.write_data = data;
        dev.write_size = sizeof(data);
        blocks = BOT_TRANSFER_LIMIT / size + 1;
        CHECK(bot_write(&dev.bot, 7, blocks, data) == MSC_OK);
        CHECK(dev.write_count == 2 && dev.write_bytes == blocks * size);
        CHECK(dev.write_lba[0] == 7 && dev.write_blocks[0] == blocks - 1);
        CHECK(dev.write_lba[1] == 7 + blocks - 1 && dev.write_blocks[1] == 1);
        for (i = 0; i < sizeof(data); ++i) CHECK(data[i] == (uint8_t)(i * 17 + 3));
        CHECK(bot_write(&dev.bot, 0, 0, NULL) == MSC_OK);
        CHECK(bot_write(&dev.bot, dev.last_lba, 2, data) == MSC_UNSUPPORTED);
        CHECK(bot_write(&dev.bot, UINT32_MAX, 2, data) == MSC_UNSUPPORTED);
        CHECK(bot_write(&dev.bot, 0, 1, NULL) == MSC_UNSUPPORTED);
        CHECK(dev.write_count == 2);
        CHECK(bot_sync(&dev.bot) == MSC_OK && dev.sync_count == 1);
    }
    init(&dev);
    CHECK(bot_write(&dev.bot, 0, 1, data) == MSC_NO_DEVICE);
    CHECK(bot_sync(&dev.bot) == MSC_NO_DEVICE);
    dev.last_lba = UINT32_MAX - 1;
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    dev.write_data = data;
    dev.write_size = sizeof(data);
    CHECK(bot_write(&dev.bot, UINT32_MAX - 1, 1, data) == MSC_OK);
    CHECK(dev.write_lba[0] == UINT32_MAX - 1 && dev.write_blocks[0] == 1);
    CHECK(bot_write(&dev.bot, UINT32_MAX, 1, data) == MSC_UNSUPPORTED);
    CHECK(dev.write_count == 1);
}

static void test_write_errors(void) {
    static const uint8_t data[1024] = {1, 2, 3, 4};
    const MscResult errors[] = {MSC_PROTOCOL_ERROR, MSC_PROTOCOL_ERROR,
        MSC_PROTOCOL_ERROR, MSC_PROTOCOL_ERROR, MSC_PROTOCOL_ERROR,
        MSC_PROTOCOL_ERROR, MSC_TIMEOUT, MSC_TIMEOUT};
    const uint8_t keys[] = {7, 5, 3, 2, 6};
    const MscResult sense_errors[] = {MSC_WRITE_PROTECTED, MSC_UNSUPPORTED,
        MSC_COMMAND_FAILED, MSC_NO_DEVICE, MSC_BUSY};
    Device dev;
    MscInfo info = {0};
    unsigned i;
    for (i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        init(&dev);
        CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
        dev.write_data = data;
        dev.write_size = sizeof(data);
        switch (i) {
            case 0: dev.data_limit = 256; break;
            case 1: dev.data_limit = 256; dev.residue_override = 0; break;
            case 2: dev.residue_override = 1; break;
            case 3: dev.data_overrun = true; break;
            case 4: dev.bad_tag = true; break;
            case 5: dev.status_override = 2; break;
            case 6: dev.csw_result = MSC_TIMEOUT; break;
            case 7: dev.data_result = MSC_TIMEOUT; break;
        }
        CHECK(bot_write(&dev.bot, 2, 1, data) == errors[i]);
        CHECK(dev.write_count == 1);
        dev.data_limit = dev.residue_override = UINT32_MAX;
        dev.data_overrun = dev.bad_tag = false;
        dev.status_override = -1;
        dev.csw_result = dev.data_result = MSC_OK;
        CHECK(bot_sync(&dev.bot) == MSC_OK);
        CHECK(dev.sync_count == 1 && dev.write_count == 1);
    }
    for (i = 0; i < sizeof(keys); ++i) {
        init(&dev);
        CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
        dev.write_data = data;
        dev.write_size = sizeof(data);
        dev.fail_opcode = 0x2a;
        dev.fail_count = 1;
        dev.sense.key = keys[i];
        dev.sense.asc = 0x3a;
        CHECK(bot_write(&dev.bot, 2, 1, data) == sense_errors[i]);
        CHECK(dev.write_count == 1 && dev.sense_count == 1);
        CHECK(bot_sync(&dev.bot) == MSC_OK);
        CHECK(dev.write_count == 1);
    }
    init(&dev);
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    dev.write_data = data;
    dev.write_size = sizeof(data);
    dev.csw_stalls = 1;
    dev.event_count = 0;
    CHECK(bot_write(&dev.bot, 0, 1, data) == MSC_OK);
    CHECK(dev.write_count == 1 && strcmp(dev.events, "CDSIS") == 0);
    {
        const uint8_t cdb[10] = {0x2a, 0, 0, 0, 0, 1, 0, 0, 1, 0};
        dev.data_result = MSC_STALLED;
        dev.status_override = 1;
        dev.event_count = 0;
        CHECK(bot_exec(&dev.bot, cdb, sizeof(cdb), (void *)data, 512) ==
              MSC_COMMAND_FAILED);
        CHECK(strcmp(dev.events, "CDOS") == 0 && !dev.bot.needs_reset);
        CHECK(dev.write_count == 2);
    }
}

static void test_sync(void) {
    static const uint8_t data[512] = {9, 8, 7, 6};
    Device dev;
    MscInfo info = {0};
    unsigned i;
    const MscResult errors[] = {MSC_UNSUPPORTED, MSC_WRITE_PROTECTED,
        MSC_COMMAND_FAILED, MSC_TIMEOUT, MSC_NO_DEVICE, MSC_PROTOCOL_ERROR};
    for (i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        init(&dev);
        CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
        dev.write_data = data;
        dev.write_size = sizeof(data);
        CHECK(bot_write(&dev.bot, 0, 1, data) == MSC_OK);
        dev.fail_opcode = 0x35;
        if (i < 3) {
            dev.fail_count = 1;
            dev.sense.key = i == 0 ? 5 : i == 1 ? 7 : 3;
        } else if (i == 3) {
            dev.csw_result = MSC_TIMEOUT;
        } else if (i == 4) {
            dev.csw_result = MSC_NO_DEVICE;
        } else {
            dev.residue_override = 1;
        }
        CHECK(bot_sync(&dev.bot) == errors[i]);
        CHECK(dev.write_count == 1 && dev.sync_count == 1);
        dev.csw_result = MSC_OK;
        dev.residue_override = UINT32_MAX;
        dev.event_count = 0;
        CHECK(bot_sync(&dev.bot) == MSC_OK);
        CHECK(dev.write_count == 1 && dev.sync_count == 2);
        CHECK(strcmp(dev.events, i == 3 || i == 4 ? "RIOCS" : "CS") == 0);
    }
}

static void test_sync_without_cache(void) {
    /* SanDisk MODE SENSE from the physical-device diagnostic report. */
    static const uint8_t mode6[68] = {
        0x43, 0, 0, 0, 8, 0x12,
        [24] = 0x1c, 0x0a, [36] = 5, 0x1e,
        0, 0, 0x10, 0x3f, 2, 0, 0xe9
    };
    static const uint8_t data[512] = {1, 2, 3, 4};
    Device dev;
    uint8_t readback[512];
    MscInfo info = {0};
    init(&dev);
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    dev.write_data = data;
    dev.write_size = sizeof(data);
    dev.sync_unsupported = true;
    dev.sense = (BotSense){5, 0x20, 0};
    memcpy(dev.mode6, mode6, sizeof(mode6));
    dev.mode6_size = sizeof(mode6);
    CHECK(bot_write(&dev.bot, 2, 1, data) == MSC_OK);
    CHECK(bot_sync(&dev.bot) == MSC_OK);
    CHECK(bot_read(&dev.bot, 2, 1, readback) == MSC_OK);
    CHECK(bot_sync(&dev.bot) == MSC_OK);
    CHECK(dev.write_count == 1 && dev.sync_count == 1);
}

static void test_sync_cache_pages(void) {
    static const struct {
        uint8_t mode6[42];
        uint8_t mode10[46];
        uint32_t size6, size10;
        MscResult result;
    } cases[] = {
        {{23, 0, 0, 0, 8, 0x12},
         {0, 26, 0, 0, 0, 0, 0, 0, 8, 0x12}, 24, 28, MSC_OK},
        {{23, 0, 0, 0, 8, 0x12, 4},
         {0, 26, 0, 0, 0, 0, 0, 0, 8, 0x12, 4}, 24, 28, MSC_UNSUPPORTED},
        /* Short headers, truncated pages, and short caching pages. */
        {{23}, {0, 26}, 3, 7, MSC_PROTOCOL_ERROR},
        {{23, 0, 0, 0, 8, 0x12},
         {0, 26, 0, 0, 0, 0, 0, 0, 8, 0x12}, 23, 27, MSC_PROTOCOL_ERROR},
        {{22, 0, 0, 0, 8, 0x11},
         {0, 25, 0, 0, 0, 0, 0, 0, 8, 0x11}, 23, 27, MSC_PROTOCOL_ERROR},
        {{7, 0, 0, 0, 0x1c, 2},
         {0, 10, 0, 0, 0, 0, 0, 0, 0x1c, 2}, 8, 12, MSC_UNSUPPORTED},
        {{3}, {0, 6}, 4, 8, MSC_UNSUPPORTED},
        /* Descriptor lengths and reported totals cannot leave the response. */
        {{23, 0, 0, 24, 8, 0x12},
         {0, 26, 0, 0, 0, 0, 1, 0, 8, 0x12}, 24, 28, MSC_PROTOCOL_ERROR},
        {{2, 0, 0, 0, 8, 0x12},
         {0, 5, 0, 0, 0, 0, 0, 0, 8, 0x12}, 24, 28, MSC_PROTOCOL_ERROR},
        {{4, 0, 0, 0, 8},
         {0, 7, 0, 0, 0, 0, 0, 0, 8}, 5, 9, MSC_PROTOCOL_ERROR},
        {{6, 0, 0, 0, 0x48},
         {0, 9, 0, 0, 0, 0, 0, 0, 0x48}, 7, 11, MSC_PROTOCOL_ERROR},
        /* A caching subpage is not the WCE-bearing caching page. */
        {{25, 0, 0, 0, 0x48, 0, 0, 0x12},
         {0, 28, 0, 0, 0, 0, 0, 0, 0x48, 0, 0, 0x12},
         26, 30, MSC_UNSUPPORTED},
        /* Skip descriptors, an ordinary page, and a subpage before WCE. */
        {{41, 0, 0, 8, 8, 0x12, 4, [12] = 0x1c, 2,
          [16] = 0x41, 1, 0, 2, [22] = 8, 0x12},
         {0, 44, 0, 0, 0, 0, 0, 8, 8, 0x12, 4, [16] = 0x1c, 2,
          [20] = 0x41, 1, 0, 2, [26] = 8, 0x12}, 42, 46, MSC_OK},
        {{41, 0, 0, 8, 8, 0x12, [12] = 0x1c, 2,
          [16] = 0x41, 1, 0, 2, [22] = 8, 0x12, 4},
         {0, 44, 0, 0, 0, 0, 0, 8, 8, 0x12, [16] = 0x1c, 2,
          [20] = 0x41, 1, 0, 2, [26] = 8, 0x12, 4},
         42, 46, MSC_UNSUPPORTED}
    };
    static const uint8_t data[512] = {1, 2, 3, 4};
    Device dev;
    MscInfo info = {0};
    unsigned i, mode;
    for (mode = 0; mode < 2; ++mode) {
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            init(&dev);
            CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
            dev.write_data = data;
            dev.write_size = sizeof(data);
            dev.sync_unsupported = true;
            dev.sense = (BotSense){5, 0x20, 0};
            if (mode) {
                dev.mode6_unsupported = true;
                memcpy(dev.mode10, cases[i].mode10, sizeof(cases[i].mode10));
                dev.mode10_size = cases[i].size10;
            } else {
                memcpy(dev.mode6, cases[i].mode6, sizeof(cases[i].mode6));
                dev.mode6_size = cases[i].size6;
                /* A rejected MODE6 page must not be overridden by MODE10. */
                memcpy(dev.mode10, cases[0].mode10, sizeof(cases[0].mode10));
                dev.mode10_size = cases[0].size10;
            }
            CHECK(bot_write(&dev.bot, 2, 1, data) == MSC_OK);
            CHECK(bot_sync(&dev.bot) == cases[i].result);
            CHECK(bot_sync(&dev.bot) == cases[i].result);
            CHECK(dev.write_count == 1 && dev.write_bytes == sizeof(data));
        }
    }
}

static void test_sync_cache_errors(void) {
    static const uint8_t mode6[24] = {23, 0, 0, 0, 8, 0x12};
    static const uint8_t mode10[28] = {0, 26, 0, 0, 0, 0, 0, 0, 8, 0x12};
    static const uint8_t data[512] = {1, 2, 3, 4};
    const BotSense senses[] = {{5, 0x24, 0}, {5, 0x20, 1},
        {3, 0x20, 0}, {7, 0x20, 0}};
    const MscResult sense_errors[] = {MSC_UNSUPPORTED, MSC_UNSUPPORTED,
        MSC_COMMAND_FAILED, MSC_WRITE_PROTECTED};
    const MscResult errors[] = {MSC_TIMEOUT, MSC_CANCELLED, MSC_NO_DEVICE, MSC_IO_ERROR};
    Device dev;
    MscInfo info = {0};
    unsigned i, phase;
    for (i = 0; i < sizeof(senses) / sizeof(senses[0]); ++i) {
        init(&dev);
        CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
        dev.write_data = data;
        dev.write_size = sizeof(data);
        dev.sync_unsupported = true;
        dev.sense = senses[i];
        memcpy(dev.mode6, mode6, sizeof(mode6));
        dev.mode6_size = sizeof(mode6);
        CHECK(bot_write(&dev.bot, 2, 1, data) == MSC_OK);
        CHECK(bot_sync(&dev.bot) == sense_errors[i]);
        CHECK(dev.write_count == 1);
    }
    for (phase = 0; phase < 5; ++phase) {
        for (i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            init(&dev);
            CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
            dev.write_data = data;
            dev.write_size = sizeof(data);
            dev.sync_unsupported = true;
            dev.sense = (BotSense){5, 0x20, 0};
            memcpy(dev.mode6, mode6, sizeof(mode6));
            dev.mode6_size = sizeof(mode6);
            memcpy(dev.mode10, mode10, sizeof(mode10));
            dev.mode10_size = sizeof(mode10);
            CHECK(bot_write(&dev.bot, 2, 1, data) == MSC_OK);
            if (phase == 0) dev.cbw_result = errors[i];
            if (phase == 1) dev.csw_result = errors[i];
            if (phase == 2) dev.data_result = errors[i]; /* REQUEST SENSE */
            if (phase == 3 || phase == 4) {
                dev.data_result = errors[i];
                dev.data_opcode = phase == 3 ? 0x1a : 0x5a;
            }
            if (phase == 4) {
                dev.mode6_unsupported = true;
                dev.mode6_size = 0;
            }
            CHECK(bot_sync(&dev.bot) == errors[i]);
            CHECK(dev.bot.needs_reset && dev.write_count == 1);
        }
    }
    for (i = 0; i < 3; ++i) {
        init(&dev);
        CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
        dev.write_data = data;
        dev.write_size = sizeof(data);
        dev.sync_unsupported = true;
        dev.sense = (BotSense){5, 0x20, 0};
        memcpy(dev.mode6, mode6, sizeof(mode6));
        dev.mode6_size = sizeof(mode6);
        CHECK(bot_write(&dev.bot, 2, 1, data) == MSC_OK);
        if (i == 0) {
            dev.fail_opcode = 0x03;
            dev.fail_count = 1;
        } else if (i == 1) {
            dev.data_limit = 7;
        } else {
            dev.mode6_unsupported = true;
            dev.mode6_size = 0;
            dev.fail_opcode = 0x5a;
            dev.fail_count = 1;
        }
        CHECK(bot_sync(&dev.bot) == (i == 0 ? MSC_COMMAND_FAILED :
              i == 1 ? MSC_PROTOCOL_ERROR : MSC_UNSUPPORTED));
        CHECK(dev.write_count == 1);
    }
}

static void test_sync_cache_reset(void) {
    static const uint8_t mode6[24] = {23, 0, 0, 0, 8, 0x12};
    static const uint8_t data[1024] = {1, 2, 3, 4};
    const MscResult errors[] = {MSC_TIMEOUT, MSC_CANCELLED, MSC_NO_DEVICE, MSC_IO_ERROR};
    Device dev;
    MscInfo info = {0};
    unsigned i, phase, writes;
    init(&dev);
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    dev.write_data = data;
    dev.write_size = sizeof(data);
    dev.sync_unsupported = true;
    dev.sense = (BotSense){5, 0x20, 0};
    memcpy(dev.mode6, mode6, sizeof(mode6));
    dev.mode6_size = sizeof(mode6);
    CHECK(bot_write(&dev.bot, 2, 1, data) == MSC_OK);
    CHECK(bot_sync(&dev.bot) == MSC_OK);
    dev.mode6[6] = 4;
    CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
    CHECK(bot_write(&dev.bot, 3, 1, data + 512) == MSC_OK);
    CHECK(bot_sync(&dev.bot) == MSC_UNSUPPORTED);
    CHECK(dev.sync_count == 2 && dev.write_count == 2);
    for (phase = 0; phase < 3; ++phase) {
        for (i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            init(&dev);
            CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
            dev.write_data = data;
            dev.write_size = sizeof(data);
            dev.sync_unsupported = true;
            dev.sense = (BotSense){5, 0x20, 0};
            memcpy(dev.mode6, mode6, sizeof(mode6));
            dev.mode6_size = sizeof(mode6);
            CHECK(bot_write(&dev.bot, 2, 1, data) == MSC_OK);
            CHECK(bot_sync(&dev.bot) == MSC_OK);
            if (phase == 0) dev.cbw_result = errors[i];
            if (phase == 1) dev.data_result = errors[i];
            if (phase == 2) dev.csw_result = errors[i];
            CHECK(bot_write(&dev.bot, 3, 1, data + 512) == errors[i]);
            CHECK(dev.bot.needs_reset);
            writes = dev.write_count;
            dev.cbw_result = dev.data_result = dev.csw_result = MSC_OK;
            dev.event_count = 0;
            CHECK(bot_sync(&dev.bot) == MSC_OK);
            CHECK(strncmp(dev.events, "RIO", 3) == 0 && !dev.bot.needs_reset);
            CHECK(dev.write_count == writes && dev.sync_count == 2);
            CHECK(bot_sync(&dev.bot) == MSC_OK && dev.sync_count == 2);
        }
    }
}

static void test_write_interruption(void) {
    static const uint8_t data[512] = {5, 4, 3, 2};
    const MscResult errors[] = {MSC_TIMEOUT, MSC_CANCELLED, MSC_NO_DEVICE, MSC_IO_ERROR};
    Device dev;
    MscInfo info = {0};
    unsigned i, phase;
    for (phase = 0; phase < 3; ++phase) {
        for (i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            init(&dev);
            CHECK(bot_probe(&dev.bot, &info) == MSC_OK);
            dev.write_data = data;
            dev.write_size = sizeof(data);
            dev.event_count = 0;
            if (phase == 0) dev.cbw_result = errors[i];
            if (phase == 1) dev.data_result = errors[i];
            if (phase == 2) dev.csw_result = errors[i];
            CHECK(bot_write(&dev.bot, 0, 1, data) == errors[i]);
            CHECK(dev.command_count == 4 && dev.bot.needs_reset);
            CHECK(strchr(dev.events, 'R') == NULL);
            CHECK(dev.write_count == (phase ? 1u : 0u));
            dev.cbw_result = dev.data_result = dev.csw_result = MSC_OK;
            dev.event_count = 0;
            CHECK(bot_sync(&dev.bot) == MSC_OK);
            CHECK(strcmp(dev.events, "RIOCS") == 0);
            CHECK(dev.command_count == 5 && dev.write_count == (phase ? 1u : 0u));
        }
    }
}

static void test_commands(void) {
    Device dev;
    uint8_t cdb[10] = {0x2a};
    uint8_t data[512];
    init(&dev);
    CHECK(bot_exec(&dev.bot, cdb, sizeof(cdb), data, sizeof(data)) == MSC_UNSUPPORTED);
    cdb[0] = 0x35;
    CHECK(bot_exec(&dev.bot, cdb, 6, NULL, 0) == MSC_UNSUPPORTED);
    CHECK(bot_exec(&dev.bot, cdb, 10, data, 512) == MSC_UNSUPPORTED);
    cdb[1] = 2; /* IMMED would not wait for the cache to reach media. */
    CHECK(bot_exec(&dev.bot, cdb, 10, NULL, 0) == MSC_UNSUPPORTED);
    cdb[1] = 0;
    cdb[0] = 0x04;
    CHECK(bot_exec(&dev.bot, cdb, 6, NULL, 0) == MSC_UNSUPPORTED);
    cdb[0] = 0x28;
    CHECK(bot_exec(&dev.bot, cdb, 10, data, BOT_TRANSFER_LIMIT + 1) == MSC_UNSUPPORTED);
    CHECK(bot_exec(&dev.bot, cdb, 10, NULL, 512) == MSC_UNSUPPORTED);
    CHECK(bot_exec(&dev.bot, cdb, 16, data, 512) == MSC_UNSUPPORTED);
    CHECK(bot_exec(&dev.bot, NULL, 0, NULL, 0) == MSC_UNSUPPORTED);
    CHECK(dev.command_count == 0);
    dev.bot.tag = UINT32_MAX;
    CHECK(inquiry(&dev) == MSC_OK);
    CHECK(dev.tag == 0 && dev.bot.tag == 0);
}

int main(void) {
    test_probe();
    test_sense();
    test_reads();
    test_writes();
    test_write_errors();
    test_write_interruption();
    test_sync();
    test_sync_without_cache();
    test_sync_cache_pages();
    test_sync_cache_errors();
    test_sync_cache_reset();
    test_recovery();
    test_interruption();
    test_commands();
    puts("BOT tests passed");
    return 0;
}
