#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "app_config.h"
#define APP_DEVICE_HOSTNAME "zolee-energy-flow-display"
esp_err_t app_wifi_start(const app_config_t *cfg);
bool app_wifi_wait_connected(TickType_t timeout_ticks);
bool app_wifi_is_connected(void);
esp_err_t app_wifi_get_rssi(int *rssi);
esp_err_t app_wifi_get_ip(char *out,size_t out_len);
