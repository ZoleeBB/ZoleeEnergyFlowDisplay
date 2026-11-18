#pragma once

#include "app_config.h"
#include "app_slave_display.h"

/* Kikapcsolt állapotban nem indít BLE hostot és hirdetést. */
esp_err_t app_ble_seed_start(const app_config_t *cfg);

typedef struct
{
    bool pin_required;
    uint32_t token;
    unsigned seconds;
} app_ble_pairing_status_t;

/* A UI csak parancsot ad; SMP API-t kizárólag a NimBLE host task hív. */
void app_ble_pairing_status(app_ble_pairing_status_t *out);
bool app_ble_pairing_submit(uint32_t token, const char *pin);
void app_ble_pairing_open(void);
/* Slave oldal: a Master kulcshiányt jelzett, ezért kézi újrapárosítás kell. */
bool app_ble_pairing_required(void);

/* Last authenticated display policy, retained across transient BLE disconnects. */
bool app_ble_seed_slave_intercom_allowed(void);
bool app_ble_seed_slave_direct(void);
bool app_ble_seed_slave_backlight(app_slave_display_t *display, int64_t now_us, bool *night);
