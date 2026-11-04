/* A hálózati címzést szándékosan nem továbbítjuk: minden slave saját DHCP-címet kap. */
#include "app_ble_profile.h"
#include <string.h>

static uint8_t brightness_or_default(uint8_t value, uint8_t fallback)
{
    return value >= 1 && value <= 100 ? value : fallback;
}

void app_ble_profile_encode(const app_config_t *cfg, uint8_t out[APP_BLE_PROFILE_SIZE])
{
    memset(out, 0, APP_BLE_PROFILE_SIZE);
    out[0] = 'Z';
    out[1] = 'B';
    out[2] = 2;
    out[3] = cfg->ota.auto_update ? 1 : 0;
    out[4] = (uint8_t)strlen(cfg->wifi.ssid);
    out[5] = (uint8_t)strlen(cfg->wifi.password);
    memcpy(out + 6, cfg->wifi.ssid, out[4]);
    memcpy(out + 38, cfg->wifi.password, out[5]);
    /* A Slave a Master nappali/éjszakai fényerejét veszi át. A 0 nem érvényes
     * érték (a sötétítést az éjszakai jelenlétszabály intézi), ezért a
     * tartományon kívüli beállítást a biztonságos alapértékre vágjuk. */
    out[102] = brightness_or_default(cfg->display.day_brightness, 80);
    out[103] = brightness_or_default(cfg->display.night_brightness, 20);
}

bool app_ble_profile_decode(const uint8_t *data, size_t length, app_config_t *cfg)
{
    /* Az 1. verzió (102 bájt) továbbra is elfogadott: ilyenkor a Slave a saját,
     * korábban mentett fényerejét tartja meg. */
    bool legacy = length == APP_BLE_PROFILE_V1_SIZE && data && data[2] == 1;
    if (!data || !cfg || (!legacy && length != APP_BLE_PROFILE_SIZE) ||
        data[0] != 'Z' || data[1] != 'B' || (!legacy && data[2] != 2) ||
        data[3] > 1 || data[4] == 0 || data[4] > 32 || data[5] > 64 ||
        memchr(data + 6, 0, data[4]) || memchr(data + 38, 0, data[5]) ||
        (!legacy && (data[102] < 1 || data[102] > 100 || data[103] < 1 || data[103] > 100)))
        return false;
    memset(cfg->wifi.ssid, 0, sizeof(cfg->wifi.ssid));
    memset(cfg->wifi.password, 0, sizeof(cfg->wifi.password));
    memcpy(cfg->wifi.ssid, data + 6, data[4]);
    memcpy(cfg->wifi.password, data + 38, data[5]);
    memset(&cfg->wifi.network, 0, sizeof(cfg->wifi.network));
    cfg->ota.auto_update = data[3] != 0;
    if (!legacy)
    {
        cfg->display.day_brightness = data[102];
        cfg->display.night_brightness = data[103];
    }
    return true;
}
