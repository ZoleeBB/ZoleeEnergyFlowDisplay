#pragma once
#include "app_config.h"
esp_err_t app_ingest_init(app_data_mode_t mode);
esp_err_t app_ingest_process(app_data_mode_t source, const char *data, size_t length, char *error, size_t error_length);
