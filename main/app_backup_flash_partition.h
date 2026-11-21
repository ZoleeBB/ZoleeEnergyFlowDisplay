#pragma once

#include "esp_partition.h"

/* Az író, a boothelyreállító és a gyári törlés ugyanazokat a slotokat használja. */
#define APP_BACKUP_FLASH_SLOT_SIZE (1024U * 1024U)
#define APP_BACKUP_FLASH_SLOT_COUNT 2U

static inline const esp_partition_t *app_backup_flash_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
}
