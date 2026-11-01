#include "bot.h"

#include <stddef.h>
#include <string.h>

static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t get_be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static void put_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

void bot_init(Bot *bot, const UsbIo *io, uint8_t interface_number,
              uint8_t ep_in, uint8_t ep_out) {
    memset(bot, 0, sizeof(*bot));
    bot->io = *io;
    bot->interface_number = interface_number;
    bot->ep_in = ep_in;
    bot->ep_out = ep_out;
}

static MscResult reset_recovery(Bot *bot) {
    const UsbRequest req = {0x21, 0xff, 0, bot->interface_number, 0};
    uint32_t actual = 0;
    MscResult result;
    bot->needs_reset = true;
    result = bot->io.control(bot->io.ctx, &req, NULL, &actual);
    if (result != MSC_OK) return result;
    if (actual != 0) return MSC_PROTOCOL_ERROR;
    result = bot->io.clear_halt(bot->io.ctx, bot->ep_in);
    if (result != MSC_OK) return result;
    result = bot->io.clear_halt(bot->io.ctx, bot->ep_out);
    if (result != MSC_OK) return result;
    bot->needs_reset = false;
    return MSC_OK;
}

static MscResult protocol_error(Bot *bot) {
    MscResult result = reset_recovery(bot);
    return result == MSC_OK ? MSC_PROTOCOL_ERROR : result;
}

static bool supported_cdb(const Bot *bot, const uint8_t *cdb,
                          uint8_t cdb_len, uint32_t len) {
    switch (cdb[0]) {
        case 0x00: return cdb_len == 6 && len == 0;
        case 0x03:
        case 0x12:
        case 0x1a: return cdb_len == 6 && len != 0;
        case 0x25:
        case 0x28:
        case 0x5a: return cdb_len == 10 && len != 0;
        case 0x2a: {
            uint32_t blocks;
            if (cdb_len != 10 || !len || !bot->sector_size) return false;
            blocks = (uint32_t)cdb[7] << 8 | cdb[8];
            return blocks && (uint64_t)blocks * bot->sector_size == len &&
                   (uint64_t)get_be32(cdb + 2) + blocks <= bot->sector_count &&
                   (uint64_t)get_be32(cdb + 2) + blocks <= (uint64_t)UINT32_MAX + 1;
        }
        case 0x35: {
            const uint8_t sync[10] = {0x35};
            return cdb_len == sizeof(sync) && !len &&
                   memcmp(cdb, sync, sizeof(sync)) == 0;
        }
        default: return false;
    }
}

MscResult bot_exec(Bot *bot, const uint8_t *cdb, uint8_t cdb_len,
                   void *data, uint32_t len) {
    uint32_t actual = 0;
    MscResult result;
    if (!cdb || !cdb_len || cdb_len > 16 || len > BOT_TRANSFER_LIMIT ||
        (len && !data)) return MSC_UNSUPPORTED;
    if (!supported_cdb(bot, cdb, cdb_len, len)) return MSC_UNSUPPORTED;
    uint8_t endpoint = cdb[0] == 0x2a ? bot->ep_out : bot->ep_in;
    bot->transferred = 0;
    bot->residue = 0;
    if (bot->needs_reset) {
        result = reset_recovery(bot);
        if (result != MSC_OK) return result;
    }
    memset(bot->cbw, 0, sizeof(bot->cbw));
    put_le32(bot->cbw, 0x43425355);
    put_le32(bot->cbw + 4, ++bot->tag);
    put_le32(bot->cbw + 8, len);
    bot->cbw[12] = len && endpoint == bot->ep_in ? 0x80 : 0;
    bot->cbw[14] = cdb_len;
    memcpy(bot->cbw + 15, cdb, cdb_len);
    /* An interrupted command cannot be followed by another CBW without reset. */
    bot->needs_reset = true;
    result = bot->io.bulk(bot->io.ctx, bot->ep_out, bot->cbw,
                          sizeof(bot->cbw), &actual);
    if (result == MSC_STALLED) {
        MscResult recovery = reset_recovery(bot);
        return recovery == MSC_OK ? MSC_STALLED : recovery;
    }
    if (result != MSC_OK) return result;
    if (actual != sizeof(bot->cbw)) return protocol_error(bot);
    if (len) {
        actual = 0;
        result = bot->io.bulk(bot->io.ctx, endpoint, data, len, &actual);
        if (actual > len) return protocol_error(bot);
        bot->transferred = actual;
        if (result == MSC_STALLED) {
            result = bot->io.clear_halt(bot->io.ctx, endpoint);
        }
        if (result != MSC_OK) return result;
    }
    actual = 0;
    result = bot->io.bulk(bot->io.ctx, bot->ep_in, bot->csw,
                          sizeof(bot->csw), &actual);
    if (result == MSC_STALLED) {
        result = bot->io.clear_halt(bot->io.ctx, bot->ep_in);
        if (result != MSC_OK) return result;
        actual = 0;
        result = bot->io.bulk(bot->io.ctx, bot->ep_in, bot->csw,
                              sizeof(bot->csw), &actual);
        if (result == MSC_STALLED) return protocol_error(bot);
    }
    if (result != MSC_OK) return result;
    if (actual != sizeof(bot->csw) || get_le32(bot->csw) != 0x53425355 ||
        get_le32(bot->csw + 4) != bot->tag || bot->csw[12] > 2) {
        return protocol_error(bot);
    }
    if (bot->csw[12] == 2) return protocol_error(bot);
    bot->residue = get_le32(bot->csw + 8);
    if (bot->residue > len) return protocol_error(bot);
    if (bot->csw[12] == 0 && len - bot->residue > bot->transferred) {
        return protocol_error(bot);
    }
    bot->needs_reset = false;
    return bot->csw[12] == 1 ? MSC_COMMAND_FAILED : MSC_OK;
}

MscResult bot_sense(Bot *bot, BotSense *sense) {
    const uint8_t cdb[6] = {0x03, 0, 0, 0, 18, 0};
    uint8_t data[18];
    MscResult result = bot_exec(bot, cdb, sizeof(cdb), data, sizeof(data));
    uint32_t len;
    if (result != MSC_OK) return result;
    len = sizeof(data) - bot->residue;
    if (len < 8) return MSC_PROTOCOL_ERROR;
    if ((data[0] & 0x7f) == 0x70 || (data[0] & 0x7f) == 0x71) {
        if (len < 14 || data[7] < 6) return MSC_PROTOCOL_ERROR;
        sense->key = data[2] & 0x0f;
        sense->asc = data[12];
        sense->ascq = data[13];
    } else if ((data[0] & 0x7f) == 0x72 || (data[0] & 0x7f) == 0x73) {
        sense->key = data[1] & 0x0f;
        sense->asc = data[2];
        sense->ascq = data[3];
    } else {
        return MSC_PROTOCOL_ERROR;
    }
    return MSC_OK;
}

static MscResult sense_result(const BotSense *sense) {
    if (sense->key == 2 && sense->asc == 0x3a) return MSC_NO_DEVICE;
    if (sense->key == 2 || sense->key == 6) return MSC_BUSY;
    if (sense->key == 5) return MSC_UNSUPPORTED;
    if (sense->key == 7) return MSC_WRITE_PROTECTED;
    return MSC_COMMAND_FAILED;
}

static MscResult command_error(Bot *bot) {
    BotSense sense;
    MscResult result = bot_sense(bot, &sense);
    return result == MSC_OK ? sense_result(&sense) : result;
}

MscResult bot_capacity(Bot *bot, MscInfo *info) {
    const uint8_t cdb[10] = {0x25};
    uint8_t data[8];
    uint32_t last_lba, sector_size;
    MscResult result;
    bot->sector_size = 0;
    bot->sector_count = 0;
    info->ready = false;
    info->sector_size = 0;
    info->sector_count = 0;
    result = bot_exec(bot, cdb, sizeof(cdb), data, sizeof(data));
    if (result == MSC_COMMAND_FAILED) return command_error(bot);
    if (result != MSC_OK) return result;
    if (bot->residue || bot->transferred != sizeof(data)) return MSC_PROTOCOL_ERROR;
    last_lba = get_be32(data);
    sector_size = get_be32(data + 4);
    if (last_lba == UINT32_MAX ||
        (sector_size != 512 && sector_size != 1024 &&
         sector_size != 2048 && sector_size != 4096)) return MSC_UNSUPPORTED;
    bot->sector_size = sector_size;
    bot->sector_count = (uint64_t)last_lba + 1;
    info->sector_size = sector_size;
    info->sector_count = bot->sector_count;
    info->ready = true;
    return MSC_OK;
}

MscResult bot_probe(Bot *bot, MscInfo *info) {
    const UsbRequest req = {0xa1, 0xfe, 0, bot->interface_number, 1};
    const uint8_t inquiry[6] = {0x12, 0, 0, 0, 36, 0};
    const uint8_t ready[6] = {0};
    uint8_t data[36];
    uint32_t actual = 0;
    unsigned attempt;
    MscResult result;
    bot->sector_size = 0;
    bot->sector_count = 0;
    bot->sync_optional = false;
    info->ready = false;
    info->sector_size = 0;
    info->sector_count = 0;
    bot->max_lun = 0;
    result = bot->io.control(bot->io.ctx, &req, &bot->max_lun, &actual);
    if (result == MSC_STALLED) {
        bot->max_lun = 0;
    } else if (result != MSC_OK) {
        return result;
    } else if (actual != 1 || bot->max_lun > 15) {
        return MSC_PROTOCOL_ERROR;
    }
    result = bot_exec(bot, inquiry, sizeof(inquiry), data, sizeof(data));
    if (result == MSC_COMMAND_FAILED) return command_error(bot);
    if (result != MSC_OK) return result;
    if (sizeof(data) - bot->residue < 5) return MSC_PROTOCOL_ERROR;
    if ((data[0] & 0xe0) != 0 || (data[0] & 0x1f) != 0) return MSC_UNSUPPORTED;
    for (attempt = 0; attempt < 3; ++attempt) {
        result = bot_exec(bot, ready, sizeof(ready), NULL, 0);
        if (result == MSC_COMMAND_FAILED) result = command_error(bot);
        if (result == MSC_OK) return bot_capacity(bot, info);
        if (result != MSC_BUSY) return result;
    }
    return MSC_BUSY;
}

MscResult bot_read(Bot *bot, uint32_t lba, uint32_t count, void *buf) {
    uint8_t cdb[10] = {0x28};
    uint8_t *out = buf;
    uint32_t blocks, len;
    MscResult result;
    if (!bot->sector_size || !bot->sector_count) return MSC_NO_DEVICE;
    if ((uint64_t)lba + count > bot->sector_count ||
        (uint64_t)lba + count > (uint64_t)UINT32_MAX + 1 ||
        (uint64_t)count * bot->sector_size > SIZE_MAX || (count && !buf)) {
        return MSC_UNSUPPORTED;
    }
    while (count) {
        blocks = BOT_TRANSFER_LIMIT / bot->sector_size;
        if (blocks > count) blocks = count;
        len = blocks * bot->sector_size;
        put_be32(cdb + 2, lba);
        cdb[7] = (uint8_t)(blocks >> 8);
        cdb[8] = (uint8_t)blocks;
        result = bot_exec(bot, cdb, sizeof(cdb), out, len);
        if (result == MSC_COMMAND_FAILED) return command_error(bot);
        if (result != MSC_OK) return result;
        if (bot->residue || bot->transferred != len) return MSC_PROTOCOL_ERROR;
        lba += blocks;
        count -= blocks;
        out += len;
    }
    return MSC_OK;
}

MscResult bot_write(Bot *bot, uint32_t lba, uint32_t count, const void *buf) {
    uint8_t cdb[10] = {0x2a};
    const uint8_t *in = buf;
    uint32_t blocks, len;
    MscResult result;
    if (!bot->sector_size || !bot->sector_count) return MSC_NO_DEVICE;
    if ((uint64_t)lba + count > bot->sector_count ||
        (uint64_t)lba + count > (uint64_t)UINT32_MAX + 1 ||
        (uint64_t)count * bot->sector_size > SIZE_MAX || (count && !buf)) {
        return MSC_UNSUPPORTED;
    }
    while (count) {
        blocks = BOT_TRANSFER_LIMIT / bot->sector_size;
        if (blocks > count) blocks = count;
        len = blocks * bot->sector_size;
        put_be32(cdb + 2, lba);
        cdb[7] = (uint8_t)(blocks >> 8);
        cdb[8] = (uint8_t)blocks;
        /* The shared bulk callback reads, but never modifies, OUT buffers. */
        result = bot_exec(bot, cdb, sizeof(cdb), (void *)in, len);
        if (result == MSC_COMMAND_FAILED) return command_error(bot);
        if (result != MSC_OK) return result;
        if (bot->residue || bot->transferred != len) return MSC_PROTOCOL_ERROR;
        lba += blocks;
        count -= blocks;
        in += len;
    }
    return MSC_OK;
}

static MscResult cache_disabled(Bot *bot) {
    const uint8_t mode6[6] = {0x1a, 8, 8, 0, 192, 0};
    const uint8_t mode10[10] = {0x5a, 8, 8, 0, 0, 0, 0, 0, 192, 0};
    uint8_t data[192];
    for (unsigned mode = 0; mode < 2; ++mode) {
        uint32_t header = mode ? 8 : 4;
        MscResult result = bot_exec(bot, mode ? mode10 : mode6,
                                    mode ? sizeof(mode10) : sizeof(mode6),
                                    data, sizeof(data));
        if (result == MSC_COMMAND_FAILED) {
            result = command_error(bot);
            if (result == MSC_UNSUPPORTED) continue;
        }
        if (result != MSC_OK) return result;
        uint32_t size = sizeof(data) - bot->residue;
        if (size < header) return MSC_PROTOCOL_ERROR;
        uint32_t total = mode ? ((uint32_t)data[0] << 8 | data[1]) + 2 :
                                (uint32_t)data[0] + 1;
        uint32_t offset = header + (mode ? (uint32_t)data[6] << 8 | data[7] : data[3]);
        if (total < offset || size < offset) return MSC_PROTOCOL_ERROR;
        if (size > total) size = total;
        while (offset < size) {
            if (size - offset < 2) return MSC_PROTOCOL_ERROR;
            const uint8_t *page = data + offset;
            bool subpage = (page[0] & 0x40) != 0;
            uint32_t head = subpage ? 4 : 2;
            if (size - offset < head) return MSC_PROTOCOL_ERROR;
            uint32_t length = subpage ? (uint32_t)page[2] << 8 | page[3] : page[1];
            if (length > size - offset - head) return MSC_PROTOCOL_ERROR;
            if (!subpage && (page[0] & 0x3f) == 8) {
                if (length < 0x12) return MSC_PROTOCOL_ERROR;
                return page[2] & 4 ? MSC_UNSUPPORTED : MSC_OK;
            }
            offset += head + length;
        }
        return MSC_UNSUPPORTED;
    }
    return MSC_UNSUPPORTED;
}

MscResult bot_sync(Bot *bot) {
    const uint8_t cdb[10] = {0x35};
    BotSense sense;
    MscResult result;
    if (!bot->sector_size || !bot->sector_count) return MSC_NO_DEVICE;
    if (bot->sync_optional && !bot->needs_reset) return MSC_OK;
    result = bot_exec(bot, cdb, sizeof(cdb), NULL, 0);
    if (result != MSC_COMMAND_FAILED) return result;
    result = bot_sense(bot, &sense);
    if (result != MSC_OK) return result;
    if (sense.key != 5 || sense.asc != 0x20 || sense.ascq != 0)
        return sense_result(&sense);
    /* WCE=0 makes a completed WRITE reach media without a separate flush. */
    result = cache_disabled(bot);
    if (result == MSC_OK) bot->sync_optional = true;
    return result;
}
