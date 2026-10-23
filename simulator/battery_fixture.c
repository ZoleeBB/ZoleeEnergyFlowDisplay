#include "battery_fixture.h"

static void point(app_battery_diagnostics_t *state, int64_t *epoch, unsigned seconds,
                   double power, double soc)
{
    *epoch += seconds;
    app_energy_sample_t sample = {.power_bat_kw = power, .soc_bat_pct = soc,
        .received_epoch_s = *epoch, .received_monotonic_us = (*epoch - 1577836800) * 1000000};
    app_battery_process(state, &sample, state->nominal_kwh, 180);
}

static void run(app_battery_diagnostics_t *state, int64_t *epoch, int direction, double ratio)
{
    double start = direction < 0 ? 20 : 80;
    double end = direction < 0 ? 80 : 20;
    double power = direction * state->nominal_kwh * .6 * ratio / 3;
    point(state, epoch, 1, power, start);
    for (unsigned i = 1; i <= 180; i++)
        point(state, epoch, 60, power, start + (end - start) * i / 180.);
    point(state, epoch, 1, 0, end);
    for (unsigned i = 0; i < 10; i++)
        point(state, epoch, 60, 0, end);
}

void app_battery_fixture(app_battery_report_t *out, double nominal, int64_t now)
{
    app_battery_diagnostics_t state;
    app_battery_init(&state, nominal);
    int64_t epoch = now - 30 * 3600;
    for (unsigned i = 0; i < 3; i++)
    {
        run(&state, &epoch, -1, 1.05);
        run(&state, &epoch, 1, .92);
    }
    app_battery_break(&state);
    epoch = now - 3600;
    point(&state, &epoch, 0, nominal * .184, 80);
    for (unsigned i = 1; i <= 60; i++)
        point(&state, &epoch, 60, nominal * .184, 80 - i / 3.);
    app_battery_report(&state, now, out);
}
