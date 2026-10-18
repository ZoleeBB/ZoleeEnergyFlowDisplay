/* A valódi SMP-eseménykezelés; a rádió helyén szigorú, állapotfigyelő adapter. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_ble_seed.h"
#include "app_ble_profile.h"
#include "app_events.h"
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_GAP_EVENT_REPEAT_PAIRING 1
#define BLE_GAP_EVENT_PASSKEY_ACTION 2
#define BLE_GAP_EVENT_ENC_CHANGE 3
#define BLE_GAP_EVENT_DISCONNECT 4
#define BLE_GAP_REPEAT_PAIRING_IGNORE 2
#define BLE_GAP_REPEAT_PAIRING_RETRY 1
#define BLE_SM_IOACT_DISP 3
#define BLE_SM_IOACT_INPUT 2
struct ble_gap_conn_desc
{
    struct
    {
        unsigned encrypted, authenticated, bonded, key_size;
    } sec_state;
    int peer_id_addr;
};
struct ble_gap_event
{
    int type;
    struct
    {
        uint16_t conn_handle;
    } repeat_pairing;
    struct
    {
        uint16_t conn_handle;
        struct
        {
            unsigned action;
        } params;
    } passkey;
    struct
    {
        uint16_t conn_handle;
        int status;
    } enc_change;
    struct
    {
        struct
        {
            uint16_t conn_handle;
        } conn;
    } disconnect;
};
struct ble_sm_io
{
    unsigned action, passkey;
};
static app_config_t s_config;
static bool is_slave;
static int64_t now = 5000000;
static struct ble_gap_conn_desc security;
static unsigned terminated, injected, deleted;
static struct ble_sm_io last_io;
static bool slave_mode(void)
{
    return is_slave;
}
static int64_t esp_timer_get_time(void)
{
    return now;
}
static int ble_gap_conn_find(uint16_t c, struct ble_gap_conn_desc *d)
{
    assert(c == 7);
    *d = security;
    return 0;
}
static int ble_gap_terminate(uint16_t c, int reason)
{
    assert(c == 7 && reason == 19);
    terminated++;
    return 0;
}
static int ble_store_util_delete_peer(const int *address)
{
    (void)address;
    deleted++;
    return 0;
}
static int ble_sm_inject_io(uint16_t c, struct ble_sm_io *io)
{
    assert(c == 7);
    last_io = *io;
    injected++;
    return 0;
}
void app_events_add(app_event_level_t level, const char *format, ...)
{
    (void)level;
    (void)format;
}
#include "../main/app_ble_security.inc"

int main(void)
{
    strcpy(s_config.data_seed_pin, "012345");
    struct ble_gap_event event = {
        .type = BLE_GAP_EVENT_PASSKEY_ACTION,
        .passkey = {.conn_handle = 7, .params = {.action = BLE_SM_IOACT_DISP}}};
    security_gap_event(&event);
    assert(terminated == 1 && !injected); /* Zárt ablak. */
    app_ble_pairing_open();
    security_poll(now);
    app_ble_pairing_status_t status;
    app_ble_pairing_status(&status);
    assert(status.seconds == 120);
    security_gap_event(&event);
    assert(injected == 1 && last_io.passkey == 12345 && last_io.action == BLE_SM_IOACT_DISP);
    now += 121000000;
    security_gap_event(&event);
    assert(terminated == 2 && injected == 1);
    app_ble_pairing_open();
    security_poll(now);
    event.type = BLE_GAP_EVENT_REPEAT_PAIRING;
    event.repeat_pairing.conn_handle = 7;
    assert(security_gap_event(&event) == BLE_GAP_REPEAT_PAIRING_RETRY && deleted == 1);
    is_slave = true;
    assert(security_gap_event(&event) == BLE_GAP_REPEAT_PAIRING_IGNORE && deleted == 1);
    event.type = BLE_GAP_EVENT_PASSKEY_ACTION;
    event.passkey.params.action = BLE_SM_IOACT_INPUT;
    security_gap_event(&event);
    app_ble_pairing_status(&status);
    assert(status.pin_required && status.seconds == 28);
    assert(!app_ble_pairing_submit(status.token, "12345"));
    assert(!app_ble_pairing_submit(status.token, "12a456"));
    assert(!app_ble_pairing_submit(status.token - 1, "012345"));
    assert(app_ble_pairing_submit(status.token, "012345"));
    assert(injected == 1); /* UI nem hív SMP-t. */
    security_poll(now);
    assert(injected == 2 && last_io.passkey == 12345 && last_io.action == BLE_SM_IOACT_INPUT);
    app_ble_pairing_status(&status);
    assert(!status.pin_required);
    security_gap_event(&event);
    app_ble_pairing_status(&status);
    assert(app_ble_pairing_submit(status.token, NULL));
    security_poll(now);
    assert(terminated == 3);
    security_gap_event(&event);
    now += 29000000;
    security_poll(now);
    assert(terminated == 4);
    security.sec_state.encrypted = security.sec_state.authenticated = security.sec_state.bonded = 1;
    security.sec_state.key_size = 16;
    assert(security_authenticated(7));
    security.sec_state.authenticated = 0;
    assert(!security_authenticated(7));
    security.sec_state.authenticated = 1;
    security.sec_state.key_size = 12;
    assert(!security_authenticated(7));
    event.type = BLE_GAP_EVENT_ENC_CHANGE;
    event.enc_change.conn_handle = 7;
    event.enc_change.status = 0;
    security_gap_event(&event);
    assert(terminated == 5); /* Just Works / gyenge kulcs nem elég. */
    security.sec_state.key_size = 16;
    security_gap_event(&event);
    assert(terminated == 5);
    is_slave = false;
    app_ble_pairing_open();
    security_poll(now);
    event.enc_change.status = 1;
    for (unsigned i = 0; i < 5; i++)
    {
        event.type = BLE_GAP_EVENT_PASSKEY_ACTION;
        event.passkey.params.action = BLE_SM_IOACT_DISP;
        security_gap_event(&event);
        event.type = BLE_GAP_EVENT_ENC_CHANGE;
        security_gap_event(&event);
    }
    app_ble_pairing_status(&status);
    assert(!status.seconds); /* Öt hibás próbálkozás lezár. */
    unsigned before_deleted = deleted;
    security.sec_state.authenticated = 0;
    event.enc_change.status = 0;
    security_gap_event(&event);
    assert(deleted == before_deleted + 1);
    app_config_t master = {0}, slave = {0};
    strcpy(master.wifi.ssid, "My WiFi");
    strcpy(master.wifi.password, "secret password");
    master.ota.auto_update = true;
    uint8_t data[APP_BLE_PROFILE_SIZE];
    app_ble_profile_encode(&master, data);
    slave.wifi.network.use_static_ip = true;
    strcpy(slave.wifi.network.ip, "192.168.2.1");
    assert(app_ble_profile_decode(data, sizeof(data), &slave));
    assert(!strcmp(slave.wifi.ssid, master.wifi.ssid) &&
           !strcmp(slave.wifi.password, master.wifi.password));
    assert(slave.ota.auto_update && !slave.wifi.network.use_static_ip && !slave.wifi.network.ip[0]);
    master.ota.auto_update = false;
    app_ble_profile_encode(&master, data);
    assert(app_ble_profile_decode(data, sizeof(data), &slave) && !slave.ota.auto_update);
    data[4] = 33;
    assert(!app_ble_profile_decode(data, sizeof(data), &slave));
    data[4] = 7;
    data[3] = 2;
    assert(!app_ble_profile_decode(data, sizeof(data), &slave));
    puts("PASS: actual SMP PIN/window/timeout/MITM/bond/key-strength/throttle, asynchronous UI "
         "commands, Wi-Fi profile and master auto-update inheritance");
}
