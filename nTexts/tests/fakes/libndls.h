#ifndef NTEXTS_TEST_LIBNDLS_H
#define NTEXTS_TEST_LIBNDLS_H

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

const char *get_documents_dir(void);

#endif
