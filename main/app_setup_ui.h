#pragma once

#include "app_setup_wifi.h"
#include "lvgl.h"

/* Mindkét panel LVGL-szálából hívható. Aktív első indítás alatt elfedi a
 * normál felületet. DONE állapotban minden saját objektumát elengedi. */
bool app_setup_ui_update(lv_obj_t *parent, int width, int height, const app_setup_wifi_state_t *state);
void app_setup_ui_close(void);
