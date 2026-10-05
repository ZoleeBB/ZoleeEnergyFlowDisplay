#pragma once
#include <string.h>
#ifdef _MSC_VER
#define strcasecmp _stricmp
#else
int strcasecmp(const char *, const char *);
#endif
