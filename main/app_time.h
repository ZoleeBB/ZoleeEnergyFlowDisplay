#pragma once

#include "esp_err.h"

#include "app_config.h"

/* NTP kliens és helyi időzóna inicializálása. */
esp_err_t app_time_init(const app_config_t *cfg);

/* Egyetlen szinkronizálási kísérlet végrehajtása. */
esp_err_t app_time_sync_once(void);

/* A konfigurált órás újraszinkront és perces hibaretry-t végző háttértask. */
esp_err_t app_time_start_background_task(void);

/* Master UTC órája és aktuális helyi UTC-eltolása; nem igényel Wi-Fi/NTP-t. */
esp_err_t app_time_sync_from_master(int64_t epoch_ms, int16_t utc_offset_minutes);
