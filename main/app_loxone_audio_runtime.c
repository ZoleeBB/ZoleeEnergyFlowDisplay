/* Audioserver-adatkapcsolat. Egy oldalhoz egy worker és egyszerre egy WS él;
 * több szervernél a részletes nézet/parancs kap elsőbbséget, egyébként körbejár.
 * A Miniserver alapvezérlése a meglévő, ellenőrzött parancssort használja. */
#include "app_loxone_audio_service.h"
#include "app_loxone_audio_auth.h"
#include "app_loxone_audio_media.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_TEMPLATE_DESKTOP
#include <stdatomic.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "mbedtls/platform_util.h"

#define AUDIO_TIMEOUT_US 8000000LL
#define AUDIO_VOLUME_INTERVAL_US 100000LL
#define AUDIO_WORKER_STACK 8192

typedef struct
{
    char id[LX_HOME_ID_SIZE];
    lx_audio_endpoint_t endpoint;
    lx_audio_track_t track;
    bool configured, hard_lock_known, hard_locked, lock_known, locked;
    bool server_known, client_known, server_online, client_online;
    bool max_known, volume_dirty, volume_final, native_ready;
    unsigned max_volume, desired_volume;
    int64_t last_volume_sent;
    lx_audio_status_t status;
} audio_slot_t;

typedef struct
{
    unsigned generation, revision, count;
    bool live;
    int detail;
    unsigned favorites_start, favorites_requested;
    bool favorites_dirty, favorite_queued;
    unsigned favorite_id, favorite_generation;
    lx_audio_favorites_t favorites;
    audio_slot_t slots[LX_HOME_MAX_CARDS];
} audio_page_t;

typedef struct
{
    char username[96], jwt[1024];
    unsigned generation;
    bool live;
} audio_credentials_t;

typedef struct
{
    char *data;
    size_t length;
} audio_message_t;

typedef struct
{
    esp_websocket_client_handle_t ws;
    QueueHandle_t messages;
    unsigned generation, credential_generation, port;
    char host[LX_AUDIO_HOST_SIZE];
    char session[LX_AUDIO_SESSION_SIZE];
    char *rx;
    size_t used, capacity;
    atomic_bool dead;
    unsigned phase;
    int64_t deadline, last_rotate, favorites_deadline;
    int favorites_slot;
    unsigned favorites_start, favorites_request;
    unsigned favorites_player;
} audio_connection_t;

static SemaphoreHandle_t audio_lock, audio_wake;
static audio_page_t *audio_page;
static audio_credentials_t *audio_credentials;
static TaskHandle_t audio_task;
static unsigned audio_generation = 1;

static void *audio_alloc(size_t size)
{
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static bool audio_initialize(void)
{
    /* A kezdeti hívás kizárólag a meglévő Loxone-workerből érkezik. */
    if (!audio_lock)
        audio_lock = xSemaphoreCreateMutex();
    if (!audio_wake)
        audio_wake = xSemaphoreCreateBinary();
    if (!audio_credentials)
        audio_credentials = audio_alloc(sizeof(*audio_credentials));
    return audio_lock && audio_wake && audio_credentials;
}

static int audio_slot_locked(const char *id, unsigned revision)
{
    if (!audio_page || !audio_page->live || !id || revision != audio_page->revision ||
        !audio_credentials || !audio_credentials->live)
        return -1;
    for (unsigned i = 0; i < audio_page->count; ++i)
        if (audio_page->slots[i].configured && !strcmp(audio_page->slots[i].id, id))
            return (int)i;
    return -1;
}

static bool audio_allowed_locked(int index, lx_audio_action_t action)
{
    if (index < 0 || !audio_page || (unsigned)index >= audio_page->count)
        return false;
    const audio_slot_t *slot = &audio_page->slots[index];
    if (!audio_page->live || !audio_credentials->live || !slot->configured || slot->endpoint.blocked ||
        !slot->hard_lock_known || slot->hard_locked || !slot->lock_known || slot->locked ||
        !slot->server_known || !slot->client_known || !slot->server_online || !slot->client_online)
        return false;
    if (action == LX_AUDIO_VOLUME && ((slot->endpoint.volume_modes & 4U) || !slot->max_known))
        return false;
    return action >= LX_AUDIO_PLAY && action <= LX_AUDIO_FAVORITE;
}

bool app_loxone_audio_allowed(const char *id, unsigned revision, lx_audio_action_t action)
{
    if (!audio_lock)
        return false;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    bool allowed = audio_allowed_locked(audio_slot_locked(id, revision), action);
    xSemaphoreGive(audio_lock);
    return allowed;
}

bool app_loxone_audio_value_allowed(const char *id, unsigned revision,
                                   lx_audio_action_t action, unsigned value)
{
    if (!audio_lock)
        return false;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int slot = audio_slot_locked(id, revision);
    bool allowed = audio_allowed_locked(slot, action) &&
        (action != LX_AUDIO_VOLUME || value <= audio_page->slots[slot].max_volume);
    xSemaphoreGive(audio_lock);
    return allowed;
}

void app_loxone_audio_session(const char *username, const char *jwt, bool live)
{
    if (!audio_initialize())
        return;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    bool valid = live && username && username[0] && strlen(username) < sizeof(audio_credentials->username) &&
                 jwt && jwt[0] && strlen(jwt) < sizeof(audio_credentials->jwt);
    if (!valid || !audio_credentials->live || strcmp(audio_credentials->username, username) ||
        strcmp(audio_credentials->jwt, jwt))
    {
        mbedtls_platform_zeroize(audio_credentials->jwt, sizeof(audio_credentials->jwt));
        audio_credentials->username[0] = 0;
        audio_credentials->live = valid;
        ++audio_credentials->generation;
        if (valid)
        {
            snprintf(audio_credentials->username, sizeof(audio_credentials->username), "%s", username);
            snprintf(audio_credentials->jwt, sizeof(audio_credentials->jwt), "%s", jwt);
        }
        if (audio_page)
            for (unsigned i = 0; i < audio_page->count; ++i)
            {
                audio_page->slots[i].volume_dirty = false;
                audio_page->slots[i].status = valid ? LX_AUDIO_CONNECTING : LX_AUDIO_UNAVAILABLE;
                audio_page->slots[i].native_ready = false;
            }
    }
    xSemaphoreGive(audio_lock);
    if (!valid)
        app_loxone_audio_hide();
    xSemaphoreGive(audio_wake);
}

void app_loxone_audio_hide(void)
{
    if (!audio_lock)
        return;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    ++audio_generation;
    if (audio_page)
    {
        audio_page->live = false;
        audio_page->detail = -1;
        audio_page->favorite_queued = false;
    }
    unsigned generation = audio_generation;
    xSemaphoreGive(audio_lock);
    app_loxone_audio_media_clear(generation);
    xSemaphoreGive(audio_wake);
}

static void audio_worker(void *argument);

void app_loxone_audio_page(const lx_home_config_t *config, const cJSON *catalog,
                           int page, unsigned revision)
{
    if (!audio_initialize())
        return;
    unsigned first = 0, count = 0;
    if (!config || page < 0 || !lx_home_page_range(config, (unsigned)page, &first, &count))
    {
        app_loxone_audio_hide();
        return;
    }
    audio_page_t *next = audio_alloc(sizeof(*next));
    if (!next)
    {
        app_loxone_audio_hide();
        return;
    }
    next->revision = revision;
    next->count = count;
    next->detail = -1;
    for (unsigned i = 0; i < count; ++i)
    {
        const lx_home_card_t *card = &config->cards[first + i];
        audio_slot_t *slot = &next->slots[i];
        if (card->type != LX_HOME_CARD_AUDIO)
            continue;
        const cJSON *entry = lx_audio_catalog_find(catalog, card->control_uuid);
        slot->configured = lx_audio_endpoint(entry, &slot->endpoint);
        snprintf(slot->id, sizeof(slot->id), "%s", card->id);
        slot->hard_lock_known = !slot->endpoint.locked_state[0];
        slot->lock_known = !slot->endpoint.lock_state[0];
        slot->server_known = slot->server_online = !slot->endpoint.server_state[0];
        slot->client_known = slot->client_online = !slot->endpoint.client_state[0];
        slot->max_known = !slot->endpoint.max_volume_state[0];
        slot->max_volume = 100;
        slot->status = LX_AUDIO_CONNECTING;
        next->live |= slot->configured;
    }
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    next->generation = ++audio_generation;
    audio_page_t *old = audio_page;
    audio_page = next;
    bool start = next->live && audio_credentials->live && !audio_task;
    if (start)
    {
        /* Az igény szerinti worker WithCaps-szal jön létre és az ahhoz tartozó
         * törléssel zárul; a hagyományos vTaskDelete nem szabadítaná a vermét. */
        BaseType_t created = xTaskCreateWithCaps(audio_worker, "lox_audio", AUDIO_WORKER_STACK,
            NULL, 3, &audio_task, MALLOC_CAP_RTCRAM);
        if (created != pdPASS)
            created = xTaskCreateWithCaps(audio_worker, "lox_audio", AUDIO_WORKER_STACK,
                NULL, 3, &audio_task, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (created != pdPASS)
            audio_task = NULL;
    }
    unsigned generation = next->generation;
    xSemaphoreGive(audio_lock);
    free(old);
    app_loxone_audio_media_clear(generation);
    xSemaphoreGive(audio_wake);
}

static bool audio_lock_value(const char *text, size_t length, bool *known)
{
    *known = false;
    if (!length)
    {
        *known = true;
        return false;
    }
    if (!text || length > 4096)
        return true;
    cJSON *root = lx_home_document_parse(text, length);
    bool locked = true;
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "locked");
    if (cJSON_IsNumber(state) && isfinite(state->valuedouble) &&
        state->valuedouble >= 0 && state->valuedouble <= 2 && state->valuedouble == floor(state->valuedouble))
    {
        *known = true;
        locked = state->valuedouble != 0;
    }
    cJSON_Delete(root);
    return locked;
}

void app_loxone_audio_state(const char *uuid, bool numeric, double value,
                            const char *text, size_t length, int64_t now)
{
    (void)now;
    if (!audio_lock || !uuid)
        return;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (audio_page && audio_page->live)
        for (unsigned i = 0; i < audio_page->count; ++i)
        {
            audio_slot_t *slot = &audio_page->slots[i];
            if (!slot->configured)
                continue;
            const lx_audio_endpoint_t *e = &slot->endpoint;
            bool binary = numeric && (value == 0 || value == 1);
            if (e->locked_state[0] && !strcmp(uuid, e->locked_state))
            {
                slot->hard_lock_known = binary;
                slot->hard_locked = !binary || value != 0;
            }
            if (e->lock_state[0] && !strcmp(uuid, e->lock_state))
                slot->locked = numeric ? (slot->lock_known = false, true) :
                    audio_lock_value(text, length, &slot->lock_known);
            if (e->server_state[0] && !strcmp(uuid, e->server_state))
            {
                slot->server_known = numeric && isfinite(value) && value == floor(value);
                slot->server_online = slot->server_known && value == 2;
            }
            if (e->client_state[0] && !strcmp(uuid, e->client_state))
            {
                slot->client_known = numeric && isfinite(value) && value == floor(value);
                slot->client_online = slot->client_known && value == 2;
            }
            if (e->volume_state[0] && !strcmp(uuid, e->volume_state))
            {
                slot->track.volume_known = numeric && isfinite(value) && value >= 0 && value <= 100;
                if (slot->track.volume_known)
                    slot->track.volume = (unsigned)lround(value);
            }
            if (e->max_volume_state[0] && !strcmp(uuid, e->max_volume_state))
            {
                slot->max_known = numeric && isfinite(value) && value >= 0 && value <= 100;
                if (slot->max_known)
                    slot->max_volume = (unsigned)floor(value);
            }
            if (e->play_state[0] && !strcmp(uuid, e->play_state))
                slot->track.mode = !numeric ? LX_AUDIO_MODE_UNKNOWN :
                    value == 2 ? LX_AUDIO_MODE_PLAY : value == 1 ? LX_AUDIO_MODE_PAUSE :
                    value == 0 ? LX_AUDIO_MODE_STOP : LX_AUDIO_MODE_UNKNOWN;
            if (!audio_allowed_locked((int)i, LX_AUDIO_VOLUME))
                slot->volume_dirty = false;
        }
    xSemaphoreGive(audio_lock);
}

bool app_loxone_audio_summary(const char *id, unsigned revision, lx_audio_summary_t *out)
{
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    if (!audio_lock)
        return false;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int index = audio_slot_locked(id, revision);
    if (index >= 0)
    {
        const audio_slot_t *slot = &audio_page->slots[index];
        out->track = slot->track;
        out->generation = audio_page->generation;
        out->available = true;
        out->controllable = audio_allowed_locked(index, LX_AUDIO_PLAY);
        out->volume_up_down = (slot->endpoint.volume_modes & 4U) != 0;
        out->volume_max = slot->max_known ? slot->max_volume : 0;
        out->volume_pending = slot->volume_dirty;
        out->status = slot->status;
        out->native_ready = slot->native_ready;
    }
    xSemaphoreGive(audio_lock);
    return index >= 0;
}

esp_err_t app_loxone_audio_command(const char *id, unsigned revision,
                                  lx_audio_action_t action, unsigned value, bool final)
{
    if (!audio_lock || action >= LX_AUDIO_FAVORITE)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int index = audio_slot_locked(id, revision);
    bool ok = audio_allowed_locked(index, action);
    if (ok && action == LX_AUDIO_VOLUME)
    {
        audio_slot_t *slot = &audio_page->slots[index];
        ok = value <= slot->max_volume;
        if (ok)
        {
            slot->desired_volume = value;
            slot->volume_dirty = true;
            slot->volume_final |= final;
        }
    }
    xSemaphoreGive(audio_lock);
    if (!ok)
        return ESP_ERR_INVALID_STATE;
    if (action == LX_AUDIO_VOLUME)
    {
        xSemaphoreGive(audio_wake);
        return ESP_OK;
    }
    return app_loxone_audio_transport(id, revision, action, value);
}

esp_err_t app_loxone_audio_favorites_request(const char *id, unsigned revision, unsigned start)
{
    if (!audio_lock)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int slot = audio_slot_locked(id, revision);
    bool ok = slot >= 0;
    if (ok)
    {
        audio_page->detail = slot;
        audio_page->favorites_start = start;
        ++audio_page->favorites_requested;
        audio_page->favorites_dirty = true;
        memset(&audio_page->favorites, 0, sizeof(audio_page->favorites));
        audio_page->favorite_queued = false;
    }
    xSemaphoreGive(audio_lock);
    if (ok)
        xSemaphoreGive(audio_wake);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool app_loxone_audio_favorites(const char *id, unsigned revision, lx_audio_favorites_t *out)
{
    if (!out || !audio_lock)
        return false;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int slot = audio_slot_locked(id, revision);
    bool ok = slot >= 0 && slot == audio_page->detail;
    if (ok)
        *out = audio_page->favorites;
    xSemaphoreGive(audio_lock);
    return ok;
}

esp_err_t app_loxone_audio_favorite(const char *id, unsigned revision,
                                   unsigned favorite_id, unsigned favorites_generation)
{
    if (!audio_lock || !favorite_id)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int slot = audio_slot_locked(id, revision);
    bool valid = audio_allowed_locked(slot, LX_AUDIO_FAVORITE) && slot == audio_page->detail &&
                 audio_page->slots[slot].native_ready &&
                 audio_page->favorites.ready && !audio_page->favorite_queued &&
                 audio_page->favorites.generation == favorites_generation;
    bool found = false;
    if (valid)
        for (unsigned i = 0; i < audio_page->favorites.count; ++i)
            found |= audio_page->favorites.items[i].id == favorite_id;
    if (found)
    {
        audio_page->favorite_queued = true;
        audio_page->favorite_id = favorite_id;
        audio_page->favorite_generation = favorites_generation;
    }
    xSemaphoreGive(audio_lock);
    if (found)
        xSemaphoreGive(audio_wake);
    return found ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void app_loxone_audio_detail_close(void)
{
    if (!audio_lock)
        return;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (audio_page)
    {
        audio_page->detail = -1;
        audio_page->favorites_dirty = audio_page->favorite_queued = false;
        ++audio_page->favorites_requested;
        memset(&audio_page->favorites, 0, sizeof(audio_page->favorites));
    }
    xSemaphoreGive(audio_lock);
    app_loxone_audio_media_close_detail();
    xSemaphoreGive(audio_wake);
}

static void audio_receive(void *argument, esp_event_base_t base, int32_t event, void *data)
{
    (void)base;
    audio_connection_t *connection = argument;
    if (event == WEBSOCKET_EVENT_DISCONNECTED || event == WEBSOCKET_EVENT_ERROR)
    {
        connection->dead = true;
        xSemaphoreGive(audio_wake);
        return;
    }
    if (event != WEBSOCKET_EVENT_DATA || connection->dead)
        return;
    esp_websocket_event_data_t *part = data;
    if (part->op_code == 8)
    {
        connection->dead = true;
        return;
    }
    if (part->op_code != 0 && part->op_code != 1)
        return;
    if (part->op_code == 1 && !part->payload_offset)
        connection->used = 0;
    if (part->data_len < 0 || part->payload_len < 0 || part->payload_offset < 0 ||
        (size_t)part->data_len > LX_AUDIO_JSON_MAX - connection->used)
    {
        connection->dead = true;
        return;
    }
    size_t need = connection->used + (size_t)part->data_len + 1;
    if (need > connection->capacity)
    {
        size_t capacity = (need + 2047) & ~(size_t)2047;
        char *next = audio_alloc(capacity);
        if (!next)
        {
            connection->dead = true;
            return;
        }
        memcpy(next, connection->rx, connection->used);
        free(connection->rx);
        connection->rx = next;
        connection->capacity = capacity;
    }
    memcpy(connection->rx + connection->used, part->data_ptr, part->data_len);
    connection->used += part->data_len;
    connection->rx[connection->used] = 0;
    if (part->payload_offset + part->data_len != part->payload_len || !part->fin)
        return;
    audio_message_t message = {.data = connection->rx, .length = connection->used};
    connection->rx = NULL;
    connection->capacity = connection->used = 0;
    if (xQueueSend(connection->messages, &message, 0) != pdTRUE)
    {
        free(message.data);
        connection->dead = true;
    }
    xSemaphoreGive(audio_wake);
}

static bool audio_send(audio_connection_t *connection, const char *command)
{
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    bool current = audio_page && audio_page->live && audio_credentials->live &&
        audio_page->generation == connection->generation &&
        audio_credentials->generation == connection->credential_generation;
    xSemaphoreGive(audio_lock);
    return current && connection->ws && !connection->dead &&
        esp_websocket_client_send_text(connection->ws, command, strlen(command),
                                        pdMS_TO_TICKS(1000)) == (int)strlen(command);
}

static void audio_connection_close(audio_connection_t *connection)
{
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (audio_page && audio_page->generation == connection->generation)
    {
        for (unsigned i = 0; i < audio_page->count; ++i)
            if (audio_page->slots[i].configured && connection->port == audio_page->slots[i].endpoint.port &&
                !strcmp(connection->host, audio_page->slots[i].endpoint.host))
            {
                audio_page->slots[i].native_ready = false;
                if (audio_page->slots[i].status != LX_AUDIO_AUTH_FAILED)
                    audio_page->slots[i].status = LX_AUDIO_UNAVAILABLE;
            }
        int detail = audio_page->detail;
        if (detail >= 0 && (unsigned)detail < audio_page->count &&
            connection->port == audio_page->slots[detail].endpoint.port &&
            !strcmp(connection->host, audio_page->slots[detail].endpoint.host))
        {
            /* Lejátszási parancsot nem ismétlünk kapcsolatvesztés után.
             * A megszakadt listaolvasás viszont újrakérhető ugyanahhoz az
             * aktuális ablakhoz, különben örökre betöltési állapotban maradna. */
            audio_page->favorite_queued = false;
            if (audio_page->live && connection->favorites_slot == detail &&
                audio_page->favorites_requested == connection->favorites_request)
            {
                audio_page->favorites_dirty = true;
                audio_page->favorites.failed = false;
            }
        }
    }
    xSemaphoreGive(audio_lock);
    if (connection->ws)
    {
        esp_websocket_client_stop(connection->ws);
        esp_websocket_client_destroy(connection->ws);
        connection->ws = NULL;
    }
    if (connection->messages)
    {
        audio_message_t message;
        while (xQueueReceive(connection->messages, &message, 0) == pdTRUE)
            free(message.data);
    }
    free(connection->rx);
    connection->rx = NULL;
    connection->used = connection->capacity = 0;
    connection->phase = 0;
    connection->favorites_slot = -1;
    mbedtls_platform_zeroize(connection->session, sizeof(connection->session));
}

static bool audio_connection_matches(const audio_connection_t *connection, const audio_slot_t *slot)
{
    return connection->port == slot->endpoint.port && !strcmp(connection->host, slot->endpoint.host);
}

static void audio_connection_open(audio_connection_t *connection, unsigned selected)
{
    char uri[LX_AUDIO_HOST_SIZE + 32];
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (!audio_page || !audio_page->live || selected >= audio_page->count || !audio_credentials->live)
    {
        xSemaphoreGive(audio_lock);
        return;
    }
    const audio_slot_t *slot = &audio_page->slots[selected];
    snprintf(connection->host, sizeof(connection->host), "%s", slot->endpoint.host);
    connection->port = slot->endpoint.port;
    connection->generation = audio_page->generation;
    connection->credential_generation = audio_credentials->generation;
    xSemaphoreGive(audio_lock);
    snprintf(uri, sizeof(uri), "ws://%s:%u/", connection->host, connection->port);
    esp_websocket_client_config_t options = {
        .uri = uri, .subprotocol = "remotecontrol", .disable_auto_reconnect = true,
        .buffer_size = 2048, .task_stack = 4096, .network_timeout_ms = 3000,
    };
    connection->dead = false;
    connection->phase = 1;
    connection->deadline = esp_timer_get_time() + AUDIO_TIMEOUT_US;
    connection->last_rotate = esp_timer_get_time();
    connection->ws = esp_websocket_client_init(&options);
    if (!connection->ws || esp_websocket_register_events(connection->ws, WEBSOCKET_EVENT_ANY,
        audio_receive, connection) != ESP_OK || esp_websocket_client_start(connection->ws) != ESP_OK)
        connection->dead = true;
}

static int audio_random(void *context, unsigned char *bytes, size_t length)
{
    (void)context;
    esp_fill_random(bytes, length);
    return 0;
}

static void audio_message(audio_connection_t *connection, const audio_message_t *message)
{
    if (connection->phase == 1 && lx_audio_session_token(message->data, connection->session))
    {
        if (!audio_send(connection, "audio/cfg/getkey/full"))
            connection->dead = true;
        connection->phase = 2;
        return;
    }
    cJSON *root = lx_home_document_parse(message->data, message->length);
    if (!root)
        return;
    if (connection->phase == 2)
    {
        const cJSON *keys = cJSON_GetObjectItemCaseSensitive(root, "getkey_result");
        const cJSON *key = cJSON_GetArrayItem(keys, 0);
        const cJSON *pem = cJSON_GetObjectItemCaseSensitive(key, "pubkey");
        if (cJSON_IsString(pem))
        {
            audio_credentials_t *copy = audio_alloc(sizeof(*copy));
            xSemaphoreTake(audio_lock, portMAX_DELAY);
            bool current = audio_credentials->live && connection->credential_generation == audio_credentials->generation;
            if (copy && current)
                *copy = *audio_credentials;
            xSemaphoreGive(audio_lock);
            char *command = copy && current ? lx_audio_auth_create(pem->valuestring, copy->username,
                copy->jwt, connection->session, audio_random, NULL) : NULL;
            if (copy)
            {
                mbedtls_platform_zeroize(copy, sizeof(*copy));
                free(copy);
            }
            if (!command || !audio_send(connection, command))
                connection->dead = true;
            lx_audio_auth_free(command);
            connection->phase = 3;
        }
    }
    else if (connection->phase == 3)
    {
        const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "authenticate_result");
        if (result)
        {
            bool ok = cJSON_IsString(result) && !strcmp(result->valuestring, "authentication successful");
            connection->phase = ok ? 4 : 3;
            connection->dead = !ok;
            xSemaphoreTake(audio_lock, portMAX_DELAY);
            if (audio_page && audio_page->generation == connection->generation)
                for (unsigned i = 0; i < audio_page->count; ++i)
                    if (audio_page->slots[i].configured && audio_connection_matches(connection, &audio_page->slots[i]))
                    {
                        audio_page->slots[i].status = ok ? LX_AUDIO_READY : LX_AUDIO_AUTH_FAILED;
                        audio_page->slots[i].native_ready = ok;
                    }
            xSemaphoreGive(audio_lock);
            if (ok)
            {
                for (unsigned i = 0; i < LX_HOME_MAX_CARDS; ++i)
                {
                    unsigned player = 0;
                    xSemaphoreTake(audio_lock, portMAX_DELAY);
                    if (audio_page && audio_page->generation == connection->generation &&
                        i < audio_page->count && audio_page->slots[i].configured &&
                        audio_connection_matches(connection, &audio_page->slots[i]))
                        player = audio_page->slots[i].endpoint.player;
                    xSemaphoreGive(audio_lock);
                    if (player)
                    {
                        char command[64];
                        snprintf(command, sizeof(command), "audio/%u/status", player);
                        if (!audio_send(connection, command))
                            connection->dead = true;
                    }
                }
            }
        }
    }
    else if (connection->phase == 4)
    {
        xSemaphoreTake(audio_lock, portMAX_DELAY);
        if (audio_page && audio_page->live && audio_page->generation == connection->generation)
        {
            for (unsigned i = 0; i < audio_page->count; ++i)
                if (audio_page->slots[i].configured && audio_connection_matches(connection, &audio_page->slots[i]))
                    lx_audio_track_update(&audio_page->slots[i].track, audio_page->slots[i].endpoint.player, root);
            if (cJSON_GetObjectItemCaseSensitive(root, "getroomfavs_result") && connection->favorites_slot >= 0)
            {
                if (audio_page->detail == connection->favorites_slot &&
                    audio_page->favorites_requested == connection->favorites_request)
                {
                    bool parsed = lx_audio_favorites_parse(&audio_page->favorites, connection->favorites_player,
                        connection->favorites_start, root);
                    if (parsed)
                        audio_page->favorites.generation = connection->favorites_request;
                    else
                        audio_page->favorites.failed = true;
                }
                /* Egyetlen in-flight listakérés él. A régi ablak válaszát
                 * elfogyasztjuk, de nem írhatja át az időközben nyitottat. */
                connection->favorites_slot = -1;
            }
        }
        xSemaphoreGive(audio_lock);
    }
    cJSON_Delete(root);
}

static void audio_volume_poll(int64_t now)
{
    for (unsigned i = 0; i < LX_HOME_MAX_CARDS; ++i)
    {
        char id[LX_HOME_ID_SIZE] = {0};
        unsigned revision = 0, value = 0, generation = 0;
        xSemaphoreTake(audio_lock, portMAX_DELAY);
        if (audio_page && audio_page->live && i < audio_page->count)
        {
            audio_slot_t *slot = &audio_page->slots[i];
            if (slot->volume_dirty && slot->max_known && slot->desired_volume > slot->max_volume)
            {
                slot->volume_dirty = false;
                slot->status = LX_AUDIO_REQUEST_FAILED;
            }
            if (slot->volume_dirty && audio_allowed_locked((int)i, LX_AUDIO_VOLUME) &&
                (slot->volume_final || now - slot->last_volume_sent >= AUDIO_VOLUME_INTERVAL_US))
            {
                snprintf(id, sizeof(id), "%s", slot->id);
                revision = audio_page->revision;
                generation = audio_page->generation;
                value = slot->desired_volume;
                slot->volume_dirty = slot->volume_final = false;
                slot->last_volume_sent = now;
            }
        }
        xSemaphoreGive(audio_lock);
        if (id[0])
        {
            esp_err_t result = app_loxone_audio_transport(id, revision, LX_AUDIO_VOLUME, value);
            xSemaphoreTake(audio_lock, portMAX_DELAY);
            if (audio_page && audio_page->generation == generation)
            {
                audio_slot_t *slot = &audio_page->slots[i];
                slot->status = result == ESP_OK ? LX_AUDIO_REQUEST_SENT : LX_AUDIO_REQUEST_FAILED;
                /* A sor által még el nem fogadott abszolút célérték felülírható.
                 * Elfogadott parancsot időtúllépés után nem ismétlünk meg. */
                if (result == ESP_ERR_INVALID_STATE && !slot->volume_dirty &&
                    audio_allowed_locked((int)i, LX_AUDIO_VOLUME))
                    slot->volume_dirty = true;
            }
            xSemaphoreGive(audio_lock);
        }
    }
}

static void audio_native_poll(audio_connection_t *connection)
{
    char command[96] = {0};
    unsigned generation = 0;
    int detail = -1;
    bool favorite = false;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (audio_page && audio_page->live && audio_page->generation == connection->generation &&
        audio_page->detail >= 0 && connection->phase == 4)
    {
        detail = audio_page->detail;
        audio_slot_t *slot = &audio_page->slots[detail];
        generation = audio_page->generation;
        if (audio_connection_matches(connection, slot))
        {
            if (audio_page->favorite_queued)
            {
                if (audio_allowed_locked(detail, LX_AUDIO_FAVORITE) &&
                    audio_page->favorite_generation == audio_page->favorites.generation)
                    favorite = lx_audio_native_command(slot->endpoint.player, LX_AUDIO_FAVORITE,
                        audio_page->favorite_id, command);
                audio_page->favorite_queued = false;
            }
            else if (audio_page->favorites_dirty && connection->favorites_slot < 0)
            {
                snprintf(command, sizeof(command), "audio/cfg/getroomfavs/%u/%u/%u",
                    slot->endpoint.player, audio_page->favorites_start, LX_AUDIO_FAVORITE_PAGE);
                connection->favorites_slot = detail;
                connection->favorites_start = audio_page->favorites_start;
                connection->favorites_player = slot->endpoint.player;
                connection->favorites_request = audio_page->favorites_requested;
                connection->favorites_deadline = esp_timer_get_time() + AUDIO_TIMEOUT_US;
                audio_page->favorites_dirty = false;
            }
        }
    }
    xSemaphoreGive(audio_lock);
    if (!command[0])
        return;
    bool sent = audio_send(connection, command);
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (audio_page && audio_page->generation == generation && detail >= 0)
    {
        if (favorite)
            audio_page->slots[detail].status = sent ? LX_AUDIO_REQUEST_SENT : LX_AUDIO_REQUEST_FAILED;
        else if (!sent)
            audio_page->favorites.failed = true;
    }
    xSemaphoreGive(audio_lock);
    if (!sent)
        connection->dead = true;
}

static void audio_favorites_timeout(audio_connection_t *connection, int64_t now)
{
    if (connection->favorites_slot < 0 || now <= connection->favorites_deadline)
        return;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (audio_page && audio_page->generation == connection->generation &&
        audio_page->favorites_requested == connection->favorites_request)
        audio_page->favorites.failed = true;
    xSemaphoreGive(audio_lock);
    connection->favorites_slot = -1;
    /* A válasz nem hordoz kérésazonosítót. Időtúllépés után új kapcsolaton
     * folytatjuk, így egy nagyon késői régi válasz sem nyelheti el az újat. */
    connection->dead = true;
}

static void audio_worker(void *argument)
{
    (void)argument;
    audio_connection_t *connection = audio_alloc(sizeof(*connection));
    if (connection)
    {
        connection->messages = xQueueCreate(4, sizeof(audio_message_t));
        connection->favorites_slot = -1;
    }
    bool initialized = connection && connection->messages;
    unsigned selected = 0;
    int64_t retry = 0;
    while (connection && connection->messages)
    {
        int64_t now = esp_timer_get_time();
        xSemaphoreTake(audio_lock, portMAX_DELAY);
        bool alive = audio_page && audio_page->live && audio_credentials->live;
        bool stale = alive && (connection->generation != audio_page->generation ||
            connection->credential_generation != audio_credentials->generation);
        int target = alive ? audio_page->detail : -1;
        if (alive && target < 0)
        {
            if (selected >= audio_page->count || !audio_page->slots[selected].configured)
                selected = 0;
            for (unsigned i = 0; i < audio_page->count; ++i)
                if (audio_page->slots[(selected + i) % audio_page->count].configured)
                {
                    selected = (selected + i) % audio_page->count;
                    target = (int)selected;
                    break;
                }
            if (target >= 0 && connection->phase == 4 && now - connection->last_rotate > 15000000LL)
                for (unsigned i = 1; i < audio_page->count; ++i)
                {
                    unsigned next = (selected + i) % audio_page->count;
                    if (audio_page->slots[next].configured &&
                        !audio_connection_matches(connection, &audio_page->slots[next]))
                    {
                        selected = next;
                        target = (int)next;
                        break;
                    }
                }
        }
        bool switch_server = alive && target >= 0 && connection->ws &&
            !audio_connection_matches(connection, &audio_page->slots[target]);
        xSemaphoreGive(audio_lock);
        if (!alive)
            break;
        if (stale || switch_server || connection->dead ||
            (connection->phase > 0 && connection->phase < 4 && now > connection->deadline))
        {
            bool failed = connection->dead || (connection->phase > 0 && connection->phase < 4 &&
                now > connection->deadline);
            audio_connection_close(connection);
            retry = failed ? now + 5000000LL : 0;
        }
        if (!connection->ws && target >= 0 && now >= retry)
            audio_connection_open(connection, (unsigned)target);
        audio_message_t message;
        while (xQueueReceive(connection->messages, &message, 0) == pdTRUE)
        {
            audio_message(connection, &message);
            free(message.data);
        }
        audio_volume_poll(now);
        audio_native_poll(connection);
        audio_favorites_timeout(connection, now);
        xSemaphoreTake(audio_wake, pdMS_TO_TICKS(50));
    }
    if (connection)
    {
        audio_connection_close(connection);
        if (connection->messages)
            vQueueDelete(connection->messages);
        free(connection);
    }
    ESP_LOGI("lox_audio", "Audio worker leáll; verem minimum: %u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    audio_task = NULL;
    /* Memóriahiány nem indíthat azonnali task-létrehozó hurkot. Újrapróba
     * csak egy későbbi oldalépítéskor; normál leállásnál az időközben nyitott
     * új oldal folytathatja a már felépített futtató életciklusát. */
    bool restart = initialized && audio_page && audio_page->live && audio_credentials->live;
    if (restart)
    {
        BaseType_t result = xTaskCreateWithCaps(audio_worker, "lox_audio", AUDIO_WORKER_STACK,
            NULL, 3, &audio_task, MALLOC_CAP_RTCRAM);
        if (result != pdPASS)
            result = xTaskCreateWithCaps(audio_worker, "lox_audio", AUDIO_WORKER_STACK,
                NULL, 3, &audio_task, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (result != pdPASS)
            audio_task = NULL;
    }
    else if (audio_page)
    {
        free(audio_page);
        audio_page = NULL;
    }
    xSemaphoreGive(audio_lock);
    vTaskDeleteWithCaps(NULL);
}

#else
/* A szimulátor kizárólag explicit helyi mintát mutat; élő szerverre nem lép be. */
#include "../simulator/audio_fixture.h"

typedef struct
{
    char id[LX_HOME_ID_SIZE];
    unsigned revision;
    lx_audio_summary_t summary;
} audio_fixture_t;

static audio_fixture_t audio_fixtures[LX_HOME_MAX_CARDS];
static lx_audio_favorites_t audio_fixture_favorites;
static unsigned audio_fixture_count, audio_fixture_favorite_total;
static char audio_fixture_detail[LX_HOME_ID_SIZE];
unsigned app_sim_audio_commands, app_sim_audio_last_favorite;
lx_audio_action_t app_sim_audio_last_action;

void app_sim_audio_reset(void)
{
    memset(audio_fixtures, 0, sizeof(audio_fixtures));
    memset(&audio_fixture_favorites, 0, sizeof(audio_fixture_favorites));
    audio_fixture_count = audio_fixture_favorite_total = 0;
    audio_fixture_detail[0] = 0;
    app_sim_audio_commands = app_sim_audio_last_favorite = 0;
    app_sim_audio_last_action = LX_AUDIO_PLAY;
}

bool app_sim_audio_configure(const char *id, unsigned revision, const lx_audio_summary_t *summary)
{
    if (!id || !id[0] || strlen(id) >= LX_HOME_ID_SIZE || !summary || audio_fixture_count >= LX_HOME_MAX_CARDS)
        return false;
    audio_fixture_t *fixture = &audio_fixtures[audio_fixture_count++];
    snprintf(fixture->id, sizeof(fixture->id), "%s", id);
    fixture->revision = revision;
    fixture->summary = *summary;
    return true;
}

void app_sim_audio_set_favorite_count(unsigned total)
{
    audio_fixture_favorite_total = total;
}

static audio_fixture_t *audio_fixture_find(const char *id, unsigned revision)
{
    if (!id)
        return NULL;
    for (unsigned i = 0; i < audio_fixture_count; ++i)
        if (audio_fixtures[i].revision == revision && !strcmp(audio_fixtures[i].id, id))
            return &audio_fixtures[i];
    return NULL;
}

void app_loxone_audio_session(const char *username, const char *jwt, bool live)
{ (void)username; (void)jwt; (void)live; }
void app_loxone_audio_page(const lx_home_config_t *config, const cJSON *catalog, int page, unsigned revision)
{ (void)config; (void)catalog; (void)page; (void)revision; }
void app_loxone_audio_hide(void) { app_loxone_audio_media_clear(0); }
void app_loxone_audio_state(const char *uuid, bool numeric, double value, const char *text, size_t length, int64_t now)
{ (void)uuid; (void)numeric; (void)value; (void)text; (void)length; (void)now; }

bool app_loxone_audio_summary(const char *id, unsigned revision, lx_audio_summary_t *out)
{
    audio_fixture_t *fixture = audio_fixture_find(id, revision);
    if (out)
    {
        memset(out, 0, sizeof(*out));
        if (fixture)
            *out = fixture->summary;
    }
    return out && fixture;
}

bool app_loxone_audio_allowed(const char *id, unsigned revision, lx_audio_action_t action)
{
    audio_fixture_t *fixture = audio_fixture_find(id, revision);
    return fixture && fixture->summary.controllable && action >= LX_AUDIO_PLAY && action <= LX_AUDIO_FAVORITE &&
        (action != LX_AUDIO_VOLUME || !fixture->summary.volume_up_down);
}

bool app_loxone_audio_value_allowed(const char *id, unsigned revision,
                                   lx_audio_action_t action, unsigned value)
{
    audio_fixture_t *fixture = audio_fixture_find(id, revision);
    return app_loxone_audio_allowed(id, revision, action) &&
        (action != LX_AUDIO_VOLUME || value <= fixture->summary.volume_max);
}

esp_err_t app_loxone_audio_command(const char *id, unsigned revision, lx_audio_action_t action, unsigned value, bool final)
{
    (void)final;
    audio_fixture_t *fixture = audio_fixture_find(id, revision);
    if (!app_loxone_audio_allowed(id, revision, action) ||
        (action == LX_AUDIO_VOLUME && value > fixture->summary.volume_max))
        return ESP_ERR_INVALID_STATE;
    ++app_sim_audio_commands;
    app_sim_audio_last_action = action;
    if (action == LX_AUDIO_VOLUME)
    {
        fixture->summary.track.volume = value;
        fixture->summary.track.volume_known = true;
    }
    if (action == LX_AUDIO_PLAY || action == LX_AUDIO_PAUSE)
        fixture->summary.track.mode = action == LX_AUDIO_PLAY ? LX_AUDIO_MODE_PLAY : LX_AUDIO_MODE_PAUSE;
    return ESP_OK;
}

esp_err_t app_loxone_audio_favorites_request(const char *id, unsigned revision, unsigned start)
{
    audio_fixture_t *fixture = audio_fixture_find(id, revision);
    if (!fixture || start > audio_fixture_favorite_total)
        return ESP_ERR_INVALID_ARG;
    unsigned generation = audio_fixture_favorites.generation + 1;
    memset(&audio_fixture_favorites, 0, sizeof(audio_fixture_favorites));
    snprintf(audio_fixture_detail, sizeof(audio_fixture_detail), "%s", id);
    audio_fixture_favorites.generation = generation;
    audio_fixture_favorites.ready = true;
    audio_fixture_favorites.player = 3;
    audio_fixture_favorites.start = start;
    audio_fixture_favorites.total = audio_fixture_favorite_total;
    audio_fixture_favorites.count = audio_fixture_favorite_total - start;
    if (audio_fixture_favorites.count > LX_AUDIO_FAVORITE_PAGE)
        audio_fixture_favorites.count = LX_AUDIO_FAVORITE_PAGE;
    for (unsigned i = 0; i < audio_fixture_favorites.count; ++i)
    {
        lx_audio_favorite_t *favorite = &audio_fixture_favorites.items[i];
        favorite->id = 1000 + start + i;
        favorite->slot = (int)(start + i);
        snprintf(favorite->name, sizeof(favorite->name), "Minta kedvenc %u", start + i + 1);
    }
    return ESP_OK;
}

bool app_loxone_audio_favorites(const char *id, unsigned revision, lx_audio_favorites_t *out)
{
    if (!out || !audio_fixture_find(id, revision) || strcmp(audio_fixture_detail, id))
        return false;
    *out = audio_fixture_favorites;
    return true;
}

esp_err_t app_loxone_audio_favorite(const char *id, unsigned revision, unsigned favorite_id, unsigned generation)
{
    if (!app_loxone_audio_allowed(id, revision, LX_AUDIO_FAVORITE) || strcmp(audio_fixture_detail, id) ||
        generation != audio_fixture_favorites.generation)
        return ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < audio_fixture_favorites.count; ++i)
        if (audio_fixture_favorites.items[i].id == favorite_id)
        {
            ++app_sim_audio_commands;
            app_sim_audio_last_favorite = favorite_id;
            return ESP_OK;
        }
    return ESP_ERR_INVALID_ARG;
}

void app_loxone_audio_detail_close(void)
{
    audio_fixture_detail[0] = 0;
    ++audio_fixture_favorites.generation;
    app_loxone_audio_media_close_detail();
}
#endif
