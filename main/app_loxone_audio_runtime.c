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

static bool audio_volume_action(lx_audio_action_t action)
{
    return action == LX_AUDIO_VOLUME || action == LX_AUDIO_VOLUME_UP || action == LX_AUDIO_VOLUME_DOWN;
}

#ifndef APP_TEMPLATE_DESKTOP
#include <stdatomic.h>
#include "app_events.h"
#include "app_ota.h"
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
#define AUDIO_VOLUME_CONFIRM_US 2000000LL
#define AUDIO_RESOURCE_RETRY_US 5000000LL
#define AUDIO_RX_QUEUE_LENGTH 32U
#define AUDIO_RX_ROUNDED_SIZE(size) (((size) + 2047U) & ~(size_t)2047U)
#define AUDIO_RX_BYTES_MAX (4U * AUDIO_RX_ROUNDED_SIZE(LX_AUDIO_JSON_MAX + 1U))

typedef struct
{
    char id[LX_HOME_ID_SIZE];
    lx_audio_endpoint_t endpoint;
    lx_audio_track_t track;
    bool configured, hard_lock_known, hard_locked, lock_known, locked;
    bool server_known, client_known, server_online, client_online;
    bool max_known, volume_dirty, volume_final, volume_pending, volume_failed, native_ready;
    unsigned max_volume, desired_volume, edit_generation;
    int64_t last_volume_sent, volume_deadline;
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
    size_t length, capacity;
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
    unsigned rx_opcode;
    atomic_bool dead;
    atomic_size_t received_bytes;
    unsigned phase;
    int64_t deadline, last_rotate, favorites_deadline;
    int favorites_slot;
    unsigned favorites_start, favorites_request;
    unsigned favorites_player;
    lx_audio_track_t *staged_tracks;
    unsigned staged_mask;
    unsigned status_pending;
    atomic_uint text_frames, binary_frames;
    _Atomic(const char *) failure_reason;
    bool diagnostic, first_native_data;
    unsigned invalid_json;
    int64_t next_diagnostic;
} audio_connection_t;

typedef struct
{
    unsigned generation, revision, request, list_generation, favorite_id;
    int slot;
    char id[LX_HOME_ID_SIZE];
    bool favorite;
} audio_detail_command_t;

static SemaphoreHandle_t audio_lock, audio_wake;
static audio_page_t *audio_page;
static audio_credentials_t *audio_credentials;
static atomic_flag audio_initializing = ATOMIC_FLAG_INIT;
static atomic_bool audio_ready, audio_runner_active;
static int64_t audio_init_retry;
static unsigned audio_generation = 1;
static atomic_uint audio_last_web_error_ms;

static void audio_web_error(const audio_connection_t *connection, const char *resource_reason);

static void *audio_alloc(size_t size)
{
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

esp_err_t app_loxone_audio_init(void)
{
    if (atomic_load(&audio_ready))
        return ESP_OK;
    /* Elsőként a boot hívja; egy későbbi párhuzamos hívó nem vár/spinel.
     * A félkész mutatók soha nem kerülnek a publikus API-k elé. */
    if (atomic_flag_test_and_set(&audio_initializing))
        return ESP_ERR_INVALID_STATE;
    if (atomic_load(&audio_ready))
    {
        atomic_flag_clear(&audio_initializing);
        return ESP_OK;
    }
    int64_t now = esp_timer_get_time();
    if (now < audio_init_retry)
    {
        atomic_flag_clear(&audio_initializing);
        return ESP_ERR_NO_MEM;
    }
    SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    SemaphoreHandle_t wake = xSemaphoreCreateBinary();
    audio_credentials_t *credentials = audio_alloc(sizeof(*credentials));
    if (!lock || !wake || !credentials)
    {
        if (lock)
            vSemaphoreDelete(lock);
        if (wake)
            vSemaphoreDelete(wake);
        free(credentials);
        audio_init_retry = now + 1000000LL;
        atomic_flag_clear(&audio_initializing);
        ESP_LOGW("lox_audio", "Audio futtató erőforrásai nem foglalhatók.");
        audio_web_error(NULL, "runtime-memory");
        return ESP_ERR_NO_MEM;
    }
    audio_lock = lock;
    audio_wake = wake;
    audio_credentials = credentials;
    atomic_store(&audio_ready, true);
    atomic_flag_clear(&audio_initializing);
    return ESP_OK;
}

static bool audio_initialize(void)
{
    return app_loxone_audio_init() == ESP_OK;
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

static void audio_volume_cancel_locked(audio_slot_t *slot, bool failed)
{
    if (++slot->edit_generation == 0)
        ++slot->edit_generation;
    slot->volume_dirty = slot->volume_final = slot->volume_pending = false;
    slot->volume_failed = failed;
    slot->volume_deadline = 0;
}

static void audio_volume_expire_locked(audio_slot_t *slot, int64_t now)
{
    if (slot->volume_pending && now >= slot->volume_deadline)
        audio_volume_cancel_locked(slot, true);
}

static void audio_volume_confirm_locked(audio_slot_t *slot)
{
    /* Régi köztes érték és még el nem küldött húzás nem nyugta. A később
     * megérkező valódi célérték a korábbi időtúllépés jelzését is feloldja. */
    if (!slot->volume_dirty && (slot->volume_pending || slot->volume_failed) &&
        slot->track.volume_known && slot->track.volume == slot->desired_volume)
    {
        slot->volume_pending = slot->volume_failed = false;
        slot->volume_deadline = 0;
    }
}

static bool audio_volume_in_message(const cJSON *message, unsigned player)
{
    const cJSON *rows = cJSON_GetObjectItemCaseSensitive(message, "audio_event");
    if (!rows)
        rows = cJSON_GetObjectItemCaseSensitive(message, "status_result");
    if (!cJSON_IsArray(rows))
        return false;
    const cJSON *row;
    cJSON_ArrayForEach(row, rows)
    {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(row, "playerid");
        if (cJSON_IsNumber(id) && id->valuedouble == player &&
            cJSON_GetObjectItemCaseSensitive(row, "volume"))
            return true;
    }
    return false;
}

bool app_loxone_audio_allowed(const char *id, unsigned revision, lx_audio_action_t action)
{
    if (!atomic_load(&audio_ready))
        return false;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    bool allowed = audio_allowed_locked(audio_slot_locked(id, revision), action);
    xSemaphoreGive(audio_lock);
    return allowed;
}

bool app_loxone_audio_value_allowed(const char *id, unsigned revision,
                                   lx_audio_action_t action, unsigned value)
{
    if (!atomic_load(&audio_ready))
        return false;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int slot = audio_slot_locked(id, revision);
    if (slot >= 0)
        audio_volume_expire_locked(&audio_page->slots[slot], esp_timer_get_time());
    bool allowed = audio_allowed_locked(slot, action) &&
        (action != LX_AUDIO_VOLUME || value <= audio_page->slots[slot].max_volume);
    xSemaphoreGive(audio_lock);
    return allowed;
}

bool app_loxone_audio_queued_allowed(const char *id, unsigned revision,
                                    lx_audio_action_t action, unsigned value,
                                    unsigned edit_generation)
{
    if (!atomic_load(&audio_ready))
        return false;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int slot = audio_slot_locked(id, revision);
    if (slot >= 0)
        audio_volume_expire_locked(&audio_page->slots[slot], esp_timer_get_time());
    bool allowed = audio_allowed_locked(slot, action) &&
        (action != LX_AUDIO_VOLUME || value <= audio_page->slots[slot].max_volume) &&
        (!audio_volume_action(action) ||
         (edit_generation && edit_generation == audio_page->slots[slot].edit_generation));
    xSemaphoreGive(audio_lock);
    return allowed;
}

void app_loxone_audio_cancel_volume(const char *id, unsigned revision)
{
    if (!atomic_load(&audio_ready))
        return;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int index = audio_slot_locked(id, revision);
    if (index >= 0)
    {
        audio_slot_t *slot = &audio_page->slots[index];
        audio_volume_cancel_locked(slot, false);
    }
    xSemaphoreGive(audio_lock);
}

bool app_loxone_audio_session(const char *username, const char *jwt, bool live)
{
    if (!audio_initialize())
        return false;
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
                audio_volume_cancel_locked(&audio_page->slots[i], false);
                audio_page->slots[i].status = valid ? LX_AUDIO_CONNECTING : LX_AUDIO_UNAVAILABLE;
                audio_page->slots[i].native_ready = false;
            }
    }
    xSemaphoreGive(audio_lock);
    if (!valid)
        app_loxone_audio_hide();
    xSemaphoreGive(audio_wake);
    return !live || valid;
}

void app_loxone_audio_hide(void)
{
    if (!atomic_load(&audio_ready))
        return;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    ++audio_generation;
    /* A futtató csak zár alatt olvassa az oldalt, és nem tart meg belőle
     * mutatót. A kapcsolat saját másolatokkal, a main szálán zárul később. */
    audio_page_t *released = audio_page;
    audio_page = NULL;
    unsigned generation = audio_generation;
    xSemaphoreGive(audio_lock);
    free(released);
    app_loxone_audio_media_clear(generation);
    xSemaphoreGive(audio_wake);
}

static void audio_native_unavailable_locked(void)
{
    if (audio_page)
        for (unsigned i = 0; i < audio_page->count; ++i)
            if (audio_page->slots[i].configured)
            {
                audio_page->slots[i].native_ready = false;
                audio_page->slots[i].status = LX_AUDIO_UNAVAILABLE;
            }
}

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
        ESP_LOGW("lox_audio", "Audio oldalmodell nem foglalható (PSRAM).");
        audio_web_error(NULL, "page-memory");
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
        slot->edit_generation = 1;
        slot->status = LX_AUDIO_CONNECTING;
        next->live |= slot->configured;
    }
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    next->generation = ++audio_generation;
    audio_page_t *old = audio_page;
    audio_page = next;
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
    if (!atomic_load(&audio_ready) || !uuid)
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
                audio_volume_confirm_locked(slot);
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
            if (slot->volume_pending && (!audio_allowed_locked((int)i, LX_AUDIO_VOLUME) ||
                (slot->max_known && slot->desired_volume > slot->max_volume)))
                audio_volume_cancel_locked(slot, true);
            audio_volume_expire_locked(slot, now);
        }
    xSemaphoreGive(audio_lock);
}

bool app_loxone_audio_summary(const char *id, unsigned revision, lx_audio_summary_t *out)
{
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    if (!atomic_load(&audio_ready))
        return false;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int index = audio_slot_locked(id, revision);
    if (index >= 0)
    {
        audio_slot_t *slot = &audio_page->slots[index];
        /* Az MS-kliens egy válaszra várhat; a GUI ettől függetlenül lezárja
         * az időkorlátos jelzést, a tényleges hangerő átírása nélkül. */
        audio_volume_expire_locked(slot, esp_timer_get_time());
        out->track = slot->track;
        out->generation = audio_page->generation;
        out->available = true;
        out->controllable = audio_allowed_locked(index, LX_AUDIO_PLAY);
        out->volume_up_down = (slot->endpoint.volume_modes & 4U) != 0;
        out->volume_max = slot->max_known ? slot->max_volume : 0;
        out->desired_volume = slot->desired_volume;
        out->volume_pending = slot->volume_pending;
        out->volume_failed = slot->volume_failed;
        out->status = slot->status;
        out->native_ready = slot->native_ready;
    }
    xSemaphoreGive(audio_lock);
    return index >= 0;
}

esp_err_t app_loxone_audio_command(const char *id, unsigned revision,
                                  lx_audio_action_t action, unsigned value, bool final)
{
    if (!atomic_load(&audio_ready) || action >= LX_AUDIO_FAVORITE)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    int index = audio_slot_locked(id, revision);
    bool ok = audio_allowed_locked(index, action);
    unsigned edit_generation = ok && audio_volume_action(action) ? audio_page->slots[index].edit_generation : 0;
    if (ok && action == LX_AUDIO_VOLUME)
    {
        audio_slot_t *slot = &audio_page->slots[index];
        ok = value <= slot->max_volume;
        if (ok)
        {
            slot->desired_volume = value;
            slot->volume_dirty = true;
            slot->volume_pending = true;
            slot->volume_failed = false;
            slot->volume_deadline = esp_timer_get_time() + AUDIO_VOLUME_CONFIRM_US;
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
    return app_loxone_audio_transport(id, revision, action, value, edit_generation);
}

esp_err_t app_loxone_audio_favorites_request(const char *id, unsigned revision, unsigned start)
{
    if (!atomic_load(&audio_ready))
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
    if (!out || !atomic_load(&audio_ready))
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
    if (!atomic_load(&audio_ready) || !favorite_id)
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
    if (!atomic_load(&audio_ready))
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

static void audio_fail(audio_connection_t *connection, const char *reason)
{
    /* Kizárólag az alábbi, fordításkor ismert oknevek kerülnek naplóba;
     * szerverválasz, parancs, JWT vagy munkamenetkulcs soha. */
    const char *empty = NULL;
    atomic_compare_exchange_strong(&connection->failure_reason, &empty, reason);
    connection->dead = true;
    xSemaphoreGive(audio_wake);
}

static void audio_diagnostic(const audio_connection_t *connection, const char *step)
{
    if (!connection->diagnostic)
        return;
    const char *reason = atomic_load(&connection->failure_reason);
    (void)step;
    (void)reason;
    ESP_LOGI("lox_audio", "Audio natív %s: phase=%u, text=%u, binary=%u, invalid_json=%u, ok=%s",
        step, connection->phase, atomic_load(&connection->text_frames),
        atomic_load(&connection->binary_frames), connection->invalid_json, reason ? reason : "nincs");
}

static void audio_web_error(const audio_connection_t *connection, const char *resource_reason)
{
    /* A Loxone/Audio workerek hívják az Audio-modellzáron kívül; WS
     * callbackből soha. Új kapcsolaton is él a közös 30 s korlát. Az
     * előjel nélküli eltelt idő hosszú csendnél és átforduláskor is jó. */
    unsigned now = (unsigned)(esp_timer_get_time() / 1000);
    unsigned last = atomic_load(&audio_last_web_error_ms);
    /* A nulla a még nem naplózott állapot. Pontosan t=0 esetén az előző
     * milliszekundum jelöli az első sort, így a következő hívás is ritkított. */
    unsigned stamp = now ? now : UINT32_MAX;
    do
    {
        if (last && (unsigned)(now - last) < 30000U)
            return;
    } while (!atomic_compare_exchange_weak(&audio_last_web_error_ms, &last, stamp));
    const char *reason = resource_reason;
    if (!reason && connection)
        reason = atomic_load(&connection->failure_reason);
    /* Csak fordításkor ismert oknevek és számlálók: nincs szerverválasz,
     * cím, felhasználónév, parancs, JWT vagy munkamenetkulcs. */
    app_events_add(APP_EVENT_WARNING,
        "Audio-kapcsolat: ok=%s; phase=%u; text=%u; binary=%u; JSON=%u.",
        reason ? reason : "connection", connection ? connection->phase : 0,
        connection ? atomic_load(&connection->text_frames) : 0,
        connection ? atomic_load(&connection->binary_frames) : 0,
        connection ? connection->invalid_json : 0);
}

static bool audio_receive_reserve(audio_connection_t *connection, size_t capacity)
{
    size_t owned = atomic_load(&connection->received_bytes);
    do
    {
        if (owned > AUDIO_RX_BYTES_MAX || capacity > AUDIO_RX_BYTES_MAX - owned)
            return false;
    } while (!atomic_compare_exchange_weak(&connection->received_bytes, &owned, owned + capacity));
    return true;
}

static void audio_buffer_release(audio_connection_t *connection, char *data, size_t capacity)
{
    if (!data)
        return;
    free(data);
    atomic_fetch_sub(&connection->received_bytes, capacity);
}

static void audio_message_release(audio_connection_t *connection, audio_message_t *message)
{
    audio_buffer_release(connection, message->data, message->capacity);
    *message = (audio_message_t){0};
}

static void audio_receive(void *argument, esp_event_base_t base, int32_t event, void *data)
{
    (void)base;
    audio_connection_t *connection = argument;
    if (event == WEBSOCKET_EVENT_CONNECTED)
    {
        if (connection->diagnostic)
            ESP_LOGI("lox_audio", "Audio natív WebSocket felépült.");
        return;
    }
    if (event == WEBSOCKET_EVENT_DISCONNECTED || event == WEBSOCKET_EVENT_ERROR)
    {
        audio_fail(connection, event == WEBSOCKET_EVENT_ERROR ? "transport" : "disconnect");
        return;
    }
    if (event != WEBSOCKET_EVENT_DATA || connection->dead)
        return;
    esp_websocket_event_data_t *part = data;
    if (part->op_code == 8)
    {
        audio_fail(connection, "server-close");
        return;
    }
    if (part->op_code != 0 && part->op_code != 1 && part->op_code != 2)
        return;
    if ((part->op_code == 1 || part->op_code == 2) && !part->payload_offset)
        connection->rx_opcode = part->op_code;
    if (connection->rx_opcode == 2)
    {
        if (part->op_code == 2 && !part->payload_offset &&
            atomic_fetch_add(&connection->binary_frames, 1) == 0 && connection->diagnostic)
            ESP_LOGW("lox_audio", "Audio natív bináris üzenetet küldött; a szöveges protokoll nem dolgozza fel.");
        if (part->fin && part->payload_len >= 0 && part->payload_offset >= 0 && part->data_len >= 0 &&
            (size_t)part->payload_offset + (size_t)part->data_len == (size_t)part->payload_len)
            connection->rx_opcode = 0;
        return;
    }
    if (connection->rx_opcode != 1)
    {
        audio_fail(connection, "unexpected-continuation");
        return;
    }
    if (part->op_code == 1 && !part->payload_offset)
    {
        connection->used = 0;
        atomic_fetch_add(&connection->text_frames, 1);
    }
    if (part->data_len < 0 || part->payload_len < 0 || part->payload_offset < 0 ||
        part->payload_offset > part->payload_len || part->data_len > part->payload_len - part->payload_offset ||
        (part->data_len && !part->data_ptr) || connection->used > LX_AUDIO_JSON_MAX ||
        (size_t)part->data_len > LX_AUDIO_JSON_MAX - connection->used)
    {
        audio_fail(connection, "frame-limit");
        return;
    }
    size_t need = connection->used + (size_t)part->data_len + 1;
    if (need > connection->capacity)
    {
        size_t capacity = AUDIO_RX_ROUNDED_SIZE(need);
        /* A sor és a worker által már kivett üzenetek is beleszámítanak.
         * Növeléskor az átmenetileg együtt élő régi és új puffer egyaránt. */
        if (!audio_receive_reserve(connection, capacity))
        {
            audio_fail(connection, "receive-byte-limit");
            return;
        }
        char *next = audio_alloc(capacity);
        if (!next)
        {
            atomic_fetch_sub(&connection->received_bytes, capacity);
            audio_fail(connection, "receive-memory");
            return;
        }
        if (connection->used)
            memcpy(next, connection->rx, connection->used);
        audio_buffer_release(connection, connection->rx, connection->capacity);
        connection->rx = next;
        connection->capacity = capacity;
    }
    if (part->data_len)
        memcpy(connection->rx + connection->used, part->data_ptr, part->data_len);
    connection->used += part->data_len;
    connection->rx[connection->used] = 0;
    if (part->payload_offset + part->data_len != part->payload_len || !part->fin)
        return;
    audio_message_t message = {.data = connection->rx, .length = connection->used,
        .capacity = connection->capacity};
    connection->rx = NULL;
    connection->capacity = connection->used = 0;
    connection->rx_opcode = 0;
    if (xQueueSend(connection->messages, &message, 0) != pdTRUE)
    {
        audio_message_release(connection, &message);
        audio_fail(connection, "receive-queue-full");
    }
    xSemaphoreGive(audio_wake);
}

static bool audio_connection_current_locked(const audio_connection_t *connection)
{
    return audio_page && audio_page->live && audio_credentials && audio_credentials->live &&
        audio_page->generation == connection->generation &&
        audio_credentials->generation == connection->credential_generation;
}

static bool audio_connection_matches(const audio_connection_t *connection, const audio_slot_t *slot)
{
    return connection->port == slot->endpoint.port && !strcmp(connection->host, slot->endpoint.host);
}

static bool audio_detail_current_locked(const audio_connection_t *connection,
                                         const audio_detail_command_t *detail)
{
    if (!audio_connection_current_locked(connection) || detail->slot < 0 ||
        (unsigned)detail->slot >= audio_page->count || audio_page->detail != detail->slot ||
        audio_page->generation != detail->generation || audio_page->revision != detail->revision ||
        audio_page->favorites_requested != detail->request)
        return false;
    const audio_slot_t *slot = &audio_page->slots[detail->slot];
    if (strcmp(slot->id, detail->id) || !slot->native_ready || !audio_connection_matches(connection, slot))
        return false;
    if (!detail->favorite)
        return true;
    if (!audio_page->favorites.ready || audio_page->favorites.generation != detail->list_generation ||
        !audio_allowed_locked(detail->slot, LX_AUDIO_FAVORITE))
        return false;
    for (unsigned i = 0; i < audio_page->favorites.count; ++i)
        if (audio_page->favorites.items[i].id == detail->favorite_id)
            return true;
    return false;
}

static bool audio_send_guarded(audio_connection_t *connection, const char *command,
                               const audio_detail_command_t *detail, bool *cancelled)
{
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    bool current = audio_connection_current_locked(connection) &&
        (!detail || audio_detail_current_locked(connection, detail));
    xSemaphoreGive(audio_lock);
    if (cancelled)
        *cancelled = !current;
    if (!current || !connection->ws || connection->dead || app_ota_operation_active())
        return false;
    /* A rövid kapu a hálózati átadás pontja; előtte a modal bezárása még
     * visszavonhatja a kivett kérést. Hálózati várakozás alatt nincs modellzár. */
    bool sent = esp_websocket_client_send_text(connection->ws, command, strlen(command),
                                               pdMS_TO_TICKS(1000)) == (int)strlen(command);
    if (!sent)
        audio_fail(connection, "send");
    return sent;
}

static bool audio_send(audio_connection_t *connection, const char *command)
{
    return audio_send_guarded(connection, command, NULL, NULL);
}

static void audio_staged_clear(audio_connection_t *connection)
{
    free(connection->staged_tracks);
    connection->staged_tracks = NULL;
    connection->staged_mask = 0;
}

static void audio_connection_close(audio_connection_t *connection)
{
    audio_staged_clear(connection);
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
        audio_diagnostic(connection, "zárás előtt");
        esp_websocket_client_stop(connection->ws);
        esp_websocket_client_destroy(connection->ws);
        connection->ws = NULL;
        audio_diagnostic(connection, "lezárva");
    }
    if (connection->messages)
    {
        audio_message_t message;
        while (xQueueReceive(connection->messages, &message, 0) == pdTRUE)
            audio_message_release(connection, &message);
    }
    audio_buffer_release(connection, connection->rx, connection->capacity);
    connection->rx = NULL;
    connection->used = connection->capacity = 0;
    connection->rx_opcode = 0;
    connection->phase = 0;
    connection->favorites_slot = -1;
    connection->status_pending = 0;
    /* A régi WS-task már leállt. A hibát most elfogyasztjuk, különben a
     * worker minden körben újra öt másodpercre tolná az újracsatlakozást. */
    connection->dead = false;
    mbedtls_platform_zeroize(connection->session, sizeof(connection->session));
}

static bool audio_stage_message(audio_connection_t *connection, const cJSON *message)
{
    if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(message, "audio_event")))
        return true;
    bool fresh = !connection->staged_tracks;
    if (fresh)
        connection->staged_tracks = audio_alloc(LX_HOME_MAX_CARDS * sizeof(*connection->staged_tracks));
    if (!connection->staged_tracks)
    {
        ESP_LOGW("lox_audio", "Audio kezdeti metaadat nem foglalható (PSRAM).");
        audio_fail(connection, "initial-metadata-memory");
        return false;
    }
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    bool current = audio_connection_current_locked(connection);
    if (current)
        for (unsigned i = 0; i < audio_page->count; ++i)
        {
            audio_slot_t *slot = &audio_page->slots[i];
            if (!slot->configured || !audio_connection_matches(connection, slot))
                continue;
            if (fresh)
                connection->staged_tracks[i] = slot->track;
            if (lx_audio_track_update(&connection->staged_tracks[i], slot->endpoint.player, message))
                connection->staged_mask |= 1U << i;
        }
    xSemaphoreGive(audio_lock);
    if (!current)
        audio_staged_clear(connection);
    return current;
}

static void audio_staged_publish_locked(audio_connection_t *connection, unsigned index)
{
    if (!connection->staged_tracks || !(connection->staged_mask & (1U << index)))
        return;
    lx_audio_track_t *target = &audio_page->slots[index].track;
    const lx_audio_track_t *staged = &connection->staged_tracks[index];
    /* A nyitáskori zene-meta csak sikeres, még aktuális auth után látszik.
     * Volume/mode közben frissebben jöhetett a Miniserverről: azt megtartjuk. */
    memcpy(target->title, staged->title, sizeof(target->title));
    memcpy(target->artist, staged->artist, sizeof(target->artist));
    memcpy(target->album, staged->album, sizeof(target->album));
    memcpy(target->station, staged->station, sizeof(target->station));
    memcpy(target->cover, staged->cover, sizeof(target->cover));
    target->version = staged->version;
}

static void audio_connection_open(audio_connection_t *connection, unsigned selected)
{
    if (app_ota_operation_active())
        return;
    audio_staged_clear(connection);
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
    int64_t now = esp_timer_get_time();
    /* Ismétlődő hálózati hibánál legfeljebb 30 másodpercenként egy próbálkozás
     * részleteit írjuk. Nincs periodikus, zeneszám- vagy tokennaplózás. */
    connection->diagnostic = now >= connection->next_diagnostic;
    if (connection->diagnostic)
    {
        connection->next_diagnostic = now + 30000000LL;
        ESP_LOGI("lox_audio", "Audio natív nyitás: host=%s, port=%u, kártya=%u, gen=%u",
            connection->host, connection->port, selected, connection->generation);
    }
    atomic_store(&connection->text_frames, 0);
    atomic_store(&connection->binary_frames, 0);
    atomic_store(&connection->failure_reason, NULL);
    connection->invalid_json = 0;
    connection->first_native_data = false;
    snprintf(uri, sizeof(uri), "ws://%s:%u/", connection->host, connection->port);
    esp_websocket_client_config_t options = {
        .uri = uri, .subprotocol = "remotecontrol", .disable_auto_reconnect = true,
        .buffer_size = 2048, .task_stack = 4096, .network_timeout_ms = 3000,
    };
    connection->dead = false;
    connection->phase = 1;
    connection->deadline = now + AUDIO_TIMEOUT_US;
    connection->last_rotate = now;
    connection->ws = esp_websocket_client_init(&options);
    if (!connection->ws || esp_websocket_register_events(connection->ws, WEBSOCKET_EVENT_ANY,
        audio_receive, connection) != ESP_OK || esp_websocket_client_start(connection->ws) != ESP_OK)
        audio_fail(connection, "websocket-start");
}

static int audio_random(void *context, unsigned char *bytes, size_t length)
{
    (void)context;
    esp_fill_random(bytes, length);
    return 0;
}

static void audio_message(audio_connection_t *connection, const audio_message_t *message)
{
    if (connection->dead)
        return;
    if (connection->phase == 1 && lx_audio_session_token(message->data, connection->session))
    {
        audio_diagnostic(connection, "greeting rendben");
        if (!audio_send(connection, "audio/cfg/getkey/full"))
            connection->dead = true;
        connection->phase = 2;
        return;
    }
    cJSON *root = lx_home_document_parse(message->data, message->length);
    if (!root)
    {
        if (++connection->invalid_json == 1)
            audio_diagnostic(connection, "nem feldolgozható JSON/greeting");
        return;
    }
    if (connection->phase > 0 && connection->phase < 4 && !audio_stage_message(connection, root))
    {
        cJSON_Delete(root);
        return;
    }
    if (connection->phase == 2)
    {
        const cJSON *keys = cJSON_GetObjectItemCaseSensitive(root, "getkey_result");
        const cJSON *key = cJSON_GetArrayItem(keys, 0);
        const cJSON *pem = cJSON_GetObjectItemCaseSensitive(key, "pubkey");
        if (cJSON_IsString(pem))
        {
            audio_diagnostic(connection, "nyilvános kulcs megérkezett");
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
            if (!command)
                audio_fail(connection, "authentication-envelope");
            else if (!audio_send(connection, command))
                audio_fail(connection, "authentication-send");
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
            if (!ok)
                audio_fail(connection, "authentication-rejected");
            audio_diagnostic(connection, ok ? "hitelesítés kész" : "hitelesítés elutasítva");
            xSemaphoreTake(audio_lock, portMAX_DELAY);
            if (audio_connection_current_locked(connection))
                for (unsigned i = 0; i < audio_page->count; ++i)
                    if (audio_page->slots[i].configured && audio_connection_matches(connection, &audio_page->slots[i]))
                    {
                        if (ok)
                        {
                            audio_staged_publish_locked(connection, i);
                            connection->status_pending |= 1U << i;
                        }
                        audio_page->slots[i].status = ok ? LX_AUDIO_READY : LX_AUDIO_AUTH_FAILED;
                        audio_page->slots[i].native_ready = ok;
                    }
            xSemaphoreGive(audio_lock);
            audio_staged_clear(connection);
        }
    }
    else if (connection->phase == 4)
    {
        xSemaphoreTake(audio_lock, portMAX_DELAY);
        bool changed = false;
        if (audio_connection_current_locked(connection))
        {
            for (unsigned i = 0; i < audio_page->count; ++i)
                if (audio_page->slots[i].configured && audio_connection_matches(connection, &audio_page->slots[i]))
                {
                    audio_slot_t *slot = &audio_page->slots[i];
                    changed |= lx_audio_track_update(&slot->track, slot->endpoint.player, root);
                    if (audio_volume_in_message(root, slot->endpoint.player))
                        audio_volume_confirm_locked(slot);
                }
            if (cJSON_GetObjectItemCaseSensitive(root, "getroomfavs_result") && connection->favorites_slot >= 0)
            {
                lx_audio_favorites_match_t match = lx_audio_favorites_match(connection->favorites_player,
                    connection->favorites_start, root);
                if (match != LX_AUDIO_FAVORITES_UNRELATED)
                {
                    if (audio_page->detail == connection->favorites_slot &&
                        audio_page->favorites_requested == connection->favorites_request)
                    {
                        bool parsed = match == LX_AUDIO_FAVORITES_MATCH &&
                            lx_audio_favorites_parse(&audio_page->favorites, connection->favorites_player,
                                connection->favorites_start, root);
                        if (parsed)
                            audio_page->favorites.generation = connection->favorites_request;
                        else
                            audio_page->favorites.failed = true;
                        if (connection->diagnostic)
                            ESP_LOGI("lox_audio", "Audio kedvenclista válasz: player=%u, start=%u, parsed=%u",
                                connection->favorites_player, connection->favorites_start, (unsigned)parsed);
                    }
                    /* Csak a saját kérés válaszát fogyasztjuk el. A régi ablak
                     * saját válasza sem írhatja át az időközben nyitott listát. */
                    connection->favorites_slot = -1;
                }
            }
        }
        xSemaphoreGive(audio_lock);
        if (changed && !connection->first_native_data)
        {
            connection->first_native_data = true;
            audio_diagnostic(connection, "első zónaadat feldolgozva");
        }
    }
    cJSON_Delete(root);
}

void app_loxone_audio_poll(int64_t now)
{
    if (!atomic_load(&audio_ready))
        return;
    /* A natív WS felépítése, bontása és hitelesítése másik feladatban várhat.
     * A Miniserver rövid köre csak a legfrissebb abszolút célértéket veszi ki;
     * a két kapcsolat nem osztozik a hangerő ütemezésén. */
    for (unsigned i = 0; i < LX_HOME_MAX_CARDS; ++i)
    {
        char id[LX_HOME_ID_SIZE] = {0};
        unsigned revision = 0, value = 0, generation = 0, edit_generation = 0;
        xSemaphoreTake(audio_lock, portMAX_DELAY);
        if (audio_page && audio_page->live && i < audio_page->count)
        {
            audio_slot_t *slot = &audio_page->slots[i];
            audio_volume_expire_locked(slot, now);
            if (slot->volume_pending && slot->max_known && slot->desired_volume > slot->max_volume)
                audio_volume_cancel_locked(slot, true);
            if (slot->volume_dirty && audio_allowed_locked((int)i, LX_AUDIO_VOLUME) &&
                (slot->volume_final || now - slot->last_volume_sent >= AUDIO_VOLUME_INTERVAL_US))
            {
                snprintf(id, sizeof(id), "%s", slot->id);
                revision = audio_page->revision;
                generation = audio_page->generation;
                edit_generation = slot->edit_generation;
                value = slot->desired_volume;
                slot->volume_dirty = slot->volume_final = false;
                slot->last_volume_sent = now;
            }
        }
        xSemaphoreGive(audio_lock);
        if (id[0])
        {
            esp_err_t result = app_loxone_audio_transport(id, revision, LX_AUDIO_VOLUME, value, edit_generation);
            xSemaphoreTake(audio_lock, portMAX_DELAY);
            if (audio_page && audio_page->generation == generation)
            {
                audio_slot_t *slot = &audio_page->slots[i];
                audio_volume_expire_locked(slot, esp_timer_get_time());
                /* A sor által még el nem fogadott abszolút célérték felülírható.
                 * Elfogadott parancsot időtúllépés után nem ismétlünk meg.
                 * Az MS-eredmény nem írhatja felül a natív kapcsolat hibáját. */
                if (slot->edit_generation == edit_generation && result != ESP_OK && !slot->volume_dirty)
                {
                    if (result == ESP_ERR_INVALID_STATE && slot->volume_pending &&
                        audio_allowed_locked((int)i, LX_AUDIO_VOLUME))
                        slot->volume_dirty = true;
                    else
                        audio_volume_cancel_locked(slot, true);
                }
            }
            xSemaphoreGive(audio_lock);
        }
    }
}

static void audio_status_poll(audio_connection_t *connection)
{
    unsigned player = 0;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (audio_connection_current_locked(connection) && connection->phase == 4)
        for (unsigned i = 0; i < audio_page->count; ++i)
            if (connection->status_pending & (1U << i))
            {
                connection->status_pending &= ~(1U << i);
                if (audio_page->slots[i].configured && audio_connection_matches(connection, &audio_page->slots[i]))
                {
                    player = audio_page->slots[i].endpoint.player;
                    break;
                }
            }
    xSemaphoreGive(audio_lock);
    if (!player)
        return;
    /* Körönként egy kérés; közben a fogadási sor kiürül. A magasabb
     * prioritású WS-task gyors válaszai így 12 kártyánál sem torlódnak. */
    char command[48];
    snprintf(command, sizeof(command), "audio/%u/status", player);
    if (!audio_send(connection, command))
        audio_fail(connection, "status-send");
}

static void audio_native_poll(audio_connection_t *connection)
{
    char command[96] = {0};
    int detail = -1;
    bool favorite = false;
    audio_detail_command_t guard = {0};
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (audio_page && audio_page->live && audio_page->generation == connection->generation &&
        audio_page->detail >= 0 && connection->phase == 4)
    {
        detail = audio_page->detail;
        audio_slot_t *slot = &audio_page->slots[detail];
        guard.generation = audio_page->generation;
        guard.revision = audio_page->revision;
        guard.request = audio_page->favorites_requested;
        guard.list_generation = audio_page->favorites.generation;
        guard.favorite_id = audio_page->favorite_id;
        guard.slot = detail;
        snprintf(guard.id, sizeof(guard.id), "%s", slot->id);
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
    {
        audio_status_poll(connection);
        return;
    }
    if (!favorite)
        audio_diagnostic(connection, "kedvenclista kérés");
    guard.favorite = favorite;
    bool cancelled = false;
    bool sent = audio_send_guarded(connection, command, &guard, &cancelled);
    if (cancelled)
    {
        if (!favorite && connection->favorites_slot == detail && connection->favorites_request == guard.request)
            connection->favorites_slot = -1;
        return;
    }
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    if (audio_detail_current_locked(connection, &guard))
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
    audio_fail(connection, "favorites-timeout");
}

typedef struct
{
    audio_connection_t *connection;
    unsigned selected;
    int64_t retry;
} audio_controller_t;

static void audio_controller_close(audio_controller_t *controller)
{
    audio_connection_t *connection = controller->connection;
    if (connection)
    {
        /* Csak a main futtató hívja. Az UI/MS szál legfeljebb érvénytelenít
         * és ébreszt; hálózati leállásra soha nem vár a modellzár alatt. */
        audio_connection_close(connection);
        if (connection->messages)
            vQueueDeleteWithCaps(connection->messages);
        free(connection);
        controller->connection = NULL;
        ESP_LOGI("lox_audio", "Audio kapcsolat felszabadult; main verem minimum: %u",
            (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    controller->selected = 0;
    controller->retry = 0;
}

static TickType_t audio_controller_step(audio_controller_t *controller, int64_t now)
{
    bool paused = app_ota_operation_active();
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    bool alive = audio_page && audio_page->live && audio_credentials->live;
    audio_page_t *released = NULL;
    if (audio_page && !audio_page->live)
    {
        released = audio_page;
        audio_page = NULL;
    }
    xSemaphoreGive(audio_lock);
    free(released);
    if (!alive || paused)
    {
        audio_controller_close(controller);
        /* Session/page átadás ébreszt. OTA végén nincs Audio-hívó, ezért
         * az élő, szünetelő oldal rövid körben ellenőrzi a foglalást. */
        return alive ? pdMS_TO_TICKS(50) : portMAX_DELAY;
    }
    if (!controller->connection)
    {
        if (now < controller->retry)
            return pdMS_TO_TICKS(50);
        audio_connection_t *connection = audio_alloc(sizeof(*connection));
        if (connection)
        {
            connection->messages = xQueueCreateWithCaps(AUDIO_RX_QUEUE_LENGTH, sizeof(audio_message_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            connection->favorites_slot = -1;
        }
        if (!connection || !connection->messages)
        {
            audio_web_error(connection, connection ? "receive-queue-memory" : "connection-memory");
            free(connection);
            xSemaphoreTake(audio_lock, portMAX_DELAY);
            audio_native_unavailable_locked();
            xSemaphoreGive(audio_lock);
            controller->retry = now + AUDIO_RESOURCE_RETRY_US;
            return pdMS_TO_TICKS(50);
        }
        controller->connection = connection;
    }
    audio_connection_t *connection = controller->connection;
    xSemaphoreTake(audio_lock, portMAX_DELAY);
    alive = audio_page && audio_page->live && audio_credentials->live;
    bool stale = alive && (connection->generation != audio_page->generation ||
        connection->credential_generation != audio_credentials->generation);
    int target = alive ? audio_page->detail : -1;
    if (alive && target < 0)
    {
        if (controller->selected >= audio_page->count || !audio_page->slots[controller->selected].configured)
            controller->selected = 0;
        for (unsigned i = 0; i < audio_page->count; ++i)
            if (audio_page->slots[(controller->selected + i) % audio_page->count].configured)
            {
                controller->selected = (controller->selected + i) % audio_page->count;
                target = (int)controller->selected;
                break;
            }
        if (target >= 0 && connection->phase == 4 && now - connection->last_rotate > 15000000LL)
            for (unsigned i = 1; i < audio_page->count; ++i)
            {
                unsigned next = (controller->selected + i) % audio_page->count;
                if (audio_page->slots[next].configured &&
                    !audio_connection_matches(connection, &audio_page->slots[next]))
                {
                    controller->selected = next;
                    target = (int)next;
                    break;
                }
            }
    }
    bool switch_server = alive && target >= 0 && connection->ws &&
        !audio_connection_matches(connection, &audio_page->slots[target]);
    xSemaphoreGive(audio_lock);
    if (!alive)
    {
        audio_controller_close(controller);
        return portMAX_DELAY;
    }
    if (stale || switch_server || connection->dead ||
        (connection->phase > 0 && connection->phase < 4 && now > connection->deadline))
    {
        if (!connection->dead && connection->phase > 0 && connection->phase < 4 && now > connection->deadline)
            audio_fail(connection, "handshake-timeout");
        bool failed = connection->dead || (connection->phase > 0 && connection->phase < 4 &&
            now > connection->deadline);
        if (failed && !stale && !switch_server)
            audio_web_error(connection, NULL);
        audio_connection_close(connection);
        controller->retry = failed ? esp_timer_get_time() + AUDIO_RESOURCE_RETRY_US : 0;
    }
    if (!connection->ws && target >= 0 && now >= controller->retry)
        audio_connection_open(connection, (unsigned)target);
    audio_message_t message;
    /* A körben legfeljebb a sor kapacitásával dolgozunk; a folyamatos
     * adatfolyam sem akadályozhatja az oldal/OTA érvénytelenítés észlelését. */
    for (unsigned processed = 0; processed < AUDIO_RX_QUEUE_LENGTH &&
         xQueueReceive(connection->messages, &message, 0) == pdTRUE; ++processed)
    {
        audio_message(connection, &message);
        audio_message_release(connection, &message);
    }
    audio_native_poll(connection);
    audio_favorites_timeout(connection, esp_timer_get_time());
    return pdMS_TO_TICKS(50);
}

void app_loxone_audio_run(void)
{
    /* Egyetlen tulajdonos: az indulást befejezett Master main-feladata.
     * Nincs új verem/TCB, task-újraindítás vagy önfelszámoló cleanup-task. */
    if (atomic_exchange(&audio_runner_active, true))
        return;
    audio_controller_t controller = {0};
    for (;;)
    {
        if (!audio_initialize())
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        TickType_t wait = audio_controller_step(&controller, esp_timer_get_time());
        xSemaphoreTake(audio_wake, wait);
    }
}

#else
/* A szimulátor kizárólag explicit helyi mintát mutat; élő szerverre nem lép be. */
esp_err_t app_loxone_audio_init(void) { return ESP_OK; }
void app_loxone_audio_run(void) {}
#include "../simulator/audio_fixture.h"

typedef struct
{
    char id[LX_HOME_ID_SIZE];
    unsigned revision, edit_generation;
    lx_audio_summary_t summary;
} audio_fixture_t;

static audio_fixture_t audio_fixtures[LX_HOME_MAX_CARDS];
static lx_audio_favorites_t audio_fixture_favorites;
static unsigned audio_fixture_count, audio_fixture_favorite_total;
static unsigned audio_fixture_page_size = LX_AUDIO_FAVORITE_PAGE;
static char audio_fixture_detail[LX_HOME_ID_SIZE];
unsigned app_sim_audio_commands, app_sim_audio_last_favorite;
unsigned app_sim_audio_volume_cancellations;
lx_audio_action_t app_sim_audio_last_action;

void app_sim_audio_reset(void)
{
    memset(audio_fixtures, 0, sizeof(audio_fixtures));
    memset(&audio_fixture_favorites, 0, sizeof(audio_fixture_favorites));
    audio_fixture_count = audio_fixture_favorite_total = 0;
    audio_fixture_page_size = LX_AUDIO_FAVORITE_PAGE;
    audio_fixture_detail[0] = 0;
    app_sim_audio_commands = app_sim_audio_last_favorite = 0;
    app_sim_audio_volume_cancellations = 0;
    app_sim_audio_last_action = LX_AUDIO_PLAY;
    app_sim_audio_media_busy(false);
    app_sim_audio_media_request_reset();
}

bool app_sim_audio_configure(const char *id, unsigned revision, const lx_audio_summary_t *summary)
{
    if (!id || !id[0] || strlen(id) >= LX_HOME_ID_SIZE || !summary || audio_fixture_count >= LX_HOME_MAX_CARDS)
        return false;
    audio_fixture_t *fixture = &audio_fixtures[audio_fixture_count++];
    snprintf(fixture->id, sizeof(fixture->id), "%s", id);
    fixture->revision = revision;
    fixture->edit_generation = 1;
    fixture->summary = *summary;
    return true;
}

void app_sim_audio_set_favorite_count(unsigned total)
{
    audio_fixture_favorite_total = total;
}

void app_sim_audio_set_favorite_page_size(unsigned count)
{
    audio_fixture_page_size = count && count <= LX_AUDIO_FAVORITE_PAGE ? count : LX_AUDIO_FAVORITE_PAGE;
}

void app_sim_audio_set_favorite_state(bool ready, bool failed)
{
    audio_fixture_favorites.ready = ready;
    audio_fixture_favorites.failed = failed;
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

bool app_sim_audio_update(const char *id, unsigned revision, const lx_audio_summary_t *summary)
{
    audio_fixture_t *fixture = audio_fixture_find(id, revision);
    if (!fixture || !summary)
        return false;
    fixture->summary = *summary;
    return true;
}

bool app_loxone_audio_session(const char *username, const char *jwt, bool live)
{ (void)username; (void)jwt; (void)live; return true; }
void app_loxone_audio_page(const lx_home_config_t *config, const cJSON *catalog, int page, unsigned revision)
{ (void)config; (void)catalog; (void)page; (void)revision; }
void app_loxone_audio_hide(void) { app_loxone_audio_media_clear(0); }
void app_loxone_audio_state(const char *uuid, bool numeric, double value, const char *text, size_t length, int64_t now)
{ (void)uuid; (void)numeric; (void)value; (void)text; (void)length; (void)now; }
void app_loxone_audio_poll(int64_t now) { (void)now; }

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

bool app_loxone_audio_queued_allowed(const char *id, unsigned revision,
                                    lx_audio_action_t action, unsigned value,
                                    unsigned edit_generation)
{
    audio_fixture_t *fixture = audio_fixture_find(id, revision);
    return app_loxone_audio_value_allowed(id, revision, action, value) &&
        (!audio_volume_action(action) || (edit_generation && fixture->edit_generation == edit_generation));
}

void app_loxone_audio_cancel_volume(const char *id, unsigned revision)
{
    audio_fixture_t *fixture = audio_fixture_find(id, revision);
    if (fixture)
    {
        if (++fixture->edit_generation == 0)
            ++fixture->edit_generation;
        fixture->summary.volume_pending = false;
        ++app_sim_audio_volume_cancellations;
    }
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
    if (audio_fixture_favorites.count > audio_fixture_page_size)
        audio_fixture_favorites.count = audio_fixture_page_size;
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
