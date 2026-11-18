#pragma once

#include "app_state.h"

/* A GX a nem csatlakozó ágakról null-t adhat. Nyugtázott teljes pillanatkép
 * után a hiányzó ág kihagyható, az ismert, de hiányos fáziscsoport viszont nem. */
typedef struct {
    bool valid;
    double value;
} inv_gx_value_t;

typedef struct {
    inv_gx_value_t phases, total, phase[3];
} inv_gx_ac_t;

typedef struct {
    inv_gx_ac_t ac[5];
    inv_gx_value_t dc_pv, battery, soc, temperature;
} inv_gx_model_t;

void inv_gx_update(inv_gx_model_t *model, const char *path, bool valid, double value);
bool inv_gx_sample(const inv_gx_model_t *model, bool battery, app_energy_sample_t *sample);
