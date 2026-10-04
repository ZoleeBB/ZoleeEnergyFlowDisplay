#pragma once
#include "esp_err.h"
#include "app_config.h"
typedef enum { APP_BOOT_SD=0,APP_BOOT_CONFIG,APP_BOOT_WIFI,APP_BOOT_NTP,APP_BOOT_MQTT,APP_BOOT_WAIT_DATA } app_boot_stage_t;
esp_err_t app_ui_init(void);
void app_ui_set_config(const app_config_t *cfg);
void app_ui_show_boot(app_boot_stage_t stage,const char *detail);
void app_ui_show_fatal(const char *title,const char *detail);
void app_ui_start_runtime(void);
