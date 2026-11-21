#pragma once
#include "esp_err.h"
typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND ESP_ERR_NOT_FOUND
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out);
esp_err_t nvs_erase_all(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
