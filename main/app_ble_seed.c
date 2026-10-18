/*
 * Csak olvasható BLE master a P4-en futó NimBLE hosttal.
 * A C6 vezérlőt az ESP-Hosted 1.4.7 meglévő VHCI/SDIO útvonala éri el.
 * A GATT, a kapcsolatok és a periodikus értesítések egyetlen host taskban
 * futnak; az MQTT/HTTP/Loxone adatfogadást nem blokkolják.
 */
#include "app_ble_seed.h"
#include "app_ble_protocol.h"
#include "app_events.h"
#include "app_time.h"
#include "app_ingest.h"
#include "app_ble_profile.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "store/config/ble_store_config.h"
/* Az IDF NimBLE config-store init hookját a publikus fejléc nem deklarálja. */
void ble_store_config_init(void);
#include "nimble/nimble_port.h"
#include "nimble/nimble_npl.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* A szöveges UUID-k fordított bájtsorrendben kerülnek a NimBLE struktúrába.
 * Szolgáltatás: 8b7e1000-6c8a-4f57-9b2e-5d1a7c0e9f30.
 * Karakterisztikák: ugyanebben a névtérben 1001 (energia), 1002 (időjárás). */
#define SEED_UUID(short_id)                                                                        \
    BLE_UUID128_INIT(0x30,                                                                         \
                     0x9f,                                                                         \
                     0x0e,                                                                         \
                     0x7c,                                                                         \
                     0x1a,                                                                         \
                     0x5d,                                                                         \
                     0x2e,                                                                         \
                     0x9b,                                                                         \
                     0x57,                                                                         \
                     0x4f,                                                                         \
                     0x8a,                                                                         \
                     0x6c,                                                                         \
                     short_id,                                                                     \
                     0x10,                                                                         \
                     0x7e,                                                                         \
                     0x8b)

static const char *TAG = "ble_seed";
static const ble_uuid128_t s_service_uuid = SEED_UUID(0x00);
static const ble_uuid128_t s_energy_uuid = SEED_UUID(0x01);
static const ble_uuid128_t s_weather_uuid = SEED_UUID(0x02);
static const ble_uuid128_t s_clock_uuid = SEED_UUID(0x03);
static const ble_uuid128_t s_profile_uuid = SEED_UUID(0x04);
static app_config_t s_config;
static bool s_started;
static bool s_synced;
static bool s_advertising_reported;
static uint8_t s_own_addr_type;
static uint16_t s_handles[3];
static uint16_t s_sequence;
static uint8_t s_packets[3][APP_BLE_PACKET_SIZE];
static char s_address[18];
static int64_t s_last_sample_us = -1;
static int64_t s_last_publish_us;
static int64_t s_sync_wait_started_us;
static bool s_sync_warning_reported;
static struct ble_npl_callout s_poll;

typedef struct
{
    bool used;
    uint16_t handle;
    bool subscribed[3];
} seed_peer_t;

static seed_peer_t s_peers[CONFIG_BT_NIMBLE_MAX_CONNECTIONS];

static int gap_event(struct ble_gap_event *event, void *arg);
static void slave_begin_scan(void);
static void slave_poll(int64_t now);
static void slave_reset(void);
static void slave_diagnostics(void);

static bool slave_mode(void)
{
    return s_config.data_recept_mode == APP_DATA_SLAVE;
}

static void diagnostics_update(void);

#include "app_ble_security.inc"

/* Az ESP-IDF 5.5 NimBLE már kéri ezt a lezáró hookot, az ESP-Hosted 1.4.7
 * viszont csak ll_init-et ad. A közös SDIO buszt itt nem zárhatjuk le,
 * mert a Wi-Fi akkor is használja, ha a BLE host inicializálása meghiúsul. */
void ble_transport_ll_deinit(void)
{
}

/* A nyilvános mérési karakterisztikák nem tartalmaznak Wi-Fi-t vagy PIN-t. */
static int read_packet(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    unsigned packet = (unsigned)(uintptr_t)arg;
    return os_mbuf_append(ctxt->om, s_packets[packet], APP_BLE_PACKET_SIZE) == 0
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int read_profile(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    if (!security_authenticated(conn))
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    uint8_t profile[APP_BLE_PROFILE_SIZE];
    app_ble_profile_encode(&s_config, profile);
    int rc = os_mbuf_append(ctxt->om, profile, sizeof(profile));
    memset(profile, 0, sizeof(profile));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &s_energy_uuid.u,
                    .access_cb = read_packet,
                    .arg = (void *)(uintptr_t)0,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_handles[0],
                },
                {
                    .uuid = &s_weather_uuid.u,
                    .access_cb = read_packet,
                    .arg = (void *)(uintptr_t)1,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_handles[1],
                },
                {
                    .uuid = &s_clock_uuid.u,
                    .access_cb = read_packet,
                    .arg = (void *)(uintptr_t)2,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_handles[2],
                },
                {
                    .uuid = &s_profile_uuid.u,
                    .access_cb = read_profile,
                    .flags =
                        BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN,
                    .min_key_size = 16,
                },
                {0},
            },
    },
    {0},
};

static unsigned peer_count(void)
{
    unsigned count = 0;
    for (unsigned i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; ++i)
        count += s_peers[i].used;
    return count;
}

static void diagnostics_update(void)
{
    if (slave_mode())
    {
        slave_diagnostics();
        return;
    }
    app_state_set_ble(s_started,
                      true,
                      s_synced,
                      s_synced && ble_gap_adv_active(),
                      false,
                      peer_count(),
                      s_address,
                      APP_BLE_SEED_NAME,
                      0);
}

/* Az UUID az elsődleges hirdetésbe, a név a scan response-ba kerül:
 * így mindkettő elfér a hagyományos BLE 31 bájtos keretében. */
static void advertise(void)
{
    if (slave_mode() || !s_synced || ble_gap_adv_active() ||
        peer_count() >= CONFIG_BT_NIMBLE_MAX_CONNECTIONS)
        return;

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc == 0)
    {
        memset(&fields, 0, sizeof(fields));
        fields.name = (uint8_t *)APP_BLE_SEED_NAME;
        fields.name_len = strlen(APP_BLE_SEED_NAME);
        fields.name_is_complete = 1;
        rc = ble_gap_adv_rsp_set_fields(&fields);
    }
    if (rc == 0)
    {
        struct ble_gap_adv_params params = {0};
        params.conn_mode = BLE_GAP_CONN_MODE_UND;
        params.disc_mode = BLE_GAP_DISC_MODE_GEN;
        params.itvl_min = 320; /* 200–250 ms: kis terhelés a közös rádión. */
        params.itvl_max = 400;
        rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    }
    if (rc != 0)
    {
        ESP_LOGW(TAG, "Advertising failed: %d; retry in 1 s", rc);
    }
    else if (!s_advertising_reported)
    {
        s_advertising_reported = true;
        app_events_add(APP_EVENT_INFO, "BLE adat seed aktív: " APP_BLE_SEED_NAME);
    }
}

/* A NimBLE átveszi az mbuf tulajdonjogát hiba esetén is.
 * Pufferhiánynál elengedjük ezt a mintát; a következő heartbeat helyrehozza. */
static void notify_peer(seed_peer_t *peer, unsigned packet)
{
    if (!peer->used || !peer->subscribed[packet])
        return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(s_packets[packet], APP_BLE_PACKET_SIZE);
    if (om)
        (void)ble_gatts_notify_custom(peer->handle, s_handles[packet], om);
}

static int64_t refresh_packets(int64_t now)
{
    app_energy_sample_t sample;
    bool valid = app_state_get_sample(&sample);
    app_ble_protocol_encode(
        valid ? &sample : NULL, &s_config, now, ++s_sequence, s_packets[0], s_packets[1]);
    struct timeval current;
    gettimeofday(&current, NULL);
    struct tm local;
    time_t epoch = current.tv_sec;
    char offset[8] = "+0000";
    if (localtime_r(&epoch, &local))
        strftime(offset, sizeof(offset), "%z", &local);
    int minutes = 0;
    if ((offset[0] == '+' || offset[0] == '-') && strlen(offset) == 5)
        minutes = ((offset[1] - '0') * 600 + (offset[2] - '0') * 60 + (offset[3] - '0') * 10 +
                   offset[4] - '0') *
                  (offset[0] == '-' ? -1 : 1);
    app_runtime_state_t runtime;
    app_state_get_runtime(&runtime);
    app_ble_clock_encode((int64_t)current.tv_sec * 1000 + current.tv_usec / 1000,
                         (int16_t)minutes,
                         (uint16_t)app_config_data_timeout(&s_config),
                         runtime.time_synced,
                         s_sequence,
                         s_packets[2]);
    return valid ? sample.received_monotonic_us : -1;
}

/* Legfeljebb 1 s késleltetés új mintára, 5 s heartbeat adatkimaradáskor is.
 * Lejárt mintát sosem jelölünk élő adatnak. A kiolvasás 1 s-os cache-t használ. */
static void poll_state(struct ble_npl_event *event)
{
    (void)event;
    int64_t now = esp_timer_get_time();
    security_poll(now);
    if (!s_synced && !s_sync_warning_reported && now - s_sync_wait_started_us >= 15000000)
    {
        s_sync_warning_reported = true;
        app_events_add(
            APP_EVENT_WARNING,
            "BLE vezérlő nem válaszol; ellenőrizd a C6 firmware Bluetooth HCI támogatását.");
    }
    uint8_t previous_flags = s_packets[0][1];
    if (slave_mode())
    {
        slave_poll(now);
        diagnostics_update();
        ble_npl_callout_reset(&s_poll, ble_npl_time_ms_to_ticks32(1000));
        return;
    }
    int64_t sample_us = refresh_packets(now);
    bool publish = sample_us != s_last_sample_us || previous_flags != s_packets[0][1] ||
                   now - s_last_publish_us >= 5000000;
    if (s_synced && publish)
    {
        s_last_sample_us = sample_us;
        s_last_publish_us = now;
        for (unsigned i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; ++i)
            for (unsigned packet = 0; packet < 3; ++packet)
                notify_peer(&s_peers[i], packet);
    }
    advertise();
    diagnostics_update();
    ble_npl_callout_reset(&s_poll, ble_npl_time_ms_to_ticks32(1000));
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    int security_result = security_gap_event(event);
    if (event->type == BLE_GAP_EVENT_REPEAT_PAIRING)
        return security_result;
    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0)
            {
                for (unsigned i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; ++i)
                {
                    if (!s_peers[i].used)
                    {
                        s_peers[i] =
                            (seed_peer_t){.used = true, .handle = event->connect.conn_handle};
                        break;
                    }
                }
                ESP_LOGI(TAG, "Slave connected; peers=%u", peer_count());
            }
            advertise();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            for (unsigned i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; ++i)
                if (s_peers[i].used && s_peers[i].handle == event->disconnect.conn.conn_handle)
                    memset(&s_peers[i], 0, sizeof(s_peers[i]));
            advertise();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            refresh_packets(esp_timer_get_time());
            for (unsigned i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; ++i)
            {
                if (!s_peers[i].used || s_peers[i].handle != event->subscribe.conn_handle)
                    continue;
                for (unsigned packet = 0; packet < 3; ++packet)
                {
                    if (s_handles[packet] == event->subscribe.attr_handle)
                    {
                        s_peers[i].subscribed[packet] = event->subscribe.cur_notify;
                    }
                }
                /* Második feliratkozáskor is azonos sorszámú pár érkezzen. */
                notify_peer(&s_peers[i], 0);
                notify_peer(&s_peers[i], 1);
                notify_peer(&s_peers[i], 2);
            }
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            /* A következő poll újraindítja; nincs rekurzív újrapróbálkozás. */
            break;
        default:
            break;
    }
    diagnostics_update();
    return 0;
}

static void on_reset(int reason)
{
    security_clear_request(s_pin_connection);
    atomic_store(&s_window_end, 0);
    if (s_synced)
        s_sync_wait_started_us = esp_timer_get_time();
    s_synced = false;
    memset(s_peers, 0, sizeof(s_peers));
    if (slave_mode())
        slave_reset();
    diagnostics_update();
    ESP_LOGW(TAG, "BLE host reset: %d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0)
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0)
    {
        app_events_add(APP_EVENT_WARNING, "BLE cím inicializálási hiba: %d", rc);
        return;
    }
    s_synced = true;
    s_sync_warning_reported = false;
    uint8_t address[6];
    if (ble_hs_id_copy_addr(s_own_addr_type, address, NULL) == 0)
        snprintf(s_address,
                 sizeof(s_address),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 address[5],
                 address[4],
                 address[3],
                 address[2],
                 address[1],
                 address[0]);
    if (slave_mode())
        slave_begin_scan();
    else
        advertise();
    diagnostics_update();
}

/* Az inicializálás külön taskban történik: egy nem megfelelő C6 firmware
 * HCI időtúllépése nem állíthatja meg az energiaadatok fogadását vagy a UI-t. */
static void host_task(void *arg)
{
    (void)arg;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK)
    {
        app_events_add(APP_EVENT_ERROR, "BLE adat seed nem indult: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = slave_mode() ? BLE_HS_IO_KEYBOARD_ONLY : BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 1;
    ble_hs_cfg.sm_sec_lvl = 4;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    if (!slave_mode() && app_ble_profile_pin_changed())
    {
        if (ble_store_clear() != 0 || app_ble_profile_confirm_pin() != ESP_OK)
        {
            app_events_add(APP_EVENT_ERROR,
                           "A BLE PIN/kulcstároló nem frissíthető; BLE leállítva.");
            (void)nimble_port_deinit();
            vTaskDelete(NULL);
            return;
        }
    }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(APP_BLE_SEED_NAME);
    if (rc == 0 && !slave_mode())
        rc = ble_gatts_count_cfg(s_services);
    if (rc == 0 && !slave_mode())
        rc = ble_gatts_add_svcs(s_services);
    if (rc != 0)
    {
        app_events_add(APP_EVENT_ERROR, "BLE szolgáltatás inicializálási hiba: %d", rc);
        (void)nimble_port_deinit();
        vTaskDelete(NULL);
        return;
    }
    refresh_packets(esp_timer_get_time());
    s_sync_wait_started_us = esp_timer_get_time();
    rc = ble_npl_callout_init(&s_poll, nimble_port_get_dflt_eventq(), poll_state, NULL);
    if (rc == 0)
        rc = ble_npl_callout_reset(&s_poll, ble_npl_time_ms_to_ticks32(1000));
    if (rc != 0)
    {
        app_events_add(APP_EVENT_ERROR, "BLE időzítő nem indult: %d", rc);
        (void)nimble_port_deinit();
        vTaskDelete(NULL);
        return;
    }
    nimble_port_run();
    ble_npl_callout_stop(&s_poll);
    (void)nimble_port_deinit();
    vTaskDelete(NULL);
}

esp_err_t app_ble_seed_start(const app_config_t *cfg)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;
    if (cfg->data_seed_enabled &&
        (strlen(cfg->data_seed_pin) != 6 || strspn(cfg->data_seed_pin, "0123456789") != 6))
        return ESP_ERR_INVALID_ARG;
    if (!cfg->data_seed_enabled && cfg->data_recept_mode != APP_DATA_SLAVE)
        return ESP_OK;
    if (s_started)
        return ESP_ERR_INVALID_STATE;
    s_config = *cfg;
    if (slave_mode())
        slave_reset();
    s_started = true;
    app_state_set_ble(true, !slave_mode(), false, false, false, 0, "", "", 0);
    if (xTaskCreatePinnedToCore(host_task,
                                "ble_seed",
                                CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,
                                NULL,
                                configMAX_PRIORITIES - 4,
                                NULL,
                                CONFIG_BT_NIMBLE_PINNED_TO_CORE) != pdPASS)
    {
        s_started = false;
        app_state_set_ble(false, !slave_mode(), false, false, false, 0, "", "", 0);
        return ESP_ERR_NO_MEM;
    }
    app_events_add(APP_EVENT_INFO,
                   slave_mode() ? "BLE Slave indul; master keresése."
                                : "BLE adat seed indul; C6 Bluetooth HCI támogatás szükséges.");
    return ESP_OK;
}

#include "app_ble_slave.inc"
