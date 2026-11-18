#pragma once
#include "app_config.h"

/* A korábbi fő kijelző SD-hibája nem válthat automatikusan Slave módra. */
esp_err_t app_boot_config_remember(const app_config_t *cfg);
esp_err_t app_boot_config_fallback(app_config_t *cfg, char *error, size_t error_len);
esp_err_t app_boot_config_save(
    const char *json, size_t length, app_config_t *cfg, char *error, size_t error_len);

/* A v4 első beállítás befejezett profilja és a Wi-Fi után félbemaradt állapot. */
bool app_boot_config_setup_pending(void);
bool app_boot_config_internal(void);
esp_err_t app_boot_config_restore(app_config_t *cfg, bool slave_only, char *error, size_t error_len);
esp_err_t app_boot_config_save_setup_wifi(const app_config_t *cfg);
esp_err_t app_boot_config_complete(const char *json, size_t length, app_config_t *cfg,
                                   char *error, size_t error_len);
void app_boot_config_use_storage(bool internal);
void app_boot_config_use_trusted_backup(void);
esp_err_t app_boot_config_cache_file(const char *path);
