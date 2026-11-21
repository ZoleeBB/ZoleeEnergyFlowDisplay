#pragma once
#include "nvs.h"

#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value);
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value);
