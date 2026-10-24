/* Ezeket a karakterisztikákat kizárólag SC/MITM/bond/16 bájtos kulcs után
 * érhetik el a regisztrált kijelzők. A kapcsolat saját véletlen azonosítót kap,
 * újracsatlakozás nem örökölheti a korábbi hívás engedélyét. */
static seed_peer_t *intercom_peer(uint16_t connection)
{
    app_slave_identity_t identity;
    if (s_config.data_recept_mode != APP_DATA_LOXONE ||
        !presence_identity(connection, &identity) || !app_slave_presence_service_register(&identity)) return NULL;
    for (unsigned i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++) {
        seed_peer_t *peer = &s_peers[i];
        if (!peer->used || peer->handle != connection) continue;
        if (!peer->intercom_id) {
            do { peer->intercom_id = esp_random(); } while (peer->intercom_id < 2);
        }
        return peer;
    }
    return NULL;
}

static int access_intercom(uint16_t connection, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr; (void)arg;
    seed_peer_t *peer = intercom_peer(connection);
    if (!peer) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    uint8_t data[APP_INTERCOM_CONTROL_SIZE];
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        app_intercom_control_status(peer->intercom_id, data);
        return os_mbuf_append(ctxt->om, data, sizeof(data)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || OS_MBUF_PKTLEN(ctxt->om) != sizeof(data) ||
        os_mbuf_copydata(ctxt->om, 0, sizeof(data), data)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    return app_intercom_control_command(peer->intercom_id, data, sizeof(data)) ? 0 : BLE_ATT_ERR_UNLIKELY;
}

static int access_intercom_profile(uint16_t connection, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr;
    if (!intercom_peer(connection)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    unsigned part = (unsigned)(uintptr_t)arg;
    uint8_t data[APP_INTERCOM_PROFILE_SIZE];
    if (!app_intercom_control_profile(part, data)) return BLE_ATT_ERR_UNLIKELY;
    int rc = os_mbuf_append(ctxt->om, data, part ? APP_INTERCOM_EXTRAS_SIZE : APP_INTERCOM_PROFILE_SIZE);
    memset(data, 0, sizeof(data));
    return rc ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
