#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "app_config.h"
esp_err_t app_mqtt_start(const app_config_t *cfg);
bool app_mqtt_wait_connected(TickType_t timeout_ticks);
