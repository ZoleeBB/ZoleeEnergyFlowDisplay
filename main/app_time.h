#pragma once
#include "esp_err.h"
#include "app_config.h"
esp_err_t app_time_init(const app_config_t *cfg);
esp_err_t app_time_sync_once(void);
void app_time_start_background_task(void);
