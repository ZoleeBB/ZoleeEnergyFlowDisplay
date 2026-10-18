/* A slave-ek számára rögzített bináris szerződés és adatfrissesség tesztjei. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "app_ble_protocol.h"

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t i16(const uint8_t *p)
{
    return (int16_t)u16(p);
}

int main(void)
{
    app_config_t cfg = {0};
    cfg.mqtt.data_timeout_seconds = 30;
    cfg.http.data_timeout_seconds = 10;
    cfg.display.day_brightness = 80;
    cfg.display.night_brightness = 20;
    uint8_t energy[20], weather[20];
    app_ble_protocol_encode(NULL, &cfg, 0, 42, energy, weather);
    assert(energy[0] == 1 && weather[0] == 1);
    assert(energy[1] == 2 && u16(energy + 16) == UINT16_MAX);
    assert(weather[1] == 0 && u16(weather + 18) == 42);

    app_energy_sample_t sample = {
        .power_grid_kw = -1.234,
        .power_bat_kw = 2.345,
        .power_pv_kw = 15,
        .power_full_kw = 0.042,
        .soc_bat_pct = 76.54,
        .battery_temperature_c = -20,
        .status = 3,
        .night_mode = true,
        .temperature_c = -4.25,
        .temperature_valid = true,
        .wind_speed_kmh = 150,
        .wind_speed_valid = true,
        .precipitation_valid = true,
        .has_precipitation = true,
        .sun_radiation_valid = true,
        .has_sun_radiation = false,
        .sun_position_valid = true,
        .sun_azimuth_deg = 359.9,
        .sun_altitude_deg = -35.25,
        .sunrise_minute = 432,
        .sunset_minute = 1083,
        .received_monotonic_us = 1000000,
        .received_epoch_s = 0x12345678,
    };
    app_ble_protocol_encode(&sample, &cfg, 2000000, 0x1234, energy, weather);
    assert(energy[1] == 5 && energy[2] == 3 && energy[3] == 20);
    assert(i16(energy + 4) == -1234 && i16(energy + 6) == 2345);
    assert(i16(energy + 8) == 15000 && i16(energy + 10) == 42);
    assert(u16(energy + 12) == 7654 && i16(energy + 14) == -2000);
    assert(u16(energy + 16) == 1);
    assert(energy[18] == 0x34 && energy[19] == 0x12);
    assert(u16(energy + 18) == u16(weather + 18));
    assert(weather[1] == (1 | 2 | 4 | 8 | 16 | 64));
    assert(i16(weather + 2) == -425 && u16(weather + 4) == 15000);
    assert(u16(weather + 6) == 3599 && i16(weather + 8) == -3525);
    assert(u16(weather + 10) == 432 && u16(weather + 12) == 1083);
    assert(weather[14] == 0x78 && weather[15] == 0x56 && weather[16] == 0x34 &&
           weather[17] == 0x12);

    /* Pontosan a forrás saját timeoutjánál vált lejártra, nem kerekítve. */
    app_ble_protocol_encode(&sample, &cfg, 30999999, 1, energy, weather);
    assert((energy[1] & 2) == 0);
    app_ble_protocol_encode(&sample, &cfg, 31000000, 2, energy, weather);
    assert(energy[1] & 2);
    cfg.data_recept_mode = APP_DATA_HTTP;
    app_ble_protocol_encode(&sample, &cfg, 11000000, 3, energy, weather);
    assert(energy[1] & 2);
    cfg.data_recept_mode = APP_DATA_LOXONE;
    app_ble_protocol_encode(&sample, &cfg, 45000000, 4, energy, weather);
    assert((energy[1] & 2) == 0);
    app_ble_protocol_encode(&sample, &cfg, 46000000, 5, energy, weather);
    assert(energy[1] & 2);

    /* Hiányzó időjárás nem nulla mérés, és a flag nem öröklődhet. */
    sample.temperature_valid = sample.wind_speed_valid = false;
    sample.precipitation_valid = sample.sun_radiation_valid = false;
    sample.sun_position_valid = sample.night_mode = false;
    sample.power_bat_kw = -15;
    sample.power_grid_kw = 15;
    app_ble_protocol_encode(&sample, &cfg, 0, UINT16_MAX, energy, weather);
    assert(weather[1] == 0 && energy[3] == 80);
    assert(u16(energy + 16) == 0 && i16(energy + 6) == -15000);
    assert(i16(energy + 4) == 15000 && u16(energy + 18) == UINT16_MAX);
    app_ble_protocol_encode(&sample, &cfg, INT64_C(100000000000), 0, energy, weather);
    assert(u16(energy + 16) == UINT16_MAX && u16(energy + 18) == 0);
    sample.power_grid_kw = NAN;
    sample.power_bat_kw = 1e100;
    sample.temperature_c = -1e100;
    app_ble_protocol_encode(&sample, &cfg, 0, 1, energy, weather);
    assert(i16(energy + 4) == 0 && i16(energy + 6) == INT16_MAX);
    assert(i16(weather + 2) == INT16_MIN);

    /* Valódi master -> slave roundtrip, hibás és eltérő sorszámú csomagok. */
    sample = (app_energy_sample_t){
        .power_grid_kw = -1.234,
        .power_bat_kw = -2.345,
        .power_pv_kw = 7,
        .power_full_kw = 3.421,
        .soc_bat_pct = 54.32,
        .battery_temperature_c = 31.25,
        .status = 2,
        .received_epoch_s = 1800000000,
        .received_monotonic_us = 1000000,
        .temperature_valid = true,
        .temperature_c = 0,
        .wind_speed_valid = true,
        .wind_speed_kmh = 0,
    };
    app_ble_protocol_encode(&sample, &cfg, 2000000, 77, energy, weather);
    app_energy_sample_t decoded = {0};
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 7000000, &decoded) == ESP_OK);
    assert(fabs(decoded.power_grid_kw + 1.234) < 1e-6);
    assert(fabs(decoded.power_bat_kw + 2.345) < 1e-6);
    assert(decoded.temperature_valid && decoded.temperature_c == 0);
    assert(decoded.wind_speed_valid && decoded.wind_speed_kmh == 0);
    assert(decoded.received_monotonic_us == 6000000 && decoded.received_epoch_s == 1800000000);
    app_energy_sample_t original = decoded;
    weather[18]++;
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 7000000, &decoded) ==
           ESP_ERR_INVALID_ARG);
    assert(decoded.received_monotonic_us == original.received_monotonic_us);
    weather[18]--;
    assert(app_ble_protocol_decode(energy, 19, weather, 20, 0, &decoded) == ESP_ERR_INVALID_ARG);
    energy[1] |= 2;
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 0, &decoded) == ESP_ERR_INVALID_STATE);
    energy[1] &= ~2;
    energy[12] = 0xff;
    energy[13] = 0xff;
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 0, &decoded) == ESP_ERR_INVALID_ARG);

    uint8_t clock_packet[20];
    app_ble_clock_t clock;
    app_ble_clock_encode(INT64_C(1800000000123), 120, 45, true, 77, clock_packet);
    assert(app_ble_clock_decode(clock_packet, 20, &clock) == ESP_OK);
    assert(clock.epoch_ms == INT64_C(1800000000123) && clock.utc_offset_minutes == 120);
    assert(clock.source_timeout_seconds == 45 && app_ble_packet_sequence(clock_packet) == 77);
    assert(app_ble_clock_decode(clock_packet, 19, &clock) == ESP_ERR_INVALID_ARG);
    clock_packet[0] = 2;
    assert(app_ble_clock_decode(clock_packet, 20, &clock) == ESP_ERR_INVALID_ARG);
    app_ble_clock_encode(INT64_C(1800000000123), -300, 30, true, 0, clock_packet);
    assert(app_ble_clock_decode(clock_packet, 20, &clock) == ESP_OK &&
           clock.utc_offset_minutes == -300);
    clock_packet[6] = 0xff;
    clock_packet[7] = 0xff;
    assert(app_ble_clock_decode(clock_packet, 20, &clock) == ESP_ERR_INVALID_ARG);
    app_ble_clock_encode(INT64_C(1800000000123), 900, 30, true, 0, clock_packet);
    assert(app_ble_clock_decode(clock_packet, 20, &clock) == ESP_ERR_INVALID_ARG);
    app_ble_clock_encode(INT64_C(1800000000123), 60, 30, false, 0, clock_packet);
    assert(app_ble_clock_decode(clock_packet, 20, &clock) == ESP_ERR_INVALID_ARG);

    app_runtime_state_t rt = {.data_recept_mode = APP_DATA_SLAVE,
                              .slave_connected = true,
                              .has_valid_sample = true,
                              .last_valid_packet_monotonic_us = 1000000,
                              .slave_packet_monotonic_us = 20000000,
                              .slave_source_timeout_seconds = 45,
                              .sd_ok = true,
                              .config_ok = true,
                              .time_synced = true};
    assert(app_state_data_ready(&rt) && app_state_ota_ready(&rt)); /* Wi-Fi nélkül is. */
    assert(!app_state_data_timed_out(&rt, 20000000, 15));
    assert(app_state_data_timed_out(&rt, 35000000, 15)); /* BLE heartbeat 15 s. */
    rt.slave_packet_monotonic_us = 46000000;
    assert(app_state_data_timed_out(&rt, 46000000, 15)); /* Forrás lejárat 45 s. */
    rt.slave_source_stale = true;
    assert(!app_state_ota_ready(&rt));
    assert(app_state_data_timed_out(&rt, 20000000, 15));
    rt.slave_connected = false;
    assert(!app_state_data_ready(&rt));
    puts("PASS: BLE v1 endian, signs, precision, optional weather, timeout, no data, saturation");
    return 0;
}
