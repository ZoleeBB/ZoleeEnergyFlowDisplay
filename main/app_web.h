#pragma once

#include "esp_err.h"

#include "app_config.h"

/*
 * A helyi információs, újraindítási és OTA webfelület elindítása.
 * A szolgáltatás a zolee-energy-flow-display.local mDNS néven érhető el.
 */
esp_err_t app_web_start(const app_config_t *cfg);
