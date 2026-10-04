#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "app_state.h"

/*
 * MQTT JSON payload teljes validálása és app_energy_sample_t objektummá alakítása.
 * Hiba esetén az error puffer emberileg olvasható magyar leírást kap.
 */
esp_err_t app_payload_parse(
    const char *json,
    size_t len,
    app_energy_sample_t *out,
    char *error,
    size_t error_len);
