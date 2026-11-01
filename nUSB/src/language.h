#ifndef NUSB_LANGUAGE_H
#define NUSB_LANGUAGE_H

#include <stdbool.h>

typedef enum Language {
    LANGUAGE_ENGLISH,
    LANGUAGE_CHINESE,
    LANGUAGE_FRENCH,
    LANGUAGE_COUNT
} Language;

/* Missing/invalid settings default to English. NULL, empty or >=260-byte paths
   disable persistence; selecting a language still changes the current UI. */
void language_init(const char *settings_path);
Language language_current(void);
bool language_select(Language language);
const char *language_name(Language language);
const char *language_text(const char *english);

#endif
