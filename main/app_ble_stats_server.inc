/* A GATT-írás csak egy kapcsolatonkénti olvasási kurzort állít, fájlt nem módosít.
 * A statisztikai mutex rövid RAM-másolást véd, SD I/O ezen az útvonalon nincs. */
static int
access_stats(uint16_t connection, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr;
    (void)arg;
    if (!security_authenticated(connection))
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    seed_peer_t *peer = NULL;
    for (unsigned i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
        if (s_peers[i].used && s_peers[i].handle == connection)
            peer = &s_peers[i];
    if (!peer)
        return BLE_ATT_ERR_UNLIKELY;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint8_t command[8];
        bool start;
        uint16_t page;
        uint32_t id;
        if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(command) ||
            os_mbuf_copydata(ctxt->om, 0, sizeof(command), command) != 0 ||
            !app_ble_stats_request_decode(command, sizeof(command), &start, &page, &id))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        int64_t now = esp_timer_get_time();
        if (start && (!peer->stats_wire || peer->stats_id != id))
        {
            if (!app_stats_is_ready() ||
                (peer->stats_wire && now - peer->stats_started_us < 3000000))
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            uint8_t *wire = malloc(APP_BLE_STATS_SIZE);
            app_stats_snapshot_t *snapshot = calloc(1, sizeof(*snapshot));
            bool valid = wire && snapshot;
            if (valid)
            {
                app_stats_get_snapshot(snapshot);
                valid = app_ble_stats_encode(snapshot, id, (uint32_t)time(NULL), wire);
            }
            free(snapshot);
            if (!valid)
            {
                free(wire);
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            free(peer->stats_wire);
            peer->stats_wire = wire;
            peer->stats_id = id;
            peer->stats_started_us = now;
        }
        if (!peer->stats_wire || peer->stats_id != id || now - peer->stats_started_us > 45000000)
            return BLE_ATT_ERR_UNLIKELY;
        peer->stats_page = page;
        return 0;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    if (!peer->stats_wire || esp_timer_get_time() - peer->stats_started_us > 45000000)
        return BLE_ATT_ERR_UNLIKELY;
    uint8_t page[APP_BLE_STATS_PAGE_MAX];
    size_t length = app_ble_stats_page(peer->stats_wire, peer->stats_id, peer->stats_page, page);
    if (!length)
        return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, page, length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
