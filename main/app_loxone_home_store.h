#pragma once

#include "app_loxone_home_model.h"
#include "esp_err.h"

/* A jelszómentes Otthon-profil külön fájlban él; a kapcsolat mentése nem törli.
 * Sikertelen betöltés/mentés nem módosítja a hívó korábbi profilját. */
#define LX_HOME_STORE_MAX LX_HOME_JSON_MAX
esp_err_t app_loxone_home_store_load(lx_home_config_t *out);
esp_err_t app_loxone_home_store_save(const lx_home_config_t *config);
