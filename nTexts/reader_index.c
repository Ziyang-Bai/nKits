#include "reader_index.h"
#include "app_log.h"
#include "crc32.h"
#include "file_replace.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define INDEX_MAGIC 0x5849544eu
#define INDEX_VERSION 8u
#define LEGACY_INDEX_VERSION_MIN 4u
#define LEGACY_INDEX_VERSION_MAX 7u

typedef struct {
    uint32_t magic, version, file_size, file_mtime, encoding;
    int32_t font_size, margin, lines_per_page, line_height;
    uint32_t page_count, line_count, total_source_lines;
    uint32_t chapter_rules_hash, chapter_count, chapters_truncated;
    uint32_t last_visible_end;
    char file_path[512];
} IndexHeader;

#define SIZE_ASSERT(name, condition) typedef char size_assert_##name[(condition) ? 1 : -1]
SIZE_ASSERT(index_header, sizeof(IndexHeader) == 576);
SIZE_ASSERT(chapter, sizeof(Chapter) == 104);

static int layout_valid(Layout layout) {
    return layout.font_size > 0 && layout.margin >= 0 && layout.margin < 160 &&
           layout.lines_per_page > 0 && layout.line_height > 0;
}

static int append_position(TextPosition **items, uint32_t *count, uint32_t *capacity,
                           TextPosition value) {
    if (*count == *capacity) {
        uint32_t cap = *capacity ? *capacity * 2 : 256;
        TextPosition *next = realloc(*items, cap * sizeof(*next));
        if (!next) return 0;
        *items = next;
        *capacity = cap;
    }
    (*items)[(*count)++] = value;
    return 1;
}

static int append_unique_position(TextPosition **items, uint32_t *count, uint32_t *capacity,
                                  TextPosition value) {
    if (*count) {
        TextPosition last = (*items)[*count - 1];
        if (last.byte_offset == value.byte_offset && last.source_line == value.source_line) return 1;
    }
    return append_position(items, count, capacity, value);
}

static int chapter_space(uint16_t value) {
    return value == 0x3000 || value == ' ' || (value >= '\t' && value <= '\r');
}

static int append_chapter(PageIndex *index, TextPosition position,
                          const uint16_t *line, uint32_t length) {
    Chapter *chapter;
    uint32_t i;
    if (index->chapter_count >= MAX_CHAPTERS) {
        index->chapters_truncated = 1;
        return 1;
    }
    if (index->chapter_count == index->chapter_capacity) {
        uint32_t capacity = index->chapter_capacity ? index->chapter_capacity * 2 : 32;
        Chapter *next;
        if (capacity > MAX_CHAPTERS) capacity = MAX_CHAPTERS;
        next = realloc(index->chapters, capacity * sizeof(*next));
        if (!next) return 0;
        index->chapters = next;
        index->chapter_capacity = capacity;
    }
    chapter = &index->chapters[index->chapter_count++];
    memset(chapter, 0, sizeof(*chapter));
    chapter->position = position;
    if (length >= CHAPTER_TITLE_LEN) {
        for (i = 0; i < CHAPTER_TITLE_LEN - 2; ++i) chapter->title[i] = line[i];
        chapter->title[CHAPTER_TITLE_LEN - 2] = 0x2026;
    } else {
        for (i = 0; i < length; ++i) chapter->title[i] = line[i];
    }
    return 1;
}

static int collect_chapter(PageIndex *index, const ChapterRules *rules, TextPosition position,
                           uint16_t line[CHAPTER_SCAN_LEN], uint32_t length, int overlong) {
    uint32_t start = 0;
    uint32_t end = length;
    while (start < end && chapter_space(line[start])) ++start;
    while (end > start && chapter_space(line[end - 1])) --end;
    if (overlong || start == end) return 1;
    line[end] = 0;
    if (!chapter_rules_match(rules, line + start)) return 1;
    return append_chapter(index, position, line + start, end - start);
}

void index_init(PageIndex *index) { memset(index, 0, sizeof(*index)); }

void index_free(PageIndex *index) {
    free(index->pages);
    free(index->lines);
    free(index->chapters);
    memset(index, 0, sizeof(*index));
}

static int same_layout(Layout a, Layout b) {
    return a.font_size == b.font_size && a.margin == b.margin &&
           a.lines_per_page == b.lines_per_page && a.line_height == b.line_height;
}

int index_build(PageIndex *index, TextFile *text, Gc gc, Layout layout,
                const ChapterRules *rules, IndexProgress progress, void *context) {
    TextDecoder d;
    TextPosition first;
    TextPosition line_start;
    uint16_t line[CHAPTER_SCAN_LEN];
    uint16_t ch;
    int ascii_width[128] = {0};
    uint16_t wide_chars[256] = {0};
    int wide_width[256];
    uint32_t line_length = 0;
    uint32_t off, visual_line = 0, last_report = 0;
    int line_overlong = 0;
    int width = 0;
    int max_width;
    uint32_t lines_per_page;
    if (!index || !text || !text->fp || !layout_valid(layout)) {
        errno = EINVAL;
        return 0;
    }
    first = (TextPosition){text->data_start, 1};
    line_start = first;
    max_width = 320 - layout.margin * 2;
    lines_per_page = (uint32_t)layout.lines_per_page;
    app_log("index", "build begin size=%lu font=%d margin=%d lpp=%d",
            (unsigned long)text->size, layout.font_size, layout.margin, layout.lines_per_page);
    index_free(index);
    index->layout = layout;
    index->file_size = text->size;
    index->last_visible_end = text->data_start;
    index->file_mtime = text->mtime;
    index->encoding = text->encoding;
    memcpy(index->file_path, text->path, strlen(text->path) + 1);
    index->chapter_rules_hash = chapter_rules_hash(rules);
    if (!append_unique_position(&index->pages, &index->page_count, &index->page_capacity, first) ||
        !append_unique_position(&index->lines, &index->line_count, &index->line_capacity, first)) return 0;
    text_decoder_at(text, &d, first);
    while (text_next(&d, &ch, &off)) {
        int cw;
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (!collect_chapter(index, rules, line_start, line, line_length, line_overlong)) return 0;
            line_length = 0;
            line_overlong = 0;
            line_start.byte_offset = d.offset;
            line_start.source_line = d.line;
            visual_line++;
            width = 0;
            if ((d.line - 1) % 64 == 0) {
                TextPosition p = {d.offset, d.line};
                if (!append_unique_position(&index->lines, &index->line_count, &index->line_capacity, p)) return 0;
            }
            if (visual_line % lines_per_page == 0 && d.offset < text->size) {
                TextPosition p = {d.offset, d.line};
                if (!append_unique_position(&index->pages, &index->page_count, &index->page_capacity, p)) return 0;
            }
        } else {
            if (line_length + 1 < CHAPTER_SCAN_LEN) line[line_length++] = ch;
            else line_overlong = 1;
            if (ch == '\t') ch = ' ';
            if (ch < 32) continue;
            if (ch != ' ' && ch != 0x3000) index->last_visible_end = d.offset;
            if (ch < 128) {
                cw = ascii_width[ch];
            } else {
                unsigned slot = ch & 255u;
                cw = wide_chars[slot] == ch ? wide_width[slot] : 0;
            }
            if (!cw) {
                cw = gui_gc_getCharWidth(gc, (gui_gc_Font)layout.font_size, (short)ch);
                if (cw <= 0) cw = layout.font_size;
                if (ch < 128) {
                    ascii_width[ch] = cw;
                } else {
                    unsigned slot = ch & 255u;
                    wide_chars[slot] = ch;
                    wide_width[slot] = cw;
                }
            }
            if (width && width + cw > max_width) {
                visual_line++;
                width = 0;
                if (visual_line % lines_per_page == 0) {
                    TextPosition p = {off, d.line};
                    if (!append_unique_position(&index->pages, &index->page_count, &index->page_capacity, p)) return 0;
                }
            }
            width += cw;
        }
        if (progress && d.offset - last_report >= 32768) {
            last_report = d.offset;
            if (!progress(d.offset, text->size, context)) return -1;
        }
    }
    if (!collect_chapter(index, rules, line_start, line, line_length, line_overlong)) return 0;
    while (index->page_count > 1 &&
           index->pages[index->page_count - 1].byte_offset >= index->last_visible_end)
        --index->page_count;
    index->total_source_lines = d.line;
    if (progress && !progress(text->size, text->size, context)) return -1;
    app_log("index", "build ok pages=%lu lines=%lu chapters=%lu source_lines=%lu",
            (unsigned long)index->page_count, (unsigned long)index->line_count,
            (unsigned long)index->chapter_count, (unsigned long)index->total_source_lines);
    return 1;
}

static void put_le32(unsigned char out[4], uint32_t value) {
    out[0] = (unsigned char)value;
    out[1] = (unsigned char)(value >> 8);
    out[2] = (unsigned char)(value >> 16);
    out[3] = (unsigned char)(value >> 24);
}

static uint32_t get_le32(const unsigned char in[4]) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

int index_save(const PageIndex *index, const char *path) {
    IndexHeader h;
    unsigned char trailer[4];
    FilePart parts[5];
    uint32_t crc = CRC32_INITIAL;
    if (!index || !path || !*path || !layout_valid(index->layout) ||
        !index->file_path[0] || !memchr(index->file_path, 0, sizeof(index->file_path)) ||
        !index->page_count || !index->line_count || !index->pages || !index->lines ||
        index->chapter_count > MAX_CHAPTERS ||
        (index->chapter_count && !index->chapters) || index->chapters_truncated > 1) {
        errno = EINVAL;
        return 0;
    }
    memset(&h, 0, sizeof(h));
    h.magic = INDEX_MAGIC;
    h.version = INDEX_VERSION;
    h.file_size = index->file_size;
    h.file_mtime = index->file_mtime;
    h.encoding = (uint32_t)index->encoding;
    h.font_size = index->layout.font_size;
    h.margin = index->layout.margin;
    h.lines_per_page = index->layout.lines_per_page;
    h.line_height = index->layout.line_height;
    h.page_count = index->page_count;
    h.line_count = index->line_count;
    h.total_source_lines = index->total_source_lines;
    h.chapter_rules_hash = index->chapter_rules_hash;
    h.chapter_count = index->chapter_count;
    h.chapters_truncated = index->chapters_truncated;
    h.last_visible_end = index->last_visible_end;
    memcpy(h.file_path, index->file_path, sizeof(h.file_path));
    crc = crc32_update(crc, &h, sizeof(h));
    crc = crc32_update(crc, index->pages, index->page_count * sizeof(*index->pages));
    crc = crc32_update(crc, index->lines, index->line_count * sizeof(*index->lines));
    crc = crc32_update(crc, index->chapters, index->chapter_count * sizeof(*index->chapters));
    put_le32(trailer, crc32_finish(crc));
    parts[0].data = &h;
    parts[0].size = sizeof(h);
    parts[1].data = index->pages;
    parts[1].size = index->page_count * sizeof(*index->pages);
    parts[2].data = index->lines;
    parts[2].size = index->line_count * sizeof(*index->lines);
    parts[3].data = index->chapters;
    parts[3].size = index->chapter_count * sizeof(*index->chapters);
    parts[4].data = trailer;
    parts[4].size = sizeof(trailer);
    return file_replace_parts(path, parts, 5);
}

static int file_length(FILE *fp, uint64_t *length) {
    long end;
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0)
        return 0;
    *length = (uint64_t)(unsigned long)end;
    return 1;
}

static int positions_valid(const TextPosition *items, uint32_t count, TextPosition first,
                           uint32_t file_size, uint32_t total_source_lines) {
    uint32_t i;
    if (!count || items[0].byte_offset != first.byte_offset ||
        items[0].source_line != first.source_line) return 0;
    for (i = 0; i < count; ++i) {
        if (items[i].byte_offset < first.byte_offset || items[i].byte_offset > file_size ||
            !items[i].source_line || items[i].source_line > total_source_lines) return 0;
        if (i && (items[i].byte_offset <= items[i - 1].byte_offset ||
                  items[i].source_line < items[i - 1].source_line)) return 0;
    }
    return 1;
}

static int chapters_valid(const Chapter *chapters, uint32_t count, TextPosition first,
                          uint32_t file_size, uint32_t total_source_lines) {
    uint32_t i;
    for (i = 0; i < count; ++i) {
        uint32_t title_length = 0;
        while (title_length < CHAPTER_TITLE_LEN && chapters[i].title[title_length]) ++title_length;
        if (!title_length || title_length == CHAPTER_TITLE_LEN ||
            chapters[i].position.byte_offset < first.byte_offset ||
            chapters[i].position.byte_offset > file_size ||
            !chapters[i].position.source_line ||
            chapters[i].position.source_line > total_source_lines) return 0;
        if (i && (chapters[i].position.byte_offset <= chapters[i - 1].position.byte_offset ||
                  chapters[i].position.source_line < chapters[i - 1].position.source_line)) return 0;
    }
    return 1;
}

int index_load(PageIndex *index, TextFile *text, Layout layout,
               const ChapterRules *rules, const char *path) {
    FILE *fp;
    IndexHeader h;
    PageIndex candidate;
    TextPosition first;
    uint32_t prefix[2];
    unsigned char trailer[4];
    uint32_t crc = CRC32_INITIAL;
    uint64_t length;
    uint64_t expected_length;
    if (!index || !text || !text->fp || !path || !*path || !layout_valid(layout)) {
        errno = EINVAL;
        return 0;
    }
    fp = fopen(path, "rb");
    first = (TextPosition){text->data_start, 1};
    index_init(&candidate);
    app_log("index", "load %s", path);
    if (!fp) {
        app_log("index", "cache miss");
        return 0;
    }
    if (!file_length(fp, &length) || fread(prefix, sizeof(prefix), 1, fp) != 1) {
        fclose(fp);
        app_log("index", "cache size");
        return 0;
    }
    if (prefix[0] != INDEX_MAGIC) {
        fclose(fp);
        app_log("index", "cache fields");
        return 0;
    }
    if (prefix[1] >= LEGACY_INDEX_VERSION_MIN && prefix[1] <= LEGACY_INDEX_VERSION_MAX) {
        fclose(fp);
        app_log("index", "cache obsolete");
        return 0;
    }
    if (prefix[1] != INDEX_VERSION || fseek(fp, 0, SEEK_SET) != 0 ||
        fread(&h, 1, sizeof(h), fp) != sizeof(h)) {
        fclose(fp);
        app_log("index", "cache incompatible");
        return 0;
    }
    if (h.file_size != text->size || h.file_mtime != text->mtime ||
        h.encoding != (uint32_t)text->encoding ||
        !memchr(h.file_path, 0, sizeof(h.file_path)) || strcmp(h.file_path, text->path) ||
        !same_layout((Layout){h.font_size, h.margin, h.lines_per_page, h.line_height}, layout) ||
        h.chapter_rules_hash != chapter_rules_hash(rules)) {
        fclose(fp);
        app_log("index", "cache stale");
        return 0;
    }
    if (!h.page_count || h.page_count > 1000000 || !h.line_count || h.line_count > 1000000 ||
        h.chapter_count > MAX_CHAPTERS || h.chapters_truncated > 1 || !h.total_source_lines ||
        h.last_visible_end < first.byte_offset || h.last_visible_end > h.file_size) {
        fclose(fp);
        app_log("index", "cache fields");
        return 0;
    }
    expected_length = (uint64_t)sizeof(h) + sizeof(trailer) +
                      (uint64_t)sizeof(TextPosition) * (h.page_count + (uint64_t)h.line_count) +
                      (uint64_t)sizeof(Chapter) * h.chapter_count;
    if (length != expected_length) {
        fclose(fp);
        app_log("index", "cache size");
        return 0;
    }
    candidate.pages = malloc(h.page_count * sizeof(*candidate.pages));
    candidate.lines = malloc(h.line_count * sizeof(*candidate.lines));
    candidate.chapters = h.chapter_count ? malloc(h.chapter_count * sizeof(*candidate.chapters)) : NULL;
    if (!candidate.pages || !candidate.lines || (h.chapter_count && !candidate.chapters) ||
        fread(candidate.pages, sizeof(*candidate.pages), h.page_count, fp) != h.page_count ||
        fread(candidate.lines, sizeof(*candidate.lines), h.line_count, fp) != h.line_count ||
        fread(candidate.chapters, sizeof(*candidate.chapters), h.chapter_count, fp) != h.chapter_count ||
        fread(trailer, 1, sizeof(trailer), fp) != sizeof(trailer)) {
        fclose(fp);
        index_free(&candidate);
        app_log("index", "cache read");
        return 0;
    }
    fclose(fp);
    crc = crc32_update(crc, &h, sizeof(h));
    crc = crc32_update(crc, candidate.pages, h.page_count * sizeof(*candidate.pages));
    crc = crc32_update(crc, candidate.lines, h.line_count * sizeof(*candidate.lines));
    crc = crc32_update(crc, candidate.chapters, h.chapter_count * sizeof(*candidate.chapters));
    if (crc32_finish(crc) != get_le32(trailer)) {
        index_free(&candidate);
        app_log("index", "cache checksum");
        return 0;
    }
    if (!positions_valid(candidate.pages, h.page_count, first, h.file_size, h.total_source_lines) ||
        !positions_valid(candidate.lines, h.line_count, first, h.file_size, h.total_source_lines) ||
        !chapters_valid(candidate.chapters, h.chapter_count, first, h.file_size, h.total_source_lines) ||
        (h.page_count > 1 && candidate.pages[h.page_count - 1].byte_offset >= h.last_visible_end)) {
        index_free(&candidate);
        app_log("index", "cache fields");
        return 0;
    }
    candidate.page_count = candidate.page_capacity = h.page_count;
    candidate.line_count = candidate.line_capacity = h.line_count;
    candidate.chapter_count = candidate.chapter_capacity = h.chapter_count;
    candidate.chapters_truncated = h.chapters_truncated;
    candidate.chapter_rules_hash = h.chapter_rules_hash;
    candidate.total_source_lines = h.total_source_lines;
    candidate.last_visible_end = h.last_visible_end;
    candidate.file_size = h.file_size;
    candidate.file_mtime = h.file_mtime;
    candidate.encoding = (TextEncoding)h.encoding;
    memcpy(candidate.file_path, h.file_path, sizeof(candidate.file_path));
    candidate.layout = layout;
    index_free(index);
    *index = candidate;
    app_log("index", "cache ok pages=%lu lines=%lu chapters=%lu",
            (unsigned long)index->page_count, (unsigned long)index->line_count,
            (unsigned long)index->chapter_count);
    return 1;
}

uint32_t index_page_for_offset(const PageIndex *index, uint32_t offset) {
    uint32_t lo = 0, hi = index->page_count;
    if (!index->page_count) return 0;
    while (lo + 1 < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (index->pages[mid].byte_offset <= offset) lo = mid; else hi = mid;
    }
    return lo;
}

TextPosition index_position_for_page(const PageIndex *index, uint32_t page) {
    if (!index->page_count) return (TextPosition){0, 1};
    if (page >= index->page_count) page = index->page_count - 1;
    return index->pages[page];
}

TextPosition index_position_for_percent(const PageIndex *index, uint32_t percent) {
    uint32_t page;
    if (!index->page_count) return (TextPosition){0, 1};
    if (percent > 100) percent = 100;
    page = (uint32_t)(((uint64_t)(index->page_count - 1) * percent) / 100u);
    return index->pages[page];
}

TextPosition index_position_for_offset(const PageIndex *index, uint32_t offset) {
    return index_position_for_page(index, index_page_for_offset(index, offset));
}

int index_chapter_for_offset(const PageIndex *index, uint32_t offset, int direction,
                             TextPosition *position) {
    uint32_t lo = 0, hi = index->chapter_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (index->chapters[mid].position.byte_offset <= offset) lo = mid + 1;
        else hi = mid;
    }
    if (direction > 0 && lo < index->chapter_count) {
        *position = index->chapters[lo].position;
        return 1;
    }
    if (direction < 0 && lo > 1) {
        *position = index->chapters[lo - 2].position;
        return 1;
    }
    return 0;
}

TextPosition index_position_for_line(const PageIndex *index, uint32_t line) {
    uint32_t lo = 0, hi = index->line_count;
    if (!index->line_count) return (TextPosition){0, 1};
    while (lo + 1 < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (index->lines[mid].source_line <= line) lo = mid; else hi = mid;
    }
    return index->lines[lo];
}
