#ifndef NTEXTS_TEXT_ENGINE_H
#define NTEXTS_TEXT_ENGINE_H

#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#define TEXT_SEARCH_QUERY_MAX 64

typedef enum {
    TEXT_UTF8 = 1,
    TEXT_GB18030 = 2,
    TEXT_UTF16_LE = 3,
    TEXT_UTF16_BE = 4
} TextEncoding;

typedef struct {
    uint32_t byte_offset;
    uint32_t source_line;
} TextPosition;

typedef struct {
    FILE *fp;
    char path[512];
    uint32_t size;
    uint32_t mtime;
    uint32_t data_start;
    TextEncoding encoding;
} TextFile;

typedef struct {
    TextFile *text;
    uint32_t offset;
    uint32_t line;
} TextDecoder;

int text_open(TextFile *text, const char *path);
int text_select_encoding(TextFile *text, TextEncoding encoding);
void text_close(TextFile *text);
void text_decoder_at(TextFile *text, TextDecoder *decoder, TextPosition position);
TextPosition text_safe_position(TextFile *text, uint32_t offset, uint32_t fallback_line);
int text_next(TextDecoder *decoder, uint16_t *value, uint32_t *char_offset);
const char *text_encoding_name(TextEncoding encoding);
uint16_t text_fold_latin(uint16_t value);
int text_find_forward(TextFile *text, const uint16_t *query, uint32_t from, uint32_t *hit_offset);
int text_find_backward(TextFile *text, const uint16_t *query, uint32_t before,
                       const TextPosition *pages, uint32_t page_count, uint32_t *hit_offset);
void text_make_excerpt(TextFile *text, TextPosition position, uint16_t *out, size_t capacity);

#endif
