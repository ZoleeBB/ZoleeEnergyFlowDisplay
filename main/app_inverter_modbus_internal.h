#pragma once

#include "app_inverter_driver.h"
#include "app_inverter_modbus_wire.h"

/* A keresés ugyanazt a keretezést és mérésdekódolást használja, mint
 * a mentett profil. A hívó adja a csatlakoztatott socketet és a határidőt. */
bool app_inverter_modbus_prepare(const app_inverter_config_t *cfg, inv_mb_t *bus, uint16_t *port);
bool app_inverter_modbus_sample(const app_inverter_config_t *cfg, inv_mb_t *bus, app_energy_sample_t *sample);
