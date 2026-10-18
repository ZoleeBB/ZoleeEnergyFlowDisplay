#pragma once

#include "lvgl.h"
#include "app_state.h"
#include "app_ble_seed.h"

/* Egy példány, egy LVGL szál: a firmware és a szimulátor közös nézete. */
void slave_4b_ui_create(lv_obj_t *screen);
void slave_4b_ui_update(const app_energy_sample_t *sample, bool available,
                    const app_runtime_state_t *runtime, const char *clock,
                    float seconds);
void slave_4b_ui_pairing(const app_ble_pairing_status_t *status);
void slave_4b_ui_message(const char *text);

extern const lv_image_dsc_t slave_4b_day, slave_4b_night, slave_4b_hub;
extern const lv_image_dsc_t slave_4b_weather_sun, slave_4b_weather_cloud;
extern const lv_image_dsc_t slave_4b_weather_partly, slave_4b_weather_rain;
extern const lv_image_dsc_t slave_4b_weather_snow, slave_4b_weather_moon, slave_4b_wind;
