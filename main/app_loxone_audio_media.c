/* A meglévő OTA-háttérfeladat képenként dolgozza fel a borítókéréseket.
 * Nincs külön task, SD-írás vagy hitelesítő HTTP fejléc: a Miniserver JWT
 * sohasem juthat a külső képszolgáltatóhoz. */
#include "app_loxone_audio_media.h"
#include "app_loxone_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef APP_TEMPLATE_DESKTOP
#include <stdatomic.h>
#include <strings.h>
#include "app_events.h"
#include "app_ota.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MEDIA_ATTEMPTS_MAX 3U
#define MEDIA_RETRY_FIRST_US 2000000LL
#define MEDIA_RETRY_SECOND_US 5000000LL
#define MEDIA_ERROR_LOG_INTERVAL_US 10000000LL

typedef struct
{
    char url[LX_AUDIO_URL_SIZE];
    unsigned version, size, sequence, attempts;
    int64_t retry_at;
    bool pending;
    lx_audio_image_t *image;
} media_slot_t;

typedef struct
{
    char url[LX_AUDIO_URL_SIZE];
    unsigned slot, generation, version, size, sequence;
} media_job_t;

typedef struct
{
    char location[LX_AUDIO_URL_SIZE];
    bool invalid;
} media_headers_t;

typedef enum
{
    MEDIA_OK,
    MEDIA_CANCELLED,
    MEDIA_MEMORY,
    MEDIA_WORKER,
    MEDIA_CONNECT,
    MEDIA_HEADERS,
    MEDIA_READ,
    MEDIA_INCOMPLETE,
    MEDIA_DEADLINE,
    MEDIA_HTTP_STATUS,
    MEDIA_REDIRECT,
    MEDIA_SIZE_LIMIT,
    MEDIA_DECODE,
    MEDIA_CACHE_LIMIT
} media_error_t;

typedef struct
{
    media_error_t error;
    int http_status;
} media_result_t;

typedef struct
{
    media_result_t result;
    unsigned slot, attempts, retry_seconds;
    bool pending, recovered;
} media_notice_t;

static SemaphoreHandle_t media_lock;
static media_slot_t *media_slots;
static unsigned media_generation;
static atomic_flag media_initializing = ATOMIC_FLAG_INIT;
static atomic_bool media_ready;
static atomic_uint media_requested_generation;
static atomic_bool media_active;
static int64_t media_start_after;
static int64_t media_next_error_log;

bool app_loxone_audio_media_idle(void)
{
    return !atomic_load(&media_active);
}

static void *media_alloc(size_t size)
{
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static bool media_initialize(void)
{
    if (atomic_load(&media_ready))
        return true;
    if (atomic_flag_test_and_set(&media_initializing))
        return false;
    if (!media_lock)
        media_lock = xSemaphoreCreateMutex();
    bool ok = media_lock != NULL;
    if (ok)
        atomic_store(&media_ready, true);
    atomic_flag_clear(&media_initializing);
    return ok;
}

static void media_release_locked(lx_audio_image_t *image)
{
    if (image && image->references && --image->references == 0)
        lx_audio_image_free(image);
}

static void media_sync_generation_locked(void)
{
    unsigned requested = atomic_load(&media_requested_generation);
    if (media_generation == requested)
        return;
    media_generation = requested;
    media_start_after = 0;
    if (media_slots)
        for (unsigned i = 0; i < LX_AUDIO_MEDIA_SLOTS; ++i)
            media_release_locked(media_slots[i].image);
    /* Az URL-ek és a slotok is csak a látható oldal munkatárai. A mutex
     * megmarad a még kifutó worker és a UI-nál levő képreferenciák miatt,
     * de clear/bezárás önmagában nem foglal új táblát. */
    free(media_slots);
    media_slots = NULL;
}

void app_loxone_audio_media_release(lx_audio_image_t *image)
{
    if (!image || !media_lock)
        return;
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_release_locked(image);
    xSemaphoreGive(media_lock);
}

lx_audio_image_t *app_loxone_audio_media_acquire(unsigned slot, unsigned generation)
{
    if (slot >= LX_AUDIO_MEDIA_SLOTS || !atomic_load(&media_ready))
        return NULL;
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_sync_generation_locked();
    lx_audio_image_t *image = media_slots && generation == media_generation ? media_slots[slot].image : NULL;
    if (image)
        ++image->references;
    xSemaphoreGive(media_lock);
    return image;
}

void app_loxone_audio_media_clear(unsigned generation)
{
    /* A clear az egyszeri inicializálással átfedve sem veszhet el. Nem foglal
     * képtárolót pusztán egy, Audio nélküli nézet bezárása miatt. */
    unsigned previous = atomic_load(&media_requested_generation);
    while (generation != previous && generation - previous <= UINT_MAX / 2U)
        if (atomic_compare_exchange_weak(&media_requested_generation, &previous, generation))
            break;
    /* A régebbi UI-hide később is megérkezhet, mint az új oldal publish-ja.
     * Az elavult clear nem teheti vissza a generációt; a kivonás a számláló
     * átfordulását is kezeli. */
    if (!atomic_load(&media_ready))
        return;
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_sync_generation_locked();
    xSemaphoreGive(media_lock);
}

void app_loxone_audio_media_close_detail(void)
{
    if (!atomic_load(&media_ready))
        return;
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_sync_generation_locked();
    if (!media_slots)
    {
        xSemaphoreGive(media_lock);
        return;
    }
    media_slot_t *slot = &media_slots[LX_AUDIO_MEDIA_SLOTS - 1];
    media_release_locked(slot->image);
    unsigned sequence = slot->sequence + 1;
    memset(slot, 0, sizeof(*slot));
    slot->sequence = sequence;
    xSemaphoreGive(media_lock);
}

static bool media_current(const media_job_t *job)
{
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_sync_generation_locked();
    bool current = media_slots && job->generation == media_generation &&
        job->sequence == media_slots[job->slot].sequence &&
        !strcmp(job->url, media_slots[job->slot].url);
    xSemaphoreGive(media_lock);
    return current && !app_ota_operation_active();
}

static bool media_retryable(const media_result_t *result)
{
    switch (result->error)
    {
        case MEDIA_MEMORY:
        case MEDIA_WORKER:
        case MEDIA_CONNECT:
        case MEDIA_HEADERS:
        case MEDIA_READ:
        case MEDIA_INCOMPLETE:
        case MEDIA_DEADLINE:
            return true;
        case MEDIA_HTTP_STATUS:
            return result->http_status == 408 || result->http_status == 429 ||
                   (result->http_status >= 500 && result->http_status < 600);
        default:
            return false;
    }
}

static void media_retry_locked(media_slot_t *slot, bool retryable, int64_t now)
{
    slot->pending = false;
    slot->retry_at = retryable && slot->attempts < MEDIA_ATTEMPTS_MAX ?
        now + (slot->attempts == 1 ? MEDIA_RETRY_FIRST_US : MEDIA_RETRY_SECOND_US) : 0;
}

static void media_notice_locked(unsigned index, const media_result_t *result, media_notice_t *notice,
                                int64_t now)
{
    if (result->error == MEDIA_CANCELLED || now < media_next_error_log)
        return;
    media_next_error_log = now + MEDIA_ERROR_LOG_INTERVAL_US;
    const media_slot_t *slot = &media_slots[index];
    *notice = (media_notice_t){.result = *result, .slot = index, .attempts = slot->attempts,
        .retry_seconds = slot->retry_at > now ? (unsigned)((slot->retry_at - now) / 1000000LL) : 0,
        .pending = true};
}

static void media_notice_emit(const media_notice_t *notice)
{
    if (!notice->pending)
        return;
    if (notice->recovered)
    {
        app_events_add(APP_EVENT_INFO, "Audio borító helyreállt: hely=%u; próba=%u/%u.",
            notice->slot + 1, notice->attempts, MEDIA_ATTEMPTS_MAX);
        return;
    }
    static const char *const reasons[] = {
        "ok", "cancel", "memory", "worker", "connect", "headers", "read", "incomplete",
        "timeout", "http", "redirect", "size", "decode", "cache"
    };
    /* Csak statikus ok és számlálók: URL, szervernév, cím és hitelesítő
     * adat nem kerül a közös, mindössze húsz bejegyzéses eseménynaplóba. */
    app_events_add(APP_EVENT_WARNING,
        "Audio borító: hely=%u; ok=%s; HTTP=%d; próba=%u/%u; újra=%u s.",
        notice->slot + 1, reasons[notice->result.error], notice->result.http_status,
        notice->attempts, MEDIA_ATTEMPTS_MAX, notice->retry_seconds);
}

static void media_start_failed_locked(media_error_t error, media_notice_t *notice)
{
    /* Sikertelen ütemezésnél vagy munkapuffer-foglalásnál sem próbálkozunk minden
     * UI-frissítéskor. A következő esedékes kérést az aktív nézet élesíti. */
    int64_t now = esp_timer_get_time();
    media_start_after = now + MEDIA_RETRY_FIRST_US;
    media_result_t result = {.error = error};
    for (unsigned i = 0; i < LX_AUDIO_MEDIA_SLOTS; ++i)
        if (media_slots[i].pending)
        {
            ++media_slots[i].attempts;
            media_retry_locked(&media_slots[i], true, now);
            media_notice_locked(i, &result, notice, now);
        }
}

static esp_err_t media_http_event(esp_http_client_event_t *event)
{
    media_headers_t *headers = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "Location"))
    {
        if (strlen(event->header_value) >= sizeof(headers->location))
            headers->invalid = true;
        else
            snprintf(headers->location, sizeof(headers->location), "%s", event->header_value);
    }
    return ESP_OK;
}

static uint8_t *media_download(const media_job_t *job, size_t *length, media_result_t *result)
{
    *result = (media_result_t){.error = MEDIA_CANCELLED};
    if (!media_current(job))
        return NULL;
    *result = (media_result_t){.error = MEDIA_MEMORY};
    uint8_t *body = media_alloc(LX_AUDIO_MEDIA_BYTES_MAX);
    media_headers_t *headers = media_alloc(sizeof(*headers));
    char *url = media_alloc(LX_AUDIO_URL_SIZE);
    if (!body || !headers || !url)
    {
        free(body);
        free(headers);
        free(url);
        return NULL;
    }
    snprintf(url, LX_AUDIO_URL_SIZE, "%s", job->url);
    bool success = false;
    result->error = MEDIA_CANCELLED;
    int64_t deadline = esp_timer_get_time() + 8000000LL;
    for (unsigned redirect = 0; redirect < 4 && media_current(job); ++redirect)
    {
        if (esp_timer_get_time() >= deadline)
        {
            result->error = MEDIA_DEADLINE;
            break;
        }
        memset(headers, 0, sizeof(*headers));
        esp_http_client_config_t options = {
            .url = url, .timeout_ms = 2000, .disable_auto_redirect = true,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = media_http_event, .user_data = headers,
            .buffer_size = 2048, .buffer_size_tx = 1024,
        };
        esp_http_client_handle_t http = esp_http_client_init(&options);
        if (!http)
        {
            result->error = MEDIA_MEMORY;
            break;
        }
        bool ok = esp_http_client_open(http, 0) == ESP_OK;
        if (!media_current(job))
        {
            result->error = MEDIA_CANCELLED;
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
            break;
        }
        result->error = ok ? MEDIA_HEADERS : MEDIA_CONNECT;
        int64_t declared = ok ? esp_http_client_fetch_headers(http) : -1;
        int status = ok ? esp_http_client_get_status_code(http) : 0;
        result->http_status = status;
        if (ok && declared >= 0 && status >= 300 && status < 400 && !headers->invalid &&
            lx_audio_cover_url(headers->location) &&
            (strncmp(url, "https://", 8) || !strncmp(headers->location, "https://", 8)))
        {
            result->error = MEDIA_REDIRECT;
            snprintf(url, LX_AUDIO_URL_SIZE, "%s", headers->location);
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
            continue;
        }
        if (ok && declared >= 0)
            result->error = status >= 300 && status < 400 ? MEDIA_REDIRECT :
                status != 200 ? MEDIA_HTTP_STATUS :
                declared > LX_AUDIO_MEDIA_BYTES_MAX ? MEDIA_SIZE_LIMIT : MEDIA_INCOMPLETE;
        ok = ok && declared >= 0 && status == 200 && declared <= LX_AUDIO_MEDIA_BYTES_MAX;
        size_t used = 0;
        while (ok && used < LX_AUDIO_MEDIA_BYTES_MAX && media_current(job) &&
               esp_timer_get_time() < deadline)
        {
            size_t remaining = LX_AUDIO_MEDIA_BYTES_MAX - used;
            int n = esp_http_client_read(http, (char *)body + used, remaining < 4096 ? remaining : 4096);
            if (n < 0)
            {
                ok = false;
                result->error = MEDIA_READ;
                break;
            }
            if (!n)
                break;
            used += (size_t)n;
        }
        success = ok && used > 0 && media_current(job) &&
                  esp_http_client_is_complete_data_received(http);
        if (success)
            result->error = MEDIA_OK;
        else if (!media_current(job))
            result->error = MEDIA_CANCELLED;
        else if (ok && used == LX_AUDIO_MEDIA_BYTES_MAX)
            result->error = MEDIA_SIZE_LIMIT;
        else if (ok && esp_timer_get_time() >= deadline)
            result->error = MEDIA_DEADLINE;
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        if (success)
            *length = used;
        break;
    }
    free(headers);
    free(url);
    if (!success)
    {
        free(body);
        return NULL;
    }
    return body;
}

static size_t media_cached_bytes_locked(void)
{
    size_t bytes = 0;
    for (unsigned i = 0; i < LX_AUDIO_MEDIA_SLOTS; ++i)
    {
        lx_audio_image_t *image = media_slots[i].image;
        bool seen = false;
        for (unsigned j = 0; j < i; ++j)
            seen |= media_slots[j].image == image;
        if (image && !seen)
            bytes += (size_t)image->width * image->height * 2;
    }
    return bytes;
}

static bool media_share_locked(unsigned target)
{
    media_slot_t *slot = &media_slots[target];
    for (unsigned i = 0; i < LX_AUDIO_MEDIA_SLOTS; ++i)
        if (i != target && media_slots[i].image && media_slots[i].size == slot->size &&
            media_slots[i].version == slot->version && !strcmp(media_slots[i].url, slot->url))
        {
            slot->image = media_slots[i].image;
            ++slot->image->references;
            slot->pending = false;
            slot->retry_at = 0;
            return true;
        }
    return false;
}

static void media_worker_step(void *argument);

static void media_post_pending(void)
{
    if (!atomic_load(&media_ready))
        return;
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_sync_generation_locked();
    unsigned generation = media_generation;
    bool pending = false;
    if (media_slots && esp_timer_get_time() >= media_start_after)
        for (unsigned i = 0; i < LX_AUDIO_MEDIA_SLOTS; ++i)
            pending |= media_slots[i].pending;
    xSemaphoreGive(media_lock);
    /* A slot a futó callback előtt kiürül: az eközben érkező kérés és a
     * saját újraütemezés azonos függvény/NULL párral összevonható. */
    if (!pending || app_ota_background_post(media_worker_step, NULL))
        return;
    media_notice_t notice = {0};
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_sync_generation_locked();
    if (media_slots && generation == media_generation && esp_timer_get_time() >= media_start_after)
        media_start_failed_locked(MEDIA_WORKER, &notice);
    xSemaphoreGive(media_lock);
    media_notice_emit(&notice);
}

static void media_worker_step(void *argument)
{
    (void)argument;
    if (!atomic_load(&media_ready))
        return;
    media_notice_t notice = {0};
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_sync_generation_locked();
    if (!media_slots)
    {
        xSemaphoreGive(media_lock);
        return;
    }
    unsigned selected = LX_AUDIO_MEDIA_SLOTS;
    /* Egy callback legfeljebb egy képet kezel; a többi előtt ismét az OTA
     * kérésé a prioritás. A részletes kép továbbra is elsőbbséget kap. */
    for (unsigned n = 0; n < LX_AUDIO_MEDIA_SLOTS; ++n)
    {
        unsigned i = n == 0 ? LX_AUDIO_MEDIA_SLOTS - 1 : n - 1;
        if (media_slots[i].pending && !media_share_locked(i))
        {
            selected = i;
            break;
        }
    }
    if (selected == LX_AUDIO_MEDIA_SLOTS || app_ota_operation_active())
    {
        xSemaphoreGive(media_lock);
        if (selected != LX_AUDIO_MEDIA_SLOTS)
            media_post_pending();
        return;
    }
    /* A várakozó callback nem foglal képpuffert, ezért nem blokkolhatja a
     * kamerát. Az aktív jel a kiválasztás és minden foglalás előtt áll be. */
    atomic_store(&media_active, true);
    media_job_t *job = media_alloc(sizeof(*job));
    if (!job)
    {
        media_start_failed_locked(MEDIA_MEMORY, &notice);
        xSemaphoreGive(media_lock);
        atomic_store(&media_active, false);
        media_notice_emit(&notice);
        return;
    }
    media_slot_t *slot = &media_slots[selected];
    job->slot = selected;
    job->generation = media_generation;
    job->version = slot->version;
    job->size = slot->size;
    job->sequence = slot->sequence;
    snprintf(job->url, sizeof(job->url), "%s", slot->url);
    slot->pending = false;
    slot->retry_at = 0;
    ++slot->attempts;
    xSemaphoreGive(media_lock);

    size_t length = 0;
    media_result_t result;
    uint8_t *compressed = media_download(job, &length, &result);
    lx_audio_image_t *image = NULL;
    if (compressed)
    {
        if (media_current(job))
        {
            image = lx_audio_image_decode(compressed, length, job->size);
            if (!image)
                result.error = MEDIA_DECODE;
        }
        else
            result.error = MEDIA_CANCELLED;
    }
    free(compressed);
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_sync_generation_locked();
    slot = media_slots ? &media_slots[job->slot] : NULL;
    bool current = slot && media_generation == job->generation && slot->sequence == job->sequence &&
                   !strcmp(slot->url, job->url);
    size_t bytes = image ? (size_t)image->width * image->height * 2 : 0;
    if (current && (result.error == MEDIA_CANCELLED || app_ota_operation_active()))
    {
        /* OTA miatt félretett, továbbra is aktuális kép: ez nem hálózati
         * hiba és nem fogyaszthat próbát. Az OTA vége ébreszti a callbacket. */
        slot->pending = true;
        slot->retry_at = 0;
        if (slot->attempts)
            --slot->attempts;
    }
    else if (image && current && media_cached_bytes_locked() + bytes <= LX_AUDIO_MEDIA_CACHE_MAX)
    {
        image->generation = job->generation;
        image->version = job->version;
        image->references = 1;
        media_release_locked(slot->image);
        slot->image = image;
        if (slot->attempts > 1)
            notice = (media_notice_t){.slot = job->slot, .attempts = slot->attempts,
                .pending = true, .recovered = true};
        image = NULL;
    }
    else if (current)
    {
        if (image)
            result.error = MEDIA_CACHE_LIMIT;
        int64_t now = esp_timer_get_time();
        media_retry_locked(slot, media_retryable(&result), now);
        media_notice_locked(job->slot, &result, &notice, now);
    }
    xSemaphoreGive(media_lock);
    lx_audio_image_free(image);
    free(job);
    atomic_store(&media_active, false);
    media_notice_emit(&notice);
    media_post_pending();
}

void app_loxone_audio_media_request(unsigned index, unsigned generation, const char *url,
                                    unsigned version, unsigned size)
{
    if (index >= LX_AUDIO_MEDIA_SLOTS || (size != 112 && size != 128 && size != 256) ||
        !media_initialize())
        return;
    xSemaphoreTake(media_lock, portMAX_DELAY);
    media_sync_generation_locked();
    if (generation != media_generation)
    {
        xSemaphoreGive(media_lock);
        return;
    }
    const char *safe_url = lx_audio_cover_url(url) ? url : "";
    if (!media_slots)
    {
        int64_t now = esp_timer_get_time();
        if (!safe_url[0] || now < media_start_after)
        {
            xSemaphoreGive(media_lock);
            return;
        }
        media_slots = media_alloc(sizeof(*media_slots) * LX_AUDIO_MEDIA_SLOTS);
        if (!media_slots)
        {
            /* Átmeneti PSRAM-hiánynál se foglaljunk minden UI-körben.
             * Az új oldal új keretet kap; rejtett oldal nem próbál újra. */
            media_start_after = now + MEDIA_RETRY_FIRST_US;
            xSemaphoreGive(media_lock);
            return;
        }
    }
    media_slot_t *slot = &media_slots[index];
    bool changed = slot->version != version || slot->size != size || strcmp(slot->url, safe_url);
    if (changed)
    {
        media_release_locked(slot->image);
        slot->image = NULL;
        slot->version = version;
        slot->size = size;
        slot->attempts = 0;
        slot->retry_at = 0;
        ++slot->sequence;
        snprintf(slot->url, sizeof(slot->url), "%s", safe_url);
        slot->pending = safe_url[0] != 0;
        if (slot->pending)
            media_share_locked(index);
    }
    int64_t now = esp_timer_get_time();
    /* A worker nem alszik az újrapróbáig. Kizárólag a még látható kép
     * időszakos kérése élesítheti az esedékes, legfeljebb harmadik próbát. */
    if (!slot->image && slot->retry_at && now >= slot->retry_at)
    {
        slot->retry_at = 0;
        slot->pending = true;
        media_share_locked(index);
    }
    xSemaphoreGive(media_lock);
    media_post_pending();
}

#else
#include "../simulator/audio_fixture.h"

static bool audio_fixture_media_busy;
static app_sim_audio_media_request_t audio_fixture_media_requests[LX_AUDIO_MEDIA_SLOTS];

void app_sim_audio_media_request_reset(void)
{
    memset(audio_fixture_media_requests, 0, sizeof(audio_fixture_media_requests));
}

bool app_sim_audio_media_request_get(unsigned slot, app_sim_audio_media_request_t *out)
{
    if (slot >= LX_AUDIO_MEDIA_SLOTS || !out || !audio_fixture_media_requests[slot].calls)
        return false;
    *out = audio_fixture_media_requests[slot];
    return true;
}

void app_sim_audio_media_busy(bool busy) { audio_fixture_media_busy = busy; }
bool app_loxone_audio_media_idle(void) { return !audio_fixture_media_busy; }
void app_loxone_audio_media_request(unsigned slot, unsigned generation, const char *url, unsigned version, unsigned size)
{
    if (slot >= LX_AUDIO_MEDIA_SLOTS)
        return;
    /* Kizárólag a UI átadását rögzíti; nincs hálózati kérés vagy hamis kép. */
    app_sim_audio_media_request_t *request = &audio_fixture_media_requests[slot];
    snprintf(request->url, sizeof(request->url), "%s", url ? url : "");
    request->generation = generation;
    request->version = version;
    request->size = size;
    ++request->calls;
}
lx_audio_image_t *app_loxone_audio_media_acquire(unsigned slot, unsigned generation)
{ (void)slot; (void)generation; return NULL; }
void app_loxone_audio_media_release(lx_audio_image_t *image)
{ lx_audio_image_free(image); }
void app_loxone_audio_media_clear(unsigned generation) { (void)generation; }
void app_loxone_audio_media_close_detail(void) {}
#endif
