#pragma once

#include "app_state.h"

/* Egyetlen aktív profil. A tanúsítvány a konfiguráció pillanatképéhez tartozik. */
typedef struct {
    char profile[40], host[128], device[96], user[96], secret[160], key[160];
    uint16_t port;
    uint8_t unit;
    bool battery;
    const char *trusted_pem;
} app_inverter_config_t;

bool app_inverter_modbus_read(const app_inverter_config_t *cfg, app_energy_sample_t *sample,
                              int64_t *source_epoch);
bool app_inverter_http_read(const app_inverter_config_t *cfg, app_energy_sample_t *sample,
                            int64_t *source_epoch);
bool app_inverter_victron_read(const app_inverter_config_t *cfg, app_energy_sample_t *sample);
void app_inverter_victron_close(void);
