#pragma once
#include "app_config.h"
#include "cJSON.h"
/* Minden hálózati és SD művelet a háttérfeladatban fut. */
esp_err_t app_loxone_start(void);
cJSON *app_loxone_web_state(void);
/* Mentés, illetve mentés nélküli kapcsolatteszt/listázás; aszinkron feladat. */
esp_err_t app_loxone_request(const cJSON *request, bool save);

#include "app_loxone_home_model.h"
cJSON *app_loxone_home_web_state(void);
esp_err_t app_loxone_home_request(const cJSON *request, char error[LX_HOME_ERROR_SIZE]);
