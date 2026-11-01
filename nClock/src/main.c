#include "app.h"
#include "diagnose.h"
#ifdef NCLOCK_DIAGNOSTIC
#include "resident_build.h"
#endif

int main(int argc, char **argv) {
#ifdef NCLOCK_DIAGNOSTIC
    if (!clock_diag_start(NKITS_BUILD_ID)) return 1;
    int result = app_run(argc, argv);
    clock_diag_finish(result);
    return result;
#else
    return app_run(argc, argv);
#endif
}
