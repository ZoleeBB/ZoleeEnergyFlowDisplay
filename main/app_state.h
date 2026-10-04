#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double power_grid_kw, power_bat_kw, power_pv_kw, soc_bat_pct, power_full_kw;
    double temperature_c, wind_speed_kmh;
    bool has_sun_radiation, has_precipitation;
    int status;
    bool night_mode;
    double battery_temperature_c;
    int64_t received_monotonic_us, received_epoch_s;
} app_energy_sample_t;

typedef struct {
    bool sd_ok, config_ok, wifi_connected, time_synced, mqtt_connected, has_valid_sample;
    int64_t mqtt_connected_monotonic_us, last_valid_packet_monotonic_us, last_valid_packet_epoch_s;
} app_runtime_state_t;

void app_state_init(void);
void app_state_set_sd_ok(bool value);
void app_state_set_config_ok(bool value);
void app_state_set_wifi_connected(bool value);
void app_state_set_time_synced(bool value);
void app_state_set_mqtt_connected(bool value);
void app_state_set_sample(const app_energy_sample_t *sample);
void app_state_get_runtime(app_runtime_state_t *out);
bool app_state_get_sample(app_energy_sample_t *out);
