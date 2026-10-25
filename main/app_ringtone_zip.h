#pragma once
#include "cJSON.h"
#include <stdbool.h>

/* A hívó fogja az SD-zárat. Fájlonként validál, hibánál folytatja és jelent. */
bool app_ringtone_unpack(const char *archive, const char *directory,
                         cJSON *report, char error[160]);
