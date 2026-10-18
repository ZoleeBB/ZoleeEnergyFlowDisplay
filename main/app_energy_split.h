#pragma once
#include <math.h>

/* A források becsült felosztása: PV először a háznak, majd töltésre;
 * az akkumulátor nem exportál. A hálózati akkutöltés nem házfogyasztás.
 * A mérési veszteséget/eltérést a maradó hálózati rész tartalmazza. */
typedef struct
{
    float pv_home, pv_export, pv_stored;
    float grid_home, battery_home;
} app_energy_split_t;

static inline app_energy_split_t app_energy_split(double pv, double load, double battery)
{
    pv = fmax(0, pv);
    load = fmax(0, load);
    double home = fmin(pv, load);
    double stored = fmin(pv - home, fmax(0, -battery));
    double discharged = fmin(load - home, fmax(0, battery));
    return (app_energy_split_t){.pv_home = (float)home,
                                .pv_export = (float)(pv - home - stored),
                                .pv_stored = (float)stored,
                                .grid_home = (float)(load - home - discharged),
                                .battery_home = (float)discharged};
}

static inline void
app_energy_split_average(app_energy_split_t *average, app_energy_split_t sample, float count)
{
#define AVERAGE(field) average->field += (sample.field - average->field) / count
    AVERAGE(pv_home);
    AVERAGE(pv_export);
    AVERAGE(pv_stored);
    AVERAGE(grid_home);
    AVERAGE(battery_home);
#undef AVERAGE
}
