#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* A Waveshare BSP által használt SD csatolási pont. */
#define APP_SD_MOUNT_POINT "/sdcard"

/* SD-kártya kötelező induláskori csatolása. */
esp_err_t app_storage_boot_mount(void);

/* Futás közben észlelt SD hiba központi jelzése. */
void app_storage_mark_fault(const char *reason);

/* Pillanatnyi SD állapot lekérése. */
bool app_storage_is_ok(void);

/* Percenkénti automatikus helyreállítást végző task elindítása. */
void app_storage_start_recovery_task(void);
