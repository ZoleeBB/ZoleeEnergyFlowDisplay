#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "app_config.h"

/* OTA próba-image háromperces automatikus validációjának elindítása. */
esp_err_t app_ota_start_validation_task(void);

/* Napváltásra ébresztett, opcionális GitHub OTA-feladat indítása. */
esp_err_t app_ota_start_auto_update(const app_config_t *cfg);

/* Kézi OTA, automatikus OTA és webes újraindítás nem futhat párhuzamosan. */
bool app_ota_try_begin_operation(void);
void app_ota_end_operation(void);

/* Ugyanazon GitHub-ellenőrzés azonnali, aszinkron indítása; az éjszakai
 * kapcsolót nem módosítja, kikapcsolt automatikánál is tesztelhető. */
esp_err_t app_ota_request_auto_update(void);
