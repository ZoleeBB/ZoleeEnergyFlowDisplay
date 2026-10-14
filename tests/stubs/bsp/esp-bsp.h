#pragma once
#include "esp_err.h"
#include "lvgl.h"
typedef struct {
    struct { unsigned task_stack_size; bool stack_in_psram; } lv_adapter_cfg;
    int rotation,tear_avoid_mode;
    struct {int swap_xy,mirror_x,mirror_y;} touch_flags;
} bsp_display_cfg_t;
#define ESP_LV_ADAPTER_DEFAULT_CONFIG() {0}
#define ESP_LV_ADAPTER_ROTATE_90 0
#define ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL 0
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);
esp_err_t bsp_display_lock(int timeout);
void bsp_display_unlock(void);
void bsp_display_backlight_on(void);
void bsp_display_brightness_set(int brightness);
