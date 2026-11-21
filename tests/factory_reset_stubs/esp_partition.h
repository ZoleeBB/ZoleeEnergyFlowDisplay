#pragma once
#include <stddef.h>
#include "esp_err.h"
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 255
typedef struct { size_t size; size_t erase_size; } esp_partition_t;
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *name);
esp_err_t esp_partition_erase_range(const esp_partition_t *part, size_t start, size_t length);
esp_err_t esp_partition_read_raw(const esp_partition_t *part, size_t offset, void *out, size_t length);
