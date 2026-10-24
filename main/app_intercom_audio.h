#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_INTERCOM_AUDIO_IDLE,
    APP_INTERCOM_AUDIO_CONNECTING,
    APP_INTERCOM_AUDIO_ACTIVE,
    APP_INTERCOM_AUDIO_STOPPING,
    APP_INTERCOM_AUDIO_ERROR,
    APP_INTERCOM_AUDIO_TESTING
} app_intercom_audio_state_t;

typedef struct {
    app_intercom_audio_state_t state;
    unsigned revision;
    bool speaker_test;
    char message[160];
} app_intercom_audio_status_t;

esp_err_t app_intercom_audio_start(void);
/* Csak helyi érintés indít hívást; false megszakítja a folyamatban lévő felépítést is. */
esp_err_t app_intercom_audio_request(bool enabled, unsigned revision);
void app_intercom_audio_snapshot(app_intercom_audio_status_t *out);

/* A hibastátusz mellett is tarthat még a kodek/socket takarítása. */
bool app_intercom_audio_busy(void);
bool app_intercom_audio_active_for(unsigned revision);
/* Az elfogadott kapunyitási kérés után 5 s; hálózati nyugtára nem várunk. */
void app_intercom_audio_gate_opened(unsigned revision);
/* Helyi hangszóróteszt, SIP/mikrofon/AEC nélkül; ugyanaz a worker birtokolja a kodeket. */
esp_err_t app_intercom_audio_test_request(bool enabled, unsigned volume);
