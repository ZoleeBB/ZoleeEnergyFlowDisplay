#pragma once
#include <stdbool.h>
#include "esp_err.h"
#define APP_SD_MOUNT_POINT "/sdcard"
esp_err_t app_storage_boot_mount(void);
void app_storage_mark_fault(const char *reason);
bool app_storage_is_ok(void);
void app_storage_start_recovery_task(void);
