#pragma once

#include "app_loxone.h"

/* Kizárólag desktop tesztadat, a firmware nem tárol teljes IRC-kártyatömböt. */
extern lx_irc_summary_t app_sim_irc_summaries[LX_HOME_MAX_TOTAL_CARDS];
void app_sim_irc_reset(void);
