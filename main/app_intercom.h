#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t app_intercom_start(void);
/* Loxone módban a Wi-Fi/BLE előtt foglalja a dekóder kis belső DMA-erőforrásait.
 * Nem indít hálózatot, feladatot vagy mikrofont; a képpufferek később PSRAM-ba kerülnek. */
esp_err_t app_intercom_prepare_video(void);
/* A UI csengetéskor, LVGL-lock nélkül indítja a rövid életű videófeladatot. */
void app_intercom_poll(void);
/* Csak a teljes médiafelszabadítás után tölthető vissza a háttér. */
bool app_intercom_idle(void);
/* Már dekódolt RGB565 kép; csak a rövid képmásolás alatt vesz LVGL-lockot.
 * A revision kizárja a régi beállításhoz tartozó későn beérkező képet. */
bool app_intercom_ui_frame(const uint8_t *pixels, unsigned width, unsigned height,
                           unsigned stride, unsigned revision);
void app_intercom_ui_status(const char *message, unsigned revision);
