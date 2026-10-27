#include "app_battery_diagnostics.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t clock_s = 1800000000;
static int64_t mono_s = 1;

static void point(app_battery_diagnostics_t *state, unsigned seconds, double power, double soc)
{
    clock_s += seconds;
    mono_s += seconds;
    app_energy_sample_t sample = {.power_bat_kw = power, .soc_bat_pct = soc,
        .received_epoch_s = clock_s, .received_monotonic_us = mono_s * 1000000};
    app_battery_process(state, &sample, 10, 180);
}

static void close_run(app_battery_diagnostics_t *state, double soc)
{
    point(state, 1, 0, soc);
    for (unsigned i = 0; i < 10; i++)
        point(state, 60, 0, soc);
}

static void run(app_battery_diagnostics_t *state, int direction, double energy, double start, double end)
{
    /* Háromórás, állandó teljesítményű mérés; a nulláról induló trapéz külön ismert. */
    point(state, 1, direction * energy / 3, start);
    for (unsigned i = 1; i <= 180; i++)
        point(state, 60, direction * energy / 3, start + (end - start) * i / 180.);
    close_run(state, end);
}

static app_battery_report_t report(app_battery_diagnostics_t *state)
{
    app_battery_report_t value;
    app_battery_report(state, clock_s, &value);
    return value;
}

static app_energy_sample_t eta_sample(int64_t seconds, double battery, double load, double soc, int status)
{
    return (app_energy_sample_t){.power_bat_kw = battery, .power_full_kw = load, .soc_bat_pct = soc,
                                 .status = status, .received_monotonic_us = seconds * 1000000};
}

/* Hátralévő idő: simítás, ismételt minta, adatrés, irány, alsó határ és áramszünet. */
static void battery_eta(void)
{
    app_battery_eta_state_t eta;
    app_battery_eta_reset(&eta);
    app_energy_sample_t sample = eta_sample(10, 2, 2, 60, 1);
    assert(app_battery_eta_estimate(&eta, &sample, 10, 0).kind == APP_BATTERY_ETA_UNKNOWN);
    app_battery_eta_update(&eta, &sample);
    /* Beállítatlan határ: normál üzemben 15%, azaz 4,5 kWh / 2 kW = 2 ó 15 p. */
    app_battery_eta_t value = app_battery_eta_estimate(&eta, &sample, 10, 0);
    assert(value.kind == APP_BATTERY_ETA_TO_EMPTY && value.minutes == 135 && !value.capped);
    assert(value.floor_pct == APP_BATTERY_ETA_FLOOR_NORMAL_PCT);
    /* Szigetüzemben ugyanez 5%-ig: 5,5 kWh / 2 kW = 2 ó 45 p. */
    app_energy_sample_t island = eta_sample(10, 2, 2, 60, 3);
    value = app_battery_eta_estimate(&eta, &island, 10, 0);
    assert(value.kind == APP_BATTERY_ETA_TO_EMPTY && value.minutes == 165);
    assert(value.floor_pct == APP_BATTERY_ETA_FLOOR_BACKUP_PCT);
    /* Kézzel megadott határ mindkét üzemmódban felülírja az alapértelmezést. */
    assert(app_battery_eta_estimate(&eta, &sample, 10, 10).minutes == 150);
    assert(app_battery_eta_estimate(&eta, &island, 10, 10).minutes == 150);
    assert(app_battery_eta_estimate(&eta, &sample, 10, 10).floor_pct == 10);
    assert(app_battery_eta_estimate(&eta, &sample, 0, 0).kind == APP_BATTERY_ETA_UNKNOWN);
    /* Ugyanaz a minta nem számít újra, egy rövid kiugrás csak részben hat. */
    app_battery_eta_update(&eta, &sample);
    assert(eta.battery_kw == 2);
    sample = eta_sample(15, 6, 6, 60, 1);
    app_battery_eta_update(&eta, &sample);
    assert(eta.battery_kw > 2 && eta.battery_kw < 2.2);
    /* Hosszú adatrés után nem keverjük a régi átlagot. */
    sample = eta_sample(400, -3, 1, 40, 1);
    app_battery_eta_update(&eta, &sample);
    assert(eta.battery_kw == -3);
    value = app_battery_eta_estimate(&eta, &sample, 10, 0);
    assert(value.kind == APP_BATTERY_ETA_TO_FULL && value.minutes == 120); /* 6 kWh / 3 kW */
    /* Áramszünetre készülve beállítatlan határnál 5%: 4,5 kWh / 1 kW = 4 ó 30 p. */
    app_energy_sample_t backup = eta_sample(400, -3, 1, 50, 2);
    assert(app_battery_eta_estimate(&eta, &backup, 10, 0).minutes == 270);
    sample = eta_sample(401, -3, 1, 99.6, 1);
    assert(app_battery_eta_estimate(&eta, &sample, 10, 0).kind == APP_BATTERY_ETA_FULL);
    /* Status 2: a házfogyasztással számol, töltés közben is. */
    sample = eta_sample(402, -3, 1, 50, 2);
    app_battery_eta_reset(&eta);
    app_battery_eta_update(&eta, &sample);
    value = app_battery_eta_estimate(&eta, &sample, 10, 10);
    assert(value.kind == APP_BATTERY_ETA_BACKUP && value.minutes == 240); /* 4 kWh / 1 kW */
    sample = eta_sample(403, 0, 0, 50, 2);
    app_battery_eta_reset(&eta);
    app_battery_eta_update(&eta, &sample);
    value = app_battery_eta_estimate(&eta, &sample, 10, 10);
    assert(value.kind == APP_BATTERY_ETA_BACKUP && value.capped && value.minutes == APP_BATTERY_ETA_MAX_MINUTES);
    /* Alsó határon és készenlétben nincs kitalált idő. */
    sample = eta_sample(404, 1, 1, 9, 3);
    app_battery_eta_reset(&eta);
    app_battery_eta_update(&eta, &sample);
    assert(app_battery_eta_estimate(&eta, &sample, 10, 10).kind == APP_BATTERY_ETA_EMPTY);
    sample = eta_sample(405, .01, 1, 50, 1);
    app_battery_eta_reset(&eta);
    app_battery_eta_update(&eta, &sample);
    assert(app_battery_eta_estimate(&eta, &sample, 10, 0).kind == APP_BATTERY_ETA_IDLE);
    puts("PASS: battery remaining time smoothing, duplicate/gap handling, floor SOC and outage estimate");
}

int main(void)
{
    app_battery_diagnostics_t state;
    app_battery_init(&state, 10);
    assert(!report(&state).capacity_valid);
    run(&state, -1, 6.6, 20, 80);
    assert(fabs(report(&state).charge_ratio_pct - 110) < .02);
    assert(!report(&state).capacity_valid);
    run(&state, 1, 6, 80, 20);
    app_battery_report_t value = report(&state);
    assert(value.discharge_runs == 1 && value.broad_runs == 1);
    assert(fabs(value.capacity_kwh - 10) < .002);
    assert(fabs(value.discharge_ratio_pct - 100) < .02);
    assert(value.efficiency_valid && fabs(value.efficiency_pct - 100 * 6 / 6.6) < .02);
    assert(fabs(value.cycles - .63) < .001);

    /* A legutóbbi hét szakasz mediánja, nem egyetlen terhelés alapján ítélünk. */
    app_battery_break(&state);
    run(&state, 1, 4.8, 80, 20);
    app_battery_break(&state);
    run(&state, 1, 5.4, 80, 20);
    value = report(&state);
    assert(value.discharge_runs == 3 && fabs(value.capacity_pct - 90) < .02);

    cJSON *json = app_battery_to_json(&state);
    assert(json);
    app_battery_diagnostics_t restored;
    assert(app_battery_from_json(json, &restored));
    assert(!restored.have_previous && !restored.active.direction && !restored.pair_charge_valid);
    assert(report(&restored).discharge_runs == 3);
    assert(fabs(report(&restored).capacity_pct - value.capacity_pct) < 1e-9);
    cJSON_SetNumberValue(cJSON_GetObjectItem(json, "nominal"), -1);
    assert(!app_battery_from_json(json, &restored));
    cJSON_Delete(json);

    /* Rövid SOC-ablakból nem jelenhet meg állapotbecslés. */
    app_battery_init(&state, 10);
    run(&state, 1, .5, 80, 75);
    assert(!report(&state).capacity_valid && state.count == 0);
    /* Adatkimaradás és óraugrás energiáját nem találjuk ki. */
    point(&state, 1, 2, 75);
    double before = state.discharge_kwh;
    point(&state, 181, 2, 74);
    assert(state.discharge_kwh == before && !state.active.direction);
    clock_s += 3600;
    point(&state, 60, 2, 73);
    assert(state.discharge_kwh == before && !state.active.direction);
    point(&state, 1, 2, 20); /* BMS-kalibráció vagy hibás SOC. */
    assert(!state.active.direction && !report(&state).capacity_valid);

    /* Nullátmetszés: 1 óra +2 -> -2 kW: mindkét irányban .5 kWh. */
    app_battery_init(&state, 10);
    app_energy_sample_t a = {.power_bat_kw = 2, .soc_bat_pct = 50,
        .received_monotonic_us = 1000000, .received_epoch_s = clock_s};
    app_energy_sample_t b = a;
    b.power_bat_kw = -2;
    b.received_monotonic_us += 3600LL * 1000000;
    b.received_epoch_s += 3600;
    app_battery_process(&state, &a, 10, 4000);
    app_battery_process(&state, &b, 10, 4000);
    assert(fabs(state.charge_kwh - .5) < 1e-9 && fabs(state.discharge_kwh - .5) < 1e-9);

    app_battery_init(&state, 10);
    run(&state, -1, 6.6, 20, 80);
    app_battery_break(&state);
    run(&state, 1, 6, 80, 20);
    assert(!report(&state).efficiency_valid); /* Azonos SOC sem hidal át egy kiesést. */
    app_battery_init(&state, 10);
    run(&state, -1, 6.6, 20, 80);
    run(&state, 1, 4, 80, 40);
    assert(!report(&state).efficiency_valid); /* Különböző SOC-ablak nem hatásfok. */

    app_battery_report(&state, clock_s + 31LL * 86400, &value);
    assert(!value.capacity_valid && !value.efficiency_valid);
    app_battery_process(&state, &b, 15, 180); /* Új modulszám új referencia. */
    assert(state.count == 0 && state.nominal_kwh == 15 && state.charge_kwh == 0);
    app_battery_init(&state, 0);
    assert(!report(&state).available);
    puts("PASS: battery energy, SOC windows, median, paired efficiency, gaps, reboot and JSON");
    battery_eta();
}
