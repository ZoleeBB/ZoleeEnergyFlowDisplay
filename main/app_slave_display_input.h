#pragma once

#include "app_slave_display.h"
#include "lvgl.h"

/* A kapu és az állapot a teljes hozzárendelés alatt érvényes, nem másolható.
 * Minden hívás az LVGL szálán vagy a kijelző zárja alatt történjen.
 * Az eredeti BSP-callback és mindkét saját adatmutató változatlan marad. */
typedef struct app_slave_display_input
{
    lv_indev_t *input;
    lv_indev_read_cb_t original_read;
    app_slave_display_t *state;
    int64_t (*clock_us)(void);
    struct app_slave_display_input *next;
} app_slave_display_input_t;

bool app_slave_display_input_bind(app_slave_display_input_t *gate, lv_indev_t *input,
                                  app_slave_display_t *state, int64_t (*clock_us)(void));
void app_slave_display_input_unbind(app_slave_display_input_t *gate);
