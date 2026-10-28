/* Az állapotzár sosem tart NVS/LVGL/BLE I/O-t. Csak a háttértask ír flash-t,
 * és csak a konfiguráció revízióját követi, nem a jelenlétmintákat. */
#include "app_slave_presence_service.h"
#include "app_events.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define PRESENCE_STORE_ENTRY_SIZE 134
#define PRESENCE_STORE_SIZE (8 + APP_SLAVE_PRESENCE_MAX_PEERS * PRESENCE_STORE_ENTRY_SIZE)

static SemaphoreHandle_t s_presence_mutex;
static QueueHandle_t s_presence_queue;
static atomic_bool s_presence_ready;
static bool s_presence_master, s_presence_online;
static int64_t s_presence_online_us;
static uint32_t s_presence_source_revision;
static app_slave_presence_snapshot_t s_presence_state;
static app_slave_presence_receiver_t s_presence_receiver;

static uint32_t store_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void store_write_u32(uint8_t *data, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        data[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t store_crc(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return ~crc;
}

static bool store_encode(const app_slave_presence_table_t *table,
                          uint8_t out[PRESENCE_STORE_SIZE])
{
    memset(out, 0, PRESENCE_STORE_SIZE);
    out[0] = 'Z';
    out[1] = 'S';
    out[2] = 1;
    out[3] = APP_SLAVE_PRESENCE_MAX_PEERS;
    for (unsigned i = 0; i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
    {
        const app_slave_presence_peer_t *peer = &table->peers[i];
        if (!peer->used)
            continue;
        if (!peer->revision || !app_slave_identity_valid(&peer->identity) ||
            !app_slave_presence_profile_valid(&peer->profile))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (table->peers[j].used &&
                table->peers[j].identity.type == peer->identity.type &&
                !memcmp(table->peers[j].identity.address, peer->identity.address, 6))
                return false;
        uint8_t *entry = out + 4 + i * PRESENCE_STORE_ENTRY_SIZE;
        entry[0] = 1;
        entry[1] = peer->identity.type;
        memcpy(entry + 2, peer->identity.address, 6);
        memcpy(entry + 8, peer->profile.name, strlen(peer->profile.name));
        memcpy(entry + 89, peer->profile.presence_uuid, strlen(peer->profile.presence_uuid));
        store_write_u32(entry + 125, peer->profile.timeout_seconds);
        entry[129] = peer->profile.blank_at_night ? 1 : 0;
        store_write_u32(entry + 130, peer->revision);
    }
    store_write_u32(out + PRESENCE_STORE_SIZE - 4,
                     store_crc(out, PRESENCE_STORE_SIZE - 4));
    return true;
}

static bool store_decode(const uint8_t *data, size_t size, app_slave_presence_table_t *out)
{
    if (size != PRESENCE_STORE_SIZE || data[0] != 'Z' || data[1] != 'S' ||
        data[2] != 1 || data[3] != APP_SLAVE_PRESENCE_MAX_PEERS ||
        store_u32(data + size - 4) != store_crc(data, size - 4))
        return false;
    app_slave_presence_table_t table = {0};
    for (unsigned i = 0; i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
    {
        const uint8_t *entry = data + 4 + i * PRESENCE_STORE_ENTRY_SIZE;
        if (!entry[0])
            continue;
        if (entry[0] != 1 || entry[129] > 1)
            return false;
        app_slave_presence_peer_t *peer = &table.peers[i];
        peer->used = true;
        peer->identity.type = entry[1];
        memcpy(peer->identity.address, entry + 2, 6);
        memcpy(peer->profile.name, entry + 8, APP_SLAVE_PRESENCE_NAME_SIZE);
        memcpy(peer->profile.presence_uuid, entry + 89, APP_SLAVE_PRESENCE_UUID_SIZE);
        peer->profile.timeout_seconds = store_u32(entry + 125);
        peer->profile.blank_at_night = entry[129] != 0;
        peer->revision = store_u32(entry + 130);
    }
    uint8_t canonical[PRESENCE_STORE_SIZE];
    if (!store_encode(&table, canonical) || memcmp(data, canonical, sizeof(canonical)))
        return false;
    *out = table;
    return true;
}

static void presence_save_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        uint8_t wake;
        /* Hiba után legfeljebb 5 másodpercenként újrapróbál; a változatlan,
         * már mentett profil nem nyitja meg az NVS-t sem. */
        (void)xQueueReceive(s_presence_queue, &wake, pdMS_TO_TICKS(5000));
        app_slave_presence_table_t table;
        uint32_t revision;
        xSemaphoreTake(s_presence_mutex, portMAX_DELAY);
        revision = s_presence_state.revision;
        bool dirty = revision != s_presence_state.saved_revision;
        table = s_presence_state.table;
        xSemaphoreGive(s_presence_mutex);
        if (!dirty)
            continue;
        uint8_t data[PRESENCE_STORE_SIZE];
        esp_err_t error = ESP_ERR_INVALID_ARG;
        if (store_encode(&table, data))
        {
            nvs_handle_t handle;
            error = nvs_open("ble_presence", NVS_READWRITE, &handle);
            if (error == ESP_OK)
            {
                error = nvs_set_blob(handle, "peers", data, sizeof(data));
                if (error == ESP_OK)
                    error = nvs_commit(handle);
                nvs_close(handle);
            }
        }
        xSemaphoreTake(s_presence_mutex, portMAX_DELAY);
        if (error == ESP_OK)
            s_presence_state.saved_revision = revision;
        bool report = s_presence_state.save_error != error;
        s_presence_state.save_error = error;
        xSemaphoreGive(s_presence_mutex);
        if (report && error != ESP_OK)
            app_events_add(APP_EVENT_ERROR, "Slave jelenlétprofil NVS-mentése sikertelen.");
    }
}

esp_err_t app_slave_presence_service_init(bool master)
{
    if (atomic_load(&s_presence_ready))
        return s_presence_master == master ? ESP_OK : ESP_ERR_INVALID_STATE;
    app_slave_presence_snapshot_t state = {0};
    state.revision = esp_random();
    if (!state.revision)
        state.revision = 1;
    state.saved_revision = state.revision;
    if (master)
    {
        nvs_handle_t handle;
        esp_err_t error = nvs_open("ble_presence", NVS_READONLY, &handle);
        if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND)
            return error;
        if (error == ESP_OK)
        {
            uint8_t data[PRESENCE_STORE_SIZE];
            size_t size = sizeof(data);
            error = nvs_get_blob(handle, "peers", data, &size);
            nvs_close(handle);
            bool decoded = error == ESP_OK && store_decode(data, size, &state.table);
            if (!decoded && error != ESP_ERR_NVS_NOT_FOUND)
            {
                /* Sérült profilból nem lehet aktív sötétítés vagy idegen célzás. */
                state.save_error = error == ESP_OK ? ESP_ERR_INVALID_ARG : error;
                app_events_add(APP_EVENT_WARNING, "Sérült Slave jelenlétprofil; sötétítés kikapcsolva.");
            }
        }
    }
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (!mutex)
        return ESP_ERR_NO_MEM;
    QueueHandle_t queue = master ? xQueueCreate(1, sizeof(uint8_t)) : NULL;
    if (master && !queue)
    {
        vSemaphoreDelete(mutex);
        return ESP_ERR_NO_MEM;
    }
    s_presence_master = master;
    s_presence_state = state;
    memset(&s_presence_receiver, 0, sizeof(s_presence_receiver));
    s_presence_online = false;
    s_presence_online_us = 0;
    s_presence_source_revision = 0;
    s_presence_mutex = mutex;
    s_presence_queue = queue;
    if (master && xTaskCreate(presence_save_task, "presence_nvs", 3072, NULL, 3, NULL) != pdPASS)
    {
        vQueueDelete(queue);
        vSemaphoreDelete(mutex);
        s_presence_queue = NULL;
        s_presence_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    atomic_store(&s_presence_ready, true);
    return ESP_OK;
}

static bool service_lock(bool master)
{
    if (!atomic_load(&s_presence_ready) || s_presence_master != master)
        return false;
    return xSemaphoreTake(s_presence_mutex, portMAX_DELAY) == pdTRUE;
}

static void config_changed(void)
{
    if (++s_presence_state.revision == 0)
        ++s_presence_state.revision;
    uint8_t wake = 1;
    (void)xQueueOverwrite(s_presence_queue, &wake);
}

bool app_slave_presence_service_register(const app_slave_identity_t *identity)
{
    if (!app_slave_identity_valid(identity) || !service_lock(true))
        return false;
    bool known = app_slave_presence_find(&s_presence_state.table, identity) >= 0;
    char name[32];
    snprintf(name, sizeof(name), "Slave %02x%02x%02x",
             (unsigned)identity->address[2], (unsigned)identity->address[1],
             (unsigned)identity->address[0]);
    int index = app_slave_presence_register(&s_presence_state.table, identity, name);
    if (index >= 0 && !known)
        config_changed();
    xSemaphoreGive(s_presence_mutex);
    return index >= 0;
}

bool app_slave_presence_service_snapshot(app_slave_presence_snapshot_t *out)
{
    if (!out || !service_lock(true))
        return false;
    *out = s_presence_state;
    /* A web nem kap kapcsolati aktivitást vagy impulzusszámlálót konfigurációként. */
    app_slave_presence_invalidate(&out->table);
    for (unsigned i = 0; i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
        out->table.peers[i].counter = 0;
    xSemaphoreGive(s_presence_mutex);
    return true;
}

bool app_slave_presence_service_update(const app_slave_identity_t *identity,
                                       const app_slave_presence_profile_t *profile,
                                       uint32_t expected_revision)
{
    if (!app_slave_presence_profile_valid(profile) || !service_lock(true))
        return false;
    int index = app_slave_presence_find(&s_presence_state.table, identity);
    bool changed = false;
    bool source_changed = index >= 0 && strcmp(s_presence_state.table.peers[index].profile.presence_uuid,
                                               profile->presence_uuid) != 0;
    bool ok = index >= 0 && expected_revision == s_presence_state.revision &&
              app_slave_presence_update(&s_presence_state.table, (unsigned)index, profile, &changed);
    if (ok && (changed || s_presence_state.save_error != ESP_OK))
        config_changed();
    if (ok && source_changed)
        ++s_presence_source_revision;
    xSemaphoreGive(s_presence_mutex);
    return ok;
}

bool app_slave_presence_service_packet(const app_slave_identity_t *identity,
                                       uint32_t sequence,
                                       uint8_t out[APP_SLAVE_PRESENCE_PACKET_SIZE])
{
    if (!out || !service_lock(true))
        return false;
    int index = app_slave_presence_find(&s_presence_state.table, identity);
    int64_t now = esp_timer_get_time();
    bool live = s_presence_online && now >= s_presence_online_us &&
                now - s_presence_online_us < INT64_C(45000000);
    bool ok = index >= 0 && app_slave_presence_encode(&s_presence_state.table.peers[index],
                                                      sequence, live, out);
    xSemaphoreGive(s_presence_mutex);
    return ok;
}

void app_slave_presence_service_source(const char *uuid, bool valid, double value)
{
    if (!service_lock(true))
        return;
    if (s_presence_online)
        (void)app_slave_presence_source(&s_presence_state.table, uuid, valid, value);
    xSemaphoreGive(s_presence_mutex);
}

void app_slave_presence_service_online(bool online)
{
    if (!service_lock(true))
        return;
    s_presence_online = online;
    s_presence_online_us = esp_timer_get_time();
    if (!online)
        app_slave_presence_invalidate(&s_presence_state.table);
    xSemaphoreGive(s_presence_mutex);
}

uint32_t app_slave_presence_service_source_revision(void)
{
    if (!service_lock(true))
        return 0;
    uint32_t revision = s_presence_source_revision;
    xSemaphoreGive(s_presence_mutex);
    return revision;
}

bool app_slave_presence_service_receive(const uint8_t *data, size_t size, bool authenticated)
{
    if (!service_lock(false))
        return false;
    /* Az időt a zár után vesszük: közben már lefuthatott egy újabb UI-tick. */
    bool ok = app_slave_presence_receive(&s_presence_receiver, data, size,
                                         authenticated, esp_timer_get_time());
    xSemaphoreGive(s_presence_mutex);
    return ok;
}

void app_slave_presence_service_disconnected(void)
{
    if (!service_lock(false))
        return;
    app_slave_presence_disconnected(&s_presence_receiver);
    xSemaphoreGive(s_presence_mutex);
}

void app_slave_presence_service_legacy(void)
{
    if (!service_lock(false))
        return;
    app_slave_presence_legacy(&s_presence_receiver);
    xSemaphoreGive(s_presence_mutex);
}

void app_slave_presence_service_apply(app_slave_display_t *display, int64_t now_us)
{
    if (!display || !atomic_load(&s_presence_ready) || s_presence_master ||
        xSemaphoreTake(s_presence_mutex, 0) != pdTRUE)
        return;
    /* A UI a képek betöltése előtt is vehette az időbélyegét. Azóta már
     * érkezhetett BLE-impulzus; azt egy régi UI-idő nem dobhatja el. */
    int64_t current = esp_timer_get_time();
    if (now_us > current)
        current = now_us;
    app_slave_presence_apply(&s_presence_receiver, display, current);
    xSemaphoreGive(s_presence_mutex);
}
