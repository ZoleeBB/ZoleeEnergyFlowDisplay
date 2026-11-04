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
/* A Slave diagnosztikai képernyőjének jelenlét- és profilállapota. */
typedef struct
{
    bool have_profile;      /* Érkezett profil a Mastertől. */
    bool source;            /* Volt már érvényes jelenlétminta (van forrás). */
    bool fresh;             /* A legutóbbi minta a 15 másodperces lease-en belül. */
    bool active;
    bool blank_at_night;
    uint32_t age_seconds;   /* A legutóbbi minta kora. */
    uint32_t timeout_seconds;
} app_slave_presence_state_t;

bool app_slave_presence_service_state(app_slave_presence_state_t *out);

/* A Master webes „frissítések keresése" gombja: a párosított Slave-ek is
 * ellenőrizzék a saját kiadásukat. Csak Masteren hat, rövid ablakra. */
void app_slave_presence_service_request_ota(void);
/* A Slave oldalán egyszer fogyasztható kérés a Mastertől. */
bool app_slave_presence_service_take_ota_request(void);
/* A Slave oldalán a Mastertől kapott, hitelesített profil tájolása (0/90/180/270).
 * false, ha még nem érkezett profil, vagy a receiver épp foglalt. */
bool app_slave_presence_service_rotation(uint16_t *degrees);
/* A hívó a kijelző zárját tartja; foglalt receiver esetén a következő tick próbálja. */
void app_slave_presence_service_apply(app_slave_display_t *display, int64_t now_us);
