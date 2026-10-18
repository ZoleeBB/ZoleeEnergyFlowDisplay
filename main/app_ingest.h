#pragma once
#include "app_config.h"
#include "app_state.h"
esp_err_t app_ingest_init(app_data_mode_t mode);
esp_err_t app_ingest_process(
    app_data_mode_t source, const char *data, size_t length, char *error, size_t error_length);

/* Kizárólag a BLE protokoll által már validált, időbélyeges slave mintához. */
esp_err_t app_ingest_slave_sample(const app_energy_sample_t *sample);
