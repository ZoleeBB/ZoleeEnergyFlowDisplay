#pragma once

#include "cJSON.h"
#include "app_state.h"

void app_inverter_tick(void);
esp_err_t app_inverter_settings_save(const cJSON *object);
cJSON *app_inverter_settings_get(void);
bool app_inverter_settings_ready(void);
bool app_inverter_sun_now(app_energy_sample_t *out);
uint32_t app_inverter_sample_timeout_s(void);
