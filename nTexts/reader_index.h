#ifndef NTEXTS_READER_INDEX_H
#define NTEXTS_READER_INDEX_H

#include "text_engine.h"
#include "chapter_rules.h"
#include <ngc.h>

#define CHAPTER_TITLE_LEN 48
#define CHAPTER_SCAN_LEN 192
#define MAX_CHAPTERS 1024

typedef struct {
    TextPosition position;
    uint16_t title[CHAPTER_TITLE_LEN];
} Chapter;

typedef struct {
    int font_size;
    int margin;
    int lines_per_page;
    int line_height;
} Layout;

typedef struct {
    TextPosition *pages;
    uint32_t page_count;
    uint32_t page_capacity;
    TextPosition *lines;
    uint32_t line_count;
    uint32_t line_capacity;
    Chapter *chapters;
    uint32_t chapter_count;
    uint32_t chapter_capacity;
    uint32_t chapters_truncated;
    uint32_t chapter_rules_hash;
    uint32_t total_source_lines;
    uint32_t last_visible_end;
    uint32_t file_size;
    uint32_t file_mtime;
    TextEncoding encoding;
    char file_path[512];
    Layout layout;
} PageIndex;

typedef int (*IndexProgress)(uint32_t done, uint32_t total, void *context);

void index_init(PageIndex *index);
void index_free(PageIndex *index);
int index_build(PageIndex *index, TextFile *text, Gc gc, Layout layout,
                const ChapterRules *rules, IndexProgress progress, void *context);
int index_load(PageIndex *index, TextFile *text, Layout layout,
               const ChapterRules *rules, const char *path);
int index_save(const PageIndex *index, const char *path);
uint32_t index_page_for_offset(const PageIndex *index, uint32_t offset);
TextPosition index_position_for_page(const PageIndex *index, uint32_t page);
TextPosition index_position_for_percent(const PageIndex *index, uint32_t percent);
TextPosition index_position_for_offset(const PageIndex *index, uint32_t offset);
int index_chapter_for_offset(const PageIndex *index, uint32_t offset, int direction,
                             TextPosition *position);
TextPosition index_position_for_line(const PageIndex *index, uint32_t line);

#endif
