#ifndef NTEXTS_CHAPTER_RULES_H
#define NTEXTS_CHAPTER_RULES_H

#include <stdint.h>

#define CHAPTER_RULES_VERSION 2u
#define MAX_CHAPTER_PATTERNS 8
#define CHAPTER_PATTERN_LEN 48

typedef struct {
    uint32_t count;
    uint16_t patterns[MAX_CHAPTER_PATTERNS][CHAPTER_PATTERN_LEN];
} ChapterRules;

void chapter_rules_defaults(ChapterRules *rules);
int chapter_rules_validate(const ChapterRules *rules);
int chapter_rules_add(ChapterRules *rules, const uint16_t *pattern);
int chapter_rules_remove(ChapterRules *rules, uint32_t index);
int chapter_rules_match(const ChapterRules *rules, const uint16_t *line);
uint32_t chapter_rules_hash(const ChapterRules *rules);

#endif
