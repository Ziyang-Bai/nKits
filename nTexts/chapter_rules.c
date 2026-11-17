#include "chapter_rules.h"
#include "crc32.h"
#include "text_engine.h"

#include <stddef.h>
#include <string.h>

static int is_space(uint16_t value) {
    return value == 0x3000 || value == ' ' || (value >= '\t' && value <= '\r');
}

static int equal_unit(uint16_t left, uint16_t right) {
    return text_fold_latin(left) == text_fold_latin(right);
}

static size_t u16_length(const uint16_t *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

static int patterns_equal(const uint16_t *left, const uint16_t *right) {
    size_t i = 0;
    while (left[i] && right[i] && equal_unit(left[i], right[i])) ++i;
    return left[i] == 0 && right[i] == 0;
}

static int normalize_pattern(const uint16_t *pattern, uint16_t out[CHAPTER_PATTERN_LEN]) {
    size_t start = 0;
    size_t end;
    size_t length;
    size_t source;
    size_t target = 0;
    int has_literal = 0;

    if (!pattern) return 0;
    length = u16_length(pattern);
    while (start < length && is_space(pattern[start])) ++start;
    end = length;
    while (end > start && is_space(pattern[end - 1])) --end;
    for (source = start; source < end; ++source) {
        uint16_t value = pattern[source];
        if (value == '*' && target && out[target - 1] == '*') continue;
        if (target + 1 >= CHAPTER_PATTERN_LEN) return 0;
        out[target++] = value;
        if (value != '*' && value != '?') has_literal = 1;
    }
    if (!target || !has_literal) return 0;
    out[target] = 0;
    return 1;
}

static int match_pattern(const uint16_t *pattern, const uint16_t *line, const uint16_t *end) {
    const uint16_t *star = NULL;
    const uint16_t *retry = NULL;

    while (line < end) {
        if (*pattern == '*') {
            star = pattern++;
            retry = line;
        } else if (*pattern == '?' || (*pattern && equal_unit(*pattern, *line))) {
            ++pattern;
            ++line;
        } else if (star) {
            pattern = star + 1;
            line = ++retry;
        } else {
            return 0;
        }
    }
    while (*pattern == '*') ++pattern;
    return *pattern == 0;
}

static int is_chinese_number(uint16_t value) {
    if ((value >= '0' && value <= '9') || (value >= 0xff10 && value <= 0xff19)) return 1;
    switch (value) {
        case 0x3007: case 0x96f6: case 0x4e00: case 0x4e8c: case 0x4e09:
        case 0x56db: case 0x4e94: case 0x516d: case 0x4e03: case 0x516b:
        case 0x4e5d: case 0x5341: case 0x767e: case 0x5343: case 0x4e07:
        case 0x4e24:
            return 1;
        default:
            return 0;
    }
}

static int is_boundary(uint16_t value) {
    return value == 0 || is_space(value) || value == ':' || value == '.' || value == '-' ||
           value == 0x2013 || value == 0x2014;
}

static int match_builtin_chinese(const uint16_t *line, const uint16_t *end) {
    const uint16_t *p = line;
    int count = 0;
    if (p == end || *p++ != 0x7b2c) return 0;
    while (p < end && count < 12 && is_chinese_number(*p)) {
        ++p;
        ++count;
    }
    if (!count || p == end || (*p != 0x7ae0 && *p != 0x56de)) return 0;
    ++p;
    return p == end || is_boundary(*p);
}

static int match_ascii_word(const uint16_t **cursor, const uint16_t *end, const char *word) {
    const uint16_t *p = *cursor;
    while (*word) {
        if (p == end || text_fold_latin(*p) != (uint16_t)*word) return 0;
        ++p;
        ++word;
    }
    *cursor = p;
    return 1;
}

static int is_roman(uint16_t value) {
    value = text_fold_latin(value);
    return value == 'i' || value == 'v' || value == 'x' || value == 'l' ||
           value == 'c' || value == 'd' || value == 'm';
}

static int match_builtin_numbered(const uint16_t *line, const uint16_t *end,
                                  const char *word) {
    const uint16_t *p = line;
    int decimal;
    if (!match_ascii_word(&p, end, word) || p == end || !is_space(*p)) return 0;
    while (p < end && is_space(*p)) ++p;
    if (p == end) return 0;
    decimal = *p >= '0' && *p <= '9';
    if (!decimal && !is_roman(*p)) return 0;
    do {
        ++p;
    } while (p < end && (decimal ? (*p >= '0' && *p <= '9') : is_roman(*p)));
    return p == end || is_boundary(*p);
}

void chapter_rules_defaults(ChapterRules *rules) {
    memset(rules, 0, sizeof(*rules));
}

int chapter_rules_validate(const ChapterRules *rules) {
    uint32_t i;
    uint32_t j;
    uint16_t normalized[CHAPTER_PATTERN_LEN];

    if (!rules || rules->count > MAX_CHAPTER_PATTERNS) return 0;
    for (i = 0; i < rules->count; ++i) {
        size_t length = 0;
        while (length < CHAPTER_PATTERN_LEN && rules->patterns[i][length]) ++length;
        if (length == CHAPTER_PATTERN_LEN || !normalize_pattern(rules->patterns[i], normalized) ||
            memcmp(normalized, rules->patterns[i], (length + 1) * sizeof(uint16_t)) != 0) return 0;
        for (j = 0; j < i; ++j) {
            if (patterns_equal(rules->patterns[i], rules->patterns[j])) return 0;
        }
    }
    return 1;
}

int chapter_rules_add(ChapterRules *rules, const uint16_t *pattern) {
    uint16_t normalized[CHAPTER_PATTERN_LEN];
    uint32_t i;
    if (!rules || !normalize_pattern(pattern, normalized)) return 0;
    for (i = 0; i < rules->count && i < MAX_CHAPTER_PATTERNS; ++i) {
        if (patterns_equal(rules->patterns[i], normalized)) return -1;
    }
    if (rules->count >= MAX_CHAPTER_PATTERNS) return -2;
    memcpy(rules->patterns[rules->count], normalized, sizeof(normalized));
    ++rules->count;
    return 1;
}

int chapter_rules_remove(ChapterRules *rules, uint32_t index) {
    if (!rules || index >= rules->count) return 0;
    if (index + 1 < rules->count) {
        memmove(rules->patterns[index], rules->patterns[index + 1],
                (rules->count - index - 1) * sizeof(rules->patterns[0]));
    }
    --rules->count;
    memset(rules->patterns[rules->count], 0, sizeof(rules->patterns[0]));
    return 1;
}

int chapter_rules_match(const ChapterRules *rules, const uint16_t *line) {
    const uint16_t *start;
    const uint16_t *end;
    uint32_t i;
    if (!line) return 0;
    start = line;
    while (*start && is_space(*start)) ++start;
    end = start + u16_length(start);
    while (end > start && is_space(end[-1])) --end;
    if (start == end) return 0;
    if (match_builtin_chinese(start, end) ||
        match_builtin_numbered(start, end, "chapter") ||
        match_builtin_numbered(start, end, "chapitre")) return 1;
    if (!rules) return 0;
    for (i = 0; i < rules->count && i < MAX_CHAPTER_PATTERNS; ++i) {
        if (match_pattern(rules->patterns[i], start, end)) return 1;
    }
    return 0;
}

uint32_t chapter_rules_hash(const ChapterRules *rules) {
    uint32_t state = CRC32_INITIAL;
    uint32_t version = CHAPTER_RULES_VERSION;
    uint32_t count = rules ? rules->count : 0;
    uint32_t i;
    state = crc32_update(state, &version, sizeof(version));
    state = crc32_update(state, &count, sizeof(count));
    if (rules) {
        for (i = 0; i < count && i < MAX_CHAPTER_PATTERNS; ++i) {
            size_t units = u16_length(rules->patterns[i]) + 1;
            state = crc32_update(state, rules->patterns[i], units * sizeof(uint16_t));
        }
    }
    return crc32_finish(state);
}
