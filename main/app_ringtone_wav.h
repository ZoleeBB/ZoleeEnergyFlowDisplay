#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Egyetlen közös, dekóder nélküli formátum mindkét kijelzőnek: PCM16 LE,
 * mono, 16 kHz, legfeljebb 8 másodperc. RIFF kiegészítő chunk megengedett. */
#define APP_RINGTONE_MAX (256000U + 4096U)
bool app_ringtone_name(const char *name);
bool app_ringtone_wav(const uint8_t *data, size_t size, size_t *offset, size_t *bytes);
