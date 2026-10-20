/* A tényleges NVS-worker, konfiguráció és receiver hibainjektálással.
 * Az NVS-mock csak sikeres commit után teszi tartóssá az új blobot. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../main/app_slave_presence_service.c"

static uint8_t stored[PRESENCE_STORE_SIZE], staged[PRESENCE_STORE_SIZE];
static bool have_stored, queued, locked;
static bool fail_open, fail_set, fail_commit, fail_mutex, fail_queue, fail_task;
static unsigned writes, commits, opens, mutex_frees, queue_frees;
static int64_t now_us;
static jmp_buf worker_return;

uint32_t esp_random(void) { return 1234567; }
int64_t esp_timer_get_time(void) { return now_us; }
void app_events_add(app_event_level_t level, const char *format, ...)
{
    (void)level;
    (void)format;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(!locked && !strcmp(name, "ble_presence"));
    assert(mode == NVS_READONLY || mode == NVS_READWRITE);
    ++opens;
    if (fail_open)
        return ESP_FAIL;
    *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size)
{
    (void)handle;
    assert(!locked && !strcmp(key, "peers") && *size == sizeof(stored));
    if (!have_stored)
        return ESP_ERR_NVS_NOT_FOUND;
    memcpy(out, stored, sizeof(stored));
    *size = sizeof(stored);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    (void)handle;
    assert(!locked && !strcmp(key, "peers") && size == sizeof(stored));
    ++writes;
    if (fail_set)
        return ESP_FAIL;
    memcpy(staged, data, size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    assert(!locked);
    ++commits;
    if (fail_commit)
        return ESP_FAIL;
    memcpy(stored, staged, sizeof(stored));
    have_stored = true;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; assert(!locked); }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return fail_mutex ? NULL : (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{
    assert(mutex && (!locked || timeout == 0));
    if (locked)
        return 0;
    locked = true;
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex && locked);
    locked = false;
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t mutex) { assert(mutex && !locked); ++mutex_frees; }
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{
    assert(count == 1 && size == 1);
    return fail_queue ? NULL : (void *)2;
}
void vQueueDelete(QueueHandle_t queue) { assert(queue); ++queue_frees; }
int xQueueOverwrite(QueueHandle_t queue, const void *data)
{
    assert(queue && *(const uint8_t *)data == 1);
    queued = true;
    return pdPASS;
}
int xQueueReceive(QueueHandle_t queue, void *data, TickType_t timeout)
{
    assert(queue && !locked && timeout == 5000);
    if (!queued)
        longjmp(worker_return, 1);
    *(uint8_t *)data = 1;
    queued = false;
    return pdTRUE;
}
int xTaskCreate(void (*function)(void *), const char *name, unsigned stack,
                 void *arg, unsigned priority, void *handle)
{
    assert(function == presence_save_task && !strcmp(name, "presence_nvs") && stack == 4096);
    (void)arg;
    (void)priority;
    (void)handle;
    return fail_task ? 0 : pdPASS;
}
static void worker_step(void)
{
    queued = true; /* A háttértask következő értesítése vagy 5 s-os ébredése. */
    if (!setjmp(worker_return))
        presence_save_task(NULL);
}
static void cold_boot(void)
{
    atomic_store(&s_presence_ready, false);
    s_presence_mutex = NULL;
    s_presence_queue = NULL;
    queued = false;
    locked = false;
}
static const app_slave_identity_t first = {0, {1, 2, 3, 4, 5, 6}};
static const app_slave_identity_t second = {0, {2, 2, 3, 4, 5, 6}};

#include "app_config.h"
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 3
#define BLE_ADDR_PUBLIC_ID 2
#define BLE_ADDR_RANDOM_ID 3
#define BLE_GATT_ACCESS_OP_READ_CHR 1
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 3
#define BLE_ATT_ERR_INSUFFICIENT_AUTHEN 5
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
struct os_mbuf { size_t length; uint8_t data[64]; };
struct ble_gatt_access_ctxt { unsigned op; struct os_mbuf *om; };
struct ble_gap_conn_desc { struct { uint8_t type, val[6]; } peer_id_addr; };
typedef struct { bool used; uint16_t handle; uint32_t presence_sequence; } seed_peer_t;
static seed_peer_t s_peers[3] = {
    {.used = true, .handle = 7}, {.used = true, .handle = 8}, {.used = true, .handle = 9},
};
static app_config_t s_config;
static bool authenticated = true, private_address, append_fail;
static bool security_authenticated(uint16_t connection)
{
    return connection >= 7 && connection <= 9 && authenticated;
}
static int ble_gap_conn_find(uint16_t connection, struct ble_gap_conn_desc *desc)
{
    if (connection < 7 || connection > 9)
        return -1;
    desc->peer_id_addr.type = private_address ? 1 : BLE_ADDR_PUBLIC_ID;
    const uint8_t address[6] = {(uint8_t)(connection - 6), 2, 3, 4, 5, 6};
    memcpy(desc->peer_id_addr.val, address, 6);
    return 0;
}
static int os_mbuf_append(struct os_mbuf *out, const void *data, size_t size)
{
    if (append_fail || out->length + size > sizeof(out->data))
        return -1;
    memcpy(out->data + out->length, data, size);
    out->length += size;
    return 0;
}
#include "../main/app_ble_presence_server.inc"

static void server_tests(void)
{
    s_config.data_recept_mode = APP_DATA_LOXONE;
    presence_register_peers();
    struct os_mbuf out = {0};
    struct ble_gatt_access_ctxt ctx = {BLE_GATT_ACCESS_OP_WRITE_CHR, &out};
    assert(read_presence(7, 1006, &ctx, NULL) == BLE_ATT_ERR_WRITE_NOT_PERMITTED);
    ctx.op = BLE_GATT_ACCESS_OP_READ_CHR;
    authenticated = false;
    assert(read_presence(7, 1006, &ctx, NULL) == BLE_ATT_ERR_INSUFFICIENT_AUTHEN && !out.length);
    authenticated = true;
    assert(read_presence(7, 1006, &ctx, NULL) == 0 && out.length == 20);
    assert((out.data[3] & 1) == 1);
    assert(out.data[16] == 1);
    out.length = 0;
    assert(read_presence(8, 1006, &ctx, (void *)1) == 0 && out.length == 20);
    assert((out.data[3] & 1) == 0); /* A kliens argja nem választhat másik profilt. */
    private_address = true;
    out.length = 0;
    assert(read_presence(7, 1006, &ctx, NULL) == BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
    private_address = false;
    append_fail = true;
    assert(read_presence(7, 1006, &ctx, NULL) == BLE_ATT_ERR_INSUFFICIENT_RES);
    append_fail = false;
    s_config.data_recept_mode = APP_DATA_HTTP;
    assert(read_presence(7, 1006, &ctx, NULL) == 0 && out.data[3] == 0);
}

static void codec_tests(void)
{
    app_slave_presence_table_t table = {0};
    assert(app_slave_presence_register(&table, &first, "Háló") == 0);
    app_slave_presence_profile_t profile = table.peers[0].profile;
    profile.blank_at_night = true;
    memcpy(profile.presence_uuid, "01234567-89ab-cdef-0123456789abcdef", 36);
    assert(app_slave_presence_update(&table, 0, &profile, NULL));
    table.peers[0].counter = 1000;
    table.peers[0].valid = table.peers[0].active = true;
    uint8_t wire[PRESENCE_STORE_SIZE];
    assert(store_encode(&table, wire));
    app_slave_presence_table_t decoded = {0};
    assert(store_decode(wire, sizeof(wire), &decoded));
    assert(decoded.peers[0].profile.blank_at_night && decoded.peers[0].revision == 2);
    assert(!decoded.peers[0].valid && !decoded.peers[0].active && !decoded.peers[0].counter);
    app_slave_presence_table_t saved = decoded;
    for (unsigned i = 0; i < sizeof(wire); ++i)
    {
        wire[i] ^= 1;
        assert(!store_decode(wire, sizeof(wire), &decoded));
        assert(!memcmp(&saved, &decoded, sizeof(saved)));
        wire[i] ^= 1;
    }
    assert(!store_decode(wire, sizeof(wire) - 1, &decoded));
    /* Érvényes CRC sem tesz érvényessé duplikált identitást vagy rejtett adatot. */
    memcpy(wire + 4 + PRESENCE_STORE_ENTRY_SIZE, wire + 4, PRESENCE_STORE_ENTRY_SIZE);
    store_write_u32(wire + sizeof(wire) - 4, store_crc(wire, sizeof(wire) - 4));
    assert(!store_decode(wire, sizeof(wire), &decoded));
    assert(store_encode(&table, wire));
    wire[4 + PRESENCE_STORE_ENTRY_SIZE + 10] = 1;
    store_write_u32(wire + sizeof(wire) - 4, store_crc(wire, sizeof(wire) - 4));
    assert(!store_decode(wire, sizeof(wire), &decoded));
}

int main(void)
{
    codec_tests();
    fail_open = true;
    assert(app_slave_presence_service_init(true) == ESP_FAIL);
    fail_open = false;
    fail_mutex = true;
    assert(app_slave_presence_service_init(true) == ESP_ERR_NO_MEM);
    fail_mutex = false;
    fail_queue = true;
    assert(app_slave_presence_service_init(true) == ESP_ERR_NO_MEM && mutex_frees == 1);
    fail_queue = false;
    fail_task = true;
    assert(app_slave_presence_service_init(true) == ESP_ERR_NO_MEM && queue_frees == 1);
    fail_task = false;
    assert(app_slave_presence_service_init(true) == ESP_OK);
    assert(app_slave_presence_service_register(&first));
    assert(app_slave_presence_service_register(&second));
    app_slave_presence_snapshot_t snapshot;
    assert(app_slave_presence_service_snapshot(&snapshot));
    assert(snapshot.revision != snapshot.saved_revision && !writes);
    worker_step();
    assert(writes == 1 && commits == 1);
    assert(app_slave_presence_service_snapshot(&snapshot));
    assert(snapshot.revision == snapshot.saved_revision && !snapshot.save_error);
    unsigned before = writes;
    assert(app_slave_presence_service_register(&first));
    worker_step();
    assert(writes == before);
    app_slave_presence_profile_t profile = snapshot.table.peers[0].profile;
    profile.blank_at_night = true;
    profile.timeout_seconds = 5;
    memcpy(profile.presence_uuid, "01234567-89ab-cdef-0123456789abcdef", 36);
    assert(!app_slave_presence_service_update(&first, &profile, snapshot.revision - 1));
    assert(app_slave_presence_service_update(&first, &profile, snapshot.revision));
    worker_step();
    assert(writes == before + 1);
    assert(app_slave_presence_service_snapshot(&snapshot));
    assert(app_slave_presence_service_update(&first, &profile, snapshot.revision));
    worker_step();
    assert(writes == before + 1);
    app_slave_presence_service_online(true);
    app_slave_presence_service_source(profile.presence_uuid, true, 1);
    app_slave_presence_service_source(profile.presence_uuid, true, 0);
    uint8_t packet[APP_SLAVE_PRESENCE_PACKET_SIZE];
    assert(app_slave_presence_service_packet(&first, 1, packet) && packet[3] == 3);
    app_slave_presence_receiver_t receiver = {0};
    assert(app_slave_presence_receive(&receiver, packet, sizeof(packet), true, 0));
    assert(receiver.counter == 1);
    assert(app_slave_presence_service_packet(&second, 1, packet) && packet[3] == 0);
    assert(app_slave_presence_service_snapshot(&snapshot));
    assert(!snapshot.table.peers[0].valid && !snapshot.table.peers[0].counter);
    worker_step();
    assert(writes == before + 1); /* Jelenlét, snapshot és heartbeat nem ír flash-t. */

    profile.timeout_seconds = 6;
    assert(app_slave_presence_service_update(&first, &profile, snapshot.revision));
    fail_commit = true;
    worker_step();
    assert(app_slave_presence_service_snapshot(&snapshot));
    assert(snapshot.save_error == ESP_FAIL && snapshot.saved_revision != snapshot.revision);
    fail_commit = false;
    cold_boot();
    assert(app_slave_presence_service_init(true) == ESP_OK);
    assert(app_slave_presence_service_snapshot(&snapshot));
    assert(snapshot.table.peers[0].profile.timeout_seconds == 5);
    assert(!snapshot.table.peers[0].counter && !snapshot.table.peers[0].valid);
    assert(app_slave_presence_service_init(false) == ESP_ERR_INVALID_STATE);

    server_tests();

    /* Sérült NVS-ből nincs aktív profil; a meglévő más névterekhez nem nyúlunk. */
    stored[20] ^= 1;
    cold_boot();
    assert(app_slave_presence_service_init(true) == ESP_OK);
    assert(app_slave_presence_service_snapshot(&snapshot));
    assert(!snapshot.table.peers[0].used && snapshot.save_error != ESP_OK);

    cold_boot();
    assert(app_slave_presence_service_init(false) == ESP_OK);
    app_slave_presence_peer_t peer = {
        .used = true, .identity = {0, {1, 2, 3, 4, 5, 6}}, .revision = 1,
        .profile = {.name = "Slave", .timeout_seconds = 5, .blank_at_night = true},
    };
    app_slave_display_profile_t own = {83, 17, false, 30};
    app_slave_display_t display;
    assert(app_slave_display_init(&display, &own, 0));
    app_slave_display_night(&display, true, 0);
    assert(app_slave_presence_encode(&peer, 1, false, packet));
    assert(!app_slave_presence_service_receive(packet, sizeof(packet), false));
    assert(app_slave_presence_service_receive(packet, sizeof(packet), true));
    app_slave_presence_service_apply(&display, 0);
    assert(app_slave_display_brightness(&display, 0) == 0);
    assert(app_slave_display_pointer(&display, true, 1));
    assert(app_slave_display_pointer(&display, false, 2));
    assert(app_slave_display_brightness(&display, 3) == 17);
    app_slave_presence_service_disconnected();
    app_slave_presence_service_apply(&display, 6000000);
    assert(display.profile.blank_at_night && app_slave_display_brightness(&display, 6000000) == 0);
    app_slave_presence_service_legacy();
    app_slave_presence_service_apply(&display, 7000000);
    assert(!display.profile.blank_at_night && display.profile.day_brightness == 83);
    assert(app_slave_display_brightness(&display, 7000000) == 17);
    printf("PASS: actual presence NVS worker, canonical %u-byte CRC blob, coalescing, "
           "auth receiver, commit/OOM/corruption, reboot, no flash per activity\n", (unsigned)PRESENCE_STORE_SIZE);
    return 0;
}
