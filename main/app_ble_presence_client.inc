/* Az opcionális, 20 bájtos karakterisztika nem változtatja meg a három
 * kötelező energia/idő feliratkozást. Egy egyszerű READ mindig elég MTU=23-mal. */
static uint16_t s_remote_presence;
static uint32_t s_presence_request;
static bool s_presence_reading, s_presence_expired;
static int64_t s_presence_deadline, s_presence_next_read;

static void slave_presence_reset(void)
{
    if (++s_presence_request == 0)
        ++s_presence_request;
    s_remote_presence = 0;
    s_presence_reading = false;
    s_presence_expired = false;
    s_presence_next_read = 0;
    app_slave_presence_service_disconnected();
}

static int slave_presence_read(uint16_t conn, const struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr, void *arg)
{
    if (conn != s_master || (uint32_t)(uintptr_t)arg != s_presence_request ||
        !s_presence_reading)
        return 0;
    s_presence_reading = false;
    bool ok = !s_presence_expired && !error->status && attr && !attr->offset &&
              s_profile_accepted && s_slave_ready && security_authenticated(conn);
    uint8_t data[APP_SLAVE_PRESENCE_PACKET_SIZE];
    bool transport_ok = ok;
    if (ok)
        ok = OS_MBUF_PKTLEN(attr->om) == sizeof(data) &&
             os_mbuf_copydata(attr->om, 0, sizeof(data), data) == 0 &&
             app_slave_presence_service_receive(data, sizeof(data), true);
    if (!transport_ok)
        app_slave_presence_service_disconnected();
    s_presence_next_read = esp_timer_get_time() + (ok ? 1000000 : 5000000);
    return 0;
}

static void slave_presence_poll(int64_t now)
{
    if (!s_remote_presence || !s_slave_ready)
        return;
    if (!security_authenticated(s_master))
    {
        app_slave_presence_service_disconnected();
        return;
    }
    if (s_presence_reading)
    {
        if (!s_presence_expired && now >= s_presence_deadline)
        {
            s_presence_expired = true;
            app_slave_presence_service_disconnected();
        }
        /* Függő ATT-olvasás mellé nem indítunk újabbat; a késői callback
         * már nem adhat át régi jelenlétet. Az energia notify tovább élhet. */
        return;
    }
    if (s_profile_reading || s_profile_waiting || slave_stats_busy() ||
        now < s_presence_next_read)
        return;
    if (++s_presence_request == 0)
        ++s_presence_request;
    s_presence_reading = true;
    s_presence_expired = false;
    s_presence_deadline = now + 4000000;
    int rc = ble_gattc_read(s_master, s_remote_presence, slave_presence_read,
                            (void *)(uintptr_t)s_presence_request);
    if (rc && s_presence_reading)
    {
        s_presence_reading = false;
        s_presence_next_read = now + 5000000;
        app_slave_presence_service_disconnected();
    }
}
