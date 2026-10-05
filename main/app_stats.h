#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_config.h"
#include "app_state.h"
#include "app_energy_split.h"

/* Egy nap 24 * 12 darab ötperces időablakból áll. */
#define APP_STATS_SLOT_COUNT 288

/* A napi és slotonkénti integrált energiamennyiségek kWh-ban. */
typedef struct {
    double production_kwh;
    double consumption_kwh;
    double grid_import_kwh;
    double grid_export_kwh;
    double battery_charge_kwh;
    double battery_discharge_kwh;
} app_energy_totals_t;

/*
 * Egy ötperces grafikonpont.
 * A teljesítményértékek a slotban érkezett minták futó átlagai,
 * az energy pedig az időben ténylegesen integrált energia.
 */
typedef struct {
    bool valid;
    uint32_t sample_count;
    float pv_kw;
    float load_kw;
    float grid_kw;
    float battery_kw;
    app_energy_totals_t energy;
    app_energy_split_t split; /* Mintánként szétválasztott, majd átlagolt kW. */
    bool chart_gap; /* Hiányos ötperces ablak: a grafikon nem köti át. */
} app_stats_slot_t;

/*
 * A UI számára készített teljes snapshot.
 * A completed_interval mezők csak az utolsó lezárt ötperces határig tartanak.
 */
typedef struct {
    char date[11];
    app_energy_totals_t totals;
    app_energy_totals_t completed_interval_totals;
    bool has_yesterday;
    app_energy_totals_t yesterday_completed_interval_totals;
    app_stats_slot_t slots[APP_STATS_SLOT_COUNT];
} app_stats_snapshot_t;

/* Statisztikai könyvtár és napi állományok inicializálása. */
esp_err_t app_stats_init(const app_config_t *cfg);

/* Új, már validált MQTT minta feldolgozása. */
void app_stats_process_sample(const app_energy_sample_t *sample);

/* Konzisztens UI snapshot lekérése. */
void app_stats_get_snapshot(app_stats_snapshot_t *out);

/* Periodikus mentést és napváltást végző task indítása. */
esp_err_t app_stats_start_task(void);

/* Egy karbantartási lépés: RAM-napváltás és szükség szerinti mentés. */
void app_stats_tick(void);

/* Azonnali kézi mentés, például diagnosztikai célra. */
esp_err_t app_stats_save_now(void);

