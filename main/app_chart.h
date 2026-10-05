#pragma once

#include "lvgl.h"
#include "app_stats.h"

/* Saját, padding nélküli napi plot. A feliratok és görbék azonos skálát használnak. */
lv_obj_t *app_chart_create(lv_obj_t *parent, bool signed_values, bool night,
                           uint32_t first_color, uint32_t second_color);
void app_chart_update(lv_obj_t *obj, const app_stats_snapshot_t *snapshot,
                      double maximum_kw, int minute_of_day);
