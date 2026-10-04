#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "app_config.h"

/* MQTT kliens indítása a betöltött konfigurációval. */
esp_err_t app_mqtt_start(const app_config_t *cfg);

/* Várakozás a brokerkapcsolat felépülésére a megadott FreeRTOS timeoutig. */
bool app_mqtt_wait_connected(TickType_t timeout_ticks);
