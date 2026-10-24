/*
 * BLE central: UUID szerinti keresés, szolgáltatás/karakterisztika/CCCD
 * felderítés, feliratkozás és első READ. Minden callback a NimBLE host
 * soros eseménysorában fut, nincs külön task vagy blokkoló rádiós várakozás.
 */
static uint16_t s_master = BLE_HS_CONN_HANDLE_NONE;
static bool s_connecting, s_slave_ready, s_source_disabled;
static uint16_t s_service_start, s_service_end;
static uint16_t s_remote_values[3], s_remote_defs[3], s_cccd;
static uint16_t s_remote_profile;
static uint16_t s_remote_stats;
static bool s_profile_reading, s_profile_waiting;
static bool s_profile_accepted;
static size_t s_profile_length;
static uint8_t s_slave_profile[APP_BLE_PROFILE_SIZE];
static int64_t s_profile_next_read;
static unsigned s_setup_packet;
static uint8_t s_received[3][APP_BLE_PACKET_SIZE];
static bool s_received_valid[3];
static int64_t s_next_scan_us, s_setup_deadline_us, s_last_frame_us;
static int64_t s_applied_sample_epoch;
static int64_t s_last_clock_receive_us;
static int64_t s_energy_receive_us;
static uint16_t s_frame_sequence;
static bool s_have_frame;
static uint16_t s_last_clock_sequence;
static bool s_have_clock_sequence;
static uint16_t s_master_timeout = 45;
static bool s_master_stale = true;
static char s_master_address[18];
static int s_master_rssi;

static int slave_gap_event(struct ble_gap_event *event, void *arg);
static void slave_setup_next(void);
static void slave_stats_reset(void);
static void slave_stats_poll(int64_t now);
static bool slave_stats_busy(void);
#ifdef APP_INTERCOM_SLAVE
static bool slave_stats_transport_busy(void);
#endif

#include "app_ble_presence_client.inc"
#ifdef APP_INTERCOM_SLAVE
#include "app_ble_intercom_client.inc"
#endif

static void slave_diagnostics(void)
{
    app_state_set_ble(s_started,
                      false,
                      s_synced,
                      false,
                      s_synced && ble_gap_disc_active(),
                      0,
                      s_address,
                      s_master_address,
                      s_master_rssi);
}

static void slave_reset(void)
{
    slave_stats_reset();
    slave_presence_reset();
#ifdef APP_INTERCOM_SLAVE
    slave_intercom_reset();
#endif
    s_master = BLE_HS_CONN_HANDLE_NONE;
    s_connecting = s_slave_ready = false;
    s_service_start = s_service_end = 0;
    memset(s_remote_values, 0, sizeof(s_remote_values));
    memset(s_remote_defs, 0, sizeof(s_remote_defs));
    s_remote_profile = 0;
    s_remote_stats = 0;
    s_profile_reading = s_profile_waiting = false;
    s_profile_accepted = false;
    s_profile_length = 0;
    memset(s_slave_profile, 0, sizeof(s_slave_profile));
    memset(s_received_valid, 0, sizeof(s_received_valid));
    s_master_address[0] = 0;
    s_master_rssi = 0;
    s_applied_sample_epoch = 0;
    s_have_clock_sequence = false;
    s_last_clock_receive_us = 0;
    s_have_frame = false;
    s_master_stale = true;
    s_master_timeout = 45;
    s_next_scan_us = esp_timer_get_time() + 3000000;
    s_source_disabled = false;
    app_state_set_slave(false, true, 45, false);
    app_state_set_energy_flow_disabled(false, false);
    app_state_clear_sample();
}

static void slave_fail(const char *stage, int status)
{
    app_events_add(APP_EVENT_WARNING, "BLE master kapcsolat: %s (%d)", stage, status);
    if (s_master != BLE_HS_CONN_HANDLE_NONE)
        (void)ble_gap_terminate(s_master, BLE_ERR_REM_USER_CONN_TERM);
    /* Az aszinkron callbackek a régi connection handle-lel már nem írhatnak. */
    slave_reset();
    slave_diagnostics();
}

static int slave_profile_read(uint16_t conn,
                              const struct ble_gatt_error *error,
                              struct ble_gatt_attr *attr,
                              void *arg)
{
    (void)arg;
    if (conn != s_master)
        return 0;
    if (error->status == 0 && attr)
    {
        size_t length = OS_MBUF_PKTLEN(attr->om);
        if (attr->offset != s_profile_length ||
            length > sizeof(s_slave_profile) - s_profile_length ||
            os_mbuf_copydata(attr->om, 0, length, s_slave_profile + s_profile_length) != 0)
        {
            slave_fail("hibás védett profilrészlet", -1);
            return 0;
        }
        s_profile_length += length;
    }
    else if (error->status == BLE_HS_EDONE && security_authenticated(conn) &&
             s_profile_length == sizeof(s_slave_profile) && app_ble_profile_submit(s_slave_profile))
    {
        memset(s_slave_profile, 0, sizeof(s_slave_profile));
        s_profile_reading = false;
        s_profile_waiting = true;
        s_profile_next_read = esp_timer_get_time() + 30000000;
    }
    else
    {
        slave_fail("védett profil nem olvasható", error->status);
    }
    return 0;
}

static void slave_read_profile(void)
{
    if (!security_authenticated(s_master))
    {
        slave_fail("a profilhoz hitelesítés kell", -1);
        return;
    }
    s_profile_length = 0;
    s_profile_reading = true;
    if (ble_gattc_read_long(s_master, s_remote_profile, 0, slave_profile_read, NULL))
        slave_fail("védett profilolvasás nem indítható", -1);
}

static void slave_try_frame(int64_t now)
{
    if (!s_slave_ready || !s_received_valid[0] || !s_received_valid[1] || !s_received_valid[2])
        return;
    uint16_t sequence = app_ble_packet_sequence(s_received[0]);
    if (sequence != app_ble_packet_sequence(s_received[1]) ||
        sequence != app_ble_packet_sequence(s_received[2]))
        return;
    app_ble_clock_t clock;
    if (app_ble_clock_decode(s_received[2], 20, &clock) != ESP_OK)
        return;
    s_master_timeout = clock.source_timeout_seconds;
    s_master_stale = !(s_received[0][1] & 1) || (s_received[0][1] & 2);
    app_energy_sample_t sample;
    esp_err_t result =
        app_ble_protocol_decode(s_received[0], 20, s_received[1], 20, s_energy_receive_us, &sample);
    if (result == ESP_ERR_INVALID_ARG)
    {
        slave_fail("hibás adatcsomag", result);
        return;
    }
    bool new_frame = !s_have_frame || s_frame_sequence != sequence;
    if (new_frame)
    {
        s_frame_sequence = sequence;
        s_have_frame = true;
        s_last_frame_us = now;
    }
    if (result == ESP_OK && sample.energy_flow_disabled) s_master_stale = false;
    app_state_set_slave(true, s_master_stale, s_master_timeout, new_frame);
    if (result != ESP_OK)
    {
        s_source_disabled = false;
        app_state_set_energy_flow_disabled(false, false);
        return;
    }
    app_state_set_energy_flow_disabled(sample.energy_flow_disabled, sample.night_mode);
    if (sample.energy_flow_disabled)
    {
        /* Visszakapcsoláskor rövid tiltási időn át sem integrálhatunk energiát. */
        if (!s_source_disabled) app_stats_break_continuity();
        s_source_disabled = true;
        s_applied_sample_epoch = 0;
        return;
    }
    s_source_disabled = false;
    /* Heartbeat nem új mérés: az eredeti minta idejét/korát nem fiatalítjuk. */
    if (sample.received_epoch_s <= s_applied_sample_epoch)
        return;
    if (sample.received_epoch_s > clock.epoch_ms / 1000 + 5)
        return;
    if (app_ingest_slave_sample(&sample) == ESP_OK)
        s_applied_sample_epoch = sample.received_epoch_s;
    /* Induláskor a main előbb órát szinkronizál, utána indít statisztikát.
     * Addig ez a csomag a cache-ben marad, és a poll újrapróbálja. */
}

static void slave_receive(unsigned packet, const struct os_mbuf *om)
{
    if (!s_profile_accepted || !security_authenticated(s_master))
        return;
    if (packet >= 3 || OS_MBUF_PKTLEN(om) != APP_BLE_PACKET_SIZE)
        return;
    if (os_mbuf_copydata(om, 0, APP_BLE_PACKET_SIZE, s_received[packet]) != 0)
        return;
    s_received_valid[packet] = true;
    int64_t now = esp_timer_get_time();
    if (packet == 0)
        s_energy_receive_us = now;
    if (packet == 2)
    {
        app_ble_clock_t clock;
        if (app_ble_clock_decode(s_received[2], 20, &clock) == ESP_OK)
        {
            uint16_t sequence = app_ble_packet_sequence(s_received[2]);
            if (!s_have_clock_sequence || sequence != s_last_clock_sequence)
            {
                if (app_time_sync_from_master(clock.epoch_ms, clock.utc_offset_minutes) == ESP_OK)
                {
                    s_last_clock_sequence = sequence;
                    s_have_clock_sequence = true;
                    s_last_clock_receive_us = now;
                }
            }
        }
    }
    slave_try_frame(now);
}

static int slave_read_done(uint16_t conn,
                           const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr,
                           void *arg)
{
    if (conn != s_master)
        return 0;
    if (error->status || !attr)
    {
        slave_fail("első olvasás sikertelen", error->status);
        return 0;
    }
    slave_receive((unsigned)(uintptr_t)arg, attr->om);
    if (conn == s_master)
    {
        ++s_setup_packet;
        slave_setup_next();
    }
    return 0;
}

static int slave_subscribed(uint16_t conn,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg)
{
    (void)attr;
    if (conn != s_master)
        return 0;
    if (error->status)
    {
        slave_fail("feliratkozás sikertelen", error->status);
        return 0;
    }
    unsigned packet = (unsigned)(uintptr_t)arg;
    int rc = ble_gattc_read(conn, s_remote_values[packet], slave_read_done, arg);
    if (rc)
        slave_fail("olvasás nem indítható", rc);
    return 0;
}

static int slave_descriptor(uint16_t conn,
                            const struct ble_gatt_error *error,
                            uint16_t chr_handle,
                            const struct ble_gatt_dsc *dsc,
                            void *arg)
{
    (void)chr_handle;
    if (conn != s_master)
        return 0;
    if (error->status == 0)
    {
        if (ble_uuid_u16(&dsc->uuid.u) == 0x2902)
            s_cccd = dsc->handle;
    }
    else if (error->status == BLE_HS_EDONE && s_cccd)
    {
        const uint8_t subscribe[] = {1, 0};
        int rc =
            ble_gattc_write_flat(conn, s_cccd, subscribe, sizeof(subscribe), slave_subscribed, arg);
        if (rc)
            slave_fail("CCCD írás nem indítható", rc);
    }
    else
    {
        slave_fail("CCCD hiányzik", error->status);
    }
    return 0;
}

static void slave_setup_next(void)
{
    if (s_setup_packet == 3)
    {
        s_slave_ready = true;
        if (!s_remote_presence)
            app_slave_presence_service_legacy();
        s_last_frame_us = esp_timer_get_time();
        app_state_set_slave(true, true, s_master_timeout, false);
        slave_try_frame(s_last_frame_us);
        app_events_add(APP_EVENT_INFO, "BLE master csatlakoztatva: %s", s_master_address);
        return;
    }
    unsigned packet = s_setup_packet;
    uint16_t end = s_service_end;
    for (unsigned i = 0; i < 3; ++i)
        if (s_remote_defs[i] > s_remote_values[packet] && s_remote_defs[i] - 1 < end)
            end = s_remote_defs[i] - 1;
    s_cccd = 0;
    int rc = ble_gattc_disc_all_dscs(
        s_master, s_remote_values[packet], end, slave_descriptor, (void *)(uintptr_t)packet);
    if (rc)
        slave_fail("CCCD keresés nem indítható", rc);
}

static int slave_characteristic(uint16_t conn,
                                const struct ble_gatt_error *error,
                                const struct ble_gatt_chr *chr,
                                void *arg)
{
    (void)arg;
    if (conn != s_master)
        return 0;
    if (error->status == 0)
    {
#ifdef APP_INTERCOM_SLAVE
        if (ble_uuid_cmp(&chr->uuid.u, &s_intercom_uuid.u) == 0 &&
            (chr->properties & BLE_GATT_CHR_PROP_READ) && (chr->properties & BLE_GATT_CHR_PROP_WRITE))
            s_remote_intercom = chr->val_handle;
        if (ble_uuid_cmp(&chr->uuid.u, &s_intercom_profile_uuid.u) == 0 && (chr->properties & BLE_GATT_CHR_PROP_READ))
            s_remote_intercom_profile = chr->val_handle;
        if (ble_uuid_cmp(&chr->uuid.u, &s_intercom_extras_uuid.u) == 0 && (chr->properties & BLE_GATT_CHR_PROP_READ))
            s_remote_intercom_extras = chr->val_handle;
#endif
        const ble_uuid128_t *uuids[] = {&s_energy_uuid, &s_weather_uuid, &s_clock_uuid};
        if (ble_uuid_cmp(&chr->uuid.u, &s_presence_uuid.u) == 0 &&
            (chr->properties & BLE_GATT_CHR_PROP_READ))
            s_remote_presence = chr->val_handle;
        if (ble_uuid_cmp(&chr->uuid.u, &s_profile_uuid.u) == 0 &&
            (chr->properties & BLE_GATT_CHR_PROP_READ))
            s_remote_profile = chr->val_handle;
        if (ble_uuid_cmp(&chr->uuid.u, &s_stats_uuid.u) == 0 &&
            (chr->properties & BLE_GATT_CHR_PROP_READ) && (chr->properties & BLE_GATT_CHR_PROP_WRITE))
            s_remote_stats = chr->val_handle;
        for (unsigned i = 0; i < 3; ++i)
        {
            if (ble_uuid_cmp(&chr->uuid.u, &uuids[i]->u) == 0 &&
                (chr->properties & BLE_GATT_CHR_PROP_READ) &&
                (chr->properties & BLE_GATT_CHR_PROP_NOTIFY))
            {
                s_remote_defs[i] = chr->def_handle;
                s_remote_values[i] = chr->val_handle;
            }
        }
    }
    else if (error->status == BLE_HS_EDONE && s_remote_values[0] && s_remote_values[1] &&
             s_remote_values[2])
    {
        if (!s_remote_profile)
        {
            slave_fail("a master nem támogat PIN-es profilátadást", -1);
            return 0;
        }
        slave_read_profile();
    }
    else
    {
        slave_fail("adat- vagy időszolgáltatás hiányzik", error->status);
    }
    return 0;
}

static int slave_service(uint16_t conn,
                         const struct ble_gatt_error *error,
                         const struct ble_gatt_svc *svc,
                         void *arg)
{
    (void)arg;
    if (conn != s_master)
        return 0;
    if (error->status == 0)
    {
        s_service_start = svc->start_handle;
        s_service_end = svc->end_handle;
    }
    else if (error->status == BLE_HS_EDONE && s_service_start)
    {
        int rc = ble_gattc_disc_all_chrs(
            conn, s_service_start, s_service_end, slave_characteristic, NULL);
        if (rc)
            slave_fail("karakterisztika-keresés nem indítható", rc);
    }
    else
    {
        slave_fail("szolgáltatás hiányzik", error->status);
    }
    return 0;
}

static int slave_mtu_done(uint16_t connection, const struct ble_gatt_error *error, uint16_t mtu, void *arg)
{
    (void)error; (void)mtu; (void)arg;
    if (connection != s_master) return 0;
    /* Sikertelen MTU-egyeztetésnél a szabványos 23 bájtos MTU is elegendő. */
    if (ble_gattc_disc_svc_by_uuid(s_master, &s_service_uuid.u, slave_service, NULL))
        slave_fail("szolgáltatáskeresés nem indítható", -1);
    return 0;
}

static int slave_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    int security_result = security_gap_event(event);
    if (event->type == BLE_GAP_EVENT_REPEAT_PAIRING)
        return security_result;
    switch (event->type)
    {
        case BLE_GAP_EVENT_DISC:
        {
            if (s_connecting || s_master != BLE_HS_CONN_HANDLE_NONE)
                break;
            struct ble_hs_adv_fields fields;
            if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data))
                break;
            bool found = false;
            for (unsigned i = 0; i < fields.num_uuids128; ++i)
                if (ble_uuid_cmp(&fields.uuids128[i].u, &s_service_uuid.u) == 0)
                    found = true;
            if (!found)
                break;
            s_connecting = true;
            s_master_rssi = event->disc.rssi;
            const uint8_t *a = event->disc.addr.val;
            snprintf(s_master_address,
                     sizeof(s_master_address),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     a[5],
                     a[4],
                     a[3],
                     a[2],
                     a[1],
                     a[0]);
            (void)ble_gap_disc_cancel();
            int rc = ble_gap_connect(
                s_own_addr_type, &event->disc.addr, 8000, NULL, slave_gap_event, NULL);
            if (rc)
                slave_fail("csatlakozás nem indítható", rc);
            break;
        }
        case BLE_GAP_EVENT_CONNECT:
            s_connecting = false;
            if (event->connect.status)
            {
                slave_reset();
                break;
            }
            s_master = event->connect.conn_handle;
            s_setup_deadline_us = esp_timer_get_time() + 45000000;
            if (ble_gap_security_initiate(s_master))
                slave_fail("hitelesítés nem indítható", -1);
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.conn_handle != s_master)
                break;
            if (event->enc_change.status || !security_authenticated(s_master))
            {
                slave_fail("PIN/titkosítás ellenőrzése sikertelen", -1);
                break;
            }
            s_setup_deadline_us = esp_timer_get_time() + 15000000;
            if (ble_gattc_exchange_mtu(s_master, slave_mtu_done, NULL))
                (void)slave_mtu_done(s_master, NULL, 23, NULL);
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            if (event->disconnect.conn.conn_handle == s_master)
                slave_reset();
            break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            s_next_scan_us = esp_timer_get_time() + 3000000;
            break;
        case BLE_GAP_EVENT_NOTIFY_RX:
            if (event->notify_rx.conn_handle == s_master)
                for (unsigned packet = 0; packet < 3; ++packet)
                    if (event->notify_rx.attr_handle == s_remote_values[packet])
                        slave_receive(packet, event->notify_rx.om);
            break;
        default:
            break;
    }
    slave_diagnostics();
    return 0;
}

static void slave_begin_scan(void)
{
    if (!s_synced || s_connecting || s_master != BLE_HS_CONN_HANDLE_NONE || ble_gap_disc_active())
        return;
    struct ble_gap_disc_params params = {0};
    params.itvl = 160;
    params.window = 32;
    params.filter_duplicates = 1;
    int rc = ble_gap_disc(s_own_addr_type, 5000, &params, slave_gap_event, NULL);
    if (rc)
        s_next_scan_us = esp_timer_get_time() + 3000000;
}

static void slave_poll(int64_t now)
{
    if (s_master != BLE_HS_CONN_HANDLE_NONE)
    {
#ifdef APP_INTERCOM_SLAVE
        slave_intercom_poll(now);
        if (!s_intercom_reading && !s_presence_reading && (!slave_intercom_available() || s_intercom_next > now))
#else
        if (!s_presence_reading)
#endif
            slave_stats_poll(now);
        if (s_profile_waiting && app_ble_profile_ready())
        {
            s_profile_waiting = false;
            if (!s_slave_ready)
            {
                s_profile_accepted = true;
                s_setup_packet = 0;
                slave_setup_next();
            }
        }
        if (
#ifdef APP_INTERCOM_SLAVE
            !s_intercom_reading &&
#endif
            s_slave_ready && !s_profile_reading && !s_profile_waiting && !s_presence_reading && !slave_stats_busy() && now >= s_profile_next_read)
            slave_read_profile();
        if ((!s_slave_ready && now >= s_setup_deadline_us) ||
            (s_slave_ready &&
             (now - s_last_frame_us >= 15000000 ||
              (s_last_clock_receive_us && now - s_last_clock_receive_us >= 15000000))))
        {
            slave_fail("adat/idő heartbeat időtúllépés", -1);
            return;
        }
        slave_try_frame(now);
#ifdef APP_INTERCOM_SLAVE
        if (!s_intercom_reading)
#endif
            slave_presence_poll(now);
        int8_t rssi;
        if (ble_gap_conn_rssi(s_master, &rssi) == 0)
            s_master_rssi = rssi;
    }
    else if (now >= s_next_scan_us)
    {
        slave_begin_scan();
    }
}

#include "app_ble_stats_client.inc"
