#pragma once
#include "app_config.h"

/* A korábbi fő kijelző SD-hibája nem válthat automatikusan Slave módra. */
esp_err_t app_boot_config_remember(const app_config_t *cfg);
esp_err_t app_boot_config_fallback(app_config_t *cfg, char *error, size_t error_len);
esp_err_t app_boot_config_save(
    const char *json, size_t length, app_config_t *cfg, char *error, size_t error_len);
