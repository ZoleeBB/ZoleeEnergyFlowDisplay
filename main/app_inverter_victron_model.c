#include "app_inverter_victron_model.h"
#include <math.h>
#include <string.h>

static const char *const groups[] = {"PvOnGrid", "PvOnOutput", "PvOnGenset", "Grid", "Consumption"};

void inv_gx_update(inv_gx_model_t *m, const char *path, bool valid, double value)
{
    inv_gx_value_t item = {.valid = valid && isfinite(value), .value = value};
    if (!strcmp(path, "Dc/Pv/Power")) m->dc_pv = item;
    else if (!strcmp(path, "Dc/Battery/Power")) m->battery = item;
    else if (!strcmp(path, "Dc/Battery/Soc")) m->soc = item;
    else if (!strcmp(path, "Dc/Battery/Temperature")) m->temperature = item;
    else if (!strncmp(path, "Ac/", 3)) {
        path += 3;
        for (int i = 0; i < 5; i++) {
            size_t len = strlen(groups[i]);
            if (strncmp(path, groups[i], len) || path[len] != '/') continue;
            const char *field = path + len + 1;
            inv_gx_ac_t *ac = &m->ac[i];
            if (!strcmp(field, "Total/Power")) ac->total = item;
            else if (!strcmp(field, "NumberOfPhases")) ac->phases = item;
            else if (*field == 'L' && field[1] >= '1' && field[1] <= '3' && !strcmp(field + 2, "/Power"))
                ac->phase[field[1] - '1'] = item;
            return;
        }
    }
}

typedef enum { GX_ABSENT, GX_READY, GX_INCOMPLETE } gx_result_t;

static gx_result_t ac_power(const inv_gx_ac_t *ac, double *watts)
{
    if (ac->total.valid) {
        *watts = ac->total.value;
        return GX_READY;
    }
    if (!ac->phases.valid) {
        for (int i = 0; i < 3; i++) if (ac->phase[i].valid) return GX_INCOMPLETE;
        return GX_ABSENT;
    }
    double n = ac->phases.value;
    if (n == 0) return GX_ABSENT;
    if (n < 1 || n > 3 || n != floor(n)) return GX_INCOMPLETE;
    *watts = 0;
    for (int i = 0; i < (int)n; i++) {
        if (!ac->phase[i].valid) return GX_INCOMPLETE;
        *watts += ac->phase[i].value;
    }
    return GX_READY;
}

bool inv_gx_sample(const inv_gx_model_t *m, bool battery, app_energy_sample_t *out)
{
    bool have_pv = m->dc_pv.valid;
    double pv = have_pv ? m->dc_pv.value : 0;
    for (int i = 0; i < 3; i++) {
        double watts;
        gx_result_t r = ac_power(&m->ac[i], &watts);
        if (r == GX_INCOMPLETE) return false;
        if (r == GX_READY) {
            have_pv = true;
            pv += watts;
        }
    }
    if (!have_pv || !isfinite(pv) || pv < -50) return false;
    app_energy_sample_t s = {.status=1, .power_pv_kw=fmax(0, pv * 0.001), .battery_unavailable=!battery,
                             .soc_unavailable=true, .battery_power_unavailable=true};
    double w;
    s.grid_unavailable = ac_power(&m->ac[3], &w) != GX_READY;
    if (!s.grid_unavailable) s.power_grid_kw = w * 0.001;
    s.home_unavailable = ac_power(&m->ac[4], &w) != GX_READY || w < -50;
    if (!s.home_unavailable) s.power_full_kw = fmax(0, w * 0.001);
    if (battery) {
        s.battery_power_unavailable = !m->battery.valid;
        if (m->battery.valid) s.power_bat_kw = m->battery.value * -0.001;
        if (m->soc.valid && m->soc.value >= 0 && m->soc.value <= 100) {
            s.soc_bat_pct = m->soc.value;
            s.soc_unavailable = false;
        }
        if (m->temperature.valid && m->temperature.value >= -50 && m->temperature.value <= 120) {
            s.battery_temperature_c = m->temperature.value;
            s.battery_temperature_valid = true;
        }
    }
    *out = s;
    return true;
}
