#pragma once
/* Csak a napváltási újraindítás jelzőjéhez kell: egyetlen u8 kulcs. */
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef unsigned nvs_handle_t;
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define ESP_ERR_NVS_NOT_FOUND ESP_ERR_NOT_FOUND
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
