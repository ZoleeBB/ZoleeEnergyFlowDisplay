#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Egy teljesen validált MQTT mérési minta.
 * A teljesítmények kW-ban, a hőmérsékletek Celsius-fokban szerepelnek.
 */
typedef struct {
    double power_grid_kw;
    double power_bat_kw;
    double power_pv_kw;
    double soc_bat_pct;
    double power_full_kw;

    double temperature_c;
    double wind_speed_kmh;

    bool has_sun_radiation;
    bool has_precipitation;

    int status;
    bool night_mode;
    double battery_temperature_c;

    /* Monotonic idő a timeoutokhoz, epoch idő a statisztikához. */
    int64_t received_monotonic_us;
    int64_t received_epoch_s;
} app_energy_sample_t;

/* Az alrendszerek közös futási állapota. */
typedef struct {
    bool sd_ok;
    bool config_ok;
    bool wifi_connected;
    bool time_synced;
    bool mqtt_connected;
    bool has_valid_sample;

    int64_t mqtt_connected_monotonic_us;
    int64_t last_valid_packet_monotonic_us;
    int64_t last_valid_packet_epoch_s;
} app_runtime_state_t;

/* Közös állapot inicializálása. */
void app_state_init(void);

/* Alrendszer-állapotok frissítése. */
void app_state_set_sd_ok(bool value);
void app_state_set_config_ok(bool value);
void app_state_set_wifi_connected(bool value);
void app_state_set_time_synced(bool value);
void app_state_set_mqtt_connected(bool value);

/* Utolsó érvényes MQTT minta tárolása. */
void app_state_set_sample(const app_energy_sample_t *sample);

/* Szálbiztos állapotlekérdezések. */
void app_state_get_runtime(app_runtime_state_t *out);
bool app_state_get_sample(app_energy_sample_t *out);
