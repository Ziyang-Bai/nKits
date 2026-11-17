#ifndef NTEXTS_STORAGE_H
#define NTEXTS_STORAGE_H

#include "text_engine.h"
#include "chapter_rules.h"
#include <stdint.h>

#define MAX_BOOKMARKS 20
#define MAX_RECENTS 10
#define MAX_HISTORY 10
#define EXCERPT_LEN 48
#define QUERY_LEN TEXT_SEARCH_QUERY_MAX

typedef struct {
    TextPosition position;
    uint32_t timestamp;
    uint16_t excerpt[EXCERPT_LEN];
} Bookmark;

typedef struct {
    uint32_t magic, version;
    char path[512];
    uint32_t file_size, file_mtime;
    uint32_t encoding;
    TextPosition progress;
    uint32_t bookmark_count;
    Bookmark bookmarks[MAX_BOOKMARKS];
    uint32_t encoding_override;
} BookState;

typedef struct {
    char path[512];
    uint32_t offset, file_size;
} RecentBook;

typedef struct {
    uint32_t magic, version;
    int font_choice, theme, margin_choice;
    uint32_t shortcut_hints_disabled;
    uint32_t recent_count;
    RecentBook recents[MAX_RECENTS];
    uint32_t history_count;
    uint16_t history[MAX_HISTORY][QUERY_LEN];
    ChapterRules chapter_rules;
    uint32_t locale;
} AppState;

uint32_t storage_hash_path(const char *path);
int storage_init(char *directory, size_t capacity);
void storage_book_path(char *out, size_t capacity, const char *directory, const char *book, const char *suffix);
void storage_app_defaults(AppState *state);
int storage_load_app(AppState *state, const char *directory);
int storage_save_app(const AppState *state, const char *directory);
void storage_book_defaults(BookState *state, TextFile *text);
int storage_load_book(BookState *state, TextFile *text, const char *directory);
int storage_save_book(const BookState *state, const char *directory);
void storage_touch_recent(AppState *state, const BookState *book);
int storage_remove_recent(AppState *state, const char *path);
int storage_prune_recents(AppState *state);
void storage_add_history(AppState *state, const uint16_t *query);

#endif
