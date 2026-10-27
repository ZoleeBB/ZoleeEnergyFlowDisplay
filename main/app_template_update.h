#pragma once
/* SD-témák frissességének ellenőrzése és egykattintásos frissítése a GitHubról.
 * A hálózati és SD-munka háttérfeladatban fut; a web csak állapotot kérdez. */
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

typedef enum
{
    APP_TEMPLATE_UPDATE_IDLE = 0,
    APP_TEMPLATE_UPDATE_CHECKING,
    APP_TEMPLATE_UPDATE_READY,
    APP_TEMPLATE_UPDATE_INSTALLING,
    APP_TEMPLATE_UPDATE_ERROR,
} app_template_update_state_t;

/* Ellenőrzés indítása (nem blokkol). Foglalt állapotban ESP_ERR_INVALID_STATE. */
esp_err_t app_template_update_check(void);
/* Egy téma (id) vagy minden elérhető frissítés (id == "all") telepítése. */
esp_err_t app_template_update_install(const char *id);
/* Webes állapot: {state, tag, message, items:[{id,name,installed,available,update}]} */
cJSON *app_template_update_status(void);
