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

/* „Mai összesítő” nézet. A számokat a hívó a mastertől kapott napi
 * statisztikából állítja össze; a nézet csak megjelenít. */
typedef struct
{
    bool available; /* Van ellenőrzött mai kép a mastertől. */
    bool stale;     /* Az utolsó jó kép elavult (kapcsolatvesztés/90 s). */
    double production_kwh, consumption_kwh, import_kwh, export_kwh;
    double charge_kwh, discharge_kwh;
    double self_sufficiency_pct, self_consumption_pct;
    bool money_valid; /* A master ár- és pénznemadata megérkezett. */
    double purchase_cost, export_revenue, savings, net_cost;
    char currency[8];
} slave_4b_summary_t;

/* Vízszintes húzással vált az energiaáramlás és az összesítő között. */
void slave_4b_ui_bind_input(lv_indev_t *input);
bool slave_4b_ui_summary_visible(void);
void slave_4b_ui_show_summary(bool show);
void slave_4b_ui_summary(const slave_4b_summary_t *summary);

extern const lv_image_dsc_t slave_4b_day, slave_4b_night, slave_4b_hub;
extern const lv_image_dsc_t slave_4b_weather_sun, slave_4b_weather_cloud;
extern const lv_image_dsc_t slave_4b_weather_partly, slave_4b_weather_rain;
extern const lv_image_dsc_t slave_4b_weather_snow, slave_4b_weather_moon, slave_4b_wind;
