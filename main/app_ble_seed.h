#pragma once

#include "app_config.h"

/* Kikapcsolt állapotban nem indít BLE hostot és hirdetést. */
esp_err_t app_ble_seed_start(const app_config_t *cfg);
