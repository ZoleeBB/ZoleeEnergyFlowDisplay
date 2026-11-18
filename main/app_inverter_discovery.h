#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_DISCOVERY_MAX_RESULTS 8

typedef enum {
    APP_DISCOVERY_RUNNING,
    APP_DISCOVERY_DONE,
    APP_DISCOVERY_CANCELLED,
    APP_DISCOVERY_FAILED
} app_inverter_discovery_state_t;

typedef struct {
    char host[16], model[64], serial[64], device[96];
    uint16_t port;
    uint8_t unit;
    bool verified;
} app_inverter_discovery_result_t;

typedef struct {
    uint32_t job;
    char profile[40];
    app_inverter_discovery_state_t state;
    uint16_t checked, total;
    bool limited;
    uint8_t count;
    app_inverter_discovery_result_t results[APP_DISCOVERY_MAX_RESULTS];
} app_inverter_discovery_status_t;

/* Egyidejűleg egy keresés indulhat a közvetlen inverteres Masteren. A job
 * azonosító megakadályozza, hogy egy korábbi böngészőlap leállítsa az újat. */
bool app_inverter_discovery_supported(const char *profile);
esp_err_t app_inverter_discovery_start(const char *profile, uint16_t port, uint8_t unit,
                                       const char *device, uint32_t *job);
esp_err_t app_inverter_discovery_snapshot(uint32_t job, app_inverter_discovery_status_t *out);
esp_err_t app_inverter_discovery_cancel(uint32_t job);
void app_inverter_discovery_expire(void);

/* A gyártói azonosítás átveszi és minden esetben bezárja a már kapcsolódott,
 * nem blokkoló socketet. A timeout a teljes protokollvizsgálatra vonatkozik. */
bool app_inverter_discovery_modbus_supported(const char *profile);
bool app_inverter_discovery_modbus(const char *profile, const char *device, uint8_t unit, int fd,
                                  uint32_t timeout_ms, app_inverter_discovery_result_t *out);
bool app_inverter_discovery_http_supported(const char *profile);
size_t app_inverter_discovery_http(const char *profile, int fd, uint32_t timeout_ms,
                                   app_inverter_discovery_result_t *out, size_t capacity);
/* A helyi STA IPv4-címe hálózati bájtsorrendben érkezik. */
size_t app_inverter_discovery_native(const char *profile, uint32_t local,
                                     app_inverter_discovery_result_t *out, size_t capacity);
