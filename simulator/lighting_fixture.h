#pragma once
#include "app_loxone.h"

/* Kizárólag desktop tesztadat. A firmware nem kapcsolódik ehhez a modellhez. */
extern lx_light_circuit_t app_sim_light_circuits[LX_LIGHT_MAX_CIRCUITS];
extern lx_light_summary_t app_sim_light_summary;
extern unsigned app_sim_light_count, app_sim_light_commands, app_sim_light_last_index;
extern bool app_sim_light_failed;
void app_sim_lighting_reset(void);
