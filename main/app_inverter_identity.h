#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app_inverter_driver.h"
#include "nvs.h"

/* Az IPv4-címek hálózati bájtsorrendűek. A rögzítés az egyetlen kiválasztott
 * helyi végpontra vonatkozik, a bemeneti IP végig a felhasználó beállítása. */
typedef struct {
    uint32_t version, original, last, network, mask, gateway;
    uint16_t port;
    uint8_t unit, mac[6];
    char ssid[33], profile[40], device[96], user[96];
} app_inverter_pin_t;

typedef struct {
    app_inverter_pin_t pin;
    uint32_t chosen;
    uint8_t before[6];
    bool bound, direct, storage_error, invalid_address;
    char active[16];
} app_inverter_identity_t;

typedef struct {
    uint32_t local, mask, gateway, match, announced;
    uint8_t exact_mac[6], router_mac[6];
    char ssid[33];
} app_inverter_link_t;

/* Az ESP adapter a TCP/IP-szálon vizsgálja a hálózati szomszédtáblát. */
bool app_inverter_link(uint32_t address, const uint8_t mac[6], bool probe, app_inverter_link_t *out);
void app_inverter_link_idle(void);
void app_inverter_identity_load(app_inverter_identity_t *s, const app_inverter_config_t *cfg, bool local);
const char *app_inverter_identity_prepare(app_inverter_identity_t *s, char out[16]);
const char *app_inverter_identity_verify(app_inverter_identity_t *s);
const char *app_inverter_identity_commit(app_inverter_identity_t *s);
esp_err_t app_inverter_identity_forget(nvs_handle_t handle);
