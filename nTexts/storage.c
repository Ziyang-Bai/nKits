#include "storage.h"
#include "app_log.h"
#include "crc32.h"
#include "file_replace.h"
#include <libndls.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define APP_MAGIC 0x5053544eu
#define BOOK_MAGIC 0x4b42544eu
#define APP_STATE_VERSION 6u
#define BOOK_STATE_VERSION 5u
#define PREVIOUS_STATE_VERSION 4u
#define LEGACY_STATE_VERSION 3u

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
} PreviousBookState;

typedef struct {
    uint32_t magic, version;
    int font_choice, theme, margin_choice;
    uint32_t tutorial_flags;
    uint32_t recent_count;
    RecentBook recents[MAX_RECENTS];
    uint32_t history_count;
    uint16_t history[MAX_HISTORY][QUERY_LEN];
    ChapterRules chapter_rules;
} PreviousAppState;

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

#define SIZE_ASSERT(name, condition) typedef char size_assert_##name[(condition) ? 1 : -1]
SIZE_ASSERT(text_position, sizeof(TextPosition) == 8);
SIZE_ASSERT(bookmark, sizeof(Bookmark) == 108);
SIZE_ASSERT(recent_book, sizeof(RecentBook) == 520);
SIZE_ASSERT(legacy_book_state, sizeof(LegacyBookState) == 2700);
SIZE_ASSERT(previous_book_state, sizeof(PreviousBookState) == 2704);
SIZE_ASSERT(book_state, sizeof(BookState) == 2708);
SIZE_ASSERT(legacy_app_state, sizeof(LegacyAppState) == 6512);
SIZE_ASSERT(chapter_rules, sizeof(ChapterRules) == 772);
SIZE_ASSERT(previous_app_state, sizeof(PreviousAppState) == 7284);
SIZE_ASSERT(v5_app_state, sizeof(V5AppState) == 7288);
SIZE_ASSERT(app_state, sizeof(AppState) == 7288);

uint32_t storage_hash_path(const char *path) {
    uint32_t h = 2166136261u;
    while (*path) { h ^= (unsigned char)*path++; h *= 16777619u; }
    return h;
}

static void storage_join_path(char *out, size_t capacity, const char *base, const char *leaf) {
    size_t len;
    if (!capacity) return;
    len = strlen(base);
    snprintf(out, capacity, "%s%s%s", base,
             (len && base[len - 1] != '/' && base[len - 1] != '\\') ? "/" : "",
             leaf);
}

int storage_init(char *directory, size_t capacity) {
    struct stat st;
    const char *documents = get_documents_dir();
    int mkdir_errno;
    app_log("storage", "init documents=%s", documents ? documents : "(null)");
    if (!documents || !*documents) {
        errno = ENOENT;
        if (capacity) directory[0] = 0;
        return 0;
    }
    storage_join_path(directory, capacity, documents, "nTexts");
    errno = 0;
    if (mkdir(directory, 0777) == 0) {
        app_log("storage", "created %s", directory);
        return 1;
    }
    mkdir_errno = errno;
    errno = 0;
    if (stat(directory, &st) == 0 && S_ISDIR(st.st_mode)) {
        errno = 0;
        app_log("storage", "using existing %s", directory);
        return 1;
    }
    errno = mkdir_errno ? mkdir_errno : (errno ? errno : ENOTDIR);
    return 0;
}

void storage_book_path(char *out, size_t capacity, const char *directory, const char *book, const char *suffix) {
    snprintf(out, capacity, "%s/%08lx.%s", directory, (unsigned long)storage_hash_path(book), suffix);
}

static int has_terminator(const void *data, size_t count, size_t unit_size) {
    size_t i;
    if (unit_size == 1) return memchr(data, 0, count) != NULL;
    for (i = 0; i < count; ++i) {
        if (((const uint16_t *)data)[i] == 0) return 1;
    }
    return 0;
}

static int app_common_valid(uint32_t magic, uint32_t version, int font_choice, int theme,
                            int margin_choice, uint32_t recent_count,
                            const RecentBook *recents, uint32_t history_count,
                            const uint16_t history[MAX_HISTORY][QUERY_LEN]) {
    uint32_t i;
    if (magic != APP_MAGIC || version == 0 || font_choice < 0 || font_choice > 2 ||
        theme < 0 || theme > 2 || margin_choice < 0 || margin_choice > 1 ||
        recent_count > MAX_RECENTS || history_count > MAX_HISTORY) return 0;
    for (i = 0; i < recent_count; ++i) {
        if (!recents[i].path[0] || !has_terminator(recents[i].path, sizeof(recents[i].path), 1) ||
            recents[i].offset > recents[i].file_size) return 0;
    }
    for (i = 0; i < history_count; ++i) {
        if (!has_terminator(history[i], QUERY_LEN, sizeof(uint16_t))) return 0;
    }
    return 1;
}

static int app_valid(const AppState *state) {
    return app_common_valid(state->magic, state->version, state->font_choice, state->theme,
                            state->margin_choice, state->recent_count,
                            state->recents, state->history_count, state->history) &&
           state->version == APP_STATE_VERSION && state->shortcut_hints_disabled <= 1 &&
           state->locale <= 2 &&
           chapter_rules_validate(&state->chapter_rules);
}

static int v5_app_valid(const V5AppState *state) {
    return state->version == 5u && !(state->tutorial_flags & ~0x80000001u) &&
           app_common_valid(state->magic, state->version, state->font_choice, state->theme,
                            state->margin_choice, state->recent_count,
                            state->recents, state->history_count, state->history) &&
           state->locale <= 2 && chapter_rules_validate(&state->chapter_rules);
}

static int previous_app_valid(const PreviousAppState *state) {
    return state->version == PREVIOUS_STATE_VERSION &&
           !(state->tutorial_flags & ~0x80000001u) &&
           app_common_valid(state->magic, state->version, state->font_choice, state->theme,
                            state->margin_choice, state->recent_count,
                            state->recents, state->history_count, state->history) &&
           chapter_rules_validate(&state->chapter_rules);
}

static void repair_app(AppState *state) {
    uint32_t read;
    uint32_t write = 0;

    state->magic = APP_MAGIC;
    state->version = APP_STATE_VERSION;
    if (state->font_choice < 0 || state->font_choice > 2) state->font_choice = 1;
    if (state->theme < 0 || state->theme > 2) state->theme = 0;
    if (state->margin_choice < 0 || state->margin_choice > 1) state->margin_choice = 0;
    if (state->locale > 2) state->locale = 0;
    if (state->shortcut_hints_disabled > 1) state->shortcut_hints_disabled = 0;

    if (state->recent_count > MAX_RECENTS) state->recent_count = MAX_RECENTS;
    for (read = 0; read < state->recent_count; ++read) {
        RecentBook recent = state->recents[read];
        recent.path[sizeof(recent.path) - 1] = 0;
        if (!recent.path[0]) continue;
        if (recent.offset > recent.file_size) recent.offset = recent.file_size;
        state->recents[write++] = recent;
    }
    state->recent_count = write;
    while (write < MAX_RECENTS)
        memset(&state->recents[write++], 0, sizeof(state->recents[0]));

    write = 0;
    if (state->history_count > MAX_HISTORY) state->history_count = MAX_HISTORY;
    for (read = 0; read < state->history_count; ++read) {
        state->history[read][QUERY_LEN - 1] = 0;
        if (!state->history[read][0]) continue;
        if (write != read)
            memcpy(state->history[write], state->history[read], sizeof(state->history[0]));
        ++write;
    }
    state->history_count = write;
    while (write < MAX_HISTORY)
        memset(state->history[write++], 0, sizeof(state->history[0]));

    if (!chapter_rules_validate(&state->chapter_rules))
        chapter_rules_defaults(&state->chapter_rules);
}

static int legacy_app_valid(const LegacyAppState *state) {
    return state->version == LEGACY_STATE_VERSION &&
           !(state->tutorial_flags & ~0x80000001u) &&
           app_common_valid(state->magic, state->version, state->font_choice, state->theme,
                            state->margin_choice, state->recent_count,
                            state->recents, state->history_count, state->history);
}

static int encoding_valid(uint32_t encoding) {
    return encoding >= TEXT_UTF8 && encoding <= TEXT_UTF16_BE;
}

static int bookmark_valid(const Bookmark *bookmark, uint32_t file_size) {
    return bookmark->position.byte_offset <= file_size && bookmark->position.source_line > 0 &&
           has_terminator(bookmark->excerpt, EXCERPT_LEN, sizeof(uint16_t));
}

static int book_common_valid(uint32_t magic, uint32_t version, const char *path,
                             uint32_t file_size, TextPosition progress, uint32_t bookmark_count,
                             const Bookmark *bookmarks) {
    uint32_t i;
    if (magic != BOOK_MAGIC || version == 0 || !path[0] || !has_terminator(path, 512, 1) ||
        progress.byte_offset > file_size || !progress.source_line ||
        bookmark_count > MAX_BOOKMARKS) return 0;
    for (i = 0; i < bookmark_count; ++i) {
        if (!bookmark_valid(&bookmarks[i], file_size)) return 0;
    }
    return 1;
}

static int book_valid(const BookState *state) {
    return state->version == BOOK_STATE_VERSION && encoding_valid(state->encoding) &&
           (!state->encoding_override || encoding_valid(state->encoding_override)) &&
           book_common_valid(state->magic, state->version, state->path, state->file_size,
                             state->progress, state->bookmark_count, state->bookmarks);
}

static int previous_book_valid(const PreviousBookState *state) {
    return state->version == PREVIOUS_STATE_VERSION && encoding_valid(state->encoding) &&
           book_common_valid(state->magic, state->version, state->path, state->file_size,
                             state->progress, state->bookmark_count, state->bookmarks);
}

static int legacy_book_valid(const LegacyBookState *state) {
    return state->version == LEGACY_STATE_VERSION &&
           book_common_valid(state->magic, state->version, state->path, state->file_size,
                             state->progress, state->bookmark_count, state->bookmarks);
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

static int atomic_write(const char *path, const void *data, size_t size) {
    unsigned char trailer[4];
    FilePart parts[2];
    uint32_t crc = crc32_finish(crc32_update(CRC32_INITIAL, data, size));
    put_le32(trailer, crc);
    parts[0].data = data;
    parts[0].size = size;
    parts[1].data = trailer;
    parts[1].size = sizeof(trailer);
    return file_replace_parts(path, parts, 2);
}

static int file_length(FILE *fp, size_t *length) {
    long end;
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0)
        return 0;
    *length = (size_t)end;
    return 1;
}

static int read_checked_payload(FILE *fp, void *data, size_t size) {
    unsigned char trailer[4];
    uint32_t actual;
    uint32_t expected;
    if (fread(data, 1, size, fp) != size || fread(trailer, 1, sizeof(trailer), fp) != sizeof(trailer))
        return 0;
    actual = crc32_finish(crc32_update(CRC32_INITIAL, data, size));
    expected = get_le32(trailer);
    return actual == expected;
}

void storage_app_defaults(AppState *state) {
    memset(state, 0, sizeof(*state));
    state->magic = APP_MAGIC;
    state->version = APP_STATE_VERSION;
    state->font_choice = 1;
    state->theme = 0;
    state->margin_choice = 0;
    chapter_rules_defaults(&state->chapter_rules);
}

int storage_load_app(AppState *state, const char *directory) {
    char path[600];
    FILE *fp;
    size_t length;
    union {
        AppState current;
        V5AppState v5;
    } candidate;
    LegacyAppState legacy;
    PreviousAppState previous;
    snprintf(path, sizeof(path), "%s/app.state", directory);
    storage_app_defaults(state);
    fp = fopen(path, "rb");
    if (!fp) {
        app_log("storage", "app miss");
        return 0;
    }
    if (!file_length(fp, &length)) {
        fclose(fp);
        app_log("storage", "app size");
        return 0;
    }
    if (length == sizeof(candidate) + 4) {
        if (!read_checked_payload(fp, &candidate, sizeof(candidate))) {
            fclose(fp);
            app_log("storage", "app checksum");
            return 0;
        }
        fclose(fp);
        if (candidate.current.version == 5u) {
            if (!v5_app_valid(&candidate.v5)) {
                app_log("storage", "app fields");
                return 0;
            }
            candidate.current.version = APP_STATE_VERSION;
            candidate.current.shortcut_hints_disabled = 0;
            app_log("storage", "app migrated v5");
        } else if (!app_valid(&candidate.current)) {
            app_log("storage", "app fields");
            return 0;
        }
        *state = candidate.current;
        return 1;
    }
    if (length == sizeof(previous) + 4) {
        if (!read_checked_payload(fp, &previous, sizeof(previous))) {
            fclose(fp);
            app_log("storage", "app checksum");
            return 0;
        }
        fclose(fp);
        if (!previous_app_valid(&previous)) {
            app_log("storage", "app fields");
            return 0;
        }
        memcpy(state, &previous, sizeof(previous));
        state->version = APP_STATE_VERSION;
        state->shortcut_hints_disabled = 0;
        app_log("storage", "app migrated v4");
        return 1;
    }
    if (length == sizeof(legacy)) {
        if (fread(&legacy, 1, sizeof(legacy), fp) != sizeof(legacy)) {
            fclose(fp);
            app_log("storage", "app size");
            return 0;
        }
        fclose(fp);
        if (!legacy_app_valid(&legacy)) {
            app_log("storage", "app incompatible");
            return 0;
        }
        memcpy(state, &legacy, sizeof(legacy));
        state->version = APP_STATE_VERSION;
        state->shortcut_hints_disabled = 0;
        chapter_rules_defaults(&state->chapter_rules);
        app_log("storage", "app migrated v3");
        return 1;
    }
    fclose(fp);
    app_log("storage", "app size");
    return 0;
}

int storage_save_app(const AppState *state, const char *directory) {
    AppState candidate;
    char path[600];
    if (!state || !directory || !*directory) {
        errno = EINVAL;
        return 0;
    }
    candidate = *state;
    if (!app_valid(&candidate)) {
        app_log("storage", "repair app before save");
        repair_app(&candidate);
    }
    if (!app_valid(&candidate)) {
        errno = EINVAL;
        app_log("storage", "save app fields");
        return 0;
    }
    snprintf(path, sizeof(path), "%s/app.state", directory);
    return atomic_write(path, &candidate, sizeof(candidate));
}

void storage_book_defaults(BookState *state, TextFile *text) {
    memset(state, 0, sizeof(*state));
    state->magic = BOOK_MAGIC;
    state->version = BOOK_STATE_VERSION;
    strncpy(state->path, text->path, sizeof(state->path) - 1);
    state->file_size = text->size;
    state->file_mtime = text->mtime;
    state->encoding = (uint32_t)text->encoding;
    state->progress.byte_offset = text->data_start;
    state->progress.source_line = 1;
}

static void adapt_stale_book(BookState *state, TextFile *text) {
    Bookmark kept[MAX_BOOKMARKS];
    uint32_t kept_count = state->bookmark_count;
    uint32_t override = state->encoding_override;
    uint32_t i;
    for (i = 0; i < kept_count; ++i) {
        kept[i] = state->bookmarks[i];
        if (kept[i].position.byte_offset > text->size)
            kept[i].position.byte_offset = text->size;
    }
    storage_book_defaults(state, text);
    state->encoding_override = override;
    state->bookmark_count = kept_count;
    for (i = 0; i < kept_count; ++i) state->bookmarks[i] = kept[i];
}

int storage_load_book(BookState *state, TextFile *text, const char *directory) {
    char path[600];
    FILE *fp;
    size_t length;
    BookState candidate;
    LegacyBookState legacy;
    PreviousBookState previous;
    storage_book_defaults(state, text);
    storage_book_path(path, sizeof(path), directory, text->path, "state");
    fp = fopen(path, "rb");
    if (!fp) {
        app_log("storage", "book miss");
        return 0;
    }
    if (!file_length(fp, &length)) {
        fclose(fp);
        app_log("storage", "book size");
        return 0;
    }
    if (length == sizeof(candidate) + 4) {
        if (!read_checked_payload(fp, &candidate, sizeof(candidate))) {
            fclose(fp);
            app_log("storage", "book checksum");
            return 0;
        }
        fclose(fp);
        if (candidate.version == BOOK_STATE_VERSION && !encoding_valid(candidate.encoding_override) &&
            candidate.encoding_override != 0 && encoding_valid(candidate.encoding) &&
            book_common_valid(candidate.magic, candidate.version, candidate.path,
                              candidate.file_size, candidate.progress, candidate.bookmark_count,
                              candidate.bookmarks)) {
            candidate.encoding_override = 0;
            app_log("storage", "book cleared invalid override");
        }
        if (!book_valid(&candidate)) {
            app_log("storage", "book fields");
            return 0;
        }
        if (strcmp(candidate.path, text->path)) {
            app_log("storage", "book path");
            return 0;
        }
        *state = candidate;
    } else if (length == sizeof(previous) + 4) {
        if (!read_checked_payload(fp, &previous, sizeof(previous))) {
            fclose(fp);
            app_log("storage", "book checksum");
            return 0;
        }
        fclose(fp);
        if (!previous_book_valid(&previous) || strcmp(previous.path, text->path)) {
            app_log("storage", "book incompatible v4");
            return 0;
        }
        memcpy(state, &previous, sizeof(previous));
        state->version = BOOK_STATE_VERSION;
        app_log("storage", "book migrated v4");
    } else if (length == sizeof(legacy)) {
        if (fread(&legacy, 1, sizeof(legacy), fp) != sizeof(legacy)) {
            fclose(fp);
            app_log("storage", "book size");
            return 0;
        }
        fclose(fp);
        if (!legacy_book_valid(&legacy)) {
            app_log("storage", "book incompatible");
            return 0;
        }
        if (strcmp(legacy.path, text->path)) {
            app_log("storage", "book path");
            return 0;
        }
        if (text->encoding != TEXT_UTF8 && text->encoding != TEXT_GB18030) {
            app_log("storage", "book incompatible encoding");
            return 0;
        }
        storage_book_defaults(&candidate, text);
        memcpy(candidate.path, legacy.path, sizeof(candidate.path));
        candidate.file_size = legacy.file_size;
        candidate.file_mtime = legacy.file_mtime;
        candidate.progress = legacy.progress;
        candidate.bookmark_count = legacy.bookmark_count;
        memcpy(candidate.bookmarks, legacy.bookmarks, sizeof(candidate.bookmarks));
        *state = candidate;
        app_log("storage", "book migrated v3");
    } else {
        fclose(fp);
        app_log("storage", "book size");
        return 0;
    }
    if (state->encoding_override &&
        !text_select_encoding(text, (TextEncoding)state->encoding_override)) {
        app_log("storage", "book invalid override");
        storage_book_defaults(state, text);
        return 0;
    }
    if (state->file_size != text->size || state->file_mtime != text->mtime ||
        state->encoding != (uint32_t)text->encoding) {
        adapt_stale_book(state, text);
        return 1;
    }
    state->file_size = text->size;
    state->file_mtime = text->mtime;
    state->encoding = (uint32_t)text->encoding;
    return 1;
}

int storage_save_book(const BookState *state, const char *directory) {
    char path[600];
    if (!book_valid(state)) {
        errno = EINVAL;
        app_log("storage", "save book fields");
        return 0;
    }
    storage_book_path(path, sizeof(path), directory, state->path, "state");
    return atomic_write(path, state, sizeof(*state));
}

void storage_touch_recent(AppState *state, const BookState *book) {
    uint32_t i, found = state->recent_count;
    if (!book->path[0]) return;
    for (i = 0; i < state->recent_count; ++i) if (!strcmp(state->recents[i].path, book->path)) { found = i; break; }
    if (found >= MAX_RECENTS) found = MAX_RECENTS - 1;
    for (i = found; i > 0; --i) state->recents[i] = state->recents[i - 1];
    memset(&state->recents[0], 0, sizeof(state->recents[0]));
    strncpy(state->recents[0].path, book->path, sizeof(state->recents[0].path) - 1);
    state->recents[0].offset = book->progress.byte_offset; state->recents[0].file_size = book->file_size;
    if (found == state->recent_count && state->recent_count < MAX_RECENTS) state->recent_count++;
    app_log("storage", "touch recent count=%lu path=%s",
            (unsigned long)state->recent_count, book->path);
}

int storage_remove_recent(AppState *state, const char *path) {
    uint32_t write = 0;
    uint32_t kept;
    int changed = 0;
    if (!path || !*path) return 0;
    for (uint32_t read = 0; read < state->recent_count && read < MAX_RECENTS; ++read) {
        if (!strcmp(state->recents[read].path, path)) {
            changed = 1;
            continue;
        }
        if (write != read) state->recents[write] = state->recents[read];
        write++;
    }
    kept = write;
    while (write < MAX_RECENTS) memset(&state->recents[write++], 0, sizeof(state->recents[0]));
    if (changed) {
        state->recent_count = kept;
        app_log("storage", "removed recent path=%s", path);
    }
    return changed;
}

int storage_prune_recents(AppState *state) {
    uint32_t write = 0;
    uint32_t kept;
    int changed = 0;
    for (uint32_t read = 0; read < state->recent_count && read < MAX_RECENTS; ++read) {
        struct stat st;
        RecentBook keep = state->recents[read];
        if (!keep.path[0] || stat(keep.path, &st) != 0 || !S_ISREG(st.st_mode)) {
            changed = 1;
            continue;
        }
        if (write != read) {
            state->recents[write] = keep;
            changed = 1;
        }
        write++;
    }
    kept = write;
    while (write < MAX_RECENTS) {
        if (state->recents[write].path[0]) changed = 1;
        memset(&state->recents[write], 0, sizeof(state->recents[write]));
        write++;
    }
    if (state->recent_count != kept) changed = 1;
    state->recent_count = kept;
    return changed;
}

void storage_add_history(AppState *state, const uint16_t *query) {
    uint32_t i, found;
    if (!state || !query || !query[0]) return;
    found = state->history_count;
    for (i = 0; i < state->history_count; ++i)
        if (!memcmp(state->history[i], query, QUERY_LEN * sizeof(uint16_t))) { found = i; break; }
    if (found >= MAX_HISTORY) found = MAX_HISTORY - 1;
    for (i = found; i > 0; --i) memcpy(state->history[i], state->history[i - 1], sizeof(state->history[i]));
    memcpy(state->history[0], query, sizeof(state->history[0]));
    if (state->history_count < MAX_HISTORY) state->history_count++;
}
