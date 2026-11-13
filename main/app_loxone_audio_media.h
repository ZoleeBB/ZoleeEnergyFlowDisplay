#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LX_AUDIO_MEDIA_SLOTS 13U
#define LX_AUDIO_MEDIA_BYTES_MAX (256U * 1024U)
#define LX_AUDIO_MEDIA_SOURCE_MAX 512U
#define LX_AUDIO_MEDIA_CACHE_MAX (512U * 1024U)

typedef struct lx_audio_image
{
    uint16_t *pixels;
    unsigned width, height, generation, version;
    unsigned references;
} lx_audio_image_t;

/* Minden API rövid, hálózat és dekódolás nélküli. A 0..11. hely a látható
 * kártyáké, a 12. a részletes ablaké. A tulajdonos előbb törli az LVGL forrást,
 * aztán elengedi a képet: a munkaszál nem szabadíthat kirajzolt pixeleket. */
void app_loxone_audio_media_request(unsigned slot, unsigned generation, const char *url,
                                    unsigned version, unsigned size);
lx_audio_image_t *app_loxone_audio_media_acquire(unsigned slot, unsigned generation);
void app_loxone_audio_media_release(lx_audio_image_t *image);
void app_loxone_audio_media_clear(unsigned generation);
void app_loxone_audio_media_close_detail(void);

/* A tiszta, korlátos dekóder külön tesztelhető. Nem használja a kaputelefon
 * JPEG hardverét és nem hív LVGL-t; minden nagy munkapuffer kizárólag PSRAM. */
lx_audio_image_t *lx_audio_image_decode(const uint8_t *data, size_t length,
                                       unsigned size);
void lx_audio_image_free(lx_audio_image_t *image);
