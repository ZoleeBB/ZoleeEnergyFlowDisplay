/* A hálózati címzést szándékosan nem továbbítjuk: minden slave saját DHCP-címet kap. */
#include "app_ble_profile.h"
#include <string.h>

void app_ble_profile_encode(const app_config_t *cfg, uint8_t out[APP_BLE_PROFILE_SIZE])
{
    memset(out, 0, APP_BLE_PROFILE_SIZE);
    out[0] = 'Z';
    out[1] = 'B';
    out[2] = 1;
    out[3] = cfg->ota.auto_update ? 1 : 0;
    out[4] = (uint8_t)strlen(cfg->wifi.ssid);
    out[5] = (uint8_t)strlen(cfg->wifi.password);
    memcpy(out + 6, cfg->wifi.ssid, out[4]);
    memcpy(out + 38, cfg->wifi.password, out[5]);
}

bool app_ble_profile_decode(const uint8_t *data, size_t length, app_config_t *cfg)
{
    if (!data || !cfg || length != APP_BLE_PROFILE_SIZE || data[0] != 'Z' || data[1] != 'B' ||
        data[2] != 1 || data[3] > 1 || data[4] == 0 || data[4] > 32 || data[5] > 64 ||
        memchr(data + 6, 0, data[4]) || memchr(data + 38, 0, data[5]))
        return false;
    memset(cfg->wifi.ssid, 0, sizeof(cfg->wifi.ssid));
    memset(cfg->wifi.password, 0, sizeof(cfg->wifi.password));
    memcpy(cfg->wifi.ssid, data + 6, data[4]);
    memcpy(cfg->wifi.password, data + 38, data[5]);
    memset(&cfg->wifi.network, 0, sizeof(cfg->wifi.network));
    cfg->ota.auto_update = data[3] != 0;
    return true;
}
