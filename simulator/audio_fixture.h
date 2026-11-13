#pragma once

#include "app_loxone_audio_service.h"

/* Explicit helyi minta: élő Audioserver elérését nem színleli. */
void app_sim_audio_reset(void);
bool app_sim_audio_configure(const char *id, unsigned revision, const lx_audio_summary_t *summary);
void app_sim_audio_set_favorite_count(unsigned total);
void app_sim_audio_set_favorite_page_size(unsigned count);
void app_sim_audio_media_busy(bool busy);
extern unsigned app_sim_audio_commands, app_sim_audio_last_favorite;
extern unsigned app_sim_audio_volume_cancellations;
extern lx_audio_action_t app_sim_audio_last_action;
