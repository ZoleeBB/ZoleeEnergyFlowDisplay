#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "app_config.h"

/* Fix mDNS/DHCP hostname, nem konfigurálható. */
#define APP_DEVICE_HOSTNAME "zolee-energy-flow-display"

/* Wi-Fi STA mód indítása a betöltött konfigurációval. */
esp_err_t app_wifi_start(const app_config_t *cfg);

/* Első beállítás: a STA egyszer indul, a választást a boot-feladat alkalmazza. */
esp_err_t app_wifi_start_setup(const app_config_t *cfg);
esp_err_t app_wifi_join_setup(const char *ssid, const char *password);
void app_wifi_finish_setup(void);

/* Kapcsolódásra várás a megadott timeoutig. */
bool app_wifi_wait_connected(TickType_t timeout_ticks);

/* Gyors pillanatnyi kapcsolatellenőrzés. */
bool app_wifi_is_connected(void);

/* Jelerősség lekérése dBm-ben. */
esp_err_t app_wifi_get_rssi(int *rssi);

const char *app_wifi_configured_ssid(void);

/* Aktuális IPv4 cím szöveges lekérése. */
esp_err_t app_wifi_get_ip(char *out, size_t out_len);
