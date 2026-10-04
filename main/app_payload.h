#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "app_state.h"
esp_err_t app_payload_parse(const char *json, size_t len, app_energy_sample_t *out, char *error, size_t error_len);
