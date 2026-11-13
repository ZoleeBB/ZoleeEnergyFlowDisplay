#pragma once

#include "app_loxone_controls_service.h"

/* Csak desktop tesztadat: a valódi firmware-ben legfeljebb a látható lap él. */
extern lx_controls_summary_t app_sim_controls_summaries[LX_HOME_MAX_TOTAL_CARDS];
extern lx_controls_window_t app_sim_controls_windows[LX_CONTROLS_MAX_WINDOWS];
extern double app_sim_controls_last_value;
extern unsigned app_sim_controls_sets;
void app_sim_controls_reset(void);
