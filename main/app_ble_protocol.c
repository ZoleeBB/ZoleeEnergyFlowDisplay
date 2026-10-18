/* BLE v1 csomagok: explicit little-endian kódolás, C struktúraátfedés nélkül. */
#include "app_ble_protocol.h"

#include <math.h>
#include <string.h>

static void put16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *dst, uint32_t value)
{
    put16(dst, (uint16_t)value);
    put16(dst + 2, (uint16_t)(value >> 16));
}

static uint16_t get16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t get32(const uint8_t *src)
{
    return (uint32_t)get16(src) | ((uint32_t)get16(src + 2) << 16);
}

uint16_t app_ble_packet_sequence(const uint8_t packet[APP_BLE_PACKET_SIZE])
{
    return get16(packet + 18);
}

void app_ble_clock_encode(int64_t epoch_ms,
                          int16_t offset_minutes,
                          uint16_t source_timeout,
                          bool synced,
                          uint16_t sequence,
                          uint8_t packet[APP_BLE_PACKET_SIZE])
{
    memset(packet, 0, APP_BLE_PACKET_SIZE);
    packet[0] = APP_BLE_PROTOCOL_VERSION;
    packet[1] = synced ? 1 : 0;
    put32(packet + 2, (uint32_t)(epoch_ms / 1000));
    put16(packet + 6, (uint16_t)(epoch_ms % 1000));
    put16(packet + 8, (uint16_t)offset_minutes);
    put16(packet + 10, source_timeout);
    put16(packet + 18, sequence);
}

esp_err_t app_ble_clock_decode(const uint8_t *packet, size_t length, app_ble_clock_t *clock)
{
    if (!packet || !clock || length != APP_BLE_PACKET_SIZE || packet[0] != 1 || packet[1] != 1 ||
        get16(packet + 6) > 999)
        return ESP_ERR_INVALID_ARG;
    app_ble_clock_t next = {
        .epoch_ms = (int64_t)get32(packet + 2) * 1000 + get16(packet + 6),
        .utc_offset_minutes = (int16_t)get16(packet + 8),
        .source_timeout_seconds = get16(packet + 10),
    };
    if (next.epoch_ms < INT64_C(1577836800000) || next.epoch_ms > INT64_C(4102444800000) ||
        next.utc_offset_minutes < -720 || next.utc_offset_minutes > 840 ||
        next.source_timeout_seconds < 5 || next.source_timeout_seconds > 3600)
        return ESP_ERR_INVALID_ARG;
    *clock = next;
    return ESP_OK;
}

/* A rádió felől érkező adatot ugyanúgy tartományellenőrizzük, mint a JSON-t.
 * A kimenet csak teljes, azonos sorszámú és érvényes csomagpárnál módosul. */
esp_err_t app_ble_protocol_decode(const uint8_t *energy,
                                  size_t energy_length,
                                  const uint8_t *weather,
                                  size_t weather_length,
                                  int64_t now_us,
                                  app_energy_sample_t *sample)
{
    if (!energy || !weather || !sample || energy_length != 20 || weather_length != 20 ||
        energy[0] != 1 || weather[0] != 1 ||
        app_ble_packet_sequence(energy) != app_ble_packet_sequence(weather) || (energy[1] & ~7) ||
        (weather[1] & 128))
        return ESP_ERR_INVALID_ARG;
    if (!(energy[1] & 1) || (energy[1] & 2))
        return ESP_ERR_INVALID_STATE;
    app_energy_sample_t next = {0};
    next.power_grid_kw = (int16_t)get16(energy + 4) / 1000.;
    next.power_bat_kw = (int16_t)get16(energy + 6) / 1000.;
    next.power_pv_kw = (int16_t)get16(energy + 8) / 1000.;
    next.power_full_kw = (int16_t)get16(energy + 10) / 1000.;
    next.soc_bat_pct = get16(energy + 12) / 100.;
    next.battery_temperature_c = (int16_t)get16(energy + 14) / 100.;
    next.status = energy[2];
    next.night_mode = (energy[1] & 4) != 0;
    next.temperature_valid = (weather[1] & 1) != 0;
    next.wind_speed_valid = (weather[1] & 2) != 0;
    next.precipitation_valid = (weather[1] & 4) != 0;
    next.has_precipitation = next.precipitation_valid && (weather[1] & 8);
    next.sun_radiation_valid = (weather[1] & 16) != 0;
    next.has_sun_radiation = next.sun_radiation_valid && (weather[1] & 32);
    next.sun_position_valid = (weather[1] & 64) != 0;
    next.temperature_c = next.temperature_valid ? (int16_t)get16(weather + 2) / 100. : 0;
    next.wind_speed_kmh = next.wind_speed_valid ? get16(weather + 4) / 100. : 0;
    next.sun_azimuth_deg = get16(weather + 6) / 10.;
    next.sun_altitude_deg = (int16_t)get16(weather + 8) / 100.;
    next.sunrise_minute = get16(weather + 10);
    next.sunset_minute = get16(weather + 12);
    next.received_epoch_s = get32(weather + 14);
    next.received_monotonic_us = now_us - (int64_t)get16(energy + 16) * 1000000;
    if (fabs(next.power_grid_kw) > 15 || fabs(next.power_bat_kw) > 15 || next.power_pv_kw < 0 ||
        next.power_pv_kw > 15 || next.power_full_kw < 0 || next.power_full_kw > 15 ||
        next.soc_bat_pct > 100 || next.battery_temperature_c < -20 ||
        next.battery_temperature_c > 80 || next.status < 1 || next.status > 4 || energy[3] < 1 ||
        energy[3] > 100 ||
        (next.temperature_valid && (next.temperature_c < -35 || next.temperature_c > 50)) ||
        (next.wind_speed_valid && next.wind_speed_kmh > 150) ||
        (next.sun_position_valid &&
         (next.sun_azimuth_deg > 360 || fabs(next.sun_altitude_deg) > 90 ||
          next.sunrise_minute > 1439 || next.sunset_minute > 1439)) ||
        next.received_epoch_s < 1577836800LL || next.received_epoch_s > 4102444800LL)
        return ESP_ERR_INVALID_ARG;
    *sample = next;
    return ESP_OK;
}

/* A hibás lebegőpontos érték sem okozhat túlcsorduló konverziót. */
static int16_t scaled(double value, double scale)
{
    if (!isfinite(value))
        return 0;
    double result = round(value * scale);
    if (result > INT16_MAX)
        return INT16_MAX;
    if (result < INT16_MIN)
        return INT16_MIN;
    return (int16_t)result;
}

void app_ble_protocol_encode(const app_energy_sample_t *sample,
                             const app_config_t *cfg,
                             int64_t now_us,
                             uint16_t sequence,
                             uint8_t energy[APP_BLE_PACKET_SIZE],
                             uint8_t weather[APP_BLE_PACKET_SIZE])
{
    memset(energy, 0, APP_BLE_PACKET_SIZE);
    memset(weather, 0, APP_BLE_PACKET_SIZE);
    energy[0] = weather[0] = APP_BLE_PROTOCOL_VERSION;
    put16(energy + 18, sequence);
    put16(weather + 18, sequence);
    if (!sample)
    {
        energy[1] = 2; /* Nincs minta: lejárt adatként kezelendő. */
        put16(energy + 16, UINT16_MAX);
        return;
    }

    int64_t age_us =
        now_us > sample->received_monotonic_us ? now_us - sample->received_monotonic_us : 0;
    uint64_t age_s = (uint64_t)age_us / 1000000;
    bool stale = age_us >= (int64_t)app_config_data_timeout(cfg) * 1000000;
    energy[1] = 1 | (stale ? 2 : 0) | (sample->night_mode ? 4 : 0);
    energy[2] = (uint8_t)sample->status;
    energy[3] = sample->night_mode ? cfg->display.night_brightness : cfg->display.day_brightness;
    put16(energy + 4, (uint16_t)scaled(sample->power_grid_kw, 1000));
    put16(energy + 6, (uint16_t)scaled(sample->power_bat_kw, 1000));
    put16(energy + 8, (uint16_t)scaled(sample->power_pv_kw, 1000));
    put16(energy + 10, (uint16_t)scaled(sample->power_full_kw, 1000));
    put16(energy + 12, (uint16_t)scaled(sample->soc_bat_pct, 100));
    put16(energy + 14, (uint16_t)scaled(sample->battery_temperature_c, 100));
    put16(energy + 16, age_s > UINT16_MAX ? UINT16_MAX : (uint16_t)age_s);

    weather[1] = (sample->temperature_valid ? 1 : 0) | (sample->wind_speed_valid ? 2 : 0) |
                 (sample->precipitation_valid ? 4 : 0) |
                 (sample->has_precipitation && sample->precipitation_valid ? 8 : 0) |
                 (sample->sun_radiation_valid ? 16 : 0) |
                 (sample->has_sun_radiation && sample->sun_radiation_valid ? 32 : 0) |
                 (sample->sun_position_valid ? 64 : 0);
    put16(weather + 2, (uint16_t)scaled(sample->temperature_c, 100));
    put16(weather + 4, (uint16_t)scaled(sample->wind_speed_kmh, 100));
    put16(weather + 6, (uint16_t)scaled(sample->sun_azimuth_deg, 10));
    put16(weather + 8, (uint16_t)scaled(sample->sun_altitude_deg, 100));
    put16(weather + 10, (uint16_t)sample->sunrise_minute);
    put16(weather + 12, (uint16_t)sample->sunset_minute);
    put32(weather + 14, sample->received_epoch_s > 0 ? (uint32_t)sample->received_epoch_s : 0);
}
