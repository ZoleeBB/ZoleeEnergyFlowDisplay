#pragma once
#include "app_loxone.h"
extern bool app_sim_home_enabled, app_sim_home_connected, app_sim_home_pending;
extern unsigned app_sim_home_commands;
extern lx_home_command_t app_sim_home_last_action;
extern char app_sim_home_last_id[LX_HOME_ID_SIZE];
extern lx_home_config_t app_sim_home_config;
extern lx_home_values_t app_sim_home_values;
void app_sim_home_reset(void);

extern app_intercom_snapshot_t app_sim_intercom;

#include "app_intercom_audio.h"
extern app_intercom_audio_status_t app_sim_audio;
extern unsigned app_sim_audio_starts, app_sim_audio_stops;

extern bool app_sim_media_idle;
extern app_loxone_signals_t app_sim_signals;
