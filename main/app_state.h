#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

/* Közvetlen profiloknál az egyes csatornákat eddig legalább 10 W felbontással továbbítjuk. */
#define APP_DIRECT_POWER_MAX_KW 327.67

static inline bool app_battery_temperature_in_range(double celsius)
{
    return celsius >= -20 && celsius <= 80;
}

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

    /* Opcionális Loxone páratartalom; a valódi 0% érvényes mérés. */
    bool relative_humidity_valid;
    uint8_t relative_humidity_pct;

    bool has_sun_radiation;
    bool has_precipitation;

    int status;
    bool night_mode;
    bool energy_flow_disabled; /* BLE vezérlőállapot, nem energiaminta. */
    double battery_temperature_c;
    bool battery_temperature_valid;
    /* A régi, teljes energiaminták nulla inicializálással is érvényesek. */
    bool battery_unavailable;       /* Nincs telepítve akkumulátor. */
    bool grid_unavailable;
    bool home_unavailable;
    bool soc_unavailable;
    bool battery_power_unavailable;

    /* Csak közvetlen Loxone módban: az „Elektromos autótöltő” ikonos
     * fogyasztók összege. Hiányzó/elavult részadatnál érvénytelen. */
    bool ev_charging_valid;
    double ev_charging_kw;

    /* Opcionális nappályaadatok a Loxone T1 bemenetéről. */
    bool sun_position_valid;
    double sun_azimuth_deg;
    double sun_altitude_deg;
    int sunrise_minute;
    int sunset_minute;

    /* Monotonic idő a timeoutokhoz, epoch idő a statisztikához. */
    uint16_t integration_period_s; /* Felhőmérés forrásoldali periódusa. Helyi mintánál 0. */
    int64_t received_monotonic_us;
    int64_t received_epoch_s;
} app_energy_sample_t;

/* Az alrendszerek közös futási állapota. */
typedef struct
{
    app_data_mode_t data_recept_mode;
    bool energy_flow_disabled, standalone_night;
    bool loxone_configured;
    bool loxone_connected;
    int64_t loxone_connected_monotonic_us;
    bool inverter_ready;
    uint16_t inverter_timeout_seconds;
    int64_t inverter_ready_monotonic_us;
    bool http_ready;
    int64_t http_ready_monotonic_us;
    bool sd_ok;
    bool cardless_slave, remote_stats_valid;
    bool config_ok;
    bool wifi_connected;
    bool time_synced;
    bool operational; /* Az adott indításban már kirajzolt üzemkész nézet. */
    bool mqtt_connected;
    bool has_valid_sample;
    bool slave_connected;
    bool slave_source_stale;
    uint16_t slave_source_timeout_seconds;
    int64_t slave_connected_monotonic_us;
    int64_t slave_packet_monotonic_us;

    /* A BLE host ugyanazon állapotmutexen keresztül ad diagnosztikát. */
    bool ble_enabled, ble_master, ble_ready, ble_advertising, ble_scanning;
    unsigned ble_slaves;
    char ble_address[18], ble_peer[18];
    int ble_rssi;

    int64_t mqtt_connected_monotonic_us;
    int64_t last_valid_packet_monotonic_us;
    int64_t last_valid_packet_epoch_s;
} app_runtime_state_t;

/* Közös állapot inicializálása. */
void app_state_init(void);

/* Alrendszer-állapotok frissítése. */
void app_state_set_sd_ok(bool value);
void app_state_set_cardless(bool value);
void app_state_set_remote_stats(bool valid);
void app_state_set_config_ok(bool value);
void app_state_set_wifi_connected(bool value);
void app_state_set_time_synced(bool value);
void app_state_set_operational(bool value);
void app_state_set_mqtt_connected(bool value);

/* Utolsó érvényes MQTT minta tárolása. */
void app_state_set_sample(const app_energy_sample_t *sample);

/* Szálbiztos állapotlekérdezések. */
void app_state_get_runtime(app_runtime_state_t *out);
bool app_state_get_sample(app_energy_sample_t *out);

void app_state_set_data_mode(app_data_mode_t mode);
void app_state_set_http_ready(bool ready);
void app_state_set_inverter_ready(bool ready);
void app_state_set_inverter_timeout(uint16_t seconds);
void app_state_set_loxone(bool configured, bool connected);
void app_state_clear_sample(void);
void app_state_set_energy_flow_disabled(bool disabled, bool night);
void app_state_set_slave(bool connected, bool stale, uint16_t source_timeout, bool packet);
void app_state_set_ble(bool enabled, bool master, bool ready, bool advertising,
                       bool scanning, unsigned slaves, const char *address,
                       const char *peer, int rssi);
static inline bool app_state_data_ready(const app_runtime_state_t *s)
{
    if (s->data_recept_mode == APP_DATA_SLAVE)
        return s->slave_connected;
    if (s->data_recept_mode == APP_DATA_INVERTER) return s->inverter_ready;
    return s->data_recept_mode == APP_DATA_LOXONE ? s->loxone_connected
         : s->data_recept_mode == APP_DATA_HTTP   ? s->http_ready
                                                  : s->mqtt_connected;
}
static inline int64_t app_state_data_ready_since(const app_runtime_state_t *s)
{
    if (s->data_recept_mode == APP_DATA_SLAVE)
        return s->slave_connected_monotonic_us;
    if (s->data_recept_mode == APP_DATA_INVERTER) return s->inverter_ready_monotonic_us;
    return s->data_recept_mode == APP_DATA_LOXONE ? s->loxone_connected_monotonic_us
         : s->data_recept_mode == APP_DATA_HTTP   ? s->http_ready_monotonic_us
                                                  : s->mqtt_connected_monotonic_us;
}
static inline bool app_state_ota_ready(const app_runtime_state_t *s)
{
    return s->time_synced && s->operational;
}

/* A felhőmérés két lekérdezési ciklus és egy perc ráhagyás után avul el. */
static inline unsigned app_cloud_sample_timeout(unsigned cadence_s)
{
    return 2 * cadence_s + 60;
}

static inline uint32_t app_state_source_timeout(const app_runtime_state_t *s, uint32_t fallback)
{
    if (s->data_recept_mode == APP_DATA_INVERTER && s->inverter_timeout_seconds)
        return s->inverter_timeout_seconds;
    return fallback;
}

static inline bool
app_state_data_timed_out(const app_runtime_state_t *s, int64_t now, uint32_t seconds)
{
    if (s->data_recept_mode == APP_DATA_SLAVE) {
        if (!s->slave_connected || s->slave_source_stale)
            return true;
        if (now - s->slave_packet_monotonic_us >= 15000000LL)
            return true;
        if (s->energy_flow_disabled) return false; /* A friss módjelzés nem vár energiamintát. */
        seconds = s->slave_source_timeout_seconds ? s->slave_source_timeout_seconds : 45;
    }
    seconds = app_state_source_timeout(s, seconds);
    int64_t since =
        s->has_valid_sample ? s->last_valid_packet_monotonic_us : app_state_data_ready_since(s);
    return app_state_data_ready(s) && now - since >= (int64_t)seconds * 1000000LL;
}
