#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_config.h"
#include "app_state.h"
#include "app_energy_split.h"
#include "app_battery_diagnostics.h"

/* Egy nap 24 * 12 darab ötperces időablakból áll. */
#define APP_STATS_SLOT_COUNT 288

/* A napi és slotonkénti integrált energiamennyiségek kWh-ban. */
typedef struct
{
    double production_kwh;
    double consumption_kwh;
    double grid_import_kwh;
    double grid_export_kwh;
    double battery_charge_kwh;
    double battery_discharge_kwh;
    double pv_home_kwh, pv_export_kwh, pv_stored_kwh, grid_home_kwh, battery_home_kwh;
} app_energy_totals_t;

/*
 * Egy ötperces grafikonpont.
 * A teljesítményértékek a slotban érkezett minták futó átlagai,
 * az energy pedig az időben ténylegesen integrált energia.
 */
typedef struct
{
    bool valid;
    uint32_t sample_count;
    float pv_kw;
    float load_kw;
    float grid_kw;
    float battery_kw;
    float soc_pct;
    bool soc_valid;
    uint32_t soc_sample_count;
    app_energy_totals_t energy;
    app_energy_split_t split; /* Mintánként szétválasztott, majd átlagolt kW. */
    bool chart_gap;           /* Hiányos ötperces ablak: a grafikon nem köti át. */
} app_stats_slot_t;

#define APP_STATS_HISTORY_DAYS 35
typedef struct
{
    char date[11];
    bool valid;
    app_energy_totals_t totals;
} app_stats_day_t;
typedef struct
{
    app_stats_day_t days[31];
    unsigned count;
    app_energy_totals_t totals;
} app_stats_period_t;
typedef struct
{
    double self_sufficiency_pct, self_consumption_pct;
    double purchase_cost, export_revenue, savings, net_cost;
} app_stats_metrics_t;
void app_stats_get_period(bool month, app_stats_period_t *out);
static inline app_stats_metrics_t app_stats_metrics(const app_energy_totals_t *t,
                                                    const app_energy_config_t *cfg)
{
    double local = t->pv_home_kwh + t->battery_home_kwh;
    return (app_stats_metrics_t){
        .self_sufficiency_pct =
            t->consumption_kwh > 0 ? fmin(100, 100 * local / t->consumption_kwh) : 0,
        .self_consumption_pct =
            t->production_kwh > 0
                ? fmin(100, 100 * (t->pv_home_kwh + t->pv_stored_kwh) / t->production_kwh)
                : 0,
        .purchase_cost = t->grid_import_kwh * cfg->electricity_purchase_price,
        .export_revenue = t->grid_export_kwh * cfg->feed_in_price,
        .net_cost = t->grid_import_kwh * cfg->electricity_purchase_price -
                    t->grid_export_kwh * cfg->feed_in_price,
        .savings =
            local * cfg->electricity_purchase_price + t->grid_export_kwh * cfg->feed_in_price};
}

/*
 * A UI számára készített teljes snapshot.
 * A completed_interval mezők csak az utolsó lezárt ötperces határig tartanak.
 */
typedef struct
{
    char date[11];
    app_energy_totals_t totals;
    /* Mai autótöltési energia (kWh); csak a Master helyi statisztikája tölti. */
    double ev_charging_today_kwh;
    app_battery_report_t battery; /* Helyi, mintánként számolt diagnosztika. */
    app_energy_totals_t completed_interval_totals;
    bool has_yesterday;
    /* Szigorúbb, legalább egy órányi közös idősáv százalékhoz. */
    bool comparison_ready;
    /* A hiányos 0:00..azonos lezárt ötperces időszakokból
     * számított százalékok félrevezetők. */
    bool yesterday_comparison_partial;
    /* Azonos lezárt ötperces ablakokban valóban mért energia mindkét napról. */
    unsigned comparison_paired_slots;
    app_energy_totals_t comparison_today_totals;
    app_energy_totals_t comparison_yesterday_totals;
    app_energy_totals_t yesterday_completed_interval_totals;
    app_stats_slot_t slots[APP_STATS_SLOT_COUNT];
    bool remote, remote_available, remote_stale;
    bool reference_valid;
    app_energy_config_t reference_energy;
    char reference_currency[APP_CURRENCY_MAX];
} app_stats_snapshot_t;

bool app_stats_is_ready(void);
bool app_stats_remote_publish(const app_stats_snapshot_t *snapshot);
void app_stats_remote_source(const char *identity);
void app_stats_remote_disconnected(void);

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

/* Csak a karbantartó task hívja, záron kívül, előrehaladó helyi napváltáskor. */
void app_stats_set_day_change_callback(void (*callback)(void));

/* Azonnali kézi mentés, például diagnosztikai célra. */
esp_err_t app_stats_save_now(void);

/* Futás közben biztonságosan módosítható mentési és integrálási paraméterek. */
void app_stats_update_config(const app_statistics_config_t *cfg);

/* Megszakadt adatfolyamon át nem integrálunk energiát. */
void app_stats_break_continuity(void);

#include "app_energy_assistant.h"
/* Csak Master/Loxone, 00..04 helyi órák igazolt mérései. Régi fájl nem referencia. */
bool app_stats_get_night_reference(app_assistant_night_t *out);

/* Kis webes pillanatkép: a teljes napi grafikont nem másolja a HTTP vermére. */
typedef struct {
    app_battery_report_t report;
    double nominal_kwh, charge_kwh, discharge_kwh;
} app_stats_battery_t;
bool app_stats_get_battery(app_stats_battery_t *out);
/* Siker csak a nullázott diagnosztika atomi SD-mentése után. */
esp_err_t app_stats_reset_battery(void);
