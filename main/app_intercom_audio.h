#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_INTERCOM_AUDIO_IDLE,
    APP_INTERCOM_AUDIO_CONNECTING,
    APP_INTERCOM_AUDIO_ACTIVE,
    APP_INTERCOM_AUDIO_STOPPING,
    APP_INTERCOM_AUDIO_ERROR
} app_intercom_audio_state_t;

typedef struct {
    app_intercom_audio_state_t state;
    unsigned revision;
    char message[160];
} app_intercom_audio_status_t;

esp_err_t app_intercom_audio_start(void);
/* Csak helyi érintés indít hívást; false megszakítja a folyamatban lévő felépítést is. */
esp_err_t app_intercom_audio_request(bool enabled, unsigned revision);
void app_intercom_audio_snapshot(app_intercom_audio_status_t *out);
/* Csak az aktív hívás módosítható; a kodeket a hangfeladat állítja, nem a UI. */
esp_err_t app_intercom_audio_set_volume(unsigned percent, unsigned revision);
unsigned app_intercom_audio_get_volume(void);

/* A hibastátusz mellett is tarthat még a kodek/socket takarítása. */
bool app_intercom_audio_busy(void);
