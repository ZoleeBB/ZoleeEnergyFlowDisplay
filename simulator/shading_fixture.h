#pragma once

#include "app_loxone.h"

/* Kizárólag desktop tesztadat; a firmware csak a látható lap állapotát tárolja. */
extern lx_shading_summary_t app_sim_shading_summaries[LX_HOME_MAX_TOTAL_CARDS];
extern unsigned app_sim_shading_positions, app_sim_shading_last_percent;
void app_sim_shading_reset(void);
