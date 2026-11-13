/* Egyetlen, igény szerinti borítóletöltő. Nincs SD-írás és nincs hitelesítő
 * HTTP fejléc: a Miniserver JWT sohasem juthat a külső képszolgáltatóhoz. */
#include "app_loxone_audio_media.h"
#include "app_loxone_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef APP_TEMPLATE_DESKTOP
#include <stdatomic.h>
#include <strings.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

typedef struct
{
    char url[LX_AUDIO_URL_SIZE];
    unsigned version, size, sequence;
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

static SemaphoreHandle_t media_lock;
static TaskHandle_t media_task;
static media_slot_t *media_slots;
static unsigned media_generation;
static atomic_flag media_initializing = ATOMIC_FLAG_INIT;
static atomic_bool media_ready;
static atomic_uint media_requested_generation;
static atomic_uint media_workers;

bool app_loxone_audio_media_idle(void)
{
    return atomic_load(&media_workers) == 0;
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
    if (!media_slots)
        media_slots = media_alloc(sizeof(*media_slots) * LX_AUDIO_MEDIA_SLOTS);
    bool ok = media_lock && media_slots;
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
    for (unsigned i = 0; i < LX_AUDIO_MEDIA_SLOTS; ++i)
    {
        media_release_locked(media_slots[i].image);
        memset(&media_slots[i], 0, sizeof(media_slots[i]));
    }
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
    lx_audio_image_t *image = generation == media_generation ? media_slots[slot].image : NULL;
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
    bool current = job->generation == media_generation &&
        job->sequence == media_slots[job->slot].sequence &&
        !strcmp(job->url, media_slots[job->slot].url);
    xSemaphoreGive(media_lock);
    return current;
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

static uint8_t *media_download(const media_job_t *job, size_t *length)
{
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
    int64_t deadline = esp_timer_get_time() + 8000000LL;
    for (unsigned redirect = 0; redirect < 4 && media_current(job); ++redirect)
    {
        memset(headers, 0, sizeof(*headers));
        esp_http_client_config_t options = {
            .url = url, .timeout_ms = 2000, .disable_auto_redirect = true,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = media_http_event, .user_data = headers,
            .buffer_size = 2048, .buffer_size_tx = 1024,
        };
        esp_http_client_handle_t http = esp_http_client_init(&options);
        if (!http)
            break;
        bool ok = esp_http_client_open(http, 0) == ESP_OK;
        int64_t declared = ok ? esp_http_client_fetch_headers(http) : -1;
        int status = ok ? esp_http_client_get_status_code(http) : 0;
        if (status >= 300 && status < 400 && !headers->invalid &&
            lx_audio_cover_url(headers->location) &&
            (strncmp(url, "https://", 8) || !strncmp(headers->location, "https://", 8)))
        {
            snprintf(url, LX_AUDIO_URL_SIZE, "%s", headers->location);
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
            continue;
        }
        ok = ok && status == 200 && declared <= LX_AUDIO_MEDIA_BYTES_MAX;
        size_t used = 0;
        while (ok && used < LX_AUDIO_MEDIA_BYTES_MAX && media_current(job) &&
               esp_timer_get_time() < deadline)
        {
            size_t remaining = LX_AUDIO_MEDIA_BYTES_MAX - used;
            int n = esp_http_client_read(http, (char *)body + used, remaining < 4096 ? remaining : 4096);
            if (n < 0)
            {
                ok = false;
                break;
            }
            if (!n)
                break;
            used += (size_t)n;
        }
        success = ok && used > 0 && media_current(job) &&
                  esp_http_client_is_complete_data_received(http);
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
            return true;
        }
    return false;
}

static void media_worker(void *argument)
{
    (void)argument;
    media_job_t *job = media_alloc(sizeof(*job));
    while (job)
    {
        bool found = false;
        xSemaphoreTake(media_lock, portMAX_DELAY);
        media_sync_generation_locked();
        /* A nagy ablak először kap képet; a többi kérés közben összevonódik. */
        for (unsigned n = 0; n < LX_AUDIO_MEDIA_SLOTS; ++n)
        {
            unsigned i = n == 0 ? LX_AUDIO_MEDIA_SLOTS - 1 : n - 1;
            media_slot_t *slot = &media_slots[i];
            if (!slot->pending || media_share_locked(i))
                continue;
            job->slot = i;
            job->generation = media_generation;
            job->version = slot->version;
            job->size = slot->size;
            job->sequence = slot->sequence;
            snprintf(job->url, sizeof(job->url), "%s", slot->url);
            slot->pending = false;
            found = true;
            break;
        }
        if (!found)
        {
            media_task = NULL;
            xSemaphoreGive(media_lock);
            break;
        }
        xSemaphoreGive(media_lock);
        size_t length = 0;
        uint8_t *compressed = media_download(job, &length);
        lx_audio_image_t *image = compressed && media_current(job) ?
            lx_audio_image_decode(compressed, length, job->size) : NULL;
        free(compressed);
        xSemaphoreTake(media_lock, portMAX_DELAY);
        media_sync_generation_locked();
        media_slot_t *slot = &media_slots[job->slot];
        bool current = media_generation == job->generation && slot->sequence == job->sequence &&
                       !strcmp(slot->url, job->url);
        size_t bytes = image ? (size_t)image->width * image->height * 2 : 0;
        if (image && current && media_cached_bytes_locked() + bytes <= LX_AUDIO_MEDIA_CACHE_MAX)
        {
            image->generation = job->generation;
            image->version = job->version;
            image->references = 1;
            media_release_locked(slot->image);
            slot->image = image;
            image = NULL;
        }
        xSemaphoreGive(media_lock);
        lx_audio_image_free(image);
    }
    if (!job)
    {
        xSemaphoreTake(media_lock, portMAX_DELAY);
        media_task = NULL;
        xSemaphoreGive(media_lock);
    }
    free(job);
    /* A hálózati/dekódolási pufferek már megszűntek. Számláló kell: az új
     * kérés a régi task utolsó lépése közben is elindíthat egy új workert. */
    atomic_fetch_sub(&media_workers, 1);
    ESP_LOGI("lox_audio_media", "Borító worker leáll; verem minimum: %u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    vTaskDeleteWithCaps(NULL);
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
    media_slot_t *slot = &media_slots[index];
    const char *safe_url = lx_audio_cover_url(url) ? url : "";
    bool changed = slot->version != version || slot->size != size || strcmp(slot->url, safe_url);
    if (changed)
    {
        media_release_locked(slot->image);
        slot->image = NULL;
        slot->version = version;
        slot->size = size;
        ++slot->sequence;
        snprintf(slot->url, sizeof(slot->url), "%s", safe_url);
        slot->pending = safe_url[0] != 0;
        if (slot->pending)
            media_share_locked(index);
    }
    if (slot->pending && !media_task)
    {
        atomic_fetch_add(&media_workers, 1);
        BaseType_t created = xTaskCreateWithCaps(media_worker, "lox_audio_media", 8192, NULL,
            2, &media_task, MALLOC_CAP_RTCRAM);
        if (created != pdPASS)
            created = xTaskCreateWithCaps(media_worker, "lox_audio_media", 8192, NULL,
                2, &media_task, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (created != pdPASS)
        {
            media_task = NULL;
            atomic_fetch_sub(&media_workers, 1);
        }
    }
    xSemaphoreGive(media_lock);
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
