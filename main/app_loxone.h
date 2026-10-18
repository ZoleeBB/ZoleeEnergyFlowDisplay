#pragma once
#include "app_config.h"
#include "cJSON.h"
/* Minden hálózati és SD művelet a háttérfeladatban fut. */
esp_err_t app_loxone_start(void);
cJSON *app_loxone_web_state(void);
/* Mentés, illetve mentés nélküli kapcsolatteszt/listázás; aszinkron feladat. */
esp_err_t app_loxone_request(const cJSON *request, bool save);
