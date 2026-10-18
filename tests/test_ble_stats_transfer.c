/* A valódi master GATT-kiszolgáló és slave kliens közti aszinkron átvitel. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_ble_stats_protocol.h"
#include "app_events.h"
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 3
#define BLE_HS_EDONE 14
#define BLE_GATT_ACCESS_OP_READ_CHR 1
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_ATT_ERR_INSUFFICIENT_AUTHEN 5
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 3
struct os_mbuf
{
    size_t len;
    uint8_t data[512];
};
#define OS_MBUF_PKTLEN(om) ((om)->len)
struct ble_gatt_access_ctxt
{
    unsigned op;
    struct os_mbuf *om;
};
struct ble_gatt_error
{
    int status;
};
struct ble_gatt_attr
{
    struct os_mbuf *om;
    uint16_t offset;
};
typedef int (*callback_t)(uint16_t, const struct ble_gatt_error *, struct ble_gatt_attr *, void *);
typedef struct
{
    bool used;
    uint16_t handle;
    uint8_t *stats_wire;
    uint32_t stats_id;
    uint16_t stats_page;
    int64_t stats_started_us;
} seed_peer_t;
static seed_peer_t s_peers[3] = {
    {.used = true, .handle = 7}, {.used = true, .handle = 8}, {.used = true, .handle = 9}};
static app_config_t s_config;
static uint16_t s_master = 7, s_remote_stats = 112;
static char s_master_address[18] = "01:02:03:04:05:06";
static bool s_slave_ready = true, s_profile_reading, s_profile_waiting, authenticated = true;
static int64_t now_us = 10000000;
static app_stats_snapshot_t master_snapshot, published;
static unsigned publishes, disconnections;
static callback_t pending;
static void *pending_arg;
static bool pending_read;
static int pending_error;
static size_t fragment_size = 20;
static int64_t esp_timer_get_time(void)
{
    return now_us;
}
static bool security_authenticated(uint16_t connection)
{
    (void)connection;
    return authenticated;
}
bool app_stats_is_ready(void)
{
    return true;
}
void app_stats_get_snapshot(app_stats_snapshot_t *out)
{
    *out = master_snapshot;
}
bool app_stats_remote_publish(const app_stats_snapshot_t *out)
{
    published = *out;
    publishes++;
    return true;
}
void app_stats_remote_source(const char *id)
{
    assert(!strcmp(id, s_master_address));
}
void app_stats_remote_disconnected(void)
{
    disconnections++;
}
void app_events_add(app_event_level_t level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}
static int os_mbuf_copydata(const struct os_mbuf *om, unsigned start, unsigned length, void *out)
{
    if (start + length > om->len)
        return -1;
    memcpy(out, om->data + start, length);
    return 0;
}
static int os_mbuf_append(struct os_mbuf *om, const void *data, size_t length)
{
    assert(om->len + length <= sizeof(om->data));
    memcpy(om->data + om->len, data, length);
    om->len += length;
    return 0;
}
#include "../main/app_ble_stats_server.inc"
static int ble_gattc_write_flat(
    uint16_t conn, uint16_t handle, const void *data, unsigned length, callback_t cb, void *arg)
{
    assert(!pending && handle == 112);
    struct os_mbuf om = {.len = length};
    memcpy(om.data, data, length);
    struct ble_gatt_access_ctxt ctx = {BLE_GATT_ACCESS_OP_WRITE_CHR, &om};
    pending_error = access_stats(conn, handle, &ctx, NULL);
    pending = cb;
    pending_arg = arg;
    pending_read = false;
    return 0;
}
static int
ble_gattc_read_long(uint16_t conn, uint16_t handle, uint16_t offset, callback_t cb, void *arg)
{
    assert(!pending && conn == 7 && handle == 112 && offset == 0);
    pending = cb;
    pending_arg = arg;
    pending_read = true;
    return 0;
}
#include "../main/app_ble_stats_client.inc"
static void pump(void)
{
    assert(pending);
    callback_t cb = pending;
    void *arg = pending_arg;
    bool read = pending_read;
    pending = NULL;
    struct ble_gatt_error error = {0};
    if (!read)
    {
        error.status = pending_error;
        cb(7, &error, NULL, arg);
        return;
    }
    struct os_mbuf response = {0};
    struct ble_gatt_access_ctxt ctx = {BLE_GATT_ACCESS_OP_READ_CHR, &response};
    error.status = access_stats(7, 112, &ctx, NULL);
    if (error.status)
    {
        cb(7, &error, NULL, arg);
        return;
    }
    for (size_t offset = 0; offset < response.len;)
    {
        size_t n = response.len - offset;
        if (n > fragment_size)
            n = fragment_size;
        struct os_mbuf fragment = {.len = n};
        memcpy(fragment.data, response.data + offset, n);
        struct ble_gatt_attr attr = {.om = &fragment, .offset = (uint16_t)offset};
        cb(7, &error, &attr, arg);
        offset += n;
    }
    error.status = BLE_HS_EDONE;
    cb(7, &error, NULL, arg);
}
static void finish(void)
{
    unsigned count = 0;
    while (pending)
    {
        pump();
        assert(++count <= 2 * APP_BLE_STATS_PAGES);
    }
    assert(!slave_stats_busy());
}
static int request(uint16_t conn, bool start, uint16_t page, uint32_t id)
{
    struct os_mbuf om = {.len = 8};
    app_ble_stats_request(om.data, start, page, id);
    struct ble_gatt_access_ctxt ctx = {BLE_GATT_ACCESS_OP_WRITE_CHR, &om};
    return access_stats(conn, 112, &ctx, NULL);
}
int main(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(master_snapshot.date, sizeof(master_snapshot.date), "%Y-%m-%d", &local);
    master_snapshot.reference_valid = true;
    master_snapshot.reference_energy.tree_absorption_kg_per_year = 20;
    strcpy(master_snapshot.reference_currency, "Ft");
    master_snapshot.totals.production_kwh = 42;
    master_snapshot.slots[287].valid = true;
    master_snapshot.slots[287].split.pv_home = 3;
    s_config.cardless_slave = true;
    slave_stats_poll(now_us);
    assert(pending && slave_stats_busy() && !publishes);
    pump();
    assert(pending && pending_read && !publishes);
    finish();
    assert(publishes == 1 && published.totals.production_kwh == 42 && published.slots[287].valid);
    /* A hiányos átvitel közben soha nem cserélődik ki az előző pillanatkép. */
    now_us += 31000000;
    fragment_size = 246;
    slave_stats_poll(now_us);
    pump();
    assert(s_peers[0].stats_wire);
    s_peers[0].stats_wire[1500] ^= 1;
    finish();
    assert(publishes == 1);
    now_us += 6000000;
    slave_stats_poll(now_us);
    finish();
    assert(publishes == 2);
    /* Profilolvasás és bulk GATT nem futhat egymásra. */
    now_us += 31000000;
    s_profile_reading = true;
    slave_stats_poll(now_us);
    assert(!pending);
    s_profile_reading = false;
    s_profile_waiting = true;
    slave_stats_poll(now_us);
    assert(!pending);
    s_profile_waiting = false;
    slave_stats_poll(now_us);
    assert(pending);
    now_us += 31000000;
    slave_stats_poll(now_us);
    assert(!s_stats_active && s_stats_inflight);
    finish();
    assert(publishes == 2);
    /* Kapcsolatvesztés után a későn beérkező régi callback sem publikálhat. */
    now_us += 6000000;
    slave_stats_poll(now_us);
    assert(pending);
    slave_stats_reset();
    pump();
    assert(!pending && !slave_stats_busy() && publishes == 2 && disconnections == 1);
    free(s_peers[0].stats_wire);
    s_peers[0].stats_wire = NULL;
    slave_stats_poll(now_us);
    finish();
    assert(publishes == 3);
    authenticated = false;
    now_us += 31000000;
    slave_stats_poll(now_us);
    assert(!pending);
    assert(request(8, true, 0, 100) == BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
    authenticated = true;
    assert(request(8, true, 0, 100) == 0 && request(9, true, 0, 200) == 0);
    assert(s_peers[1].stats_id == 100 && s_peers[2].stats_id == 200);
    assert(request(8, false, 2, 200) != 0 && s_peers[1].stats_page == 0);
    assert(request(8, false, 2, 100) == 0 && s_peers[1].stats_page == 2 &&
           s_peers[2].stats_page == 0);
    assert(request(8, true, 0, 101) == BLE_ATT_ERR_INSUFFICIENT_RES);
    now_us += 46000000;
    assert(request(8, false, 3, 100) != 0);
    for (unsigned i = 0; i < 3; i++)
        free(s_peers[i].stats_wire);
    slave_stats_reset();
    puts("PASS: actual encrypted BLE statistics server/client, 20/246-byte fragments, atomic "
         "cache, CRC, timeout, late callbacks, profile exclusion and independent peers");
}
