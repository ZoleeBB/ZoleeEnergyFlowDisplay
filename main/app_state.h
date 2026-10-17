#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

/*
 * Egy teljesen validált MQTT mérési minta.
 * A teljesítmények kW-ban, a hőmérsékletek Celsius-fokban szerepelnek.
 */
typedef struct
{
    double power_grid_kw;
    double power_bat_kw;
    double power_pv_kw;
    double soc_bat_pct;
    double power_full_kw;

    double temperature_c;
    double wind_speed_kmh;

    /* A null külön állapot: a valódi 0 továbbra is érvényes mérés. */
    bool temperature_valid;
    bool wind_speed_valid;
    bool sun_radiation_valid;
    bool precipitation_valid;

    bool has_sun_radiation;
    bool has_precipitation;

    int status;
    bool night_mode;
    double battery_temperature_c;

    /* Opcionális nappályaadatok a Loxone T1 bemenetéről. */
    bool sun_position_valid;
    double sun_azimuth_deg;
    double sun_altitude_deg;
    int sunrise_minute;
    int sunset_minute;

    /* Monotonic idő a timeoutokhoz, epoch idő a statisztikához. */
    int64_t received_monotonic_us;
    int64_t received_epoch_s;
} app_energy_sample_t;

/* Az alrendszerek közös futási állapota. */
typedef struct
{
    app_data_mode_t data_recept_mode;
    bool loxone_configured;
    bool loxone_connected;
    int64_t loxone_connected_monotonic_us;
    bool http_ready;
    int64_t http_ready_monotonic_us;
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

void app_state_set_data_mode(app_data_mode_t mode);
void app_state_set_http_ready(bool ready);
void app_state_set_loxone(bool configured, bool connected);
void app_state_clear_sample(void);
static inline bool app_state_data_ready(const app_runtime_state_t *s)
{
    return s->data_recept_mode == APP_DATA_LOXONE ? s->loxone_connected
         : s->data_recept_mode == APP_DATA_HTTP   ? s->http_ready
                                                  : s->mqtt_connected;
}
static inline int64_t app_state_data_ready_since(const app_runtime_state_t *s)
{
    return s->data_recept_mode == APP_DATA_LOXONE ? s->loxone_connected_monotonic_us
         : s->data_recept_mode == APP_DATA_HTTP   ? s->http_ready_monotonic_us
                                                  : s->mqtt_connected_monotonic_us;
}
static inline bool app_state_ota_ready(const app_runtime_state_t *s)
{
    return s->sd_ok && s->config_ok && s->wifi_connected && s->time_synced &&
           (s->data_recept_mode == APP_DATA_LOXONE ? s->http_ready : app_state_data_ready(s));
}
static inline bool
app_state_data_timed_out(const app_runtime_state_t *s, int64_t now, uint32_t seconds)
{
    int64_t since =
        s->has_valid_sample ? s->last_valid_packet_monotonic_us : app_state_data_ready_since(s);
    return app_state_data_ready(s) && now - since >= (int64_t)seconds * 1000000LL;
}
