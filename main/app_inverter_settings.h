#pragma once
#include "app_inverter_driver.h"
#include "app_inverter_schedule.h"
#include "app_inverter_solar.h"
#include "app_slave_presence.h"

typedef enum { INV_MODBUS, INV_HTTP, INV_MQTT } app_inverter_transport_t;

typedef struct {
    const char *id;
    app_inverter_transport_t transport;
    uint16_t seconds;
} app_inverter_profile_t;

typedef struct {
    unsigned version;
    app_inverter_config_t driver;
    double latitude, longitude;
    app_inverter_schedule_t master;
    struct {
        char id[APP_SLAVE_PRESENCE_ID_SIZE];
        app_inverter_schedule_t schedule;
    } peers[APP_SLAVE_PRESENCE_MAX_PEERS];
    char pem[3072];
} app_inverter_settings_t;

const app_inverter_profile_t *app_inverter_profile(const char *id);
/* Csak a Master inverteres feldolgozója nyitja meg az NVS-t. */
bool app_inverter_settings_open(void);
bool app_inverter_settings_local_name(const char *host, size_t length);
bool app_inverter_settings_snapshot(app_inverter_settings_t *out, uint32_t *generation, uint32_t *source);
bool app_inverter_settings_coords(double *lat, double *lon, uint32_t *generation);
bool app_inverter_settings_current(uint32_t generation);
/* A már zárolt, biztosan aktuális profil mellett publikálunk egy mintát. */
bool app_inverter_settings_lock_version(uint32_t generation);
void app_inverter_settings_unlock(void);
void app_inverter_report_ipv4_locked(const char *address);
void app_inverter_report(uint32_t generation, const char *status, int64_t measured);
