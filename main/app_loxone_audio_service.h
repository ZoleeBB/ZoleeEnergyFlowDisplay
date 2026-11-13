#pragma once

#include "app_loxone_audio.h"
#include "app_loxone_home_model.h"
#include "esp_err.h"

typedef enum
{
    LX_AUDIO_CONNECTING,
    LX_AUDIO_READY,
    LX_AUDIO_UNAVAILABLE,
    LX_AUDIO_AUTH_FAILED,
    LX_AUDIO_REQUEST_SENT,
    LX_AUDIO_REQUEST_FAILED
} lx_audio_status_t;

typedef struct
{
    lx_audio_track_t track;
    unsigned generation, volume_max;
    lx_audio_status_t status;
    bool available, controllable, native_ready, volume_up_down, volume_pending;
} lx_audio_summary_t;

/* A Miniserver-worker hívja. A JWT privát másolat, sohasem része pillanatképnek,
 * katalógusnak, URL-nek vagy naplóbejegyzésnek. live=false mindent visszavon. */
void app_loxone_audio_session(const char *username, const char *jwt, bool live);
void app_loxone_audio_page(const lx_home_config_t *config, const cJSON *catalog,
                           int page, unsigned revision);
void app_loxone_audio_hide(void);
void app_loxone_audio_state(const char *uuid, bool numeric, double value,
                            const char *text, size_t length, int64_t now);

bool app_loxone_audio_summary(const char *id, unsigned revision, lx_audio_summary_t *out);
bool app_loxone_audio_allowed(const char *id, unsigned revision, lx_audio_action_t action);
bool app_loxone_audio_value_allowed(const char *id, unsigned revision,
                                   lx_audio_action_t action, unsigned value);
esp_err_t app_loxone_audio_command(const char *id, unsigned revision,
                                  lx_audio_action_t action, unsigned value, bool final);
esp_err_t app_loxone_audio_favorites_request(const char *id, unsigned revision, unsigned start);
bool app_loxone_audio_favorites(const char *id, unsigned revision,
                               lx_audio_favorites_t *out);
esp_err_t app_loxone_audio_favorite(const char *id, unsigned revision,
                                   unsigned favorite_id, unsigned favorites_generation);
void app_loxone_audio_detail_close(void);

/* Az app_loxone.c meglévő sorának hídja. A futtató ezt Audio-mutex NÉLKÜL
 * hívja; a közös kliens a végső elküldés előtt ismét ellenőriz mindent. */
esp_err_t app_loxone_audio_transport(const char *id, unsigned revision,
                                    lx_audio_action_t action, unsigned value);
