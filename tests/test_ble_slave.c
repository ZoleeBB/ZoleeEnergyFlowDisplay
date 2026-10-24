/* A tényleges central állapotgép futtatása aszinkron GATT/GAP tesztillesztővel. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "app_ble_stats_protocol.h"
#include "app_ble_protocol.h"
#include "app_events.h"
#include "app_ble_profile.h"
#include "app_slave_presence_service.h"

#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_HS_EDONE 14
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_GATT_CHR_PROP_READ 2
#define BLE_GATT_CHR_PROP_WRITE 8
#define BLE_GATT_CHR_PROP_NOTIFY 16
#define BLE_GAP_EVENT_DISC 1
#define BLE_GAP_EVENT_CONNECT 2
#define BLE_GAP_EVENT_DISCONNECT 3
#define BLE_GAP_EVENT_DISC_COMPLETE 4
#define BLE_GAP_EVENT_NOTIFY_RX 5
#define BLE_GAP_EVENT_ENC_CHANGE 6
#define BLE_GAP_EVENT_REPEAT_PAIRING 7
typedef struct
{
    unsigned value;
} ble_uuid_t;
typedef struct
{
    ble_uuid_t u;
} ble_uuid128_t;
typedef struct
{
    uint8_t type, val[6];
} ble_addr_t;
struct os_mbuf
{
    uint16_t len;
    uint8_t data[20];
};
#define OS_MBUF_PKTLEN(om) ((om)->len)
struct ble_gatt_error
{
    int status;
};
struct ble_gatt_attr
{
    struct os_mbuf *om;
    uint16_t offset;
};
struct ble_gatt_svc
{
    uint16_t start_handle, end_handle;
};
struct ble_gatt_chr
{
    ble_uuid128_t uuid;
    uint16_t def_handle, val_handle;
    uint8_t properties;
};
struct ble_gatt_dsc
{
    ble_uuid128_t uuid;
    uint16_t handle;
};
struct ble_hs_adv_fields
{
    unsigned num_uuids128;
    ble_uuid128_t *uuids128;
};
struct ble_gap_disc_params
{
    unsigned itvl, window, filter_duplicates;
};
struct ble_gap_event
{
    int type;
    struct
    {
        uint8_t *data, length_data;
        int8_t rssi;
        ble_addr_t addr;
    } disc;
    struct
    {
        int status;
        uint16_t conn_handle;
    } connect;
    struct
    {
        struct
        {
            uint16_t conn_handle;
        } conn;
    } disconnect;
    struct
    {
        uint16_t conn_handle, attr_handle;
        struct os_mbuf *om;
    } notify_rx;
    struct
    {
        uint16_t conn_handle;
        int status;
    } enc_change;
};
typedef int (*gap_cb)(struct ble_gap_event *, void *);
typedef int (*service_cb)(uint16_t,
                          const struct ble_gatt_error *,
                          const struct ble_gatt_svc *,
                          void *);
typedef int (*chr_cb)(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_chr *, void *);
typedef int (*desc_cb)(
    uint16_t, const struct ble_gatt_error *, uint16_t, const struct ble_gatt_dsc *, void *);
typedef int (*attr_cb)(uint16_t, const struct ble_gatt_error *, struct ble_gatt_attr *, void *);

static const ble_uuid128_t s_service_uuid = {{1000}}, s_energy_uuid = {{1001}},
                           s_weather_uuid = {{1002}}, s_clock_uuid = {{1003}},
                           s_profile_uuid = {{1004}}, s_stats_uuid = {{1005}}, s_presence_uuid = {{1006}};
static app_config_t s_config;
bool app_stats_is_ready(void) { return false; }
bool app_stats_remote_publish(const app_stats_snapshot_t *snapshot) { (void)snapshot; return true; }
void app_stats_remote_source(const char *id) { (void)id; }
void app_stats_remote_disconnected(void) {}
static bool s_started = true, s_synced = true, scanning;
static uint8_t s_own_addr_type;
static char s_address[18] = "01:02:03:04:05:06";
static int64_t now_us = 5000000;
static app_runtime_state_t runtime;
static app_energy_sample_t ingested;
static bool ingest_ready;
static unsigned scans, connects, services, characteristics, descriptors, writes, reads,
    terminations, ingests, clocks, clears;
static uint16_t requested_value, requested_cccd;
static int64_t clock_ms;
static int clock_offset;
static app_slave_presence_receiver_t presence_receiver;
static attr_cb presence_callback;
static void *presence_arg;
static unsigned presence_resets, presence_legacy;
bool app_slave_presence_service_receive(const uint8_t *data, size_t size, bool auth)
{
    return app_slave_presence_receive(&presence_receiver, data, size, auth, now_us);
}
void app_slave_presence_service_disconnected(void)
{
    ++presence_resets;
    app_slave_presence_disconnected(&presence_receiver);
}
void app_slave_presence_service_legacy(void)
{
    ++presence_legacy;
    app_slave_presence_legacy(&presence_receiver);
}
static bool authenticated = true, profile_ready;
static unsigned security_requests, profile_reads, profile_submits;
static bool security_authenticated(uint16_t conn)
{
    assert(conn == 7);
    return authenticated;
}
static int security_gap_event(struct ble_gap_event *event)
{
    (void)event;
    return 0;
}
static int ble_gap_security_initiate(uint16_t conn)
{
    assert(conn == 7);
    security_requests++;
    return 0;
}
bool app_ble_profile_ready(void)
{
    return profile_ready;
}
bool app_ble_profile_submit(const uint8_t data[APP_BLE_PROFILE_SIZE])
{
    (void)data;
    profile_submits++;
    profile_ready = true;
    return true;
}

int64_t esp_timer_get_time(void)
{
    return now_us;
}
static bool ble_gap_disc_active(void)
{
    return scanning;
}
static int ble_uuid_cmp(const ble_uuid_t *a, const ble_uuid_t *b)
{
    return a->value != b->value;
}
static uint16_t ble_uuid_u16(const ble_uuid_t *uuid)
{
    return (uint16_t)uuid->value;
}
static int os_mbuf_copydata(const struct os_mbuf *om, unsigned start, unsigned length, void *dst)
{
    if (start + length > om->len)
        return -1;
    memcpy(dst, om->data + start, length);
    return 0;
}
static int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *out, const uint8_t *data, unsigned len)
{
    static ble_uuid128_t uuid;
    if (!len)
        return -1;
    uuid.u.value = data[0] ? 1000 : 999;
    *out = (struct ble_hs_adv_fields){1, &uuid};
    return 0;
}
static int ble_gap_disc(
    uint8_t own, unsigned duration, const struct ble_gap_disc_params *params, gap_cb cb, void *arg)
{
    (void)own;
    (void)cb;
    (void)arg;
    assert(duration == 5000 && params->window <= params->itvl);
    ++scans;
    scanning = true;
    return 0;
}
static int ble_gap_disc_cancel(void)
{
    scanning = false;
    return 0;
}
static int ble_gap_connect(
    uint8_t own, const ble_addr_t *addr, unsigned timeout, const void *params, gap_cb cb, void *arg)
{
    (void)own;
    (void)addr;
    (void)params;
    (void)cb;
    (void)arg;
    assert(timeout == 8000);
    ++connects;
    return 0;
}
static int ble_gap_terminate(uint16_t conn, unsigned reason)
{
    assert(conn == 7 && reason == BLE_ERR_REM_USER_CONN_TERM);
    ++terminations;
    return 0;
}
static int ble_gap_conn_rssi(uint16_t conn, int8_t *out)
{
    assert(conn == 7);
    *out = -66;
    return 0;
}
static int
ble_gattc_disc_svc_by_uuid(uint16_t conn, const ble_uuid_t *uuid, service_cb cb, void *arg)
{
    (void)cb;
    (void)arg;
    assert(conn == 7 && uuid->value == 1000);
    ++services;
    return 0;
}
static int ble_gattc_exchange_mtu(uint16_t conn,
    int (*cb)(uint16_t, const struct ble_gatt_error *, uint16_t, void *), void *arg)
{
    struct ble_gatt_error error = {0};
    return cb(conn, &error, 247, arg);
}
static int
ble_gattc_disc_all_chrs(uint16_t conn, uint16_t start, uint16_t end, chr_cb cb, void *arg)
{
    (void)cb;
    (void)arg;
    assert(conn == 7 && start == 100 && end == 110);
    ++characteristics;
    return 0;
}
static int
ble_gattc_disc_all_dscs(uint16_t conn, uint16_t start, uint16_t end, desc_cb cb, void *arg)
{
    (void)cb;
    assert(conn == 7);
    unsigned packet = (unsigned)(uintptr_t)arg;
    assert(start == 102 + packet * 3 && end == (packet == 2 ? 110 : 103 + packet * 3));
    ++descriptors;
    return 0;
}
static int ble_gattc_write_flat(
    uint16_t conn, uint16_t handle, const void *data, unsigned len, attr_cb cb, void *arg)
{
    (void)cb;
    (void)arg;
    assert(conn == 7 && len == 2);
    const uint8_t *bytes = data;
    assert(bytes[0] == 1 && bytes[1] == 0);
    requested_cccd = handle;
    ++writes;
    return 0;
}
static int ble_gattc_read(uint16_t conn, uint16_t handle, attr_cb cb, void *arg)
{
    assert(conn == 7);
    if (handle == 140)
    {
        presence_callback = cb;
        presence_arg = arg;
    }
    requested_value = handle;
    ++reads;
    return 0;
}
static int
ble_gattc_read_long(uint16_t conn, uint16_t handle, uint16_t offset, attr_cb cb, void *arg)
{
    (void)cb;
    (void)arg;
    assert(conn == 7 && handle == 110 && offset == 0);
    profile_reads++;
    return 0;
}
void app_events_add(app_event_level_t level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}
void app_state_clear_sample(void)
{
    runtime.has_valid_sample = false;
    ++clears;
}
void app_state_set_energy_flow_disabled(bool disabled, bool night)
{
    runtime.energy_flow_disabled = disabled;
    runtime.standalone_night = night;
    if (disabled) runtime.has_valid_sample = false;
}
void app_state_set_slave(bool connected, bool stale, uint16_t timeout, bool packet)
{
    runtime.slave_connected = connected;
    runtime.slave_source_stale = stale;
    runtime.slave_source_timeout_seconds = timeout;
    if (packet)
        runtime.slave_packet_monotonic_us = now_us;
}
void app_state_set_ble(bool enabled,
                       bool master,
                       bool ready,
                       bool advertising,
                       bool scan,
                       unsigned slaves,
                       const char *address,
                       const char *peer,
                       int rssi)
{
    (void)slaves;
    (void)address;
    (void)peer;
    assert(enabled && !master && ready && !advertising);
    runtime.ble_scanning = scan;
    runtime.ble_rssi = rssi;
}
esp_err_t app_time_sync_from_master(int64_t epoch, int16_t offset)
{
    clock_ms = epoch;
    clock_offset = offset;
    ++clocks;
    return ESP_OK;
}
esp_err_t app_ingest_slave_sample(const app_energy_sample_t *sample)
{
    if (!ingest_ready)
        return ESP_ERR_INVALID_STATE;
    ingested = *sample;
    runtime.has_valid_sample = true;
    ++ingests;
    return ESP_OK;
}

#include "../main/app_ble_slave.inc"

static const struct ble_gatt_error ok = {0}, done = {BLE_HS_EDONE};
static struct os_mbuf packets[3];

static void make_frame(uint16_t sequence, int64_t epoch)
{
    app_energy_sample_t sample = {.power_pv_kw = 7,
                                  .power_full_kw = 4,
                                  .power_bat_kw = -2,
                                  .power_grid_kw = -1,
                                  .soc_bat_pct = 80,
                                  .battery_temperature_c = 25,
                                  .status = 1,
                                  .received_monotonic_us = 1000000,
                                  .received_epoch_s = epoch};
    app_config_t cfg = {0};
    cfg.display.day_brightness = 80;
    cfg.mqtt.data_timeout_seconds = 30;
    for (unsigned i = 0; i < 3; ++i)
        packets[i].len = 20;
    app_ble_protocol_encode(&sample, &cfg, 3000000, sequence, packets[0].data, packets[1].data);
    app_ble_clock_encode(epoch * 1000 + 123, 120, 30, true, sequence, packets[2].data);
}

static void connect_link(bool clock_service)
{
    struct ble_gap_event event = {.type = BLE_GAP_EVENT_CONNECT, .connect = {.conn_handle = 7}};
    slave_gap_event(&event, NULL);
    assert(security_requests > 0);
    event.type = BLE_GAP_EVENT_ENC_CHANGE;
    event.enc_change.conn_handle = 7;
    event.enc_change.status = 0;
    slave_gap_event(&event, NULL);
    const struct ble_gatt_svc svc = {100, 110};
    slave_service(7, &ok, &svc, NULL);
    slave_service(7, &done, NULL, NULL);
    for (unsigned i = 0; i < (clock_service ? 3U : 2U); ++i)
    {
        const struct ble_gatt_chr chr = {.uuid = {{1001 + i}},
                                         .def_handle = 101 + i * 3,
                                         .val_handle = 102 + i * 3,
                                         .properties =
                                             BLE_GATT_CHR_PROP_READ | BLE_GATT_CHR_PROP_NOTIFY};
        slave_characteristic(7, &ok, &chr, NULL);
    }
    const struct ble_gatt_chr profile = {.uuid = {{1004}},
                                         .def_handle = 109,
                                         .val_handle = 110,
                                         .properties = BLE_GATT_CHR_PROP_READ};
    slave_characteristic(7, &ok, &profile, NULL);
    slave_characteristic(7, &done, NULL, NULL);
    if (!clock_service)
        return;
    assert(s_profile_reading && !s_slave_ready);
    unsigned before_clocks = clocks;
    slave_receive(2, &packets[2]);
    assert(clocks == before_clocks); /* Profil előtt nincs óraszinkron. */
    for (unsigned offset = 0; offset < APP_BLE_PROFILE_SIZE; offset += 20)
    {
        struct os_mbuf part = {
            .len = APP_BLE_PROFILE_SIZE - offset < 20 ? APP_BLE_PROFILE_SIZE - offset : 20};
        struct ble_gatt_attr attr = {.om = &part, .offset = offset};
        slave_profile_read(7, &ok, &attr, NULL);
    }
    slave_profile_read(7, &done, NULL, NULL);
    slave_poll(now_us);
    assert(profile_submits > 0 && profile_reads > 0 && !s_profile_waiting);
    for (unsigned i = 0; i < 3; ++i)
    {
        void *arg = (void *)(uintptr_t)i;
        const struct ble_gatt_dsc dsc = {.uuid = {{0x2902}}, .handle = 103 + i * 3};
        slave_descriptor(7, &ok, 102 + i * 3, &dsc, arg);
        slave_descriptor(7, &done, 102 + i * 3, NULL, arg);
        assert(requested_cccd == 103 + i * 3);
        slave_subscribed(7, &ok, NULL, arg);
        assert(requested_value == 102 + i * 3);
        struct ble_gatt_attr attr = {.om = &packets[i]};
        slave_read_done(7, &ok, &attr, arg);
    }
}

static void notify(unsigned packet)
{
    struct ble_gap_event event = {.type = BLE_GAP_EVENT_NOTIFY_RX,
                                  .notify_rx = {.conn_handle = 7,
                                                .attr_handle = (uint16_t)(102 + packet * 3),
                                                .om = &packets[packet]}};
    slave_gap_event(&event, NULL);
}

static void presence_client_tests(void)
{
    assert(presence_legacy > 0);
    slave_reset();
    s_master = 7;
    s_slave_ready = s_profile_accepted = true;
    const struct ble_gatt_chr presence = {.uuid = {{1006}}, .def_handle = 139,
                                           .val_handle = 140,
                                           .properties = BLE_GATT_CHR_PROP_READ};
    slave_characteristic(7, &ok, &presence, NULL);
    assert(s_remote_presence == 140);
    slave_presence_poll(now_us);
    assert(s_presence_reading && presence_callback);
    app_slave_presence_peer_t peer = {
        .used = true, .identity = {0, {1, 2, 3, 4, 5, 6}}, .revision = 1,
        .profile = {.name = "Nappali", .timeout_seconds = 30, .blank_at_night = true},
        .counter = 100, .valid = true,
    };
    memcpy(peer.profile.presence_uuid, "01234567-89ab-cdef-0123456789abcdef", 36);
    struct os_mbuf wire = {.len = APP_SLAVE_PRESENCE_PACKET_SIZE};
    struct ble_gatt_attr attr = {.om = &wire};
    assert(app_slave_presence_encode(&peer, 1, true, wire.data));
    presence_callback(7, &ok, &attr, presence_arg);
    assert(presence_receiver.baseline && !presence_receiver.pending_pulse);
    assert(!s_presence_reading);
    now_us += 1000000;
    slave_presence_poll(now_us);
    peer.counter++;
    assert(app_slave_presence_encode(&peer, 2, true, wire.data));
    presence_callback(7, &ok, &attr, presence_arg);
    assert(presence_receiver.pending_pulse && !presence_receiver.active);

    /* Hibás wire nem törli a replay-bázist és nem újít lease-t. */
    now_us += 1000000;
    slave_presence_poll(now_us);
    wire.data[0] = 'X';
    app_slave_presence_receiver_t saved = presence_receiver;
    presence_callback(7, &ok, &attr, presence_arg);
    assert(!memcmp(&saved, &presence_receiver, sizeof(saved)));

    /* Régi callback ugyanazzal a connection handle-lel sem írhat új kapcsolatot. */
    now_us += 5000000;
    slave_presence_poll(now_us);
    attr_cb late = presence_callback;
    void *late_arg = presence_arg;
    slave_reset();
    s_master = 7;
    s_slave_ready = s_profile_accepted = true;
    s_remote_presence = 140;
    slave_presence_poll(now_us);
    uint32_t generation = s_presence_request;
    late(7, &ok, &attr, late_arg);
    assert(s_presence_reading && s_presence_request == generation && !presence_receiver.baseline);
    assert(app_slave_presence_encode(&peer, 1, true, wire.data));
    presence_callback(7, &ok, &attr, presence_arg);
    assert(presence_receiver.baseline && !presence_receiver.pending_pulse);

    now_us += 1000000;
    slave_presence_poll(now_us);
    unsigned resets = presence_resets;
    now_us += 4000000;
    slave_presence_poll(now_us);
    assert(s_presence_reading && s_presence_expired && presence_resets == resets + 1);
    assert(!presence_receiver.baseline);
    presence_callback(7, &ok, &attr, presence_arg);
    assert(!s_presence_reading && !presence_receiver.baseline);
    now_us += 5000000;
    authenticated = false;
    slave_presence_poll(now_us);
    assert(!s_presence_reading && !presence_receiver.baseline);
    authenticated = true;
}

int main(void)
{
    slave_reset();
    now_us += 3000000;
    slave_poll(now_us);
    assert(scans == 1 && scanning);
    uint8_t advertised = 0;
    struct ble_gap_event adv = {.type = BLE_GAP_EVENT_DISC,
                                .disc = {.data = &advertised,
                                         .length_data = 1,
                                         .rssi = -52,
                                         .addr = {.val = {6, 5, 4, 3, 2, 1}}}};
    slave_gap_event(&adv, NULL);
    assert(connects == 0);
    advertised = 1;
    slave_gap_event(&adv, NULL);
    assert(connects == 1 && !scanning && s_connecting);
    make_frame(77, 1800000000);
    connect_link(true);
    assert(services == 1 && characteristics == 1 && descriptors == 3 && reads == 3 && writes == 3);
    assert(s_slave_ready && runtime.slave_connected && ingests == 0 && clocks == 1);
    assert(clock_ms == 1800000000123LL && clock_offset == 120);
    int64_t received_at = now_us, frame_at = s_last_frame_us;
    ingest_ready = true;
    now_us += 2000000;
    slave_poll(now_us);
    assert(ingests == 1 && ingested.received_monotonic_us == received_at - 2000000);
    assert(s_last_frame_us == frame_at); /* A cache poll nem rádiós heartbeat. */
    assert(ingested.power_grid_kw == -1 && ingested.power_bat_kw == -2);
    make_frame(78, 1800000000);
    notify(1);
    notify(2);
    notify(0);
    assert(ingests == 1); /* Azonos mérésből nem képződhet új statisztika. */
    now_us += 5000000;
    make_frame(79, 1800000005);
    notify(2);
    notify(0);
    notify(1);
    assert(ingests == 2 && ingested.received_epoch_s == 1800000005);
    /* Hitelesített teljes módcsomag nem kerülhet a mérési/statisztikai ingestbe. */
    app_config_t mode_cfg = {.data_recept_mode=APP_DATA_LOXONE,.energy_flow_disabled=true};
    app_energy_sample_t night_only = {.night_mode=true};
    app_ble_protocol_encode(&night_only,&mode_cfg,now_us,900,packets[0].data,packets[1].data);
    app_ble_clock_encode(1800000005123LL,120,30,true,900,packets[2].data);
    notify(2);notify(1);notify(0);
    assert(ingests==2&&runtime.energy_flow_disabled&&runtime.standalone_night&&!runtime.has_valid_sample);
    make_frame(901,1800000006);notify(0);notify(1);notify(2);
    assert(ingests==3&&!runtime.energy_flow_disabled);
    slave_poll(now_us);
    slave_diagnostics();
    assert(runtime.ble_rssi == -66);
    now_us += 15000000;
    slave_poll(now_us);
    assert(terminations == 1 && !runtime.slave_connected && !runtime.has_valid_sample);
    now_us += 3000000;
    slave_poll(now_us);
    assert(scans == 2);
    /* Óraszolgáltatás nélküli régi masterre nem maradhat ráragadva. */
    scanning = false;
    connect_link(false);
    assert(terminations == 2 && !runtime.slave_connected);
    make_frame(80, 1800000010);
    connect_link(true);
    assert(ingests == 4);
    make_frame(81, 1800000015);
    packets[0].data[12] = 0xff;
    packets[0].data[13] = 0xff;
    notify(0);
    notify(1);
    notify(2);
    assert(terminations == 3 && ingests == 4 && !runtime.has_valid_sample);
    assert(clears >= 4);
    unsigned before_termination = terminations;
    s_master = 7;
    authenticated = false;
    slave_read_profile();
    assert(terminations == before_termination + 1);
    authenticated = true;
    s_master = 7;
    s_profile_length = 0;
    struct os_mbuf invalid_part = {.len = 20};
    struct ble_gatt_attr invalid_attr = {.om = &invalid_part, .offset = 1};
    slave_profile_read(7, &ok, &invalid_attr, NULL);
    assert(terminations == before_termination + 2);
    presence_client_tests();
    puts("PASS: actual BLE central discovery/CCCD/read/notify, ordering, startup cache, dedup, "
         "heartbeat, reconnect, old-master and malformed rejection");
    return 0;
}
