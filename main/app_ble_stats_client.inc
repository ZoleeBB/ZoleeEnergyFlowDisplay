/* Egyetlen soros, időkorlátos transzfer. A részleges puffer soha nem kerül UI-ra. */
static uint8_t *s_stats_wire;
static uint8_t s_stats_page_data[APP_BLE_STATS_PAGE_MAX];
static size_t s_stats_page_length;
static uint16_t s_stats_page_index;
static uint32_t s_stats_transfer_id;
static bool s_stats_active, s_stats_missing_reported;
static bool s_stats_inflight;
static int64_t s_stats_next_us, s_stats_deadline_us;

static void slave_stats_next(void);
#ifdef APP_INTERCOM_SLAVE
static bool s_stats_page_pending;
static bool slave_stats_transport_busy(void) { return s_stats_inflight; }
#endif
static bool slave_stats_busy(void)
{
    return s_stats_active || s_stats_inflight;
}

static void slave_stats_reset(void)
{
    free(s_stats_wire);
    s_stats_wire = NULL;
    s_stats_active = false;
    s_stats_inflight = false;
    s_stats_missing_reported = false;
    ++s_stats_transfer_id;
    s_stats_next_us = 0;
    app_stats_remote_disconnected();
}

static void slave_stats_failed(void)
{
    s_stats_active = false;
    s_stats_next_us = esp_timer_get_time() + 5000000;
    free(s_stats_wire);
    s_stats_wire = NULL;
    /* Az élő BLE adat/idő kapcsolatot statisztikai hiba miatt nem bontjuk. */
}

static int slave_stats_read(uint16_t conn,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg)
{
    if (conn != s_master || (uint32_t)(uintptr_t)arg != s_stats_transfer_id)
        return 0;
    if (error->status)
        s_stats_inflight = false;
    if (!s_stats_active)
        return 0;
    if (!security_authenticated(conn))
    {
        slave_stats_failed();
        return 0;
    }
    if (error->status == 0 && attr)
    {
        size_t length = OS_MBUF_PKTLEN(attr->om);
        if (attr->offset != s_stats_page_length ||
            length > sizeof(s_stats_page_data) - s_stats_page_length ||
            os_mbuf_copydata(attr->om, 0, length, s_stats_page_data + s_stats_page_length) != 0)
        {
            slave_stats_failed();
            return 0;
        }
        s_stats_page_length += length;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || !app_ble_stats_page_decode(s_stats_page_data,
                                                                    s_stats_page_length,
                                                                    s_stats_transfer_id,
                                                                    s_stats_page_index,
                                                                    s_stats_wire))
    {
        slave_stats_failed();
        return 0;
    }
    if (++s_stats_page_index < APP_BLE_STATS_PAGES)
    {
        slave_stats_next();
        return 0;
    }
    time_t now = time(NULL);
    struct tm local;
    char date[11];
    localtime_r(&now, &local);
    strftime(date, sizeof(date), "%Y-%m-%d", &local);
    app_stats_snapshot_t *snapshot = calloc(1, sizeof(*snapshot));
    bool valid =
        snapshot &&
        app_ble_stats_decode(
            s_stats_wire, APP_BLE_STATS_SIZE, s_stats_transfer_id, (uint32_t)now, date, snapshot) &&
        app_stats_remote_publish(snapshot);
    free(snapshot);
    if (!valid)
    {
        slave_stats_failed();
        return 0;
    }
    s_stats_active = false;
    s_stats_next_us = esp_timer_get_time() + 30000000;
    free(s_stats_wire);
    s_stats_wire = NULL;
    return 0;
}

static int slave_stats_written(uint16_t conn,
                               const struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr,
                               void *arg)
{
    (void)attr;
    if (conn != s_master || (uint32_t)(uintptr_t)arg != s_stats_transfer_id)
        return 0;
    s_stats_inflight = false;
    if (!s_stats_active)
        return 0;
    if (error->status)
    {
        slave_stats_failed();
        return 0;
    }
    s_stats_page_length = 0;
    s_stats_inflight = true;
    if (ble_gattc_read_long(conn, s_remote_stats, 0, slave_stats_read, arg))
    {
        s_stats_inflight = false;
        slave_stats_failed();
    }
    return 0;
}

static void slave_stats_next(void)
{
#ifdef APP_INTERCOM_SLAVE
    if (s_remote_intercom && (s_intercom_reading || esp_timer_get_time() >= s_intercom_next)) {
        s_stats_page_pending = true;
        return;
    }
    s_stats_page_pending = false;
#endif
    uint8_t command[8];
    app_ble_stats_request(
        command, s_stats_page_index == 0, s_stats_page_index, s_stats_transfer_id);
    s_stats_inflight = true;
    if (ble_gattc_write_flat(s_master,
                             s_remote_stats,
                             command,
                             sizeof(command),
                             slave_stats_written,
                             (void *)(uintptr_t)s_stats_transfer_id))
    {
        s_stats_inflight = false;
        slave_stats_failed();
    }
}

static void slave_stats_poll(int64_t now)
{
    if (!s_config.cardless_slave || !s_slave_ready || !app_stats_is_ready())
        return;
    if (!s_remote_stats)
    {
        if (!s_stats_missing_reported)
        {
            app_events_add(
                APP_EVENT_WARNING,
                "A master nem támogat Rendszer-statisztikaszinkront; frissítsd a mastert.");
            s_stats_missing_reported = true;
        }
        return;
    }
    if (s_stats_active)
    {
        if (now >= s_stats_deadline_us)
            slave_stats_failed();
#ifdef APP_INTERCOM_SLAVE
        else if (s_stats_page_pending && !s_stats_inflight)
            slave_stats_next();
#endif
        return;
    }
    if (s_stats_inflight)
        return;
    if (s_profile_reading || s_profile_waiting || now < s_stats_next_us)
        return;
    if (!security_authenticated(s_master))
        return;
    app_stats_remote_source(s_master_address);
    s_stats_wire = malloc(APP_BLE_STATS_SIZE);
    if (!s_stats_wire)
    {
        s_stats_next_us = now + 5000000;
        return;
    }
    memset(s_stats_wire, 0, APP_BLE_STATS_SIZE);
    if (++s_stats_transfer_id == 0)
        ++s_stats_transfer_id;
    s_stats_page_index = 0;
    s_stats_active = true;
    s_stats_deadline_us = now + 30000000;
    slave_stats_next();
}
