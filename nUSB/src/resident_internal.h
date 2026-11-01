#ifndef NUSB_RESIDENT_INTERNAL_H
#define NUSB_RESIDENT_INTERNAL_H

#include "resident.h"

void msc_backend_init(ResidentApi *api);
MscResult msc_backend_register(void);

#endif
