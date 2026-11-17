#include "chapter_rules.h"
#include "crc32.h"
#include "file_replace.h"
#include "reader_index.h"
#include "storage.h"
#include "text_engine.h"

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define APP_MAGIC 0x5053544eu
#define BOOK_MAGIC 0x4b42544eu
#define LEGACY_VERSION 3u


extern int host_rename_calls;
extern int host_rename_errno;
extern int host_char_width;
extern const char *host_documents_dir;
static const char *test_directory;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __func__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct {
    uint32_t magic, version;
    char path[512];
    uint32_t file_size, file_mtime;
    TextPosition progress;
    uint32_t bookmark_count;
    Bookmark bookmarks[MAX_BOOKMARKS];
} LegacyBookState;

typedef struct {
    uint32_t magic, version;
    int font_choice, theme, margin_choice;
    uint32_t tutorial_flags;
    uint32_t recent_count;
    RecentBook recents[MAX_RECENTS];
    uint32_t history_count;
    uint16_t history[MAX_HISTORY][QUERY_LEN];
} LegacyAppState;

typedef struct {
    uint32_t magic, version;
    char path[512];
    uint32_t file_size, file_mtime;
    uint32_t encoding;
    TextPosition progress;
    uint32_t bookmark_count;
    Bookmark bookmarks[MAX_BOOKMARKS];
} V4BookState;

typedef struct {
    uint32_t magic, version;
    int font_choice, theme, margin_choice;
    uint32_t tutorial_flags;
    uint32_t recent_count;
    RecentBook recents[MAX_RECENTS];
    uint32_t history_count;
    uint16_t history[MAX_HISTORY][QUERY_LEN];
    ChapterRules chapter_rules;
} V4AppState;

typedef struct {
    uint32_t magic, version;
    int font_choice, theme, margin_choice;
    uint32_t tutorial_flags;
    uint32_t recent_count;
    RecentBook recents[MAX_RECENTS];
    uint32_t history_count;
    uint16_t history[MAX_HISTORY][QUERY_LEN];
    ChapterRules chapter_rules;
    uint32_t locale;
} V5AppState;


static void make_path(char *out, size_t capacity, const char *leaf) {
    snprintf(out, capacity, "%s/%s", test_directory, leaf);
}

static int write_bytes(const char *path, const void *data, size_t size) {
    FILE *fp = fopen(path, "wb");
    int ok;
    if (!fp) return 0;
    ok = fwrite(data, 1, size, fp) == size && fflush(fp) == 0;
    if (fclose(fp) != 0) ok = 0;
    return ok;
}

static unsigned char *read_bytes(const char *path, size_t *size) {
    FILE *fp = fopen(path, "rb");
    unsigned char *data;
    long length;
    if (!fp || fseek(fp, 0, SEEK_END) != 0 || (length = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        if (fp) fclose(fp);
        return NULL;
    }
    data = malloc((size_t)length + 1);
    if (!data || fread(data, 1, (size_t)length, fp) != (size_t)length) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size = (size_t)length;
    return data;
}

static void put_le32(unsigned char out[4], uint32_t value) {
    out[0] = (unsigned char)value;
    out[1] = (unsigned char)(value >> 8);
    out[2] = (unsigned char)(value >> 16);
    out[3] = (unsigned char)(value >> 24);
}

static int write_checked_raw(const char *path, const void *payload, size_t payload_size) {
    unsigned char *data = malloc(payload_size + 4);
    uint32_t crc;
    int ok;
    if (!data) return 0;
    memcpy(data, payload, payload_size);
    crc = crc32_finish(crc32_update(CRC32_INITIAL, payload, payload_size));
    put_le32(data + payload_size, crc);
    ok = write_bytes(path, data, payload_size + 4);
    free(data);
    return ok;
}

static int rewrite_crc(unsigned char *data, size_t size) {
    uint32_t crc;
    if (size < 4) return 0;
    crc = crc32_finish(crc32_update(CRC32_INITIAL, data, size - 4));
    put_le32(data + size - 4, crc);
    return 1;
}

static void ascii_u16(uint16_t *out, size_t capacity, const char *text) {
    size_t i = 0;
    if (!capacity) return;
    while (i + 1 < capacity && text[i]) {
        out[i] = (unsigned char)text[i];
        ++i;
    }
    out[i] = 0;
}

static int u16_ascii_equal(const uint16_t *text, const char *ascii) {
    size_t i = 0;
    while (text[i] && ascii[i] && text[i] == (unsigned char)ascii[i]) ++i;
    return text[i] == 0 && ascii[i] == 0;
}

static int test_crc32_known_vector(void) {
    static const char vector[] = "123456789";
    uint32_t state = CRC32_INITIAL;
    state = crc32_update(state, vector, 4);
    state = crc32_update(state, vector + 4, 5);
    CHECK(crc32_finish(state) == 0xcbf43926u);
    return 1;
}

static int test_ndless_rename_fallback(void) {
    static const char first[] = "first state";
    static const char second[] = "updated state";
    char path[600];
    char temporary[604];
    FilePart part;
    unsigned char *saved;
    size_t saved_size;

    make_path(path, sizeof(path), "rename-fallback.state");
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    remove(path);
    remove(temporary);
    host_rename_calls = 0;
    host_rename_errno = ENOSYS;
    part.data = first;
    part.size = sizeof(first);
    CHECK(file_replace_parts(path, &part, 1));
    CHECK(host_rename_calls == 1);
    saved = read_bytes(path, &saved_size);
    CHECK(saved && saved_size == sizeof(first) && memcmp(saved, first, sizeof(first)) == 0);
    free(saved);
    CHECK(fopen(temporary, "rb") == NULL);

    host_rename_errno = EEXIST;
    part.data = second;
    part.size = sizeof(second);
    CHECK(file_replace_parts(path, &part, 1));
    CHECK(host_rename_calls == 2);
    saved = read_bytes(path, &saved_size);
    CHECK(saved && saved_size == sizeof(second) && memcmp(saved, second, sizeof(second)) == 0);
    free(saved);
    CHECK(fopen(temporary, "rb") == NULL);
    return 1;
}

static int test_storage_init_documents_directory(void) {
    char output[600];
    char expected[600];
    char documents[600];
    struct stat st;
    int result, saved_errno;

    host_documents_dir = NULL;
    strcpy(output, "previous");
    CHECK(!storage_init(output, sizeof(output)));
    CHECK(errno == ENOENT && output[0] == 0);
    host_documents_dir = "";
    strcpy(output, "previous");
    CHECK(!storage_init(output, sizeof(output)));
    CHECK(errno == ENOENT && output[0] == 0);

    host_documents_dir = test_directory;
    make_path(expected, sizeof(expected), "nTexts");
    CHECK(storage_init(output, sizeof(output)));
    CHECK(strcmp(output, expected) == 0);
    CHECK(stat(output, &st) == 0 && S_ISDIR(st.st_mode));
    snprintf(documents, sizeof(documents), "%s/", test_directory);
    host_documents_dir = documents;
    CHECK(storage_init(output, sizeof(output)));
    CHECK(strcmp(output, expected) == 0 && errno == 0);

    make_path(documents, sizeof(documents), "documents-file");
    CHECK(write_bytes(documents, "file", 4));
    make_path(expected, sizeof(expected), "documents-file/nTexts");
    host_documents_dir = documents;
    result = storage_init(output, sizeof(output));
    saved_errno = errno;
    host_documents_dir = ".";
    CHECK(!result && saved_errno == ENOTDIR);
    CHECK(strcmp(output, expected) == 0);
    return 1;
}

static int test_canonical_path_persistence_identity(void) {
    static const char content[] = "first line\nsecond line\nthird line";
    AppState app;
    BookState state;
    BookState loaded_state;
    ChapterRules rules;
    PageIndex built;
    PageIndex loaded_index;
    TextFile direct;
    TextFile aliased;
    Layout layout = {10, 6, 8, 20};
    char direct_path[600];
    char alias_path[600];
    char cache_path[600];

    make_path(direct_path, sizeof(direct_path), "canonical-identity.txt");
    snprintf(alias_path, sizeof(alias_path), "%s/./canonical-identity.txt", test_directory);
    CHECK(write_bytes(direct_path, content, sizeof(content) - 1));
    CHECK(text_open(&direct, direct_path));
    CHECK(text_open(&aliased, alias_path));
    CHECK(direct.path[0] == '/' && strcmp(direct.path, aliased.path) == 0);
    CHECK(storage_hash_path(direct.path) == storage_hash_path(aliased.path));

    storage_book_defaults(&state, &direct);
    state.progress.byte_offset = 11;
    state.progress.source_line = 2;
    CHECK(storage_save_book(&state, test_directory));
    CHECK(storage_load_book(&loaded_state, &aliased, test_directory));
    CHECK(loaded_state.progress.byte_offset == 11 && loaded_state.progress.source_line == 2);

    storage_app_defaults(&app);
    storage_touch_recent(&app, &loaded_state);
    CHECK(app.recent_count == 1 && strcmp(app.recents[0].path, direct.path) == 0);
    loaded_state.progress.byte_offset = 14;
    storage_touch_recent(&app, &loaded_state);
    CHECK(app.recent_count == 1 && strcmp(app.recents[0].path, direct.path) == 0);
    CHECK(app.recents[0].offset == 14 && app.recents[1].path[0] == 0);
    CHECK(!storage_prune_recents(&app));
    CHECK(strlen(alias_path) < sizeof(state.path));
    memcpy(state.path, alias_path, strlen(alias_path) + 1);
    storage_touch_recent(&app, &state);
    CHECK(app.recent_count == 2 && strcmp(app.recents[0].path, alias_path) == 0);
    storage_touch_recent(&app, &loaded_state);
    CHECK(app.recent_count == 2 && strcmp(app.recents[0].path, direct.path) == 0);
    CHECK(strcmp(app.recents[1].path, alias_path) == 0);
    storage_app_defaults(&app);
    storage_touch_recent(&app, &loaded_state);
    app.recent_count = 2;
    snprintf(app.recents[1].path, sizeof(app.recents[1].path),
             "%s/missing-recent.txt", test_directory);
    app.recents[1].file_size = 1;
    CHECK(storage_prune_recents(&app));
    CHECK(app.recent_count == 1 && strcmp(app.recents[0].path, direct.path) == 0);

    app.recent_count = MAX_RECENTS;
    for (uint32_t i = 0; i < MAX_RECENTS; ++i) {
        const char *recent_path = i % 2 ? alias_path : direct.path;
        size_t path_length = strlen(recent_path);
        CHECK(path_length < sizeof(app.recents[i].path));
        memcpy(app.recents[i].path, recent_path, path_length + 1);
        app.recents[i].offset = i;
        app.recents[i].file_size = direct.size;
    }
    CHECK(!storage_prune_recents(&app));
    CHECK(!storage_remove_recent(&app, NULL));
    CHECK(!storage_remove_recent(&app, ""));
    CHECK(!storage_remove_recent(&app, "absent"));
    CHECK(app.recent_count == MAX_RECENTS);
    for (uint32_t i = 0; i < MAX_RECENTS; ++i) {
        CHECK(strcmp(app.recents[i].path, i % 2 ? alias_path : direct.path) == 0);
        CHECK(app.recents[i].offset == i && app.recents[i].file_size == direct.size);
    }
    CHECK(storage_remove_recent(&app, alias_path));
    CHECK(app.recent_count == MAX_RECENTS / 2);
    for (uint32_t i = 0; i < MAX_RECENTS / 2; ++i) {
        CHECK(strcmp(app.recents[i].path, direct.path) == 0);
        CHECK(app.recents[i].offset == i * 2 && app.recents[i].file_size == direct.size);
    }
    for (uint32_t i = MAX_RECENTS / 2; i < MAX_RECENTS; ++i)
        CHECK(app.recents[i].path[0] == 0 && app.recents[i].offset == 0 && app.recents[i].file_size == 0);
    CHECK(!storage_remove_recent(&app, alias_path));
    CHECK(storage_remove_recent(&app, direct.path));
    CHECK(app.recent_count == 0);
    for (uint32_t i = 0; i < MAX_RECENTS; ++i)
        CHECK(app.recents[i].path[0] == 0 && app.recents[i].offset == 0 && app.recents[i].file_size == 0);

    app.recent_count = MAX_RECENTS;
    for (uint32_t i = 0; i < MAX_RECENTS; ++i) {
        if (i % 2 == 0)
            snprintf(app.recents[i].path, sizeof(app.recents[i].path), "%s", direct.path);
        else if (i == 1 || i == 7)
            make_path(app.recents[i].path, sizeof(app.recents[i].path), "missing-recent.txt");
        else if (i == 3 || i == 9)
            snprintf(app.recents[i].path, sizeof(app.recents[i].path), "%s", test_directory);
        else
            app.recents[i].path[0] = 0;
        app.recents[i].offset = i;
        app.recents[i].file_size = direct.size;
    }
    CHECK(storage_prune_recents(&app));
    CHECK(app.recent_count == MAX_RECENTS / 2);
    for (uint32_t i = 0; i < MAX_RECENTS / 2; ++i) {
        CHECK(strcmp(app.recents[i].path, direct.path) == 0);
        CHECK(app.recents[i].offset == i * 2 && app.recents[i].file_size == direct.size);
    }
    for (uint32_t i = MAX_RECENTS / 2; i < MAX_RECENTS; ++i)
        CHECK(app.recents[i].path[0] == 0 && app.recents[i].offset == 0 && app.recents[i].file_size == 0);
    CHECK(!storage_prune_recents(&app));

    chapter_rules_defaults(&rules);
    index_init(&built);
    index_init(&loaded_index);
    CHECK(index_build(&built, &direct, NULL, layout, &rules, NULL, NULL) == 1);
    storage_book_path(cache_path, sizeof(cache_path), test_directory, direct.path, "idx");
    CHECK(index_save(&built, cache_path));
    CHECK(index_load(&loaded_index, &aliased, layout, &rules, cache_path));
    index_free(&built);
    index_free(&loaded_index);
    text_close(&direct);
    text_close(&aliased);
    return 1;
}

static int test_utf16_decoding_and_positions(void) {
    static const unsigned char le[] = {
        0xff, 0xfe, 0x41, 0x00, 0x2d, 0x4e, 0x0a, 0x00, 0x42, 0x00
    };
    static const unsigned char be[] = {
        0xfe, 0xff, 0x00, 0x41, 0x4e, 0x2d, 0x00, 0x0a, 0x00, 0x42
    };
    static const unsigned char crlf[] = {
        0xff, 0xfe, 0x41, 0x00, 0x0d, 0x00, 0x0a, 0x00, 0x42, 0x00
    };
    static const unsigned char malformed[] = {
        0xff, 0xfe, 0x00, 0xdc, 0x00, 0xd8, 0x43, 0x00,
        0x3d, 0xd8, 0x00, 0xde, 0x7f
    };
    static const unsigned char bomless[] = {0x41, 0x00, 0x42, 0x00};
    static const uint16_t expected[] = {'A', 0x4e2d, '\n', 'B'};
    static const uint32_t expected_offsets[] = {2, 4, 6, 8};
    static const uint32_t malformed_offsets[] = {2, 4, 6, 8, 12};
    char path[600];
    TextFile text;
    TextDecoder decoder;
    TextPosition start = {2, 1};
    uint16_t value;
    uint32_t offset;
    int i;

    make_path(path, sizeof(path), "utf16-le.bin");
    CHECK(write_bytes(path, le, sizeof(le)) && text_open(&text, path));
    CHECK(text.encoding == TEXT_UTF16_LE && text.data_start == 2);
    CHECK(strcmp(text_encoding_name(text.encoding), "UTF-16 LE") == 0);
    text_decoder_at(&text, &decoder, start);
    for (i = 0; i < 4; ++i) {
        CHECK(text_next(&decoder, &value, &offset));
        CHECK(value == expected[i] && offset == expected_offsets[i]);
    }
    CHECK(!text_next(&decoder, &value, &offset));
    CHECK(decoder.line == 2);
    CHECK(text_safe_position(&text, 5, 9).byte_offset == 4);
    CHECK(text_safe_position(&text, text.size, 9).byte_offset == text.size);
    text_close(&text);

    make_path(path, sizeof(path), "utf16-be.bin");
    CHECK(write_bytes(path, be, sizeof(be)) && text_open(&text, path));
    CHECK(text.encoding == TEXT_UTF16_BE && text.data_start == 2);
    CHECK(strcmp(text_encoding_name(text.encoding), "UTF-16 BE") == 0);
    text_decoder_at(&text, &decoder, start);
    for (i = 0; i < 4; ++i) {
        CHECK(text_next(&decoder, &value, &offset));
        CHECK(value == expected[i] && offset == expected_offsets[i]);
    }
    text_close(&text);

    make_path(path, sizeof(path), "utf16-crlf.bin");
    CHECK(write_bytes(path, crlf, sizeof(crlf)) && text_open(&text, path));
    text_decoder_at(&text, &decoder, start);
    CHECK(text_next(&decoder, &value, &offset) && value == 'A' && offset == 2);
    CHECK(text_next(&decoder, &value, &offset) && value == '\r' && offset == 4 && decoder.line == 1);
    CHECK(text_next(&decoder, &value, &offset) && value == '\n' && offset == 6 && decoder.line == 2);
    CHECK(text_next(&decoder, &value, &offset) && value == 'B' && offset == 8);
    text_close(&text);

    make_path(path, sizeof(path), "utf16-malformed.bin");
    CHECK(write_bytes(path, malformed, sizeof(malformed)) && text_open(&text, path));
    text_decoder_at(&text, &decoder, start);
    for (i = 0; i < 5; ++i) {
        CHECK(text_next(&decoder, &value, &offset));
        CHECK(offset == malformed_offsets[i]);
        CHECK(i == 2 ? value == 'C' : value == 0xfffd);
    }
    CHECK(!text_next(&decoder, &value, &offset));
    CHECK(text_safe_position(&text, 11, 1).byte_offset == 8);
    CHECK(text_safe_position(&text, text.size, 1).byte_offset == text.size);
    text_close(&text);

    make_path(path, sizeof(path), "utf16-bomless.bin");
    CHECK(write_bytes(path, bomless, sizeof(bomless)) && text_open(&text, path));
    CHECK(text.encoding != TEXT_UTF16_LE && text.encoding != TEXT_UTF16_BE && text.data_start == 0);
    text_close(&text);
    CHECK(strcmp(text_encoding_name((TextEncoding)99), "Unknown") == 0);
    return 1;
}

static int test_manual_encoding_selection(void) {
    static const unsigned char le[] = {'A', 0, 0x2d, 0x4e, '\n', 0};
    static const unsigned char be[] = {0, 'A', 0x4e, 0x2d, 0, '\n'};
    static const unsigned char gb[] = {0xd6, 0xd0, '\n'};
    static const unsigned char utf8_bom[] = {0xef, 0xbb, 0xbf, 'A'};
    char path[600];
    TextFile text;
    TextDecoder decoder;
    TextPosition start;
    uint16_t value;
    uint32_t offset;

    make_path(path, sizeof(path), "manual-utf16-le.txt");
    CHECK(write_bytes(path, le, sizeof(le)) && text_open(&text, path));
    CHECK(text.encoding == TEXT_UTF8);
    CHECK(text_select_encoding(&text, TEXT_UTF16_LE) && text.data_start == 0);
    start.byte_offset = text.data_start;
    start.source_line = 1;
    text_decoder_at(&text, &decoder, start);
    CHECK(text_next(&decoder, &value, &offset) && value == 'A' && offset == 0);
    CHECK(text_next(&decoder, &value, &offset) && value == 0x4e2d && offset == 2);
    CHECK(text_next(&decoder, &value, &offset) && value == '\n' && offset == 4);
    CHECK(decoder.line == 2);
    CHECK(!text_select_encoding(&text, (TextEncoding)99) && text.encoding == TEXT_UTF16_LE);
    text_close(&text);

    make_path(path, sizeof(path), "manual-utf16-be.txt");
    CHECK(write_bytes(path, be, sizeof(be)) && text_open(&text, path));
    CHECK(text_select_encoding(&text, TEXT_UTF16_BE) && text.data_start == 0);
    text_decoder_at(&text, &decoder, start);
    CHECK(text_next(&decoder, &value, &offset) && value == 'A' && offset == 0);
    CHECK(text_next(&decoder, &value, &offset) && value == 0x4e2d && offset == 2);
    text_close(&text);

    make_path(path, sizeof(path), "manual-gb.txt");
    CHECK(write_bytes(path, gb, sizeof(gb)) && text_open(&text, path));
    CHECK(text.encoding == TEXT_GB18030);
    CHECK(text_select_encoding(&text, TEXT_GB18030) && text.data_start == 0);
    text_decoder_at(&text, &decoder, start);
    CHECK(text_next(&decoder, &value, &offset) && value == 0x4e2d && offset == 0);
    text_close(&text);

    make_path(path, sizeof(path), "manual-utf8-bom.txt");
    CHECK(write_bytes(path, utf8_bom, sizeof(utf8_bom)) && text_open(&text, path));
    CHECK(text_select_encoding(&text, TEXT_UTF8) && text.data_start == 3);
    start.byte_offset = text.data_start;
    text_decoder_at(&text, &decoder, start);
    CHECK(text_next(&decoder, &value, &offset) && value == 'A' && offset == 3);
    text_close(&text);
    return 1;
}

static int test_multilingual_utf8_stream(void) {
    static const char content[] = "A \xe6\x97\xa5 \xed\x95\x9c \xe4\xb8\xad\n";
    uint16_t japanese[] = {0x65e5, 0};
    uint16_t korean[] = {0xd55c, 0};
    uint16_t chinese[] = {0x4e2d, 0};
    char path[600];
    TextFile text;
    TextDecoder decoder;
    TextPosition start = {0, 1};
    uint16_t value;
    uint32_t offset, hit;

    make_path(path, sizeof(path), "multilingual-utf8.txt");
    CHECK(write_bytes(path, content, sizeof(content) - 1) && text_open(&text, path));
    CHECK(text.encoding == TEXT_UTF8);
    text_decoder_at(&text, &decoder, start);
    CHECK(text_next(&decoder, &value, &offset) && value == 'A');
    CHECK(text_next(&decoder, &value, &offset) && value == ' ');
    CHECK(text_next(&decoder, &value, &offset) && value == 0x65e5 && offset == 2);
    CHECK(text_find_forward(&text, japanese, 0, &hit) && hit == 2);
    CHECK(text_find_forward(&text, korean, 0, &hit) && hit == 6);
    CHECK(text_find_forward(&text, chinese, 0, &hit) && hit == 10);
    text_close(&text);
    return 1;
}

static int test_french_chapters_and_index_cache(void) {
    static const uint16_t arabic[] = {'C','h','a','p','i','t','r','e',' ','2',0};
    static const uint16_t roman[] = {'c','H','a','P','i','T','r','E',' ','I','V',':',' ','F','i','n',0};
    static const uint16_t prose[] = {'M','o','n',' ','c','h','a','p','i','t','r','e',' ','3',0};
    static const uint16_t word[] = {'C','h','a','p','i','t','r','e',' ','D','e','u','x',0};
    static const uint16_t suffix[] = {'C','h','a','p','i','t','r','e',' ','2','a',0};
    static const uint16_t joined[] = {'C','h','a','p','i','t','r','e','2',0};
    static const uint16_t roman_suffix[] = {'C','h','a','p','i','t','r','e',' ','I','V','s',0};
    static const uint16_t custom[] = {0x00c9, 'p', 'i', 's', 'o', 'd', 'e', ' ', '*', 0};
    static const uint16_t custom_lower[] = {0x00e9, 'P', 'I', 'S', 'O', 'D', 'E', ' ', '1', 0};
    static const uint16_t custom_plain[] = {'E', 'p', 'i', 's', 'o', 'd', 'e', ' ', '1', 0};
    static const char content[] =
        "Paragraphe ordinaire\n"
        "Chapitre 2 - \xc3\x89t\xc3\xa9\n"
        "chapitre IV: fin\n"
        "Mon chapitre 3 dans le texte\n"
        "Chapitre Deux\n";
    ChapterRules rules;
    PageIndex built;
    PageIndex loaded;
    TextFile text;
    TextPosition target;
    Layout layout = {10, 6, 10, 20};
    uint32_t old_version = 1;
    uint32_t count = 0;
    uint32_t old_hash;
    uint32_t current_hash;
    char path[600];
    char cache[600];

    chapter_rules_defaults(&rules);
    CHECK(chapter_rules_match(&rules, arabic));
    CHECK(chapter_rules_match(&rules, roman));
    CHECK(!chapter_rules_match(&rules, prose));
    CHECK(!chapter_rules_match(&rules, word));
    CHECK(!chapter_rules_match(&rules, suffix));
    CHECK(!chapter_rules_match(&rules, joined));
    CHECK(!chapter_rules_match(&rules, roman_suffix));
    CHECK(chapter_rules_add(&rules, custom) == 1);
    CHECK(chapter_rules_match(&rules, custom_lower));
    CHECK(!chapter_rules_match(&rules, custom_plain));
    chapter_rules_defaults(&rules);
    old_hash = crc32_finish(crc32_update(crc32_update(CRC32_INITIAL,
                                  &old_version, sizeof(old_version)), &count, sizeof(count)));
    current_hash = chapter_rules_hash(&rules);
    CHECK(old_hash != current_hash);

    make_path(path, sizeof(path), "french-chapters.txt");
    make_path(cache, sizeof(cache), "french-chapters.idx");
    CHECK(write_bytes(path, content, sizeof(content) - 1) && text_open(&text, path));
    index_init(&built);
    index_init(&loaded);
    CHECK(index_build(&built, &text, NULL, layout, &rules, NULL, NULL) == 1);
    CHECK(built.chapter_count == 2 && built.chapter_rules_hash == current_hash);
    CHECK(built.chapters[0].position.byte_offset == sizeof("Paragraphe ordinaire\n") - 1);
    CHECK(built.chapters[1].position.source_line == 3);
    CHECK(index_chapter_for_offset(&built, 0, 1, &target) &&
          target.byte_offset == built.chapters[0].position.byte_offset);
    CHECK(index_chapter_for_offset(&built, built.chapters[1].position.byte_offset,
                                   -1, &target) &&
          target.byte_offset == built.chapters[0].position.byte_offset);
    built.chapter_rules_hash = old_hash;
    CHECK(index_save(&built, cache));
    CHECK(!index_load(&loaded, &text, layout, &rules, cache));
    built.chapter_rules_hash = current_hash;
    CHECK(index_save(&built, cache));
    CHECK(index_load(&loaded, &text, layout, &rules, cache));
    CHECK(loaded.chapter_count == 2);
    CHECK(index_chapter_for_offset(&loaded, loaded.chapters[0].position.byte_offset,
                                   1, &target) &&
          target.byte_offset == loaded.chapters[1].position.byte_offset);
    index_free(&loaded);
    index_free(&built);
    text_close(&text);
    return 1;
}

static int test_chapter_rules_and_index_round_trip(void) {
    static const uint16_t chinese_heading[] = {0x7b2c, 0x5341, 0x4e8c, 0x7ae0, ' ', 0x5f00, 0x59cb, 0};
    static const uint16_t chinese_near_miss[] = {0x7b2c, 0x5341, 0x4e8c, 0x8282, 0};
    static const uint16_t roman_heading[] = {'c','H','a','P','t','E','r',' ','I','V',0x2014,'E','n','d',0};
    static const uint16_t word_heading[] = {'C','h','a','p','t','e','r',' ','O','n','e',0};
    static const uint16_t markdown_heading[] = {'#',' ','C','h','a','p','t','e','r',' ','1',0};
    static const char prefix[] = "Chapter IV - End\nordinary Act 1 - Start prose\nAct 1 - ";
    static const char suffix[] = "\nAct 2 - Final";
    uint16_t pattern[CHAPTER_PATTERN_LEN];
    uint16_t duplicate[CHAPTER_PATTERN_LEN];
    uint16_t only_wildcards[CHAPTER_PATTERN_LEN];
    uint16_t overlong[64];
    uint16_t matching[64];
    uint16_t nonmatching[64];
    ChapterRules rules;
    ChapterRules defaults;
    PageIndex built;
    PageIndex loaded;
    PageIndex capped;
    TextFile text;
    TextPosition chapter_target;
    Layout layout = {10, 6, 10, 20};
    char path[600];
    char cache[600];
    char cap_path[600];
    FILE *fp;
    int i;

    chapter_rules_defaults(&rules);
    chapter_rules_defaults(&defaults);
    CHECK(chapter_rules_match(&rules, chinese_heading));
    CHECK(chapter_rules_match(&rules, roman_heading));
    CHECK(!chapter_rules_match(&rules, chinese_near_miss));
    CHECK(!chapter_rules_match(&rules, word_heading));
    CHECK(!chapter_rules_match(&rules, markdown_heading));

    memset(pattern, 0, sizeof(pattern));
    pattern[0] = 0x3000;
    ascii_u16(pattern + 1, CHAPTER_PATTERN_LEN - 1, "  Act **? - *  ");
    ascii_u16(duplicate, CHAPTER_PATTERN_LEN, "act *? - *");
    ascii_u16(only_wildcards, CHAPTER_PATTERN_LEN, "**??");
    ascii_u16(matching, 64, "ACT 1 - Start");
    ascii_u16(nonmatching, 64, "text ACT 1 - Start");
    for (i = 0; i < 63; ++i) overlong[i] = 'A';
    overlong[63] = 0;
    CHECK(chapter_rules_add(&rules, pattern) == 1);
    CHECK(chapter_rules_validate(&rules));
    CHECK(u16_ascii_equal(rules.patterns[0], "Act *? - *"));
    CHECK(chapter_rules_add(&rules, duplicate) == -1);
    CHECK(chapter_rules_add(&rules, only_wildcards) == 0);
    CHECK(chapter_rules_add(&rules, overlong) == 0);
    CHECK(chapter_rules_match(&rules, matching));
    CHECK(!chapter_rules_match(&rules, nonmatching));
    CHECK(chapter_rules_remove(&rules, 9) == 0);

    make_path(path, sizeof(path), "chapters.txt");
    fp = fopen(path, "wb");
    CHECK(fp != NULL);
    CHECK(fwrite(prefix, 1, sizeof(prefix) - 1, fp) == sizeof(prefix) - 1);
    for (i = 0; i < 60; ++i) CHECK(fputc('x', fp) != EOF);
    CHECK(fwrite(suffix, 1, sizeof(suffix) - 1, fp) == sizeof(suffix) - 1);
    CHECK(fclose(fp) == 0);
    CHECK(text_open(&text, path));
    index_init(&built);
    index_init(&loaded);
    CHECK(index_build(&built, &text, NULL, layout, &rules, NULL, NULL) == 1);
    CHECK(built.chapter_count == 3 && !built.chapters_truncated);
    CHECK(built.chapters[0].position.byte_offset == 0 && built.chapters[2].position.source_line == 4);
    CHECK(built.chapters[1].title[CHAPTER_TITLE_LEN - 2] == 0x2026);
    CHECK(built.chapters[1].title[CHAPTER_TITLE_LEN - 1] == 0);
    make_path(cache, sizeof(cache), "chapters.idx");
    remove(cache);
    CHECK(index_save(&built, cache));
    CHECK(index_load(&loaded, &text, layout, &rules, cache));
    CHECK(loaded.chapter_count == 3 && loaded.chapter_rules_hash == chapter_rules_hash(&rules));
    CHECK(index_chapter_for_offset(&loaded, 0, 1, &chapter_target) &&
          chapter_target.byte_offset == loaded.chapters[1].position.byte_offset);
    CHECK(index_chapter_for_offset(&loaded, loaded.chapters[1].position.byte_offset + 1,
                                   -1, &chapter_target) && chapter_target.byte_offset == 0);
    CHECK(index_chapter_for_offset(&loaded, loaded.chapters[2].position.byte_offset,
                                   -1, &chapter_target) &&
          chapter_target.byte_offset == loaded.chapters[1].position.byte_offset);
    CHECK(!index_chapter_for_offset(&loaded, 0, -1, &chapter_target));
    CHECK(!index_chapter_for_offset(&loaded, loaded.chapters[2].position.byte_offset,
                                    1, &chapter_target));
    index_free(&loaded);
    CHECK(!index_chapter_for_offset(&loaded, 0, 1, &chapter_target));
    loaded.total_source_lines = 77;
    CHECK(!index_load(&loaded, &text, layout, &defaults, cache));
    CHECK(loaded.total_source_lines == 77);
    index_free(&loaded);
    index_free(&built);
    text_close(&text);

    {
        char content[48];
        char expected[48];
        char padded[50];
        memcpy(content, "Chapter 1 ", 10);
        memset(content + 10, 'x', 36);
        content[46] = 'Z';
        content[47] = ' ';
        memcpy(expected, content, 47);
        expected[47] = 0;
        make_path(path, sizeof(path), "chapter-title-boundary.txt");
        make_path(cache, sizeof(cache), "chapter-title-boundary.idx");
        CHECK(write_bytes(path, content, sizeof(content)));
        CHECK(text_open(&text, path));
        CHECK(index_build(&built, &text, NULL, layout, &defaults, NULL, NULL) == 1);
        CHECK(built.chapter_count == 1);
        CHECK(built.chapters[0].position.byte_offset == 0);
        CHECK(built.chapters[0].position.source_line == 1);
        CHECK(u16_ascii_equal(built.chapters[0].title, expected));
        CHECK(built.chapters[0].title[47] == 0);
        CHECK(index_save(&built, cache));
        CHECK(index_load(&loaded, &text, layout, &defaults, cache));
        CHECK(loaded.chapter_count == 1);
        CHECK(u16_ascii_equal(loaded.chapters[0].title, expected));
        index_free(&loaded);
        index_free(&built);
        text_close(&text);

        padded[0] = padded[1] = ' ';
        memcpy(padded + 2, content, sizeof(content));
        padded[49] = '\t';
        CHECK(write_bytes(path, padded, sizeof(padded)));
        CHECK(text_open(&text, path));
        CHECK(index_build(&built, &text, NULL, layout, &defaults, NULL, NULL) == 1);
        CHECK(built.chapter_count == 1);
        CHECK(built.chapters[0].position.byte_offset == 0);
        CHECK(built.chapters[0].position.source_line == 1);
        CHECK(u16_ascii_equal(built.chapters[0].title, expected));
        index_free(&built);
        text_close(&text);
    }

    make_path(cap_path, sizeof(cap_path), "chapter-cap.txt");
    fp = fopen(cap_path, "wb");
    CHECK(fp != NULL);
    for (i = 1; i <= 1025; ++i) CHECK(fprintf(fp, "Chapter %d\n", i) > 0);
    CHECK(fclose(fp) == 0 && text_open(&text, cap_path));
    index_init(&capped);
    CHECK(index_build(&capped, &text, NULL, layout, &defaults, NULL, NULL) == 1);
    CHECK(capped.chapter_count == MAX_CHAPTERS && capped.chapters_truncated == 1);
    index_free(&capped);
    text_close(&text);
    return 1;
}

static int test_bookmark_excerpt(void) {
    static const unsigned char content[] = {
        0xff,0xfe,
        0x20,0x00,0x09,0x00,0x0d,0x00,0x0a,0x00,
        0x00,0x30,0x41,0x00,0x6c,0x00,0x70,0x00,0x68,0x00,0x61,0x00,
        0x09,0x00,0x20,0x00,0x20,0x00,0x62,0x00,0x65,0x00,0x74,0x00,0x61,0x00,
        0x20,0x00,0x0a,0x00,0x5a,0x00
    };
    char path[600];
    TextFile text;
    TextPosition start = {2, 1};
    uint16_t excerpt[32];
    uint16_t bounded[6];
    uint16_t sentinel = 0x5555;
    size_t i;

    make_path(path, sizeof(path), "excerpt.bin");
    CHECK(write_bytes(path, content, sizeof(content)) && text_open(&text, path));
    text_make_excerpt(&text, start, excerpt, 32);
    CHECK(u16_ascii_equal(excerpt, "Alpha beta"));
    for (i = 0; i < 6; ++i) bounded[i] = 0x7777;
    text_make_excerpt(&text, start, bounded, 5);
    CHECK(bounded[4] == 0 && bounded[5] == 0x7777);
    text_make_excerpt(&text, start, &sentinel, 0);
    CHECK(sentinel == 0x5555);
    text_close(&text);
    return 1;
}

static int test_app_state_integrity_and_v3_migration(void) {
    AppState state;
    AppState loaded;
    AppState invalid;
    LegacyAppState legacy;
    char path[600];
    unsigned char *data;
    size_t size;
    uint16_t empty_query[QUERY_LEN] = {0};

    make_path(path, sizeof(path), "app.state");
    remove(path);
    storage_app_defaults(&state);
    storage_add_history(&state, empty_query);
    CHECK(state.history_count == 0);
    state.theme = 2;
    state.history_count = 1;
    state.history[0][0] = 'x';
    CHECK(storage_save_app(&state, test_directory));
    CHECK(storage_load_app(&loaded, test_directory) && loaded.theme == 2 && loaded.locale == 0);
    state.locale = 2;
    CHECK(storage_save_app(&state, test_directory));
    CHECK(storage_load_app(&loaded, test_directory) && loaded.locale == 2 && loaded.theme == 2);
    state.locale = 0;

    invalid = state;
    invalid.magic = 0;
    invalid.version = 0;
    invalid.theme = 99;
    invalid.shortcut_hints_disabled = 0x7fffffffu;
    invalid.locale = 99;
    invalid.recent_count = MAX_RECENTS + 1;
    snprintf(invalid.recents[0].path, sizeof(invalid.recents[0].path), "%s", "recoverable.txt");
    invalid.recents[0].offset = 9;
    invalid.recents[0].file_size = 3;
    invalid.history_count = MAX_HISTORY + 1;
    memset(invalid.history[0], 'q', sizeof(invalid.history[0]));
    invalid.chapter_rules.count = MAX_CHAPTER_PATTERNS + 1;
    CHECK(storage_save_app(&invalid, test_directory));
    CHECK(storage_load_app(&loaded, test_directory));
    CHECK(loaded.magic == APP_MAGIC && loaded.version == 6 && loaded.theme == 0 && loaded.locale == 0);
    CHECK(loaded.shortcut_hints_disabled == 0);
    CHECK(loaded.recent_count == 1 && loaded.recents[0].offset == 3);
    CHECK(loaded.history_count == 1 && loaded.history[0][QUERY_LEN - 1] == 0);
    CHECK(chapter_rules_validate(&loaded.chapter_rules) && loaded.chapter_rules.count == 0);

    data = read_bytes(path, &size);
    CHECK(data != NULL);
    data[100] ^= 1;
    CHECK(write_bytes(path, data, size));
    free(data);
    CHECK(!storage_load_app(&loaded, test_directory) && loaded.theme == 0);

    remove(path);
    CHECK(storage_save_app(&state, test_directory));
    data = read_bytes(path, &size);
    CHECK(data != NULL);
    data[size - 1] ^= 1;
    CHECK(write_bytes(path, data, size));
    free(data);
    CHECK(!storage_load_app(&loaded, test_directory));

    remove(path);
    CHECK(storage_save_app(&state, test_directory));
    data = read_bytes(path, &size);
    CHECK(data && write_bytes(path, data, size - 1));
    free(data);
    CHECK(!storage_load_app(&loaded, test_directory));

    remove(path);
    CHECK(storage_save_app(&state, test_directory));
    data = read_bytes(path, &size);
    CHECK(data != NULL);
    data = realloc(data, size + 1);
    CHECK(data != NULL);
    data[size] = 0;
    CHECK(write_bytes(path, data, size + 1));
    free(data);
    CHECK(!storage_load_app(&loaded, test_directory));

    invalid = state;
    invalid.theme = 99;
    CHECK(write_checked_raw(path, &invalid, sizeof(invalid)));
    CHECK(!storage_load_app(&loaded, test_directory) && loaded.theme == 0);

    invalid = state;
    invalid.locale = 99;
    CHECK(write_checked_raw(path, &invalid, sizeof(invalid)));
    CHECK(!storage_load_app(&loaded, test_directory) && loaded.locale == 0);

    invalid = state;
    invalid.shortcut_hints_disabled = 2;
    CHECK(write_checked_raw(path, &invalid, sizeof(invalid)));
    CHECK(!storage_load_app(&loaded, test_directory) && loaded.shortcut_hints_disabled == 0);

    memset(&legacy, 0, sizeof(legacy));
    legacy.magic = APP_MAGIC;
    legacy.version = LEGACY_VERSION;
    legacy.font_choice = 2;
    legacy.theme = 1;
    legacy.margin_choice = 1;
    legacy.tutorial_flags = 0x80000001u;
    legacy.recent_count = 1;
    snprintf(legacy.recents[0].path, sizeof(legacy.recents[0].path), "legacy.txt");
    legacy.recents[0].file_size = 50;
    legacy.recents[0].offset = 12;
    legacy.history_count = 1;
    legacy.history[0][0] = 'q';
    CHECK(sizeof(legacy) == 6512 && write_bytes(path, &legacy, sizeof(legacy)));
    CHECK(storage_load_app(&loaded, test_directory));
    CHECK(loaded.version == 6 && loaded.font_choice == 2 && loaded.theme == 1);
    CHECK(loaded.margin_choice == 1 && loaded.locale == 0 && loaded.shortcut_hints_disabled == 0);
    CHECK(loaded.recent_count == 1 && loaded.recents[0].offset == 12 &&
          strcmp(loaded.recents[0].path, "legacy.txt") == 0);
    CHECK(loaded.history_count == 1 && loaded.history[0][0] == 'q');
    CHECK(loaded.chapter_rules.count == 0 && chapter_rules_validate(&loaded.chapter_rules));
    return 1;
}

static int test_shortcut_hints_preferences_persist(void) {
    static const uint32_t invalid_flags[] = {2u, 0x80000000u, 0xffffffffu};
    AppState state;
    AppState loaded;
    AppState invalid;
    char path[600];
    size_t i;

    make_path(path, sizeof(path), "app.state");
    storage_app_defaults(&state);
    CHECK(state.version == 6 && state.shortcut_hints_disabled == 0);
    state.theme = 2;
    state.locale = 1;
    state.history_count = 1;
    state.history[0][0] = 'h';
    state.shortcut_hints_disabled = 1;
    CHECK(storage_save_app(&state, test_directory));
    CHECK(storage_load_app(&loaded, test_directory));
    CHECK(loaded.version == 6 && loaded.shortcut_hints_disabled == 1);
    CHECK(loaded.theme == 2 && loaded.locale == 1 &&
          loaded.history_count == 1 && loaded.history[0][0] == 'h');
    state.shortcut_hints_disabled = 0;
    CHECK(storage_save_app(&state, test_directory));
    CHECK(storage_load_app(&loaded, test_directory) && loaded.shortcut_hints_disabled == 0);

    for (i = 0; i < sizeof(invalid_flags) / sizeof(invalid_flags[0]); ++i) {
        invalid = state;
        invalid.shortcut_hints_disabled = invalid_flags[i];
        CHECK(write_checked_raw(path, &invalid, sizeof(invalid)));
        CHECK(!storage_load_app(&loaded, test_directory));
        CHECK(loaded.version == 6 && loaded.shortcut_hints_disabled == 0);
        CHECK(storage_save_app(&invalid, test_directory));
        CHECK(storage_load_app(&loaded, test_directory));
        CHECK(loaded.shortcut_hints_disabled == 0 && loaded.theme == 2 && loaded.locale == 1);
        CHECK(loaded.history_count == 1 && loaded.history[0][0] == 'h');
    }
    return 1;
}

static int test_v5_preferences_migrate_with_hints_enabled(void) {
    static const uint32_t old_flags[] = {0u, 1u, 0x80000000u, 0x80000001u};
    static const uint16_t pattern[] = {'A', 'c', 't', ' ', '*', 0};
    V5AppState old_app;
    AppState app;
    char path[600];
    unsigned char *data;
    size_t size;
    size_t i;
    uint32_t locale;

    memset(&old_app, 0, sizeof(old_app));
    old_app.magic = APP_MAGIC;
    old_app.version = 5;
    old_app.font_choice = 2;
    old_app.theme = 1;
    old_app.margin_choice = 1;
    old_app.recent_count = 1;
    snprintf(old_app.recents[0].path, sizeof(old_app.recents[0].path), "v5-book.txt");
    old_app.recents[0].file_size = 100;
    old_app.recents[0].offset = 37;
    old_app.history_count = 1;
    old_app.history[0][0] = 0x4e2d;
    old_app.history[0][1] = 0x00e9;
    chapter_rules_defaults(&old_app.chapter_rules);
    CHECK(chapter_rules_add(&old_app.chapter_rules, pattern) == 1);
    CHECK(sizeof(old_app) == 7288 && sizeof(old_app) == sizeof(AppState));
    make_path(path, sizeof(path), "app.state");

    for (i = 0; i < sizeof(old_flags) / sizeof(old_flags[0]); ++i) {
        old_app.tutorial_flags = old_flags[i];
        for (locale = 0; locale <= 2; ++locale) {
            old_app.locale = locale;
            CHECK(write_checked_raw(path, &old_app, sizeof(old_app)));
            CHECK(storage_load_app(&app, test_directory));
            CHECK(app.version == 6 && app.shortcut_hints_disabled == 0 && app.locale == locale);
            CHECK(app.font_choice == 2 && app.theme == 1 && app.margin_choice == 1);
            CHECK(app.recent_count == 1 && app.history_count == 1);
            CHECK(memcmp(app.recents, old_app.recents, sizeof(app.recents)) == 0);
            CHECK(memcmp(app.history, old_app.history, sizeof(app.history)) == 0);
            CHECK(memcmp(&app.chapter_rules, &old_app.chapter_rules, sizeof(app.chapter_rules)) == 0);
            CHECK(storage_save_app(&app, test_directory));
            CHECK(storage_load_app(&app, test_directory));
            CHECK(app.version == 6 && app.shortcut_hints_disabled == 0 && app.locale == locale);
        }
    }

    CHECK(write_checked_raw(path, &old_app, sizeof(old_app)));
    data = read_bytes(path, &size);
    CHECK(data != NULL);
    data[100] ^= 1;
    CHECK(write_bytes(path, data, size));
    free(data);
    CHECK(!storage_load_app(&app, test_directory));
    CHECK(app.version == 6 && app.shortcut_hints_disabled == 0);

    old_app.tutorial_flags = 2;
    CHECK(write_checked_raw(path, &old_app, sizeof(old_app)));
    CHECK(!storage_load_app(&app, test_directory));
    old_app.tutorial_flags = 1;
    old_app.locale = 3;
    CHECK(write_checked_raw(path, &old_app, sizeof(old_app)));
    CHECK(!storage_load_app(&app, test_directory));
    return 1;
}

static int test_book_state_integrity_encoding_and_v3_migration(void) {
    TextFile text;
    TextFile changed;
    BookState state;
    BookState loaded;
    BookState invalid;
    LegacyBookState legacy;
    char path[600];
    unsigned char *data;
    size_t size;

    memset(&text, 0, sizeof(text));
    snprintf(text.path, sizeof(text.path), "%s", "book-identity.txt");
    text.size = 100;
    text.mtime = 7;
    text.encoding = TEXT_UTF8;
    text.data_start = 0;
    storage_book_path(path, sizeof(path), test_directory, text.path, "state");
    remove(path);
    storage_book_defaults(&state, &text);
    state.progress.byte_offset = 20;
    state.bookmark_count = 1;
    state.bookmarks[0].position.byte_offset = 90;
    state.bookmarks[0].position.source_line = 9;
    state.bookmarks[0].excerpt[0] = 's';
    state.bookmarks[0].excerpt[1] = 0;
    CHECK(storage_save_book(&state, test_directory));
    CHECK(storage_load_book(&loaded, &text, test_directory));
    CHECK(loaded.encoding == TEXT_UTF8 && loaded.progress.byte_offset == 20);

    data = read_bytes(path, &size);
    CHECK(data != NULL);
    data[80] ^= 1;
    CHECK(write_bytes(path, data, size));
    free(data);
    CHECK(!storage_load_book(&loaded, &text, test_directory));
    CHECK(loaded.progress.byte_offset == text.data_start && loaded.encoding == TEXT_UTF8);

    remove(path);
    CHECK(storage_save_book(&state, test_directory));
    changed = text;
    changed.encoding = TEXT_GB18030;
    CHECK(storage_load_book(&loaded, &changed, test_directory));
    CHECK(loaded.encoding == TEXT_GB18030 && loaded.progress.byte_offset == 0);
    CHECK(loaded.bookmark_count == 1 && loaded.bookmarks[0].position.byte_offset == 90);

    invalid = state;
    invalid.encoding = 99;
    CHECK(write_checked_raw(path, &invalid, sizeof(invalid)));
    CHECK(!storage_load_book(&loaded, &text, test_directory));

    invalid = state;
    invalid.encoding_override = 99;
    CHECK(write_checked_raw(path, &invalid, sizeof(invalid)));
    CHECK(storage_load_book(&loaded, &text, test_directory));
    CHECK(loaded.encoding_override == 0 && loaded.progress.byte_offset == 20);

    memset(&legacy, 0, sizeof(legacy));
    legacy.magic = BOOK_MAGIC;
    legacy.version = LEGACY_VERSION;
    snprintf(legacy.path, sizeof(legacy.path), "%s", text.path);
    legacy.file_size = text.size;
    legacy.file_mtime = text.mtime;
    legacy.progress.byte_offset = 30;
    legacy.progress.source_line = 3;
    CHECK(sizeof(legacy) == 2700 && write_bytes(path, &legacy, sizeof(legacy)));
    CHECK(storage_load_book(&loaded, &text, test_directory));
    CHECK(loaded.version == 5 && loaded.encoding == TEXT_UTF8 && loaded.progress.byte_offset == 30);

    CHECK(write_bytes(path, &legacy, sizeof(legacy)));
    changed = text;
    changed.encoding = TEXT_UTF16_LE;
    changed.data_start = 2;
    CHECK(!storage_load_book(&loaded, &changed, test_directory));
    CHECK(loaded.encoding == TEXT_UTF16_LE && loaded.progress.byte_offset == 2);

    remove(path);
    CHECK(storage_save_book(&state, test_directory));
    changed = text;
    changed.size = 50;
    changed.mtime = 8;
    CHECK(storage_load_book(&loaded, &changed, test_directory));
    CHECK(loaded.progress.byte_offset == changed.data_start);
    CHECK(loaded.bookmark_count == 1 && loaded.bookmarks[0].position.byte_offset == 50);
    CHECK(loaded.bookmarks[0].position.source_line == 9);
    CHECK(loaded.bookmarks[0].excerpt[0] == 's');
    return 1;
}

static int test_v4_preferences_and_bookmarks_migrate(void) {
    V4AppState old_app;
    V4BookState old_book;
    AppState app;
    BookState book;
    TextFile text;
    char path[600];

    memset(&old_app, 0, sizeof(old_app));
    old_app.magic = APP_MAGIC;
    old_app.version = 4;
    old_app.font_choice = 2;
    old_app.theme = 1;
    old_app.tutorial_flags = 0x80000001u;
    old_app.recent_count = 1;
    snprintf(old_app.recents[0].path, sizeof(old_app.recents[0].path), "migrate.txt");
    old_app.recents[0].file_size = 50;
    old_app.recents[0].offset = 10;
    old_app.history_count = 1;
    old_app.history[0][0] = 0x65e5;
    chapter_rules_defaults(&old_app.chapter_rules);
    make_path(path, sizeof(path), "app.state");
    CHECK(write_checked_raw(path, &old_app, sizeof(old_app)));
    CHECK(storage_load_app(&app, test_directory));
    CHECK(app.version == 6 && app.locale == 0 && app.font_choice == 2 && app.theme == 1);
    CHECK(app.shortcut_hints_disabled == 0 &&
          app.recent_count == 1 && app.recents[0].offset == 10);
    CHECK(app.history_count == 1 && app.history[0][0] == 0x65e5);
    app.locale = 1;
    CHECK(storage_save_app(&app, test_directory));
    CHECK(storage_load_app(&app, test_directory) && app.locale == 1);

    memset(&text, 0, sizeof(text));
    snprintf(text.path, sizeof(text.path), "migrate.txt");
    text.size = 50;
    text.mtime = 7;
    text.encoding = TEXT_UTF8;
    memset(&old_book, 0, sizeof(old_book));
    old_book.magic = BOOK_MAGIC;
    old_book.version = 4;
    snprintf(old_book.path, sizeof(old_book.path), "%s", text.path);
    old_book.file_size = text.size;
    old_book.file_mtime = text.mtime;
    old_book.encoding = TEXT_UTF8;
    old_book.progress.byte_offset = 10;
    old_book.progress.source_line = 2;
    old_book.bookmark_count = 1;
    old_book.bookmarks[0].position.byte_offset = 20;
    old_book.bookmarks[0].position.source_line = 3;
    old_book.bookmarks[0].excerpt[0] = 0x4e2d;
    storage_book_path(path, sizeof(path), test_directory, text.path, "state");
    CHECK(write_checked_raw(path, &old_book, sizeof(old_book)));
    CHECK(storage_load_book(&book, &text, test_directory));
    CHECK(book.version == 5 && book.encoding_override == 0 && book.progress.byte_offset == 10);
    CHECK(book.bookmark_count == 1 && book.bookmarks[0].position.byte_offset == 20);
    CHECK(book.bookmarks[0].excerpt[0] == 0x4e2d);
    return 1;
}

static int test_manual_encoding_persists_per_book(void) {
    static const unsigned char content[] = {'A', 0, 0x2d, 0x4e, '\n', 0};
    TextFile text;
    BookState saved, loaded;
    TextDecoder decoder;
    TextPosition start = {0, 1};
    uint16_t value;
    uint32_t offset;
    char path[600];

    make_path(path, sizeof(path), "manual-book.txt");
    CHECK(write_bytes(path, content, sizeof(content)) && text_open(&text, path));
    CHECK(text.encoding == TEXT_UTF8);
    storage_book_defaults(&saved, &text);
    saved.encoding_override = TEXT_UTF16_LE;
    saved.encoding = TEXT_UTF16_LE;
    saved.progress = start;
    CHECK(storage_save_book(&saved, test_directory));
    CHECK(storage_load_book(&loaded, &text, test_directory));
    CHECK(loaded.encoding_override == TEXT_UTF16_LE && text.encoding == TEXT_UTF16_LE);
    CHECK(text.data_start == 0);
    text_decoder_at(&text, &decoder, loaded.progress);
    CHECK(text_next(&decoder, &value, &offset) && value == 'A');
    CHECK(text_next(&decoder, &value, &offset) && value == 0x4e2d);
    text_close(&text);
    return 1;
}

static int test_index_corruption_recovery(void) {
    static const char content[] = "Chapter 1\nbody\nChapter 2\nend";
    ChapterRules rules;
    PageIndex built;
    PageIndex loaded;
    PageIndex recovered;
    TextFile text;
    Layout layout = {10, 6, 8, 20};
    char text_path[600];
    char cache_path[600];
    unsigned char *data;
    size_t size;

    make_path(text_path, sizeof(text_path), "index-corrupt.txt");
    make_path(cache_path, sizeof(cache_path), "index-corrupt.idx");
    CHECK(write_bytes(text_path, content, sizeof(content) - 1));
    CHECK(text_open(&text, text_path));
    chapter_rules_defaults(&rules);
    index_init(&built);
    index_init(&loaded);
    index_init(&recovered);
    CHECK(index_build(&built, &text, NULL, layout, &rules, NULL, NULL) == 1);
    remove(cache_path);
    CHECK(index_save(&built, cache_path));
    CHECK(index_load(&loaded, &text, layout, &rules, cache_path));
    index_free(&loaded);

    data = read_bytes(cache_path, &size);
    CHECK(data && size >= 64);
    data[20] ^= 1;
    CHECK(write_bytes(cache_path, data, size));
    free(data);
    loaded.total_source_lines = 55;
    CHECK(!index_load(&loaded, &text, layout, &rules, cache_path));
    CHECK(loaded.total_source_lines == 55);
    index_free(&loaded);

    remove(cache_path);
    CHECK(index_save(&built, cache_path));
    data = read_bytes(cache_path, &size);
    CHECK(data && write_bytes(cache_path, data, size - 1));
    free(data);
    CHECK(!index_load(&loaded, &text, layout, &rules, cache_path));

    remove(cache_path);
    CHECK(index_save(&built, cache_path));
    data = read_bytes(cache_path, &size);
    CHECK(data != NULL);
    data = realloc(data, size + 1);
    CHECK(data != NULL);
    data[size] = 0;
    CHECK(write_bytes(cache_path, data, size + 1));
    free(data);
    CHECK(!index_load(&loaded, &text, layout, &rules, cache_path));

    remove(cache_path);
    CHECK(index_save(&built, cache_path));
    data = read_bytes(cache_path, &size);
    CHECK(data && size >= 72);
    data[64] = 1;
    data[65] = data[66] = data[67] = 0;
    CHECK(rewrite_crc(data, size) && write_bytes(cache_path, data, size));
    free(data);
    CHECK(!index_load(&loaded, &text, layout, &rules, cache_path));

    CHECK(index_save(&built, cache_path));
    data = read_bytes(cache_path, &size);
    CHECK(data && size >= 8);
    put_le32(data + 4, 6u);
    CHECK(rewrite_crc(data, size) && write_bytes(cache_path, data, size));
    free(data);
    loaded.total_source_lines = 55;
    CHECK(!index_load(&loaded, &text, layout, &rules, cache_path));
    CHECK(loaded.total_source_lines == 55);
    index_free(&loaded);

    CHECK(index_save(&built, cache_path));
    data = read_bytes(cache_path, &size);
    CHECK(data && size >= 8);
    put_le32(data + 4, 7u);
    CHECK(rewrite_crc(data, size) && write_bytes(cache_path, data, size));
    free(data);
    CHECK(!index_load(&loaded, &text, layout, &rules, cache_path));

    CHECK(index_build(&recovered, &text, NULL, layout, &rules, NULL, NULL) == 1);
    CHECK(index_save(&recovered, cache_path));
    CHECK(index_load(&loaded, &text, layout, &rules, cache_path));
    CHECK(loaded.chapter_count == 2);
    index_free(&built);
    index_free(&loaded);
    index_free(&recovered);
    text_close(&text);
    return 1;
}

static int test_index_keeps_colliding_glyph_widths_distinct(void) {
    static const unsigned char content[] = {
        0xe4, 0xb8, 0x80, 0xe4, 0xbc, 0x80, 0xe4, 0xb8, 0x80, '\n'
    };
    ChapterRules rules;
    PageIndex index;
    TextFile text;
    Layout layout = {10, 0, 1, 20};
    char path[600];

    make_path(path, sizeof(path), "glyph-widths.txt");
    CHECK(write_bytes(path, content, sizeof(content)) && text_open(&text, path));
    chapter_rules_defaults(&rules);
    index_init(&index);
    host_char_width = -2;
    CHECK(index_build(&index, &text, NULL, layout, &rules, NULL, NULL) == 1);
    host_char_width = 8;
    CHECK(index.page_count == 2 && index.pages[0].byte_offset == 0);
    CHECK(index.pages[1].byte_offset == 6);
    index_free(&index);
    text_close(&text);
    return 1;
}

static int test_index_skips_trailing_blank_page(void) {
    static const char trailing[] = "x\n\n";
    static const char interior[] = "x\n\ny";
    ChapterRules rules;
    PageIndex index;
    TextFile text;
    Layout layout = {10, 6, 1, 20};
    char path[600];

    chapter_rules_defaults(&rules);
    index_init(&index);
    host_char_width = 8;
    make_path(path, sizeof(path), "trailing-blank.txt");
    CHECK(write_bytes(path, trailing, sizeof(trailing) - 1) && text_open(&text, path));
    CHECK(index_build(&index, &text, NULL, layout, &rules, NULL, NULL) == 1);
    CHECK(index.page_count == 1 && index.total_source_lines == 3);
    index_free(&index);
    text_close(&text);

    make_path(path, sizeof(path), "interior-blank.txt");
    CHECK(write_bytes(path, interior, sizeof(interior) - 1) && text_open(&text, path));
    CHECK(index_build(&index, &text, NULL, layout, &rules, NULL, NULL) == 1);
    CHECK(index.page_count == 3 && index.pages[1].byte_offset == 2 &&
          index.pages[2].byte_offset == 3);
    index_free(&index);
    text_close(&text);
    return 1;
}

static int test_streaming_search_boundaries_and_overlap(void) {
    static const TextPosition pages[] = {{0, 1}, {7, 1}, {13, 1}, {16, 2}, {20, 2}};
    static const char content[] = "Alpha aaa ALPHA\nalpha";
    uint16_t alpha[TEXT_SEARCH_QUERY_MAX];
    uint16_t double_a[TEXT_SEARCH_QUERY_MAX];
    uint16_t absent[TEXT_SEARCH_QUERY_MAX];
    TextFile text;
    char path[600];
    uint32_t hit = UINT32_MAX;

    ascii_u16(alpha, TEXT_SEARCH_QUERY_MAX, "alpha");
    ascii_u16(double_a, TEXT_SEARCH_QUERY_MAX, "aa");
    ascii_u16(absent, TEXT_SEARCH_QUERY_MAX, "missing");
    make_path(path, sizeof(path), "search.txt");
    CHECK(write_bytes(path, content, sizeof(content) - 1));
    CHECK(text_open(&text, path));
    CHECK(text_find_forward(&text, alpha, 0, &hit) && hit == 0);
    CHECK(text_find_forward(&text, alpha, 1, &hit) && hit == 10);
    CHECK(text_find_backward(&text, alpha, text.size, pages, 5, &hit) && hit == 16);
    CHECK(text_find_backward(&text, alpha, 16, pages, 5, &hit) && hit == 10);
    CHECK(text_find_forward(&text, double_a, 6, &hit) && hit == 6);
    CHECK(text_find_forward(&text, double_a, 7, &hit) && hit == 7);
    CHECK(text_find_backward(&text, double_a, 9, pages, 5, &hit) && hit == 7);
    CHECK(text_find_backward(&text, double_a, 7, pages, 5, &hit) && hit == 6);
    CHECK(text_find_backward(&text, double_a, 6, pages, 5, &hit) == 0);
    CHECK(text_find_backward(&text, alpha, 0, pages, 5, &hit) == 0);
    CHECK(!text_find_backward(&text, absent, text.size, pages, 5, &hit));
    errno = 0;
    CHECK(!text_find_backward(&text, alpha, text.size, NULL, 0, &hit) && errno == EINVAL);
    errno = 0;
    CHECK(!text_find_forward(&text, NULL, 0, &hit) && errno == EINVAL);
    text_close(&text);
    return 1;
}

static int test_backward_search_multibyte_pages(void) {
    static const unsigned char le[] = {
        0xff, 0xfe, 'x', 0, 'A', 0, 'b', 0, 'a', 0, 'B', 0, 'a', 0, 'x', 0
    };
    static const unsigned char be[] = {
        0xfe, 0xff, 0, 'x', 0, 'A', 0, 'b', 0, 'a', 0, 'B', 0, 'a', 0, 'x'
    };
    static const unsigned char *utf16[] = {le, be};
    static const TextPosition utf16_pages[] = {{2, 1}, {8, 1}, {12, 1}};
    static const uint16_t aba[] = {'a', 'B', 'A', 0};
    static const unsigned char gb[] = {
        'x', 0xd6, 0xd0, 'a', 0xd6, 0xd0, 'a', 0xd6, 0xd0, 'a', 'x'
    };
    static const TextPosition gb_pages[] = {{0, 1}, {3, 1}, {6, 1}, {9, 1}};
    static const unsigned char utf8[] = {
        'x', 0xe4, 0xb8, 0xad, 'a', 0xe4, 0xb8, 0xad,
        'a', 0xe4, 0xb8, 0xad, 'a', 'x'
    };
    static const TextPosition utf8_pages[] = {{0, 1}, {4, 1}, {8, 1}, {12, 1}};
    static const uint16_t chinese[] = {0x4e2d, 'A', 0x4e2d, 0};
    TextFile text;
    char path[600];
    uint32_t hit;
    int i;

    for (i = 0; i < 2; ++i) {
        make_path(path, sizeof(path), i ? "search-utf16-be.txt" : "search-utf16-le.txt");
        CHECK(write_bytes(path, utf16[i], sizeof(le)) && text_open(&text, path));
        CHECK(text.encoding == (i ? TEXT_UTF16_BE : TEXT_UTF16_LE));
        CHECK(text_find_backward(&text, aba, text.size, utf16_pages, 3, &hit) && hit == 8);
        CHECK(text_find_backward(&text, aba, 8, utf16_pages, 3, &hit) && hit == 4);
        CHECK(!text_find_backward(&text, aba, 4, utf16_pages, 3, &hit));
        text_close(&text);
    }

    make_path(path, sizeof(path), "search-gb.txt");
    CHECK(write_bytes(path, gb, sizeof(gb)) && text_open(&text, path));
    CHECK(text.encoding == TEXT_GB18030);
    CHECK(text_find_backward(&text, chinese, text.size, gb_pages, 4, &hit) && hit == 4);
    CHECK(text_find_backward(&text, chinese, 4, gb_pages, 4, &hit) && hit == 1);
    CHECK(!text_find_backward(&text, chinese, 1, gb_pages, 4, &hit));
    text_close(&text);

    make_path(path, sizeof(path), "search-utf8.txt");
    CHECK(write_bytes(path, utf8, sizeof(utf8)) && text_open(&text, path));
    CHECK(text.encoding == TEXT_UTF8);
    CHECK(text_find_backward(&text, chinese, text.size, utf8_pages, 4, &hit) && hit == 5);
    CHECK(text_find_backward(&text, chinese, 5, utf8_pages, 4, &hit) && hit == 1);
    text_close(&text);
    return 1;
}

static int test_french_search_case_and_accents(void) {
    static const char content[] =
        "cote c\xc3\xb4te \xc3\x89T\xc3\x89 \xc3\xa9t\xc3\xa9 "
        "\xc5\x92uvre \xc5\x93uvre";
    static const TextPosition pages[] = {
        {0, 1}, {5, 1}, {11, 1}, {17, 1}, {23, 1}, {30, 1}
    };
    static const uint16_t ete[] = {0x00c9, 't', 0x00e9, 0};
    static const uint16_t upper_ete[] = {0x00c9, 'T', 0x00c9, 0};
    static const uint16_t oeuvre[] = {0x0153, 'u', 'v', 'r', 'e', 0};
    static const uint16_t cote[] = {'c', 'o', 't', 'e', 0};
    static const uint16_t circumflex[] = {'c', 0x00f4, 't', 'e', 0};
    TextFile text;
    char path[600];
    uint32_t hit;

    CHECK(text_fold_latin('A') == 'a');
    CHECK(text_fold_latin(0x00c0) == 0x00e0);
    CHECK(text_fold_latin(0x00d8) == 0x00f8);
    CHECK(text_fold_latin(0x00d7) == 0x00d7);
    CHECK(text_fold_latin(0x0178) == 0x00ff);
    CHECK(text_fold_latin(0x0152) == 0x0153);
    CHECK(text_fold_latin(0x00e9) != text_fold_latin('e'));
    make_path(path, sizeof(path), "french-search.txt");
    CHECK(write_bytes(path, content, sizeof(content) - 1) && text_open(&text, path));
    CHECK(text.encoding == TEXT_UTF8);
    CHECK(text_find_forward(&text, ete, 0, &hit) && hit == 11);
    CHECK(text_find_forward(&text, upper_ete, 17, &hit) && hit == 17);
    CHECK(text_find_backward(&text, ete, text.size, pages, 6, &hit) && hit == 17);
    CHECK(text_find_backward(&text, upper_ete, 17, pages, 6, &hit) && hit == 11);
    CHECK(text_find_forward(&text, oeuvre, 0, &hit) && hit == 23);
    CHECK(text_find_forward(&text, oeuvre, 30, &hit) && hit == 30);
    CHECK(text_find_backward(&text, oeuvre, 30, pages, 6, &hit) && hit == 23);
    CHECK(text_find_forward(&text, cote, 0, &hit) && hit == 0);
    CHECK(!text_find_forward(&text, cote, 5, &hit));
    CHECK(text_find_forward(&text, circumflex, 0, &hit) && hit == 5);
    CHECK(text_find_backward(&text, circumflex, text.size, pages, 6, &hit) && hit == 5);
    CHECK(text_find_backward(&text, cote, text.size, pages, 6, &hit) && hit == 0);
    text_close(&text);
    return 1;
}

static int cancel_at_completion(uint32_t done, uint32_t total, void *context) {
    int *calls = context;
    ++*calls;
    return done != total;
}

static int test_invalid_inputs_layout_and_index_identity(void) {
    static const char content[] = "abcde";
    FilePart invalid_part = {NULL, 1};
    ChapterRules rules;
    PageIndex built;
    PageIndex loaded;
    TextFile text;
    Layout layout = {12, 150, 2, 28};
    Layout invalid_layout = {12, 6, 0, 28};
    char text_path[600];
    char cache_path[600];
    char original_path[sizeof(text.path)];
    char long_path[600];
    int progress_calls = 0;

    errno = 0;
    CHECK(!file_replace_parts("invalid-part.tmp", &invalid_part, 1) && errno == EINVAL);
    errno = 0;
    CHECK(!text_open(&text, NULL) && errno == EINVAL);
    errno = 0;
    CHECK(!text_open(&text, test_directory) && errno == EINVAL);
    memset(long_path, 'x', sizeof(long_path));
    long_path[sizeof(long_path) - 1] = 0;
    errno = 0;
    CHECK(!text_open(&text, long_path) && errno == ENAMETOOLONG);

    make_path(text_path, sizeof(text_path), "index-identity.txt");
    make_path(cache_path, sizeof(cache_path), "index-identity.idx");
    CHECK(write_bytes(text_path, content, sizeof(content) - 1));
    CHECK(text_open(&text, text_path));
    chapter_rules_defaults(&rules);
    index_init(&built);
    index_init(&loaded);
    errno = 0;
    CHECK(!index_build(&built, &text, NULL, invalid_layout, &rules, NULL, NULL));
    CHECK(errno == EINVAL);
    CHECK(index_build(&built, &text, NULL, layout, &rules,
                      cancel_at_completion, &progress_calls) == -1);
    CHECK(progress_calls == 1);

    host_char_width = -1;
    CHECK(index_build(&built, &text, NULL, layout, &rules, NULL, NULL) == 1);
    host_char_width = 8;
    CHECK(built.page_count == 3);
    CHECK(strcmp(built.file_path, text.path) == 0);
    remove(cache_path);
    CHECK(index_save(&built, cache_path));

    snprintf(original_path, sizeof(original_path), "%s", text.path);
    snprintf(text.path, sizeof(text.path), "%s", "different-book-with-same-cache-key.txt");
    CHECK(!index_load(&loaded, &text, layout, &rules, cache_path));
    CHECK(loaded.page_count == 0);
    snprintf(text.path, sizeof(text.path), "%s", original_path);
    CHECK(index_load(&loaded, &text, layout, &rules, cache_path));

    index_free(&built);
    index_free(&loaded);
    text_close(&text);
    return 1;
}

typedef int (*TestFunction)(void);

typedef struct {
    const char *name;
    TestFunction function;
} TestCase;

int main(int argc, char **argv) {
    static const TestCase tests[] = {
        {"test_crc32_known_vector", test_crc32_known_vector},
        {"test_ndless_rename_fallback", test_ndless_rename_fallback},
        {"test_storage_init_documents_directory", test_storage_init_documents_directory},
        {"test_canonical_path_persistence_identity", test_canonical_path_persistence_identity},
        {"test_utf16_decoding_and_positions", test_utf16_decoding_and_positions},
        {"test_manual_encoding_selection", test_manual_encoding_selection},
        {"test_multilingual_utf8_stream", test_multilingual_utf8_stream},
        {"test_chapter_rules_and_index_round_trip", test_chapter_rules_and_index_round_trip},
        {"test_french_chapters_and_index_cache", test_french_chapters_and_index_cache},
        {"test_bookmark_excerpt", test_bookmark_excerpt},
        {"test_app_state_integrity_and_v3_migration", test_app_state_integrity_and_v3_migration},
        {"test_shortcut_hints_preferences_persist", test_shortcut_hints_preferences_persist},
        {"test_v5_preferences_migrate_with_hints_enabled", test_v5_preferences_migrate_with_hints_enabled},
        {"test_book_state_integrity_encoding_and_v3_migration", test_book_state_integrity_encoding_and_v3_migration},
        {"test_v4_preferences_and_bookmarks_migrate", test_v4_preferences_and_bookmarks_migrate},
        {"test_manual_encoding_persists_per_book", test_manual_encoding_persists_per_book},
        {"test_index_corruption_recovery", test_index_corruption_recovery},
        {"test_index_keeps_colliding_glyph_widths_distinct", test_index_keeps_colliding_glyph_widths_distinct},
        {"test_index_skips_trailing_blank_page", test_index_skips_trailing_blank_page},
        {"test_streaming_search_boundaries_and_overlap", test_streaming_search_boundaries_and_overlap},
        {"test_backward_search_multibyte_pages", test_backward_search_multibyte_pages},
        {"test_french_search_case_and_accents", test_french_search_case_and_accents},
        {"test_invalid_inputs_layout_and_index_identity", test_invalid_inputs_layout_and_index_identity},
    };
    size_t i;
    if (argc != 2) {
        fprintf(stderr, "usage: %s TEST_DATA_DIRECTORY\n", argv[0]);
        return 2;
    }
    test_directory = argv[1];
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].function()) {
            fprintf(stderr, "%s failed\n", tests[i].name);
            return 1;
        }
    }
    printf("%lu host behavioral tests passed\n",
           (unsigned long)(sizeof(tests) / sizeof(tests[0])));
    return 0;
}
