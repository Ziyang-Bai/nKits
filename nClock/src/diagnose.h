#ifndef NCLOCK_DIAGNOSE_H
#define NCLOCK_DIAGNOSE_H

#ifdef NCLOCK_DIAGNOSTIC
#include <stdbool.h>

bool clock_diag_start(const char *build);
void clock_diag(const char *stage, unsigned detail);
void clock_diag_mark(const char *stage, unsigned detail);
unsigned clock_diag_step(void);
void clock_diag_finish(int result);
void clock_diag_frame_ready(void);
void clock_diag_abort(const char *reason) __attribute__((noreturn));
#else
#define clock_diag(stage, detail) ((void)0)
#define clock_diag_mark(stage, detail) ((void)0)
#endif

#endif
