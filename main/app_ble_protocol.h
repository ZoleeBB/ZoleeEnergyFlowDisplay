#pragma once

#include <stddef.h>
#include <stdint.h>
#include "app_config.h"
#include "app_state.h"

/* Mindkét csomag a legkisebb, 23 bájtos ATT MTU mellett is elfér. */
#define APP_BLE_PACKET_SIZE 20
#define APP_BLE_SEED_NAME "ZoleeEnergy"
#define APP_BLE_PROTOCOL_VERSION 1

/* Csak nyilvános mérési adatokat kódol, memóriát nem foglal. */
void app_ble_protocol_encode(const app_energy_sample_t *sample,
                             const app_config_t *cfg,
                             int64_t now_us,
                             uint16_t sequence,
                             uint8_t energy[APP_BLE_PACKET_SIZE],
                             uint8_t weather[APP_BLE_PACKET_SIZE]);

typedef struct
{
    int64_t epoch_ms;
    int16_t utc_offset_minutes;
    uint16_t source_timeout_seconds;
} app_ble_clock_t;

void app_ble_clock_encode(int64_t epoch_ms,
                          int16_t offset_minutes,
                          uint16_t source_timeout,
                          bool synced,
                          uint16_t sequence,
                          uint8_t packet[APP_BLE_PACKET_SIZE]);
esp_err_t app_ble_clock_decode(const uint8_t *packet, size_t length, app_ble_clock_t *clock);
esp_err_t app_ble_protocol_decode(const uint8_t *energy,
                                  size_t energy_length,
                                  const uint8_t *weather,
                                  size_t weather_length,
                                  int64_t now_us,
                                  app_energy_sample_t *sample);
uint16_t app_ble_packet_sequence(const uint8_t packet[APP_BLE_PACKET_SIZE]);
