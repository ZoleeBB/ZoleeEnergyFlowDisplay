/* Minden NimBLE hívás a host taskban történik. A cél kizárólag a jelenlegi,
 * teljes SMP-védelemmel ellenőrzött kapcsolat feloldott identitása. */
static bool presence_identity(uint16_t connection, app_slave_identity_t *identity)
{
    struct ble_gap_conn_desc desc;
    if (!security_authenticated(connection) || ble_gap_conn_find(connection, &desc) != 0)
        return false;
    identity->type = desc.peer_id_addr.type;
    /* A NimBLE identity típusokat public/random alakra normalizáljuk. */
    if (identity->type == BLE_ADDR_PUBLIC_ID)
        identity->type = 0;
    else if (identity->type == BLE_ADDR_RANDOM_ID)
        identity->type = 1;
    memcpy(identity->address, desc.peer_id_addr.val, sizeof(identity->address));
    return app_slave_identity_valid(identity);
}

static void presence_register_peers(void)
{
    if (s_config.data_recept_mode != APP_DATA_LOXONE)
        return;
    for (unsigned i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; ++i)
    {
        app_slave_identity_t identity;
        if (s_peers[i].used && presence_identity(s_peers[i].handle, &identity))
            (void)app_slave_presence_service_register(&identity);
    }
}

static int read_presence(uint16_t conn, uint16_t attr,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    app_slave_identity_t identity;
    if (!presence_identity(conn, &identity))
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    seed_peer_t *peer = NULL;
    for (unsigned i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; ++i)
        if (s_peers[i].used && s_peers[i].handle == conn)
            peer = &s_peers[i];
    if (!peer)
        return BLE_ATT_ERR_UNLIKELY;
    uint8_t data[APP_SLAVE_PRESENCE_PACKET_SIZE];
    if (s_config.data_recept_mode == APP_DATA_LOXONE)
    {
        if (!app_slave_presence_service_register(&identity) ||
            !app_slave_presence_service_packet(&identity, ++peer->presence_sequence, data))
            return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    else
    {
        /* HTTP/MQTT seed mellett az opcionális profil mindig kikapcsolt,
         * miközben a régi energia/Wi-Fi/OTA szolgáltatás tovább működik. */
        app_slave_presence_peer_t disabled = {
            .used = true, .identity = identity, .revision = 1,
            .profile = {.name = "Slave", .timeout_seconds = 30},
        };
        if (!app_slave_presence_encode(&disabled, ++peer->presence_sequence, false, data))
            return BLE_ATT_ERR_UNLIKELY;
    }
    return os_mbuf_append(ctxt->om, data, sizeof(data)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
