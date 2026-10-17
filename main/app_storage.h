#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* A Waveshare BSP által használt SD csatolási pont. */
#ifndef APP_SD_MOUNT_POINT
#define APP_SD_MOUNT_POINT "/sdcard"
#endif

/* SD-kártya kötelező induláskori csatolása. */
esp_err_t app_storage_boot_mount(void);

/* Futás közben észlelt SD hiba központi jelzése. */
void app_storage_mark_fault(const char *reason);

/* Pillanatnyi SD állapot lekérése. */
bool app_storage_is_ok(void);

/* Percenkénti automatikus helyreállítást végző task elindítása. */
esp_err_t app_storage_start_recovery_task(void);

/* Kizárólag SD műveletekhez; LVGL lock alatt és MQTT callbackből tilos.
 * A sablonok előtöltése a grafikus lock megszerzése ELŐTT történik. */
esp_err_t app_storage_lock(void);
/* UI-előtöltéshez: soha nem vár hosszú feltöltésre/mentésre. */
bool app_storage_try_lock(void);
void app_storage_unlock(void);


/* Gyorsítótárazott FAT-kapacitás; a lekérés nem végez SD-műveletet. */
bool app_storage_space(uint64_t *total,uint64_t *free_bytes);
