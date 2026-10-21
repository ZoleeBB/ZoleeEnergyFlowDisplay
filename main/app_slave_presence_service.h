#pragma once

#include "app_slave_presence.h"
#include "esp_err.h"

/* A Master konfigurációja külön NVS-névtérben él. A hálózati és UI-taskok csak
 * korlátos pillanatképet kapnak; NVS-t és NimBLE-t nem hívnak egymás helyett. */
typedef struct
{
    app_slave_presence_table_t table;
    uint32_t revision;
    uint32_t saved_revision;
    esp_err_t save_error;
} app_slave_presence_snapshot_t;

esp_err_t app_slave_presence_service_init(bool master);
bool app_slave_presence_service_register(const app_slave_identity_t *identity);
bool app_slave_presence_service_snapshot(app_slave_presence_snapshot_t *out);
/* Az elavult webes szerkesztés nem írhat felül újabb profilt. */
bool app_slave_presence_service_update(const app_slave_identity_t *identity,
                                       const app_slave_presence_profile_t *profile,
                                       uint32_t expected_revision);
bool app_slave_presence_service_packet(const app_slave_identity_t *identity,
                                       uint32_t sequence,
                                       uint8_t out[APP_SLAVE_PRESENCE_PACKET_SIZE]);
void app_slave_presence_service_source(const char *uuid, bool valid, double value);
void app_slave_presence_service_online(bool online);
uint32_t app_slave_presence_service_source_revision(void);
bool app_slave_presence_service_receive(const uint8_t *data, size_t size, bool authenticated);
void app_slave_presence_service_disconnected(void);
void app_slave_presence_service_legacy(void);
/* A hívó a kijelző zárját tartja; foglalt receiver esetén a következő tick próbálja. */
void app_slave_presence_service_apply(app_slave_display_t *display, int64_t now_us);
