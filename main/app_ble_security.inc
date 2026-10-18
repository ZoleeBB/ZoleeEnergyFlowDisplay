/* A UI és a web csak atomi parancsot ad. Minden SMP művelet a host taskon fut. */
#include <stdatomic.h>

static atomic_uint s_window_end, s_pin_token, s_pin_command, s_pin_command_token;
static atomic_uint s_pin_expiry;
static atomic_bool s_window_request, s_pin_requested;
static uint16_t s_pin_connection = BLE_HS_CONN_HANDLE_NONE;
static int64_t s_pin_deadline;
static unsigned s_pair_failures;

static bool security_authenticated(uint16_t connection)
{
    struct ble_gap_conn_desc desc;
    return ble_gap_conn_find(connection, &desc) == 0 && desc.sec_state.encrypted &&
           desc.sec_state.authenticated && desc.sec_state.bonded && desc.sec_state.key_size == 16;
}

static unsigned pairing_seconds(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);
    int32_t remaining = (int32_t)(atomic_load(&s_window_end) - now);
    return remaining > 0 ? (unsigned)remaining : 0;
}

void app_ble_pairing_status(app_ble_pairing_status_t *out)
{
    if (!out)
        return;
    out->pin_required = atomic_load(&s_pin_requested);
    out->token = atomic_load(&s_pin_token);
    out->seconds = pairing_seconds();
    if (out->pin_required)
    {
        int32_t remaining =
            (int32_t)(atomic_load(&s_pin_expiry) - (uint32_t)(esp_timer_get_time() / 1000000));
        out->seconds = remaining > 0 ? (unsigned)remaining : 0;
    }
}

bool app_ble_pairing_submit(uint32_t token, const char *pin)
{
    if (!atomic_load(&s_pin_requested) || token != atomic_load(&s_pin_token))
        return false;
    unsigned value = 1000001; /* Mégsem. */
    if (pin)
    {
        if (strlen(pin) != 6 || strspn(pin, "0123456789") != 6)
            return false;
        value = 1;
        unsigned number = 0;
        for (unsigned i = 0; i < 6; i++)
            number = number * 10 + (unsigned)(pin[i] - '0');
        value += number;
    }
    atomic_store(&s_pin_command_token, token);
    atomic_store(&s_pin_command, value);
    return true;
}

void app_ble_pairing_open(void)
{
    atomic_store(&s_window_request, true);
}

static void security_clear_request(uint16_t connection)
{
    if (s_pin_connection != connection)
        return;
    s_pin_connection = BLE_HS_CONN_HANDLE_NONE;
    atomic_store(&s_pin_requested, false);
    atomic_store(&s_pin_command, 0);
}

static int security_gap_event(struct ble_gap_event *event)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_REPEAT_PAIRING:
        {
            /* Kulcsot csak szándékosan megnyitott ablakban cserélhetünk. */
            struct ble_gap_conn_desc desc;
            if (slave_mode() || !pairing_seconds() ||
                ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) != 0)
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            if (ble_store_util_delete_peer(&desc.peer_id_addr) != 0)
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_PASSKEY_ACTION:
        {
            uint16_t connection = event->passkey.conn_handle;
            unsigned action = event->passkey.params.action;
            if (!slave_mode() && action == BLE_SM_IOACT_DISP && pairing_seconds() &&
                s_pair_failures < 5)
            {
                ++s_pair_failures; /* A megszakított próbálkozás is fogyasztja a keretet. */
                struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP};
                for (unsigned i = 0; i < 6; i++)
                    io.passkey = io.passkey * 10 + (s_config.data_seed_pin[i] - '0');
                if (ble_sm_inject_io(connection, &io) == 0)
                    break;
            }
            else if (slave_mode() && action == BLE_SM_IOACT_INPUT)
            {
                s_pin_connection = connection;
                s_pin_deadline = esp_timer_get_time() + 28000000;
                atomic_store(&s_pin_expiry, (uint32_t)(s_pin_deadline / 1000000));
                atomic_fetch_add(&s_pin_token, 1);
                atomic_store(&s_pin_command, 0);
                atomic_store(&s_pin_requested, true);
                break;
            }
            (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        case BLE_GAP_EVENT_ENC_CHANGE:
            security_clear_request(event->enc_change.conn_handle);
            if (event->enc_change.status || !security_authenticated(event->enc_change.conn_handle))
            {
                if (slave_mode() && event->enc_change.status)
                {
                    struct ble_gap_conn_desc desc;
                    if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0)
                        (void)ble_store_util_delete_peer(&desc.peer_id_addr);
                }
                if (!slave_mode() && s_pair_failures >= 5)
                    atomic_store(&s_window_end, 0);
                app_events_add(
                    APP_EVENT_WARNING,
                    "BLE hitelesítés sikertelen; ellenőrizd a PIN-t és a párosítási ablakot.");
                (void)ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            security_clear_request(event->disconnect.conn.conn_handle);
            break;
        default:
            break;
    }
    return 0;
}

static void security_poll(int64_t now)
{
    if (atomic_exchange(&s_window_request, false) && !slave_mode())
    {
        s_pair_failures = 0;
        atomic_store(&s_window_end, (uint32_t)(now / 1000000) + 120);
        app_events_add(APP_EVENT_INFO, "Új BLE párosítás engedélyezve 2 percre.");
    }
    if (!atomic_load(&s_pin_requested))
        return;
    unsigned command = atomic_exchange(&s_pin_command, 0);
    if (command && atomic_load(&s_pin_command_token) == atomic_load(&s_pin_token))
    {
        uint16_t connection = s_pin_connection;
        security_clear_request(connection);
        if (command > 1000000)
        {
            (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        }
        else
        {
            struct ble_sm_io io = {.action = BLE_SM_IOACT_INPUT, .passkey = command - 1};
            if (ble_sm_inject_io(connection, &io) != 0)
                (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    else if (now >= s_pin_deadline)
    {
        uint16_t connection = s_pin_connection;
        security_clear_request(connection);
        (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }
}
