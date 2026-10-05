#pragma once

#include "esp_err.h"

#include "app_config.h"

/* NTP kliens és helyi időzóna inicializálása. */
esp_err_t app_time_init(const app_config_t *cfg);

/* Egyetlen szinkronizálási kísérlet végrehajtása. */
esp_err_t app_time_sync_once(void);

/* Hatórás normál újraszinkront és 15 perces hibaretry-t végző háttértask. */
esp_err_t app_time_start_background_task(void);

