#include "app.h"
#include "input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_PATH "build/nedit-app.txt"
#define OUTPUT_PATH "build/nedit-app-saved.txt"
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

typedef enum { KEY, HOME, CONFIRM, PROMPT, MESSAGE } StepKind;
typedef struct {
    StepKind kind;
    unsigned value;
    uint32_t character;
    const char *path;
} Step;

static App app;
static const Step *steps;
static size_t step_count, step_index;

static const Step *next_step(StepKind kind) {
    const Step *step;
    CHECK(step_index < step_count);
    step = &steps[step_index++];
    CHECK(step->kind == kind);
    return step;
}

static char *copy_path(const char *path) {
    char *copy;
    if (!path) return NULL;
    copy = malloc(strlen(path) + 1);
    CHECK(copy != NULL);
    strcpy(copy, path);
    return copy;
}

bool ui_init(Ui *ui) { ui->initialized = true; return true; }
void ui_destroy(Ui *ui) { ui->initialized = false; }
void ui_draw(Ui *ui, Editor *editor, const char *message, bool caps, bool sticky) {
    (void)ui; (void)message; (void)caps; (void)sticky;
    CHECK(app.file_open && editor->buffer == &app.buffer);
}
char *ui_choose_file(Ui *ui, const char *initial, bool *create, bool home) {
    const Step *step = next_step(HOME);
    (void)ui; (void)initial;
    CHECK(home && !app.file_open && !app.quit);
    *create = step->value != 0;
    return copy_path(step->path);
}
int ui_confirm_save(Ui *ui) {
    (void)ui;
    CHECK(app.file_open && text_buffer_modified(&app.buffer));
    return (int)next_step(CONFIRM)->value;
}
char *ui_prompt(const char *title, const char *label, const char *initial) {
    (void)title; (void)label; (void)initial;
    return copy_path(next_step(PROMPT)->path);
}
void ui_message(const char *title, const char *message) {
    (void)title; (void)message;
    next_step(MESSAGE);
}
InputAction ui_show_menu(Ui *ui) { (void)ui; return INPUT_NONE; }
uint32_t ui_choose_symbol(Ui *ui) { (void)ui; return 0; }
void input_wait(void) {}
void wait_no_key_pressed(void) {}
const char *get_documents_dir(void) { return "build"; }
InputEvent input_read(void) {
    const Step *step = next_step(KEY);
    InputEvent event = {(InputAction)step->value, step->character};
    CHECK(app.file_open && !app.quit);
    return event;
}

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(text, 1, strlen(text), file) == strlen(text));
    CHECK(fclose(file) == 0);
}

static void expect_file(const char *path, const char *text) {
    FILE *file = fopen(path, "rb");
    char data[64];
    size_t length = strlen(text);
    CHECK(file != NULL && length < sizeof(data));
    CHECK(fread(data, 1, sizeof(data), file) == length);
    CHECK(!memcmp(data, text, length));
    CHECK(fclose(file) == 0);
}

static void run(const Step *script, size_t count, const char *initial) {
    steps = script;
    step_count = count;
    step_index = 0;
    CHECK(app_init(&app, initial));
    app_run(&app);
    CHECK(step_index == step_count && app.quit && !app.file_open);
    app_destroy(&app);
}
#define RUN(script, initial) run(script, sizeof(script) / sizeof(script[0]), initial)

int main(void) {
    const Step save[] = {
        {KEY, INPUT_CHARACTER, 0x03c0, NULL}, {KEY, INPUT_SAVE, 0, NULL},
        {HOME, 0, 0, NULL}
    };
    const Step reopen[] = {
        {KEY, INPUT_EXIT, 0, NULL}, {HOME, 0, 0, INPUT_PATH},
        {KEY, INPUT_EXIT, 0, NULL}, {HOME, 0, 0, NULL}
    };
    const Step cancel[] = {
        {KEY, INPUT_CHARACTER, 'A', NULL}, {KEY, INPUT_EXIT, 0, NULL},
        {CONFIRM, 3, 0, NULL}, {KEY, INPUT_CHARACTER, 'B', NULL},
        {KEY, INPUT_SAVE, 0, NULL}, {HOME, 0, 0, NULL}
    };
    const Step discard[] = {
        {KEY, INPUT_CHARACTER, 'X', NULL}, {KEY, INPUT_EXIT, 0, NULL},
        {CONFIRM, 2, 0, NULL}, {HOME, 0, 0, NULL}
    };
    const Step confirm_save[] = {
        {KEY, INPUT_CHARACTER, 0x221a, NULL}, {KEY, INPUT_EXIT, 0, NULL},
        {CONFIRM, 1, 0, NULL}, {HOME, 0, 0, NULL}
    };
    const Step save_as[] = {
        {KEY, INPUT_CHARACTER, 'Y', NULL}, {KEY, INPUT_SAVE_AS, 0, NULL},
        {PROMPT, 0, 0, NULL}, {KEY, INPUT_CHARACTER, 'Z', NULL},
        {KEY, INPUT_SAVE_AS, 0, NULL}, {PROMPT, 0, 0, OUTPUT_PATH},
        {HOME, 0, 0, NULL}
    };
    const Step failed_save[] = {
        {KEY, INPUT_CHARACTER, 'X', NULL}, {KEY, INPUT_SAVE, 0, NULL},
        {MESSAGE, 0, 0, NULL}, {KEY, INPUT_EXIT, 0, NULL},
        {CONFIRM, 1, 0, NULL}, {MESSAGE, 0, 0, NULL},
        {KEY, INPUT_EXIT, 0, NULL}, {CONFIRM, 2, 0, NULL}, {HOME, 0, 0, NULL}
    };
    const Step failed_open[] = {
        {MESSAGE, 0, 0, NULL}, {HOME, 0, 0, INPUT_PATH},
        {KEY, INPUT_EXIT, 0, NULL}, {HOME, 0, 0, NULL}
    };
    const Step home_retry[] = {
        {HOME, 0, 0, "build/missing-directory/file"}, {MESSAGE, 0, 0, NULL},
        {HOME, 0, 0, INPUT_PATH}, {KEY, INPUT_EXIT, 0, NULL}, {HOME, 0, 0, NULL}
    };
    remove(OUTPUT_PATH);
    remove(INPUT_PATH ".nedit.backup");
    write_file(INPUT_PATH, "text");
    RUN(save, INPUT_PATH);
    expect_file(INPUT_PATH, "\xcf\x80text");
    RUN(reopen, INPUT_PATH);
    expect_file(INPUT_PATH, "\xcf\x80text");
    write_file(INPUT_PATH, "text");
    RUN(cancel, INPUT_PATH);
    expect_file(INPUT_PATH, "ABtext");
    RUN(discard, INPUT_PATH);
    expect_file(INPUT_PATH, "ABtext");
    RUN(confirm_save, INPUT_PATH);
    expect_file(INPUT_PATH, "\xe2\x88\x9a" "ABtext");
    write_file(INPUT_PATH, "text");
    RUN(save_as, INPUT_PATH);
    expect_file(INPUT_PATH, "text");
    expect_file(OUTPUT_PATH, "YZtext");
    write_file(INPUT_PATH ".nedit.backup", "recovery");
    RUN(failed_save, INPUT_PATH);
    expect_file(INPUT_PATH, "text");
    expect_file(INPUT_PATH ".nedit.backup", "recovery");
    RUN(failed_open, "build/missing-directory/file");
    RUN(home_retry, NULL);
    remove(INPUT_PATH);
    remove(OUTPUT_PATH);
    remove(INPUT_PATH ".nedit.backup");
    puts("PASS nEdit: save/home/exit, cancel/discard, failed save/open recovery");
    return 0;
}
