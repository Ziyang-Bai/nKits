#ifndef NKITS_RESIDENT_H
#define NKITS_RESIDENT_H

#include <libndls.h>
#include <stddef.h>
#include <stdint.h>

/* Refresh callbacks paint the logical RGB565 display target and return whether
   event-thread work is pending. Only event callbacks may call native I/O. */
typedef bool (*ResidentTick)(void *state);
typedef void (*ResidentEvent)(void *state, struct s_ns_event *event);

void *resident_install(uint32_t id, const char *build, void *state, size_t size,
                       ResidentTick tick, ResidentEvent event,
                       bool *fresh, const char **error);
/* Logical RGB565 drawing target during OS callbacks. Buffered displays present
   this target on their next display refresh. */
volatile uint16_t *resident_framebuffer(void);
/* Copy the displayed LCD image into logical 320x240 RGB565 order. */
bool resident_copy_screen(uint16_t pixels[320 * 240]);
uint32_t resident_seconds(void);
/* Short memory-only critical sections shared with the OS timer callback. */
unsigned resident_lock(void);
void resident_unlock(unsigned mask);
/* Direct OS entries for callbacks; extension syscalls remain SDK-only. */
void *resident_native(unsigned syscall);
/* Enter/Esc presses consumed by the chooser can leave releases for its caller. */
int resident_save_dialog(char folder[256], char name[256], unsigned *pending_actions);
/* Button IDs are 1/2; Escape returns 0, independently of button order. */
int resident_question(const uint16_t *title, const uint16_t *text,
                       const uint16_t *first, const uint16_t *second,
                       unsigned *pending_actions);
/* Captured OS entry points, safe from resident event callbacks. */
int resident_open(const char *path, int flags, int mode);
int resident_write(int fd, const void *data, unsigned size);
int resident_close(int fd);
int resident_remove(const char *path);
/* REL rename preserves an existing destination and reports EEXIST. */
int resident_rename(const char *from, const char *to);

int resident_errno(void);
#endif
