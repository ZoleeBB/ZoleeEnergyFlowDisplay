#pragma once

#include "app_loxone_audio_service.h"

/* Explicit helyi minta: élő Audioserver elérését nem színleli. */
void app_sim_audio_reset(void);
bool app_sim_audio_configure(const char *id, unsigned revision, const lx_audio_summary_t *summary);
void app_sim_audio_set_favorite_count(unsigned total);
extern unsigned app_sim_audio_commands, app_sim_audio_last_favorite;
extern lx_audio_action_t app_sim_audio_last_action;
