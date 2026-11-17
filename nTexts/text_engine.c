#include "text_engine.h"
#include "app_log.h"
#include "gb18030_table.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static uint16_t gb_pair_lookup(uint16_t key) {
    int lo = 0, hi = (int)ARRAY_LEN(gb_pairs) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (gb_pairs[mid].key == key) return gb_pairs[mid].value;
        if (gb_pairs[mid].key < key) lo = mid + 1; else hi = mid - 1;
    }
    return 0xfffd;
}

static uint16_t gb_range_lookup(unsigned pointer) {
    int lo = 0, hi = (int)ARRAY_LEN(gb_ranges) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (pointer < gb_ranges[mid].start) hi = mid - 1;
        else if (pointer > gb_ranges[mid].end) lo = mid + 1;
        else return (uint16_t)(gb_ranges[mid].unicode + pointer - gb_ranges[mid].start);
    }
    return 0xfffd;
}

static int text_utf8_valid_sample(FILE *fp, uint32_t start, uint32_t max_bytes) {
    uint32_t read = 0;
    int c;
    if (fseek(fp, start, SEEK_SET) != 0) return 0;
    while (read < max_bytes && (c = fgetc(fp)) != EOF) {
        int need = 0, i;
        unsigned value, min;
        read++;
        if (c < 0x80) continue;
        if ((c & 0xe0) == 0xc0) { need = 1; value = c & 0x1f; min = 0x80; }
        else if ((c & 0xf0) == 0xe0) { need = 2; value = c & 0x0f; min = 0x800; }
        else if ((c & 0xf8) == 0xf0) { need = 3; value = c & 7; min = 0x10000; }
        else return 0;
        for (i = 0; i < need; ++i) {
            c = fgetc(fp); read++;
            if (c == EOF || (c & 0xc0) != 0x80) return 0;
            value = (value << 6) | (c & 0x3f);
        }
        if (value < min || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
    }
    return 1;
}

static int path_separator(char value) {
    return value == '/' || value == '\\';
}

static int normalize_path(char out[512], const char *path) {
    char cwd[512];
    char joined[1024];
    const char *cursor;
    size_t written = 1;
    int length;

    if (path_separator(path[0])) {
        length = snprintf(joined, sizeof(joined), "%s", path);
    } else {
        if (!getcwd(cwd, sizeof(cwd))) return 0;
        length = snprintf(joined, sizeof(joined), "%s/%s", cwd, path);
    }
    if (length < 0 || (size_t)length >= sizeof(joined)) {
        errno = ENAMETOOLONG;
        return 0;
    }

    out[0] = '/';
    out[1] = 0;
    cursor = joined;
    while (*cursor) {
        const char *component;
        size_t component_length;
        while (path_separator(*cursor)) ++cursor;
        if (!*cursor) break;
        component = cursor;
        while (*cursor && !path_separator(*cursor)) ++cursor;
        component_length = (size_t)(cursor - component);
        if (component_length == 1 && component[0] == '.') continue;
        if (component_length == 2 && component[0] == '.' && component[1] == '.') {
            while (written > 1 && out[written - 1] != '/') --written;
            if (written > 1) --written;
            out[written] = 0;
            continue;
        }
        if (written > 1) {
            if (written + 1 >= 512) {
                errno = ENAMETOOLONG;
                return 0;
            }
            out[written++] = '/';
        }
        if (component_length >= 512 - written) {
            errno = ENAMETOOLONG;
            return 0;
        }
        memcpy(out + written, component, component_length);
        written += component_length;
        out[written] = 0;
    }
    return 1;
}

int text_open(TextFile *text, const char *path) {
    struct stat st;
    char resolved[512];
    unsigned char bom[3] = {0};
    if (!text || !path || !*path) {
        errno = EINVAL;
        return 0;
    }
    memset(text, 0, sizeof(*text));
    memset(&st, 0, sizeof(st));
    app_log("text", "open begin %s", path);
    if (strlen(path) >= sizeof(text->path)) {
        errno = ENAMETOOLONG;
        app_log("text", "path too long");
        return 0;
    }
    if (!normalize_path(resolved, path)) {
        app_log("text", "normalize failed %s", path);
        return 0;
    }
    if (stat(resolved, &st)) {
        app_log("text", "stat failed %s", resolved);
        return 0;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) {
        errno = S_ISREG(st.st_mode) ? EFBIG : EINVAL;
        app_log("text", "unsupported file size or type %s", resolved);
        return 0;
    }
    text->fp = fopen(resolved, "rb");
    if (!text->fp) {
        app_log("text", "fopen failed %s", resolved);
        return 0;
    }
    memcpy(text->path, resolved, strlen(resolved) + 1);
    text->size = (uint32_t)st.st_size;
    text->mtime = (uint32_t)st.st_mtime;
    (void)fread(bom, 1, 3, text->fp);
    if (bom[0] == 0xef && bom[1] == 0xbb && bom[2] == 0xbf) {
        text->encoding = TEXT_UTF8;
        text->data_start = 3;
    } else if (bom[0] == 0xff && bom[1] == 0xfe) {
        text->encoding = TEXT_UTF16_LE;
        text->data_start = 2;
    } else if (bom[0] == 0xfe && bom[1] == 0xff) {
        text->encoding = TEXT_UTF16_BE;
        text->data_start = 2;
    } else {
        text->data_start = 0;
        text->encoding = text_utf8_valid_sample(text->fp, 0, 65536) ? TEXT_UTF8 : TEXT_GB18030;
    }
    app_log("text", "open ok path=%s size=%lu encoding=%s data_start=%lu",
            text->path, (unsigned long)text->size, text_encoding_name(text->encoding),
            (unsigned long)text->data_start);
    return 1;
}

int text_select_encoding(TextFile *text, TextEncoding encoding) {
    unsigned char bom[3] = {0};
    uint32_t data_start = 0;

    if (!text || !text->fp ||
        (encoding != TEXT_UTF8 && encoding != TEXT_GB18030 &&
         encoding != TEXT_UTF16_LE && encoding != TEXT_UTF16_BE)) {
        errno = EINVAL;
        return 0;
    }
    if (fseek(text->fp, 0, SEEK_SET) != 0) return 0;
    (void)fread(bom, 1, sizeof(bom), text->fp);
    if (encoding == TEXT_UTF8 &&
        bom[0] == 0xef && bom[1] == 0xbb && bom[2] == 0xbf) {
        data_start = 3;
    } else if (encoding == TEXT_UTF16_LE && bom[0] == 0xff && bom[1] == 0xfe) {
        data_start = 2;
    } else if (encoding == TEXT_UTF16_BE && bom[0] == 0xfe && bom[1] == 0xff) {
        data_start = 2;
    }
    if (fseek(text->fp, data_start, SEEK_SET) != 0) return 0;
    text->encoding = encoding;
    text->data_start = data_start;
    return 1;
}

void text_close(TextFile *text) {
    if (text->fp) fclose(text->fp);
    text->fp = NULL;
}

void text_decoder_at(TextFile *text, TextDecoder *decoder, TextPosition position) {
    decoder->text = text;
    decoder->offset = position.byte_offset < text->data_start ? text->data_start : position.byte_offset;
    if (decoder->offset > text->size) decoder->offset = text->size;
    decoder->line = position.source_line ? position.source_line : 1;
    fseek(text->fp, decoder->offset, SEEK_SET);
}

static int read_utf16_unit_at(TextFile *text, uint32_t offset, uint16_t *value) {
    int first;
    int second;
    if (offset + 1 >= text->size) return 0;
    fseek(text->fp, offset, SEEK_SET);
    first = fgetc(text->fp);
    second = fgetc(text->fp);
    if (first == EOF || second == EOF) return 0;
    if (text->encoding == TEXT_UTF16_LE) {
        *value = (uint16_t)((unsigned)first | ((unsigned)second << 8));
    } else {
        *value = (uint16_t)(((unsigned)first << 8) | (unsigned)second);
    }
    return 1;
}

TextPosition text_safe_position(TextFile *text, uint32_t offset, uint32_t fallback_line) {
    TextPosition p;
    int c;
    uint32_t i;
    p.source_line = fallback_line ? fallback_line : 1;
    if (offset < text->data_start) offset = text->data_start;
    if (offset > text->size) offset = text->size;
    if ((text->encoding == TEXT_UTF16_LE || text->encoding == TEXT_UTF16_BE) &&
        offset != text->data_start && offset != text->size) {
        uint16_t unit;
        uint16_t previous;
        offset = text->data_start + ((offset - text->data_start) & ~1u);
        if (read_utf16_unit_at(text, offset, &unit) && unit >= 0xdc00 && unit <= 0xdfff &&
            offset >= text->data_start + 2 &&
            read_utf16_unit_at(text, offset - 2, &previous) &&
            previous >= 0xd800 && previous <= 0xdbff) {
            offset -= 2;
        }
        p.byte_offset = offset;
        return p;
    }
    p.byte_offset = offset;
    if (text->encoding != TEXT_UTF8 || offset == text->data_start || offset == text->size) return p;
    fseek(text->fp, offset, SEEK_SET);
    c = fgetc(text->fp);
    if (c == EOF || ((unsigned char)c & 0xc0) != 0x80) return p;
    for (i = 1; i <= 3 && offset >= text->data_start + i; ++i) {
        fseek(text->fp, offset - i, SEEK_SET);
        c = fgetc(text->fp);
        if (c == EOF || ((unsigned char)c & 0xc0) != 0x80) {
            p.byte_offset = offset - i;
            return p;
        }
    }
    p.byte_offset = text->data_start;
    return p;
}

static int next_utf8(TextDecoder *d, int first, uint16_t *out) {
    int need, i, c;
    unsigned value, min;
    if (first < 0x80) { *out = (uint16_t)first; return 1; }
    if ((first & 0xe0) == 0xc0) { need = 1; value = first & 0x1f; min = 0x80; }
    else if ((first & 0xf0) == 0xe0) { need = 2; value = first & 0x0f; min = 0x800; }
    else if ((first & 0xf8) == 0xf0) { need = 3; value = first & 7; min = 0x10000; }
    else { *out = 0xfffd; return 1; }
    for (i = 0; i < need; ++i) {
        c = fgetc(d->text->fp);
        if (c == EOF || (c & 0xc0) != 0x80) {
            if (c != EOF) ungetc(c, d->text->fp);
            *out = 0xfffd; return 1;
        }
        d->offset++;
        value = (value << 6) | (c & 0x3f);
    }
    if (value < min || value > 0xffff || (value >= 0xd800 && value <= 0xdfff)) value = 0xfffd;
    *out = (uint16_t)value;
    return 1;
}

static int next_gb(TextDecoder *d, int first, uint16_t *out) {
    int b2, b3, b4;
    if (first < 0x80) { *out = (uint16_t)first; return 1; }
    if (first < 0x81 || first > 0xfe) { *out = 0xfffd; return 1; }
    b2 = fgetc(d->text->fp);
    if (b2 == EOF) { *out = 0xfffd; return 1; }
    d->offset++;
    if (b2 >= 0x30 && b2 <= 0x39) {
        b3 = fgetc(d->text->fp); b4 = fgetc(d->text->fp);
        if (b3 == EOF || b4 == EOF) { *out = 0xfffd; return 1; }
        d->offset += 2;
        if (b3 >= 0x81 && b3 <= 0xfe && b4 >= 0x30 && b4 <= 0x39) {
            unsigned p = ((((unsigned)first - 0x81u) * 10u + ((unsigned)b2 - 0x30u)) * 126u +
                          ((unsigned)b3 - 0x81u)) * 10u + ((unsigned)b4 - 0x30u);
            *out = gb_range_lookup(p); return 1;
        }
        *out = 0xfffd; return 1;
    }
    *out = gb_pair_lookup((uint16_t)((first << 8) | b2));
    return 1;
}

static int next_utf16(TextDecoder *d, int first, uint16_t *out) {
    int second;
    int third;
    int fourth;
    uint16_t unit;
    uint16_t following;
    uint32_t following_offset;

    second = fgetc(d->text->fp);
    if (second == EOF) {
        *out = 0xfffd;
        return 1;
    }
    d->offset++;
    if (d->text->encoding == TEXT_UTF16_LE) {
        unit = (uint16_t)((unsigned)first | ((unsigned)second << 8));
    } else {
        unit = (uint16_t)(((unsigned)first << 8) | (unsigned)second);
    }
    if (unit >= 0xdc00 && unit <= 0xdfff) {
        *out = 0xfffd;
        return 1;
    }
    if (unit < 0xd800 || unit > 0xdbff) {
        *out = unit;
        return 1;
    }

    following_offset = d->offset;
    third = fgetc(d->text->fp);
    fourth = fgetc(d->text->fp);
    if (third == EOF || fourth == EOF) {
        fseek(d->text->fp, following_offset, SEEK_SET);
        *out = 0xfffd;
        return 1;
    }
    if (d->text->encoding == TEXT_UTF16_LE) {
        following = (uint16_t)((unsigned)third | ((unsigned)fourth << 8));
    } else {
        following = (uint16_t)(((unsigned)third << 8) | (unsigned)fourth);
    }
    if (following < 0xdc00 || following > 0xdfff) {
        fseek(d->text->fp, following_offset, SEEK_SET);
        *out = 0xfffd;
        return 1;
    }
    d->offset += 2;
    *out = 0xfffd;
    return 1;
}

int text_next(TextDecoder *decoder, uint16_t *value, uint32_t *char_offset) {
    int first;
    if (decoder->offset >= decoder->text->size) return 0;
    if (char_offset) *char_offset = decoder->offset;
    first = fgetc(decoder->text->fp);
    if (first == EOF) return 0;
    decoder->offset++;
    switch (decoder->text->encoding) {
        case TEXT_UTF8:
            next_utf8(decoder, first, value);
            break;
        case TEXT_GB18030:
            next_gb(decoder, first, value);
            break;
        case TEXT_UTF16_LE:
        case TEXT_UTF16_BE:
            next_utf16(decoder, first, value);
            break;
        default:
            *value = 0xfffd;
            break;
    }
    if (*value == '\n') decoder->line++;
    return 1;
}

const char *text_encoding_name(TextEncoding encoding) {
    switch (encoding) {
        case TEXT_UTF8: return "UTF-8";
        case TEXT_GB18030: return "GB18030";
        case TEXT_UTF16_LE: return "UTF-16 LE";
        case TEXT_UTF16_BE: return "UTF-16 BE";
        default: return "Unknown";
    }
}

uint16_t text_fold_latin(uint16_t value) {
    if ((value >= 'A' && value <= 'Z') ||
        (value >= 0x00c0 && value <= 0x00d6) ||
        (value >= 0x00d8 && value <= 0x00de))
        return (uint16_t)(value + 0x20);
    if (value == 0x0152) return 0x0153;
    if (value == 0x0178) return 0x00ff;
    return value;
}

static int search_char_equal(uint16_t left, uint16_t right) {
    return text_fold_latin(left) == text_fold_latin(right);
}

static uint32_t search_query_length(const uint16_t *query) {
    uint32_t length = 0;
    if (!query) return 0;
    while (length < TEXT_SEARCH_QUERY_MAX && query[length]) ++length;
    return length;
}

int text_find_forward(TextFile *text, const uint16_t *query, uint32_t from, uint32_t *hit_offset) {
    TextDecoder decoder;
    TextPosition start;
    uint16_t ring[TEXT_SEARCH_QUERY_MAX];
    uint32_t offsets[TEXT_SEARCH_QUERY_MAX];
    uint16_t value;
    uint32_t offset;
    uint32_t query_length = search_query_length(query);
    uint32_t seen = 0;
    if (!text || !text->fp || !query_length || !hit_offset) {
        errno = EINVAL;
        return 0;
    }
    start = text_safe_position(text, from, 1);
    text_decoder_at(text, &decoder, start);
    while (text_next(&decoder, &value, &offset)) {
        uint32_t i;
        ring[seen % query_length] = value;
        offsets[seen % query_length] = offset;
        ++seen;
        if (seen < query_length) continue;
        for (i = 0; i < query_length; ++i) {
            uint32_t index = (seen - query_length + i) % query_length;
            if (!search_char_equal(ring[index], query[i])) break;
        }
        if (i == query_length) {
            *hit_offset = offsets[(seen - query_length) % query_length];
            return 1;
        }
    }
    return 0;
}

int text_find_backward(TextFile *text, const uint16_t *query, uint32_t before,
                       const TextPosition *pages, uint32_t page_count, uint32_t *hit_offset) {
    TextDecoder decoder;
    uint16_t ring[TEXT_SEARCH_QUERY_MAX];
    uint32_t offsets[TEXT_SEARCH_QUERY_MAX];
    uint16_t value;
    uint32_t offset;
    uint32_t query_length = search_query_length(query);
    uint32_t lo = 0, hi = page_count;
    if (!text || !text->fp || !query_length || !pages || !page_count || !hit_offset) {
        errno = EINVAL;
        return 0;
    }
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (pages[mid].byte_offset < before) lo = mid + 1;
        else hi = mid;
    }
    while (lo) {
        uint32_t limit;
        uint32_t seen = 0;
        int found = 0;
        --lo;
        limit = lo + 1 < page_count ? pages[lo + 1].byte_offset : text->size;
        if (limit > before) limit = before;
        text_decoder_at(text, &decoder, pages[lo]);
        while (text_next(&decoder, &value, &offset)) {
            uint32_t i;
            uint32_t start;
            ring[seen % query_length] = value;
            offsets[seen % query_length] = offset;
            ++seen;
            if (seen < query_length) continue;
            start = offsets[(seen - query_length) % query_length];
            if (start >= limit) break;
            for (i = 0; i < query_length; ++i) {
                uint32_t index = (seen - query_length + i) % query_length;
                if (!search_char_equal(ring[index], query[i])) break;
            }
            if (i == query_length) {
                *hit_offset = start;
                found = 1;
            }
        }
        if (found) return 1;
    }
    return 0;
}

void text_make_excerpt(TextFile *text, TextPosition position, uint16_t *out, size_t capacity) {
    TextDecoder decoder;
    uint16_t value;
    uint32_t offset;
    size_t length = 0;
    int started = 0;
    int pending_space = 0;

    if (!capacity) return;
    out[0] = 0;
    text_decoder_at(text, &decoder, position);
    while (text_next(&decoder, &value, &offset)) {
        (void)offset;
        if (value == '\r') continue;
        if (value == '\n') {
            if (started) break;
            pending_space = 0;
            continue;
        }
        if (value <= ' ' || value == 0x3000) {
            if (started) pending_space = 1;
            continue;
        }
        if (pending_space && length + 1 < capacity) out[length++] = ' ';
        pending_space = 0;
        started = 1;
        if (length + 1 >= capacity) break;
        out[length++] = value;
    }
    if (length && out[length - 1] == ' ') --length;
    out[length] = 0;
}
