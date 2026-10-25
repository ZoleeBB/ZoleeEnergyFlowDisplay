#pragma once
#include <stdbool.h>
/* Csak saját, üres staging könyvtárba ír. STORED/DEFLATE, CRC, méret- és útvonalvédelem. */
bool app_template_unzip(const char *archive, const char *directory, char error[160]);

