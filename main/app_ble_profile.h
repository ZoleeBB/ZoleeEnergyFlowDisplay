#pragma once
#include "app_config.h"

/* A védett 1004 karakterisztika fix, hosszú olvasással átadott formátuma.
 * A 2. verzió két bájttal hosszabb: a Slave-nek nincs SD-kártyája és saját
 * beállítóoldala, ezért a nappali/éjszakai fényerőt is a Mastertől kapja. */
#define APP_BLE_PROFILE_SIZE 104
#define APP_BLE_PROFILE_V1_SIZE 102
/* Innentől a mezők futás közben is alkalmazhatók, nem kell értük újraindulni. */
#define APP_BLE_PROFILE_LIVE_OFFSET APP_BLE_PROFILE_V1_SIZE

void app_ble_profile_encode(const app_config_t *cfg, uint8_t out[APP_BLE_PROFILE_SIZE]);
bool app_ble_profile_decode(const uint8_t *data, size_t length, app_config_t *cfg);

/* Induláskor NVS-ből visszatölt; a master automatikus PIN-jét is előkészíti. */
esp_err_t app_ble_profile_init(app_config_t *cfg);
bool app_ble_profile_pin_changed(void);
esp_err_t app_ble_profile_confirm_pin(void);

/* Csak már hitelesített peer adata kerülhet ebbe a nem blokkoló sorba. */
bool app_ble_profile_submit(const uint8_t data[APP_BLE_PROFILE_SIZE]);
bool app_ble_profile_ready(void);
void app_ble_profile_apply(app_config_t *cfg);
/* Futás közbeni profilmódosítás ezután mentés utáni újraindítást kérhet. */
void app_ble_profile_boot_complete(void);
