#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t app_intercom_start(void);
/* A UI csengetéskor, LVGL-lock nélkül indítja a rövid életű videófeladatot. */
void app_intercom_poll(void);
/* Már dekódolt RGB565 kép; csak a rövid képmásolás alatt vesz LVGL-lockot.
 * A revision kizárja a régi beállításhoz tartozó későn beérkező képet. */
bool app_intercom_ui_frame(const uint8_t *pixels, unsigned width, unsigned height,
                           unsigned stride, unsigned revision);
void app_intercom_ui_status(const char *message, unsigned revision);
