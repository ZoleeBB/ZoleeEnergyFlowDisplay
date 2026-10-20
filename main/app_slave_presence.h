#pragma once

#include "app_slave_display.h"
#include <stddef.h>

/* Tiszta modell: a bond ellenőrzése, a taskok közötti zár és a tartós mentés
 * a hívó feladata. Identitás kizárólag a hitelesített peer_id_addr lehet. */
#define APP_SLAVE_PRESENCE_MAX_PEERS 3
#define APP_SLAVE_PRESENCE_NAME_SIZE 81
#define APP_SLAVE_PRESENCE_UUID_SIZE 36
#define APP_SLAVE_PRESENCE_ID_SIZE 19
#define APP_SLAVE_PRESENCE_PACKET_SIZE 20
#define APP_SLAVE_PRESENCE_LEASE_US INT64_C(15000000)

typedef struct
{
    uint8_t type; /* Normalizált public=0 vagy random-static=1; nem RPA. */
    uint8_t address[6]; /* NimBLE bájtsorrend. */
} app_slave_identity_t;

typedef struct
{
    char name[APP_SLAVE_PRESENCE_NAME_SIZE];
    char presence_uuid[APP_SLAVE_PRESENCE_UUID_SIZE];
    uint32_t timeout_seconds;
    bool blank_at_night;
} app_slave_presence_profile_t;

typedef struct
{
    bool used;
    app_slave_identity_t identity;
    app_slave_presence_profile_t profile;
    uint32_t revision;
    uint32_t counter;
    bool valid;
    bool active;
} app_slave_presence_peer_t;

typedef struct
{
    app_slave_presence_peer_t peers[APP_SLAVE_PRESENCE_MAX_PEERS];
} app_slave_presence_table_t;

bool app_slave_identity_valid(const app_slave_identity_t *identity);
bool app_slave_identity_id(const app_slave_identity_t *identity,
                           char out[APP_SLAVE_PRESENCE_ID_SIZE]);
bool app_slave_presence_profile_valid(const app_slave_presence_profile_t *profile);
int app_slave_presence_find(const app_slave_presence_table_t *table,
                            const app_slave_identity_t *identity);
/* A hívó már ellenőrizte az SMP/MITM/bondot. Ismert identitás nevét nem írjuk
 * felül reconnectkor; a negyedik peer nem szoríthat ki mentett profilt. */
int app_slave_presence_register(app_slave_presence_table_t *table,
                                const app_slave_identity_t *identity,
                                const char *name);
bool app_slave_presence_update(app_slave_presence_table_t *table, unsigned index,
                                const app_slave_presence_profile_t *profile,
                                bool *changed);
/* Nincs implicit slot-törlés vagy automatikus újrapárosítás. */
bool app_slave_presence_remove(app_slave_presence_table_t *table, unsigned index);
/* Már normalizált, kisbetűs Loxone állapot-UUID; minden WS-delta után hívható.
 * A számláló minden felfutó élnél lép, a BLE-polltól függetlenül. */
bool app_slave_presence_source(app_slave_presence_table_t *table, const char *uuid,
                               bool valid, double value);
void app_slave_presence_invalidate(app_slave_presence_table_t *table);

/* Opcionális, célzott 1006 karakterisztikához. A 20 bájt a legkisebb ATT MTU-ba
 * is belefér. Csak védett olvasás/értesítés adhatja tovább; jelszó és fényerő nincs.
 * A sequence kapcsolatonként, minden új mintánál lép; a live az egyetlen
 * hitelesített Loxone adatkapcsolat frissessége, nem a UUID utolsó változásáé. */
bool app_slave_presence_encode(const app_slave_presence_peer_t *peer,
                               uint32_t sequence, bool live,
                               uint8_t out[APP_SLAVE_PRESENCE_PACKET_SIZE]);

typedef struct
{
    bool baseline;
    bool have_profile;
    bool blank_at_night;
    bool valid;
    bool active;
    bool pending_pulse;
    bool reset_presence;
    uint32_t revision;
    uint32_t sequence;
    uint32_t counter;
    uint32_t timeout_seconds;
    int64_t received_us;
    int64_t last_now_us;
} app_slave_presence_receiver_t;

/* Minden új BLE-kapcsolat és hitelesítésvesztés előtt kötelező. A legutóbbi
 * profil megmarad, de nincs aktív jel vagy régi impulzus visszajátszása. */
void app_slave_presence_disconnected(app_slave_presence_receiver_t *receiver);
/* Csak igazoltan régi, opcionális karakterisztika nélküli Masterhez. */
void app_slave_presence_legacy(app_slave_presence_receiver_t *receiver);
/* Hibánál a teljes receiver változatlan. Az authenticated kizárólag a tényleges
 * SMP encrypted+authenticated+bonded+16-byte ellenőrzés eredménye lehet. */
bool app_slave_presence_receive(app_slave_presence_receiver_t *receiver,
                                const uint8_t *data, size_t size,
                                bool authenticated, int64_t now_us);
/* Az inputkapuval azonos LVGL-zár alatt, periodikusan. Egy impulzust egyszer
 * fogyaszt; lejárt jel és kapcsolatvesztés nem hosszabbítja az ébrenlétet.
 * A day/night fényerő és a napszak a Slave saját meglévő állapotában marad. */
void app_slave_presence_apply(app_slave_presence_receiver_t *receiver,
                              app_slave_display_t *display, int64_t now_us);
