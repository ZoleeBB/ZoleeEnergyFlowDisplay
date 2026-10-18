#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "esp_app_desc.h"
typedef struct
{
    size_t size;
} esp_partition_t;
typedef unsigned esp_ota_handle_t;
typedef int esp_ota_img_states_t;
#define ESP_OTA_IMG_PENDING_VERIFY 1
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *partition);
const esp_partition_t *esp_ota_get_last_invalid_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition,
                                      esp_ota_img_states_t *state);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition, esp_app_desc_t *desc);
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t size, esp_ota_handle_t *handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
