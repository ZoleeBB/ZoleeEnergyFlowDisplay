#pragma once

#include "lvgl.h"
#include "app_stats.h"

/* Saját, padding nélküli napi plot. A feliratok és görbék azonos skálát használnak. */
lv_obj_t *app_chart_create(lv_obj_t *parent,
                           bool consumption,
                           bool night,
                           uint32_t first_color,
                           uint32_t second_color,
                           uint32_t third_color);
void app_chart_update(lv_obj_t *obj,
                      const app_stats_snapshot_t *snapshot,
                      double maximum_kw,
                      int minute_of_day);

typedef enum
{
    APP_CHART_PRODUCTION,
    APP_CHART_CONSUMPTION,
    APP_CHART_GRID,
    APP_CHART_BATTERY,
    APP_CHART_SOC,
    APP_CHART_WEEK,
    APP_CHART_MONTH,
    APP_CHART_SOLAR_HOURLY
} app_chart_kind_t;
lv_obj_t *app_chart_create_kind(lv_obj_t *parent,
                                app_chart_kind_t kind,
                                bool night,
                                uint32_t first,
                                uint32_t second,
                                uint32_t third);
void app_chart_update_period(lv_obj_t *obj, const app_stats_period_t *period);

void app_chart_set_reference_style(lv_obj_t *obj);
