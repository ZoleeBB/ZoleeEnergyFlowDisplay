#pragma once

#include "esp_err.h"

#include "app_config.h"

/* A bootképernyő egymás utáni, felhasználónak látható lépései. */
typedef enum {
    APP_BOOT_SD = 0,
    APP_BOOT_CONFIG,
    APP_BOOT_WIFI,
    APP_BOOT_NTP,
    APP_BOOT_MQTT,
    APP_BOOT_WAIT_DATA
} app_boot_stage_t;

/* Kijelző, LVGL és háttérvilágítás inicializálása. */
esp_err_t app_ui_init(void);

/* A betöltött konfiguráció átadása a UI-nak. */
void app_ui_set_config(const app_config_t *cfg);

/* Egy bootfázis és opcionális részletes üzenet megjelenítése. */
void app_ui_show_boot(
    app_boot_stage_t stage,
    const char *detail);

/* Blokkoló/végzetes indulási hiba megjelenítése. */
void app_ui_show_fatal(
    const char *title,
    const char *detail);

/* Normál runtime UI task elindítása. */
esp_err_t app_ui_start_runtime(void);

