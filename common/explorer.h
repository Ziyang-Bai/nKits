#ifndef NKITS_EXPLORER_H
#define NKITS_EXPLORER_H

#include "resident.h"

bool explorer_install(uint32_t marker, const uint32_t *natives,
                      ResidentExplore (*decide)(void), const char **error);

#endif
