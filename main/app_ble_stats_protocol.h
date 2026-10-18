#pragma once
#include "app_stats.h"

/* Kizárólag a beépített két diagram és napi összehasonlítás adatai. */
#define APP_BLE_STATS_SIZE 6664
#define APP_BLE_STATS_PAGE_DATA 480
#define APP_BLE_STATS_PAGE_MAX 492
#define APP_BLE_STATS_PAGES                                                                        \
    ((APP_BLE_STATS_SIZE + APP_BLE_STATS_PAGE_DATA - 1) / APP_BLE_STATS_PAGE_DATA)

uint32_t app_ble_stats_crc(const uint8_t *data, size_t length);
bool app_ble_stats_encode(const app_stats_snapshot_t *snapshot,
                          uint32_t id,
                          uint32_t epoch,
                          uint8_t out[APP_BLE_STATS_SIZE]);
bool app_ble_stats_decode(const uint8_t *wire,
                          size_t length,
                          uint32_t id,
                          uint32_t now,
                          const char *date,
                          app_stats_snapshot_t *out);
void app_ble_stats_request(uint8_t out[8], bool start, uint16_t page, uint32_t id);
bool app_ble_stats_request_decode(
    const uint8_t *data, size_t length, bool *start, uint16_t *page, uint32_t *id);
size_t app_ble_stats_page(const uint8_t wire[APP_BLE_STATS_SIZE],
                          uint32_t id,
                          uint16_t page,
                          uint8_t out[APP_BLE_STATS_PAGE_MAX]);
bool app_ble_stats_page_decode(const uint8_t *data,
                               size_t length,
                               uint32_t id,
                               uint16_t page,
                               uint8_t wire[APP_BLE_STATS_SIZE]);
