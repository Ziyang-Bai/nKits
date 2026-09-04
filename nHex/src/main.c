#include "app.h"

#include <libndls.h>

int main(int argc, char **argv) {
    static App app;
    const char *initial_path = argc > 1 ? argv[1] : NULL;

    if (argc > 0) {
        enable_relative_paths(argv);
    }
    if (!app_init(&app, initial_path)) {
        return 1;
    }
    app_run(&app);
    app_destroy(&app);
    return 0;
}
