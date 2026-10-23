#pragma once

#include "app_state.h"
#include "cJSON.h"

/* Korlátos, mintaszámtól független memória; az előzmény nem nyers mérésnapló. */
#define APP_BATTERY_HISTORY 7

typedef struct
{
    int direction; /* -1: töltés, +1: kisütés. */
    double start_soc, end_soc, energy_kwh, seconds;
    int64_t end_epoch_s;
} app_battery_run_t;

typedef struct
{
    double nominal_kwh;
    double charge_kwh, discharge_kwh; /* Csak a diagnosztikával megfigyelt időszak. */
    app_battery_run_t history[APP_BATTERY_HISTORY];
    unsigned count;
    double efficiency_pct;
    int64_t efficiency_epoch_s;
    bool efficiency_valid;

    /* Ezek RAM-állapotok: mentésből nem folytatunk félbeszakadt mérést. */
    bool have_previous, pair_charge_valid;
    app_energy_sample_t previous;
    app_battery_run_t active, pair_charge;
    double idle_seconds, progress_soc;
    unsigned interruptions;
} app_battery_diagnostics_t;

typedef struct
{
    bool available, active_ready, charge_valid, discharge_valid, capacity_valid, efficiency_valid;
    double charge_ratio_pct, discharge_ratio_pct, capacity_kwh, capacity_pct;
    double efficiency_pct, cycles, active_soc_span, active_energy_kwh, active_soc_error_pp;
    double charge_soc_span, discharge_soc_span, charge_energy_kwh, discharge_energy_kwh;
    unsigned discharge_runs, broad_runs, interruptions;
    int active_direction;
    int64_t latest_epoch_s;
} app_battery_report_t;

void app_battery_init(app_battery_diagnostics_t *state, double nominal_kwh);
void app_battery_break(app_battery_diagnostics_t *state);
void app_battery_process(app_battery_diagnostics_t *state, const app_energy_sample_t *sample,
                         double nominal_kwh, unsigned timeout_seconds);
void app_battery_report(const app_battery_diagnostics_t *state, int64_t now_epoch_s,
                        app_battery_report_t *out);
/* Csak lezárt eredmények kerülnek az atomi napi statisztikamentésbe. */
cJSON *app_battery_to_json(const app_battery_diagnostics_t *state);
bool app_battery_from_json(const cJSON *json, app_battery_diagnostics_t *out);
