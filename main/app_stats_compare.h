#pragma once
/* Pure five-minute slot comparison; requires app_stats_slot_t and
 * app_energy_totals_t, declared by app_stats.h. No ESP/RTOS dependency. */
#include <stdbool.h>
#include <math.h>

typedef struct
{
    app_energy_totals_t today;
    app_energy_totals_t yesterday;
    unsigned paired_slots;
    bool partial;
} app_stats_comparison_result_t;

static inline bool app_stats_slot_measured(const app_stats_slot_t *slot)
{
    const app_energy_totals_t *e = &slot->energy;
    /* A chart_gap hiányos idősávot jelöl, de benne már lehet valós
     * mérés; az energiát megtartjuk, a százalékot közelítővé tesszük. */
    /* Earlier saved versions may have integrated energy but no sample count. */
    return (slot->valid && slot->sample_count > 0) || fabs(e->production_kwh) > 1e-9 ||
           fabs(e->consumption_kwh) > 1e-9 || fabs(e->grid_import_kwh) > 1e-9 ||
           fabs(e->grid_export_kwh) > 1e-9 || fabs(e->battery_charge_kwh) > 1e-9 ||
           fabs(e->battery_discharge_kwh) > 1e-9 || fabs(e->pv_home_kwh) > 1e-9 ||
           fabs(e->pv_export_kwh) > 1e-9 || fabs(e->pv_stored_kwh) > 1e-9 ||
           fabs(e->grid_home_kwh) > 1e-9 || fabs(e->battery_home_kwh) > 1e-9;
}

static inline void app_stats_comparison_add(app_energy_totals_t *sum,
                                            const app_energy_totals_t *slot)
{
#define ADD(field) sum->field += slot->field
    ADD(production_kwh);
    ADD(consumption_kwh);
    ADD(grid_import_kwh);
    ADD(grid_export_kwh);
    ADD(battery_charge_kwh);
    ADD(battery_discharge_kwh);
    ADD(pv_home_kwh);
    ADD(pv_export_kwh);
    ADD(pv_stored_kwh);
    ADD(grid_home_kwh);
    ADD(battery_home_kwh);
#undef ADD
}

static inline app_stats_comparison_result_t app_stats_compare_slots(
    const app_stats_slot_t *today, const app_stats_slot_t *yesterday, int completed_slots)
{
    app_stats_comparison_result_t result = {0};
    if (completed_slots < 0)
        completed_slots = 0;
    if (completed_slots > APP_STATS_SLOT_COUNT)
        completed_slots = APP_STATS_SLOT_COUNT;
    bool had_gap = false;
    for (int i = 0; i < completed_slots; i++)
    {
        if (!app_stats_slot_measured(&today[i]) || !app_stats_slot_measured(&yesterday[i]))
            continue;
        if (today[i].chart_gap || yesterday[i].chart_gap)
            had_gap = true;
        app_stats_comparison_add(&result.today, &today[i].energy);
        app_stats_comparison_add(&result.yesterday, &yesterday[i].energy);
        result.paired_slots++;
    }
    result.partial = had_gap || result.paired_slots < (unsigned)completed_slots;
    return result;
}
