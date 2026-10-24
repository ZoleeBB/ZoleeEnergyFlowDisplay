/* Egyetlen ATT művelet egyszerre. A vezérlés megelőzi a nagy statisztikát;
 * profil csak generációváltáskor olvasódik, a heartbeat 250 ms-os. */
static uint16_t s_remote_intercom, s_remote_intercom_profile, s_remote_intercom_extras;
static bool s_intercom_reading;
static unsigned s_intercom_part;
static uint32_t s_intercom_request;
static size_t s_intercom_length;
static int64_t s_intercom_next, s_intercom_deadline;
static uint8_t s_intercom_profile_data[APP_INTERCOM_PROFILE_SIZE];
static uint8_t s_intercom_extras_data[APP_INTERCOM_EXTRAS_SIZE];
static uint8_t s_intercom_last_profile[8];
static uint8_t s_intercom_status_data[APP_INTERCOM_CONTROL_SIZE];

static void slave_intercom_failed(void)
{
    memset(s_intercom_last_profile, 0, sizeof(s_intercom_last_profile));
    app_intercom_control_disconnected();
}

static void slave_intercom_reset(void)
{
    s_remote_intercom = s_remote_intercom_profile = s_remote_intercom_extras = 0;
    s_intercom_reading = false;
    s_intercom_next = 0;
    s_intercom_part = 0;
    memset(s_intercom_status_data, 0, sizeof(s_intercom_status_data));
    ++s_intercom_request;
    memset(s_intercom_profile_data, 0, sizeof(s_intercom_profile_data));
    memset(s_intercom_extras_data, 0, sizeof(s_intercom_extras_data));
    memset(s_intercom_last_profile, 0, sizeof(s_intercom_last_profile));
    slave_intercom_failed();
}

static int slave_intercom_read(uint16_t conn, const struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr, void *arg)
{
    if (conn != s_master || (uint32_t)(uintptr_t)arg != s_intercom_request || !s_intercom_reading) return 0;
    if (!security_authenticated(conn)) { slave_intercom_failed(); return 0; }
    if (esp_timer_get_time() > s_intercom_deadline) {
        slave_intercom_failed();
        if (!s_intercom_part || error->status) {
            s_intercom_reading = false;
            s_intercom_part = 3;
            s_intercom_next = esp_timer_get_time() + 500000;
        }
        return 0;
    }
    if (!s_intercom_part) {
        s_intercom_reading = false;
        uint8_t data[APP_INTERCOM_CONTROL_SIZE];
        if (error->status || !attr || attr->offset || OS_MBUF_PKTLEN(attr->om) != sizeof(data) ||
            os_mbuf_copydata(attr->om, 0, sizeof(data), data) || !app_intercom_control_receive(data, sizeof(data))) {
            slave_intercom_failed(); return 0;
        }
        memcpy(s_intercom_status_data, data, sizeof(data));
        s_intercom_next = esp_timer_get_time() + 500000;
        return 0;
    }
    uint8_t *data = s_intercom_part == 1 ? s_intercom_profile_data : s_intercom_extras_data;
    size_t capacity = s_intercom_part == 1 ? sizeof(s_intercom_profile_data) : sizeof(s_intercom_extras_data);
    if (!error->status && attr) {
        size_t length = OS_MBUF_PKTLEN(attr->om);
        if (attr->offset != s_intercom_length || length > capacity - s_intercom_length ||
            os_mbuf_copydata(attr->om, 0, length, data + s_intercom_length)) {
            slave_intercom_failed(); return 0;
        }
        s_intercom_length += length;
        return 0;
    }
    s_intercom_reading = false;
    if (error->status != BLE_HS_EDONE || s_intercom_length != capacity) {
        memset(s_intercom_profile_data, 0, sizeof(s_intercom_profile_data));
        s_intercom_part = 3;
        slave_intercom_failed();
        return 0;
    }
    if (s_intercom_part == 1) s_intercom_part = 2;
    else {
        if (app_intercom_control_receive_profile(s_intercom_profile_data, s_intercom_extras_data))
            memcpy(s_intercom_last_profile, s_intercom_profile_data + 4, 8);
        memset(s_intercom_profile_data, 0, sizeof(s_intercom_profile_data));
        s_intercom_part = 3;
    }
    s_intercom_next = 0;
    return 0;
}

static int slave_intercom_written(uint16_t conn, const struct ble_gatt_error *error,
                                   struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    if (conn != s_master || (uint32_t)(uintptr_t)arg != s_intercom_request) return 0;
    s_intercom_reading = false;
    app_intercom_control_sent(!error->status);
    s_intercom_next = 0;
    return 0;
}

static void slave_intercom_poll(int64_t now)
{
    if (!s_remote_intercom || !s_remote_intercom_profile || !s_remote_intercom_extras || !s_slave_ready) return;
    if (s_intercom_reading) {
        if (now >= s_intercom_deadline) slave_intercom_failed();
        return;
    }
    if (!security_authenticated(s_master) || s_profile_reading || s_profile_waiting ||
        s_presence_reading || slave_stats_transport_busy() || now < s_intercom_next) return;
    if (++s_intercom_request == 0) ++s_intercom_request;
    s_intercom_deadline = now + 3000000;
    s_intercom_reading = true;
    int rc;
    uint8_t command[APP_INTERCOM_CONTROL_SIZE];
    if (!s_intercom_part && app_intercom_control_outgoing(command)) {
        /* Minden írás után kötelező státuszolvasás: különben a KEEP kiszorítaná
         * a Master visszaigazolását, és a helyi lease tévesen lejárna. */
        s_intercom_next = now + 200000;
        rc = ble_gattc_write_flat(s_master, s_remote_intercom, command, sizeof(command),
                                  slave_intercom_written, (void *)(uintptr_t)s_intercom_request);
        s_intercom_part = 3;
    } else {
        bool force_status = s_intercom_part == 3;
        if (force_status) s_intercom_part = 0;
        if (!force_status && !s_intercom_part && s_intercom_status_data[0] == 1 &&
            memcmp(s_intercom_last_profile, s_intercom_status_data + 4, 8)) s_intercom_part = 1;
        s_intercom_length = 0;
        if (s_intercom_part == 1 || s_intercom_part == 2)
            rc = ble_gattc_read_long(s_master, s_intercom_part == 1 ? s_remote_intercom_profile : s_remote_intercom_extras,
                                      0, slave_intercom_read, (void *)(uintptr_t)s_intercom_request);
        else
            rc = ble_gattc_read(s_master, s_remote_intercom, slave_intercom_read, (void *)(uintptr_t)s_intercom_request);
    }
    if (rc) {
        s_intercom_reading = false;
        s_intercom_part = 0;
        s_intercom_next = now + 500000;
        slave_intercom_failed();
    }
}
