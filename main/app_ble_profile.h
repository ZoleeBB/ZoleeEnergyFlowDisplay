#pragma once
#include "app_config.h"

/* A védett 1004 karakterisztika fix, hosszú olvasással átadott formátuma.
 * A hossz és a verzióbájt szándékosan változatlan: a korábbi Slave a 102 bájtos,
 * 1-es verziójú blobot várja, és bármilyen eltérés végleg leállítaná a
 * kapcsolatát. A Slave-nek nincs SD-kártyája és beállítóoldala, ezért a
 * nappali/éjszakai fényerő a jelszómező mögötti, eddig kihasználatlan két
 * bájtban utazik: a régi olvasó csak a jelszó hosszáig másol, így ezeket a
 * bájtokat észre sem veszi. */
#define APP_BLE_PROFILE_SIZE 102
#define APP_BLE_PROFILE_BRIGHTNESS_OFFSET 100
/* A jelszó legfeljebb ekkora lehet, hogy a fényerő elférjen mögötte. */
#define APP_BLE_PROFILE_BRIGHTNESS_MAX_PASSWORD 62
/* Innentől a mezők futás közben is alkalmazhatók, nem kell értük újraindulni. */
#define APP_BLE_PROFILE_LIVE_OFFSET APP_BLE_PROFILE_BRIGHTNESS_OFFSET

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
