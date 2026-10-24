#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Egyetlen közös, dekóder nélküli formátum mindkét kijelzőnek: PCM16 LE,
 * mono, 16 kHz, legfeljebb 60 másodperc. RIFF kiegészítő chunk megengedett. */
#define APP_RINGTONE_PCM_MAX (16000U * 2U * 60U)
#define APP_RINGTONE_MAX (APP_RINGTONE_PCM_MAX + 4096U)
bool app_ringtone_name(const char* name);
bool app_ringtone_wav(const uint8_t* data, size_t size, size_t* offset, size_t* bytes);

/* A P4 size_t-je 32 bites: 60 s × 24 kHz köztes szorzata már túlcsordulna.
 * A kimeneti képkockaszám és a forrásindex ezért 64 bites szorzással készül. */
static inline uint32_t app_ringtone_frames(uint32_t pcm_bytes, uint32_t rate)
{
    return (uint32_t)((uint64_t)(pcm_bytes / 2U) * rate / 16000U);
}

static inline uint32_t app_ringtone_source_frame(uint32_t frame, uint32_t rate)
{
    return (uint32_t)((uint64_t)frame * 16000U / rate);
}
