#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_config.h"
#include "app_state.h"
#define APP_STATS_SLOT_COUNT 288
typedef struct { double production_kwh, consumption_kwh, grid_import_kwh, grid_export_kwh, battery_charge_kwh, battery_discharge_kwh; } app_energy_totals_t;
typedef struct { bool valid; uint32_t sample_count; float pv_kw, load_kw, grid_kw, battery_kw; app_energy_totals_t energy; } app_stats_slot_t;
typedef struct { char date[11]; app_energy_totals_t totals; app_energy_totals_t completed_interval_totals; bool has_yesterday; app_energy_totals_t yesterday_completed_interval_totals; app_stats_slot_t slots[APP_STATS_SLOT_COUNT]; } app_stats_snapshot_t;
esp_err_t app_stats_init(const app_config_t *cfg);
void app_stats_process_sample(const app_energy_sample_t *sample);
void app_stats_get_snapshot(app_stats_snapshot_t *out);
void app_stats_start_task(void);
esp_err_t app_stats_save_now(void);
