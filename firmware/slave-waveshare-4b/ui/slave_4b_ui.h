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

/* A teljes képernyős diagnosztika tartalma. A Slave-nek nincs saját weboldala,
 * ezért a firmware verziója, a hálózati címe, a fényerő- és jelenlétállapota és
 * a RAM-naplója itt olvasható le. A hívó másodpercenként frissíti. */
typedef struct
{
    const char *firmware;        /* NULL vagy üres: „—". */
    const char *ip;              /* NULL vagy üres: „—". */
    const char *log;             /* Többsoros, a legfrissebb elöl. */
    uint16_t rotation_degrees;
    uint32_t uptime_seconds;
    bool night;
    unsigned brightness;         /* Ténylegesen beállított fényerő (%). */
    unsigned day_brightness;     /* A Mastertől kapott nappali fényerő (%). */
    unsigned night_brightness;   /* A Mastertől kapott éjszakai fényerő (%). */
    bool blank_at_night;         /* A Master pillanatnyi sötétítési kérése. */
    bool presence_source;        /* Van kiválasztott jelenlétforrás. */
    bool presence_fresh;         /* Friss, érvényes jelenlétminta. */
    bool presence_active;
    uint32_t presence_age_seconds;
    uint32_t timeout_seconds;
    int32_t blank_in_seconds;    /* Negatív: nincs folyamatban visszaszámlálás. */
    uint32_t touch_count;
} slave_4b_status_t;

void slave_4b_ui_status(const slave_4b_status_t *status);
bool slave_4b_ui_diagnostics_visible(void);

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
    char currency[APP_CURRENCY_MAX];
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
extern const lv_image_dsc_t slave_4b_weather_humidity;
