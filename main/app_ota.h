#pragma once
#include "esp_err.h"

/* OTA próba-image háromperces automatikus validációjának elindítása. */
esp_err_t app_ota_start_validation_task(void);
