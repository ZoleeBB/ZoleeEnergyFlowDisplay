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

#include "app_loxone_home_runtime.h"
/* UI-híváskor a pillanatkép memóriáját a hívó biztosítja, SD/hálózat nélkül. */
bool app_loxone_home_snapshot(lx_home_config_t *config, lx_home_values_t *values,
                              unsigned *revision, bool *connected,
                              bool *command_pending, char status[LX_HOME_ERROR_SIZE]);
esp_err_t app_loxone_home_command(const char *card_id, lx_home_command_t action);
