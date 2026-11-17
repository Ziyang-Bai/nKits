#include "reader.h"
#include "app_language.h"
#include "app_log.h"
#include "app_strings.h"
#include "interface.h"
#include "reader_index.h"
#include "settings.h"
#include "text_engine.h"
#include <libndls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct Reader {
    TextFile text;
    PageIndex index;
    BookState book;
    AppState *app;
    char data_dir[512];
    Gc gc;
    Layout layout;
    int ascii_width[128];
    uint16_t wide_chars[256];
    int wide_width[256];
    TextPosition top;
    uint32_t page;
    uint32_t hit_offset;
    uint32_t hit_end;
    int search_active;
    uint16_t last_query[QUERY_LEN];
};

static Layout layout_from_state(const AppState *state) {
    const int fonts[] = {Regular9, Regular10, Regular12};
    const int margins[] = {6, 14};
    const int line_heights[] = {20, 24, 28};
    Layout l;
    int fi = state->font_choice < 0 || state->font_choice > 2 ? 1 : state->font_choice;
    int mi = state->margin_choice ? 1 : 0;
    l.font_size = fonts[fi];
    l.margin = margins[mi];
    l.line_height = line_heights[fi];
    l.lines_per_page = (UI_HEIGHT - 22 - l.margin * 2) / l.line_height;
    if (l.lines_per_page < 1) l.lines_per_page = 1;
    return l;
}

static int progress_cb(uint32_t done, uint32_t total, void *ctx) {
    Reader *r = (Reader *)ctx;
    char msg[64];
    if (isKeyPressed(KEY_NSPIRE_ESC)) return 0;
    gui_gc_begin(r->gc);
    ui_fill(r->gc, 0, 0, UI_WIDTH, UI_HEIGHT, ui_themes[r->app->theme].bg);
    snprintf(msg, sizeof(msg), L("首次索引: %lu%%  Esc取消", "Indexing: %lu%%  Esc cancel", "Indexation : %lu%%  Esc annuler"),
             total ? (unsigned long)(((uint64_t)done * 100u) / total) : 0ul);
    ui_draw_text(r->gc, msg, 42, 104, Regular10, ui_themes[r->app->theme].fg);
    gui_gc_finish(r->gc);
    gui_gc_blit_to_screen(r->gc);
    return 1;
}

static int ensure_index(Reader *r) {
    char path[600];
    int loaded;
    storage_book_path(path, sizeof(path), r->data_dir, r->text.path, "idx");
    app_log("reader", "ensure index %s", path);
    loaded = index_load(&r->index, &r->text, r->layout, &r->app->chapter_rules, path);
    if (loaded) {
        app_log("reader", "index cache loaded");
        return 1;
    }
    app_log("reader", "index build needed");
    if (index_build(&r->index, &r->text, r->gc, r->layout, &r->app->chapter_rules,
                    progress_cb, r) <= 0) {
        debug_fail("ensure_index:index_build", "%s", L("索引构建被取消，或构建失败", "Index canceled or failed", "Indexation annulée ou échouée"));
        return 0;
    }
    if (!index_save(&r->index, path))
        debug_fail_errno("ensure_index:index_save", path);
    return 1;
}

static int save_reader(Reader *r) {
    r->book.progress = r->top;
    if (!storage_save_book(&r->book, r->data_dir)) {
        debug_fail_errno("save_reader:book", r->book.path);
        return 0;
    }
    storage_touch_recent(r->app, &r->book);
    return 1;
}

static void save_reader_all(Reader *r) {
    save_reader(r);
    if (!storage_save_app(r->app, r->data_dir))
        debug_fail_errno("save_reader:app", r->data_dir);
}

static uint32_t percent_for_reader(const Reader *r) {
    if (!r->text.size) return 0;
    return (uint32_t)(((uint64_t)r->top.byte_offset * 100u) / r->text.size);
}

typedef struct {
    int left, right;
    int first, last;
} HitSpan;

static int wrap_push(uint16_t lines[][UI_LINE_UNITS], HitSpan spans[],
                     int *line, const uint16_t *buf, int len, HitSpan span) {
    if (*line >= 32) return 0;
    memcpy(lines[*line], buf, (len + 1) * sizeof(uint16_t));
    spans[*line] = span;
    (*line)++;
    return 1;
}

static int reader_char_width(Reader *r, uint16_t ch) {
    int width;
    if (ch < 128) {
        width = r->ascii_width[ch];
    } else {
        unsigned slot = ch & 255u;
        width = r->wide_chars[slot] == ch ? r->wide_width[slot] : 0;
    }
    if (width) return width;
    width = gui_gc_getCharWidth(r->gc, (gui_gc_Font)r->layout.font_size, (short)ch);
    if (width <= 0) width = r->layout.font_size & 31;
    if (ch < 128) {
        r->ascii_width[ch] = width;
    } else {
        unsigned slot = ch & 255u;
        r->wide_chars[slot] = ch;
        r->wide_width[slot] = width;
    }
    return width;
}

static void draw_page(Reader *r) {
    const UiTheme *t = &ui_themes[r->app->theme];
    TextDecoder d;
    uint16_t lines[32][UI_LINE_UNITS], linebuf[UI_LINE_UNITS], ch;
    HitSpan spans[32], span = {-1, 0, 0, 0};
    int line = 0, len = 0, width = 0, maxw = UI_WIDTH - r->layout.margin * 2;
    uint32_t off;
    char status[96];
    text_decoder_at(&r->text, &d, r->top);
    while (line < r->layout.lines_per_page && text_next(&d, &ch, &off)) {
        int cw;
        if (ch == '\r') continue;
        if (ch == '\n') {
            linebuf[len] = 0;
            wrap_push(lines, spans, &line, linebuf, len, span);
            span = (HitSpan){-1, 0, 0, 0};
            len = width = 0;
            continue;
        }
        if (ch == '\t') ch = ' ';
        if (ch < 32) continue;
        cw = reader_char_width(r, ch);
        if (len && width + cw > maxw) {
            linebuf[len] = 0;
            wrap_push(lines, spans, &line, linebuf, len, span);
            span = (HitSpan){-1, 0, 0, 0};
            len = width = 0;
            if (line >= r->layout.lines_per_page) break;
        }
        if (len + 1 < UI_LINE_UNITS) {
            if (r->search_active && off >= r->hit_offset && off < r->hit_end) {
                if (span.left < 0) {
                    span.left = width;
                    span.first = len;
                }
                span.right = width + cw;
                span.last = len + 1;
            }
            linebuf[len++] = ch;
        }
        width += cw;
    }
    if (line < r->layout.lines_per_page && len) {
        linebuf[len] = 0;
        wrap_push(lines, spans, &line, linebuf, len, span);
    }

    gui_gc_begin(r->gc);
    ui_fill(r->gc, 0, 0, UI_WIDTH, UI_HEIGHT, t->bg);
    for (int i = 0; i < line; ++i) {
        int y = r->layout.margin + i * r->layout.line_height;
        ui_draw_u16(r->gc, lines[i], r->layout.margin, y, (gui_gc_Font)r->layout.font_size, t->fg);
        if (spans[i].left >= 0) {
            uint16_t saved = lines[i][spans[i].last];
            ui_fill(r->gc, r->layout.margin + spans[i].left - 1, y - 1,
                            spans[i].right - spans[i].left + 2, r->layout.line_height, t->highlight_bg);
            lines[i][spans[i].last] = 0;
            ui_draw_u16(r->gc, lines[i] + spans[i].first, r->layout.margin + spans[i].left, y,
                     (gui_gc_Font)r->layout.font_size, t->highlight_fg);
            lines[i][spans[i].last] = saved;
        }
    }
    gui_gc_setColorRGB(r->gc, (t->muted >> 16) & 255, (t->muted >> 8) & 255, t->muted & 255);
    gui_gc_drawLine(r->gc, 0, UI_HEIGHT - 18, UI_WIDTH, UI_HEIGHT - 18);
    if (r->search_active) {
        snprintf(status, sizeof(status), "%s", TXT_SEARCH_MODE_HINT);
    } else {
        snprintf(status, sizeof(status), L("%lu/%lu页 %lu%% %s H:提示", "%lu/%lu %lu%% %s H:tips", "%lu/%lu %lu%% %s H:aide"),
                 (unsigned long)(r->page + 1), (unsigned long)r->index.page_count,
                 (unsigned long)percent_for_reader(r), text_encoding_name(r->text.encoding));
    }
    ui_draw_text(r->gc, status, 6, UI_HEIGHT - 15, Regular9, t->muted);
    gui_gc_finish(r->gc);
    gui_gc_blit_to_screen(r->gc);
}

static void reader_goto(Reader *r, TextPosition pos) {
    if (pos.byte_offset >= r->index.last_visible_end)
        pos = index_position_for_page(&r->index, r->index.page_count - 1);
    r->top = text_safe_position(&r->text, pos.byte_offset, pos.source_line);
    r->page = index_page_for_offset(&r->index, r->top.byte_offset);
}

static void reader_goto_page(Reader *r, uint32_t page) {
    if (!r->index.page_count) return;
    if (page >= r->index.page_count) page = r->index.page_count - 1;
    r->page = page;
    r->top = text_safe_position(&r->text, r->index.pages[page].byte_offset,
                                r->index.pages[page].source_line);
}

static TextPosition position_for_line_exact(Reader *r, uint32_t line) {
    TextPosition pos = index_position_for_line(&r->index, line);
    TextDecoder d;
    uint16_t ch;
    uint32_t off;
    if (line <= 1) return r->index.pages[0];
    text_decoder_at(&r->text, &d, pos);
    while (d.line < line && text_next(&d, &ch, &off)) {
        if (ch == '\n' && d.line == line) {
            pos.byte_offset = d.offset;
            pos.source_line = d.line;
            return pos;
        }
    }
    pos.byte_offset = d.offset;
    pos.source_line = d.line;
    return pos;
}


static void add_bookmark(Reader *r) {
    Bookmark *b;
    if (r->book.bookmark_count >= MAX_BOOKMARKS) {
        app_message(r->gc, r->app, L("书签", "Bookmarks", "Signets"), L("书签已满，请先删除", "Bookmark list full; delete one first", "Signets pleins ; supprimez-en un"));
        return;
    }
    b = &r->book.bookmarks[r->book.bookmark_count++];
    memset(b, 0, sizeof(*b));
    b->position = r->top;
    b->timestamp = (uint32_t)time(NULL);
    text_make_excerpt(&r->text, r->top, b->excerpt, EXCERPT_LEN);
    save_reader(r);
    app_message(r->gc, r->app, L("书签", "Bookmarks", "Signets"), L("已添加书签", "Bookmark added", "Signet ajouté"));
}


static size_t append_u16(uint16_t *out, size_t capacity, size_t length, const uint16_t *text) {
    while (length + 1 < capacity && *text) out[length++] = *text++;
    if (capacity) out[length] = 0;
    return length;
}

static int manage_bookmarks(Reader *r) {
    uint16_t labels[MAX_BOOKMARKS][72];
    uint16_t blank[16];
    char prefix[24];
    int backfilled = 0;
    int i, pick;
    if (!r->book.bookmark_count) {
        app_message(r->gc, r->app, L("书签", "Bookmarks", "Signets"), L("暂无书签", "No bookmarks", "Aucun signet"));
        return 0;
    }
    ui_utf8_to_u16(L("（空白）", "(blank)", "(vide)"), blank, (int)(sizeof(blank) / sizeof(blank[0])));
    for (i = 0; i < (int)r->book.bookmark_count; ++i) {
        Bookmark *bookmark = &r->book.bookmarks[i];
        uint32_t percent = r->text.size ?
            (uint32_t)(((uint64_t)bookmark->position.byte_offset * 100u) / r->text.size) : 0;
        size_t length;
        if (!bookmark->excerpt[0]) {
            text_make_excerpt(&r->text, bookmark->position, bookmark->excerpt, EXCERPT_LEN);
            if (bookmark->excerpt[0]) backfilled = 1;
        }
        snprintf(prefix, sizeof(prefix), "%02d %lu%% ", i + 1, (unsigned long)percent);
        length = (size_t)ui_utf8_to_u16(prefix, labels[i],
                                    (int)(sizeof(labels[i]) / sizeof(labels[i][0])));
        length = append_u16(labels[i], sizeof(labels[i]) / sizeof(labels[i][0]), length,
                            bookmark->excerpt[0] ? bookmark->excerpt : blank);
        (void)length;
    }
    if (backfilled) save_reader(r);
    pick = choose_u16_list(r->gc, r->app, L("管理书签", "Bookmarks", "Signets"), labels[0],
                           sizeof(labels[0]) / sizeof(labels[0][0]), r->book.bookmark_count);
    if (pick >= 0) {
        const char *actions[] = {L("跳转", "Go to", "Aller à"), L("删除", "Delete", "Supprimer"), L("取消", "Cancel", "Annuler")};
        int ans = choose_list(r->gc, r->app, L("书签操作", "Bookmark action", "Action sur le signet"), actions, 3);
        if (ans == 0) {
            reader_goto(r, r->book.bookmarks[pick].position);
            save_reader(r);
            return 1;
        } else if (ans == 1) {
            memmove(&r->book.bookmarks[pick], &r->book.bookmarks[pick + 1],
                    (r->book.bookmark_count - pick - 1) * sizeof(r->book.bookmarks[0]));
            r->book.bookmark_count--;
            save_reader(r);
        }
    }
    return 0;
}

static int query_from_user(Reader *r, uint16_t *query) {
    static const uint16_t empty[1] = {0};
    const uint16_t *initial = r->app->history_count ? r->app->history[0] : empty;
    return request_u16_input(L("搜索", "Search", "Recherche"), L("输入搜索词", "Search for", "Texte à chercher"), initial, query, QUERY_LEN);
}


static uint32_t next_char_offset(Reader *r, TextPosition pos) {
    TextDecoder d;
    uint16_t ch;
    uint32_t off;
    text_decoder_at(&r->text, &d, pos);
    if (text_next(&d, &ch, &off)) return d.offset;
    return pos.byte_offset;
}

static int do_search(Reader *r, int direction, int reuse) {
    uint32_t hit;
    uint16_t query[QUERY_LEN], value;
    TextDecoder match;
    uint32_t anchor, off;
    int continuing;
    memcpy(query, r->last_query, sizeof(query));
    if (reuse && !query[0] && r->app->history_count)
        memcpy(query, r->app->history[0], sizeof(query));
    if (!reuse && !query_from_user(r, query)) return 0;
    if (!query[0]) return 0;
    memcpy(r->last_query, query, sizeof(r->last_query));
    if (!reuse) {
        storage_add_history(r->app, query);
        if (!storage_save_app(r->app, r->data_dir))
            debug_fail_errno("do_search:save_history", r->data_dir);
    }
    continuing = reuse && r->search_active;
    if (direction >= 0) {
        uint32_t from = continuing ?
            next_char_offset(r, (TextPosition){r->hit_offset, r->top.source_line}) :
            r->top.byte_offset;
        if (!text_find_forward(&r->text, query, from, &hit)) {
            app_message(r->gc, r->app, L("搜索", "Search", "Recherche"), continuing ? TXT_SEARCH_NO_NEXT : TXT_SEARCH_NOT_FOUND);
            return 0;
        }
    } else {
        anchor = continuing ? r->hit_offset : r->top.byte_offset;
        if (!text_find_backward(&r->text, query, anchor, r->index.pages, r->index.page_count, &hit)) {
            app_message(r->gc, r->app, L("搜索", "Search", "Recherche"), continuing ? TXT_SEARCH_NO_PREV : TXT_SEARCH_NOT_FOUND);
            return 0;
        }
    }
    text_decoder_at(&r->text, &match, (TextPosition){hit, r->top.source_line});
    for (int i = 0; query[i]; ++i)
        if (!text_next(&match, &value, &off)) break;
    r->hit_offset = hit;
    r->hit_end = match.offset;
    r->search_active = 1;
    reader_goto(r, index_position_for_offset(&r->index, hit));
    return 1;
}

static void do_chapter_jump(Reader *r) {
    int pick;
    const char *title;
    if (!r->index.chapter_count) {
        app_message(r->gc, r->app, TXT_CHAPTER_DIRECTORY, TXT_NO_CHAPTERS);
        return;
    }
    title = r->index.chapters_truncated ? TXT_CHAPTER_DIRECTORY_LIMIT : TXT_CHAPTER_DIRECTORY;
    pick = choose_u16_list(r->gc, r->app, title, r->index.chapters[0].title,
                           sizeof(r->index.chapters[0]) / sizeof(r->index.chapters[0].title[0]),
                           (int)r->index.chapter_count);
    if (pick >= 0) reader_goto(r, r->index.chapters[pick].position);
}

static void reader_chapter_step(Reader *r, int direction) {
    TextPosition pos;
    if (index_chapter_for_offset(&r->index, r->top.byte_offset, direction, &pos))
        reader_goto(r, pos);
}

static void do_jump(Reader *r) {
    const char *items[] = {L("百分比", "Percent", "Pourcentage"), L("页码", "Page", "Page"),
                           L("物理行号", "Source line", "Ligne source"), TXT_CHAPTER_DIRECTORY};
    int pick = choose_list(r->gc, r->app, L("跳转", "Go to", "Aller à"), items, 4);
    int value = 0;
    if (pick < 0) return;
    if (pick == 0) {
        value = (int)percent_for_reader(r);
        if (show_1numeric_input(L("跳转", "Go to", "Aller à"), "", L("百分比 0-100", "Percent 0-100", "Pourcentage 0-100"), &value, 0, 100))
            reader_goto(r, index_position_for_percent(&r->index, value));
    } else if (pick == 1) {
        value = (int)r->page + 1;
        if (show_1numeric_input(L("跳转", "Go to", "Aller à"), "", L("页码", "Page", "Page"), &value, 1, (int)r->index.page_count))
            reader_goto(r, index_position_for_page(&r->index, (uint32_t)value - 1));
    } else if (pick == 2) {
        value = (int)r->top.source_line;
        if (show_1numeric_input(L("跳转", "Go to", "Aller à"), "", L("原始物理行号", "Source line", "Ligne source"), &value, 1, (int)r->index.total_source_lines))
            reader_goto(r, position_for_line_exact(r, (uint32_t)value));
    } else {
        do_chapter_jump(r);
    }
}

static void file_info(Reader *r) {
    const UiTheme *t = &ui_themes[r->app->theme];
    char path[68];
    char name[72];
    char line[128];
    int y;
    shorten_tail(name, sizeof(name), base_name(r->text.path));
    shorten_tail(path, sizeof(path), r->text.path);
    for (;;) {
        gui_gc_begin(r->gc);
        ui_draw_header(r->gc, t, L("文件信息", "File info", "Infos sur le fichier"));
        snprintf(line, sizeof(line), L("名称: %s", "Name: %s", "Nom : %s"), name);
        ui_draw_text(r->gc, line, 10, 42, Regular10, t->fg);
        ui_draw_text(r->gc, L("路径:", "Path:", "Chemin :"), 10, 64, Regular10, t->fg);
        y = ui_draw_wrapped(r->gc, path, 10, 82, UI_WIDTH - 20, Regular9, t->fg);
        if (y < 112) y = 112;

        snprintf(line, sizeof(line), L("大小: %lu 字节", "Size: %lu bytes", "Taille : %lu octets"), (unsigned long)r->text.size);
        ui_draw_text(r->gc, line, 10, y, Regular9, t->fg);
        y += 18;
        snprintf(line, sizeof(line), L("编码: %s", "Encoding: %s", "Encodage : %s"), text_encoding_name(r->text.encoding));
        ui_draw_text(r->gc, line, 10, y, Regular9, t->fg);
        y += 18;
        snprintf(line, sizeof(line), L("行数: %lu", "Lines: %lu", "Lignes : %lu"), (unsigned long)r->index.total_source_lines);
        ui_draw_text(r->gc, line, 10, y, Regular9, t->fg);
        y += 18;
        snprintf(line, sizeof(line), L("页数: %lu", "Pages: %lu", "Pages : %lu"), (unsigned long)r->index.page_count);
        ui_draw_text(r->gc, line, 10, y, Regular9, t->fg);
        y += 18;
        snprintf(line, sizeof(line), L("修改: %lu", "Modified: %lu", "Modifié : %lu"), (unsigned long)r->text.mtime);
        ui_draw_text(r->gc, line, 10, y, Regular9, t->fg);

        ui_draw_footer(r->gc, t, TXT_ESC_OR_ENTER_BACK);
        gui_gc_finish(r->gc);
        gui_gc_blit_to_screen(r->gc);

        y = wait_key();
        wait_release();
        if (y == 5 || y == 6) return;
    }
}

static int choose_encoding(Reader *r) {
    const char *names[] = {L("自动检测", "Auto detect", "Détection auto"), "UTF-8", "GB18030",
                           "UTF-16 LE", "UTF-16 BE"};
    const char *items[5];
    char labels[5][40];
    int pick;
    TextFile detected;
    TextEncoding old_encoding = r->text.encoding;
    uint32_t old_start = r->text.data_start;
    uint32_t old_override = r->book.encoding_override;
    int changed;
    uint32_t i;
    for (i = 0; i < 5; ++i) {
        snprintf(labels[i], sizeof(labels[i]), "%s%s",
                 i == old_override ? "* " : "  ", names[i]);
        items[i] = labels[i];
    }
    pick = choose_list(r->gc, r->app, L("文本编码 (*当前)", "Text encoding (* current)", "Encodage (* actuel)"), items, 5);
    if (pick < 0 || (uint32_t)pick == old_override) return 0;
    if (pick == 0) {
        if (!text_open(&detected, r->text.path)) {
            debug_fail_errno("choose_encoding:text_open", r->text.path);
            return 0;
        }
        r->text.encoding = detected.encoding;
        r->text.data_start = detected.data_start;
        text_close(&detected);
    } else if (!text_select_encoding(&r->text, (TextEncoding)pick)) {
        debug_fail_errno("choose_encoding:text_select_encoding", r->text.path);
        return 0;
    }
    r->book.encoding_override = (uint32_t)pick;
    r->book.encoding = (uint32_t)r->text.encoding;
    changed = r->text.encoding != old_encoding || r->text.data_start != old_start;
    if (changed) {
        index_free(&r->index);
        if (!ensure_index(r)) {
            r->text.encoding = old_encoding;
            r->text.data_start = old_start;
            r->book.encoding_override = old_override;
            r->book.encoding = (uint32_t)old_encoding;
            return -1;
        }
        for (i = 0; i < r->book.bookmark_count; ++i) {
            Bookmark *b = &r->book.bookmarks[i];
            TextDecoder d;
            TextPosition pos = index_position_for_offset(&r->index, b->position.byte_offset);
            uint16_t ch;
            uint32_t off;
            text_decoder_at(&r->text, &d, pos);
            while (d.offset < b->position.byte_offset && text_next(&d, &ch, &off)) {}
            b->position.byte_offset = d.offset;
            b->position.source_line = d.line;
            text_make_excerpt(&r->text, b->position, b->excerpt, EXCERPT_LEN);
        }
        r->search_active = 0;
        r->hit_offset = 0;
        reader_goto_page(r, 0);
    }
    if (!save_reader(r)) return -1;
    if (changed)
        app_message(r->gc, r->app, L("文本编码", "Text encoding", "Encodage du texte"),
                    L("编码已切换，阅读进度从开头开始", "Encoding changed; reading starts at beginning", "Encodage modifié ; reprise au début"));
    return 1;
}

static int reader_menu(Reader *r) {
    for (;;) {
        const char *items[] = {
            L("1. 添加书签", "1. Add bookmark", "1. Ajouter un signet"),
            L("2. 管理书签", "2. Bookmarks", "2. Signets"),
            L("3. 搜索", "3. Search", "3. Rechercher"),
            L("4. 向后查找", "4. Find next", "4. Résultat suivant"),
            L("5. 向前查找", "5. Find previous", "5. Résultat précédent"),
            L("6. 跳转", "6. Go to", "6. Aller à"),
            L("7. 阅读设置", "7. Reader settings", "7. Réglages de lecture"),
            L("8. 文件信息", "8. File info", "8. Infos sur le fichier"),
            L("9. 返回书库", "9. Return home", "9. Retour à l'accueil"),
            L("0. 文本编码", "0. Text encoding", "0. Encodage du texte")
        };
        int pick = choose_list_rows(r->gc, r->app, L("功能菜单", "Reader menu", "Menu de lecture"),
                                    items, NULL, 0, 10, 9);
        if (pick < 0) return 0;
        if (pick == 8) return -1;
        if (pick == 0) add_bookmark(r);
        else if (pick == 1) {
            if (manage_bookmarks(r) > 0) return 0;
        }
        else if (pick == 2) { do_search(r, 1, 0); return 0; }
        else if (pick == 3) { do_search(r, 1, 1); return 0; }
        else if (pick == 4) { do_search(r, -1, 1); return 0; }
        else if (pick == 5) { do_jump(r); return 0; }
        else if (pick == 6) {
            if (settings_menu(r->gc, r->app, r->data_dir, r) < 0) return -1;
        } else if (pick == 7) {
            file_info(r);
        } else if (pick == 9) {
            if (choose_encoding(r) < 0) return -1;
            return 0;
        }
    }
}

int open_reader(const char *path, Gc gc, AppState *app, const char *data_dir) {
    Reader r;
    int key, menu_res, page_drawn = 0;
    app_log("reader", "open begin %s", path ? path : "(null)");
    memset(&r, 0, sizeof(r));
    r.gc = gc;
    r.app = app;
    strncpy(r.data_dir, data_dir, sizeof(r.data_dir) - 1);
    index_init(&r.index);
    if (!text_open(&r.text, path)) {
        debug_fail_errno("open_reader:text_open", path);
        app_log("reader", "open text failed");
        return 0;
    }
    if (strcmp(path, r.text.path))
        storage_remove_recent(app, path);
    storage_load_book(&r.book, &r.text, data_dir);
    r.layout = layout_from_state(app);
    if (!ensure_index(&r)) {
        app_log("reader", "ensure index failed");
        index_free(&r.index);
        text_close(&r.text);
        return 0;
    }
    reader_goto(&r, r.book.progress.byte_offset ? r.book.progress : r.index.pages[0]);
    app_log("reader", "loop begin page_count=%lu start_offset=%lu",
            (unsigned long)r.index.page_count, (unsigned long)r.top.byte_offset);
    for (;;) {
        TextPosition before;
        if (!page_drawn) draw_page(&r);
        page_drawn = 0;
        if (shortcut_hints(gc, app, data_dir, SHORTCUT_READER, r.search_active, 0)) continue;
        key = wait_key();
        if (!last_key_ctrl &&
            ((key == 4 && r.page + 1 < r.index.page_count) || (key == 3 && r.page > 0))) {
            uint32_t page = key == 4 ? r.page + 1 : r.page - 1;
            uint32_t start = r.index.pages[page].byte_offset;
            uint32_t end = page + 1 < r.index.page_count ?
                           r.index.pages[page + 1].byte_offset : r.text.size;
            if (!r.search_active || start >= r.hit_end || end <= r.hit_offset) {
                r.search_active = 0;
                r.hit_offset = 0;
            }
            reader_goto_page(&r, page);
            draw_page(&r);
            page_drawn = 1;
            wait_release();
            if (!save_reader(&r)) page_drawn = 0;
            continue;
        }
        wait_release();
        if (key == 14) {
            shortcut_hints(gc, app, data_dir, SHORTCUT_READER, r.search_active, 1);
            continue;
        }
        before = r.top;
        if (r.search_active) {
            if (key == 5 || key == 2 || key == 9) {
                if (do_search(&r, 1, 1)) save_reader(&r);
                continue;
            } else if (key == 1) {
                if (do_search(&r, -1, 1)) save_reader(&r);
                continue;
            } else if (key == 6) {
                r.search_active = 0;
                r.hit_offset = 0;
                continue;
            }
            r.search_active = 0;
            r.hit_offset = 0;
        }
        r.hit_offset = 0;
        if (last_key_ctrl && (key == 3 || key == 4))
            reader_chapter_step(&r, key == 4 ? 1 : -1);
        else if (key == 4 && r.page + 1 < r.index.page_count) reader_goto_page(&r, r.page + 1);
        else if (key == 3 && r.page > 0) reader_goto_page(&r, r.page - 1);
        else if (key == 1 && last_key_ctrl) reader_goto_page(&r, 0);
        else if (key == 2 && last_key_ctrl) reader_goto_page(&r, r.index.page_count - 1);
        else if (key == 2 && r.top.source_line < r.index.total_source_lines) reader_goto(&r, position_for_line_exact(&r, r.top.source_line + 1));
        else if (key == 1 && r.top.source_line > 1) reader_goto(&r, position_for_line_exact(&r, r.top.source_line - 1));
        else if (key == 8) add_bookmark(&r);
        else if (key == 9) do_search(&r, 1, 0);
        else if (key == 10) do_jump(&r);
        else if (key == 7) {
            menu_res = reader_menu(&r);
            if (menu_res < 0) break;
        } else if (key == 6) break;
        if (r.top.byte_offset != before.byte_offset || r.top.source_line != before.source_line)
            save_reader(&r);
    }
    save_reader_all(&r);
    index_free(&r.index);
    text_close(&r.text);
    app_log("reader", "open end");
    return 1;
}

int reader_apply_layout(Reader *r, int font_changed) {
    uint32_t pct = percent_for_reader(r);
    r->layout = layout_from_state(r->app);
    if (font_changed) {
        memset(r->ascii_width, 0, sizeof(r->ascii_width));
        memset(r->wide_chars, 0, sizeof(r->wide_chars));
    }
    index_free(&r->index);
    if (!ensure_index(r)) return -1;
    reader_goto(r, index_position_for_percent(&r->index, pct));
    return 1;
}

int reader_reindex(Reader *r) {
    TextPosition current = r->top;
    index_free(&r->index);
    if (!ensure_index(r)) return -1;
    reader_goto(r, current);
    return 1;
}
