/* A tényleges ESP borító-worker életciklusa: determinisztikus RTOS/HTTP. */
#include <assert.h>
#include "../main/app_loxone_audio_media.c"

struct test_audio_semaphore { bool held; };
struct test_audio_http { esp_http_client_config_t configuration; unsigned position; };
static unsigned test_decodes, test_freed, test_reads, test_connections;
static unsigned test_cancel_generation;
static int test_status = 200;
static int64_t test_declared = 4;
static const char *test_redirect;

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return calloc(count, size);
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1, sizeof(struct test_audio_semaphore)); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xSemaphoreCreateMutex(); }
int xSemaphoreTake(SemaphoreHandle_t lock, TickType_t timeout)
{
    (void)timeout;
    assert(!lock->held);
    lock->held = true;
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t lock)
{
    assert(lock->held);
    lock->held = false;
    return pdTRUE;
}
BaseType_t xTaskCreateWithCaps(TaskFunction_t function, const char *name, uint32_t stack,
                              void *argument, UBaseType_t priority, TaskHandle_t *handle, unsigned caps)
{
    (void)function; (void)name; (void)argument; (void)priority;
    assert(stack >= 8192 && caps == MALLOC_CAP_RTCRAM);
    *handle = (void *)1;
    return pdPASS;
}
void vTaskDeleteWithCaps(TaskHandle_t handle) { (void)handle; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle) { (void)handle; return 4096; }
int64_t esp_timer_get_time(void) { return 1000000; }
esp_err_t esp_crt_bundle_attach(void *configuration) { (void)configuration; return ESP_OK; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *configuration)
{
    assert(!media_lock->held);
    assert(configuration->disable_auto_redirect && configuration->timeout_ms == 2000);
    assert(configuration->crt_bundle_attach == esp_crt_bundle_attach);
    assert(lx_audio_cover_url(configuration->url));
    ++test_connections;
    esp_http_client_handle_t http = calloc(1, sizeof(*http));
    assert(http);
    http->configuration = *configuration;
    return http;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t http, int write_length)
{
    (void)http;
    assert(write_length == 0);
    return ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t http)
{
    if (test_redirect)
    {
        esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_HEADER,
            .header_key = "Location", .header_value = (char *)test_redirect,
            .user_data = http->configuration.user_data};
        assert(http->configuration.event_handler(&event) == ESP_OK);
    }
    return test_declared;
}
int esp_http_client_get_status_code(esp_http_client_handle_t http) { (void)http; return test_status; }
int esp_http_client_read(esp_http_client_handle_t http, char *output, int capacity)
{
    assert(!media_lock->held && capacity >= 4);
    ++test_reads;
    if (test_cancel_generation)
    {
        unsigned generation = test_cancel_generation;
        test_cancel_generation = 0;
        app_loxone_audio_media_clear(generation);
    }
    if (http->position)
        return 0;
    memcpy(output, "test", 4);
    http->position = 4;
    return 4;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t http) { return http->position == 4; }
esp_err_t esp_http_client_close(esp_http_client_handle_t http) { (void)http; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t http) { free(http); return ESP_OK; }

lx_audio_image_t *lx_audio_image_decode(const uint8_t *data, size_t length, unsigned size)
{
    assert(!media_lock->held && length == 4 && !memcmp(data, "test", 4));
    ++test_decodes;
    lx_audio_image_t *image = calloc(1, sizeof(*image));
    assert(image);
    image->width = image->height = size;
    image->pixels = calloc(size * size, 2);
    assert(image->pixels);
    return image;
}
void lx_audio_image_free(lx_audio_image_t *image)
{
    if (image)
    {
        ++test_freed;
        free(image->pixels);
        free(image);
    }
}

static void initialize_and_generation_test(void)
{
    /* Clear az első inicializálás közben: a generáció nem veszhet el. */
    atomic_flag_test_and_set(&media_initializing);
    app_loxone_audio_media_clear(11);
    assert(!media_slots && !media_lock && atomic_load(&media_requested_generation) == 11);
    atomic_flag_clear(&media_initializing);
    app_loxone_audio_media_request(0, 11, "https://covers.example/test.png", 1, 112);
    assert(media_generation == 11 && media_slots[0].pending);

    /* A régi implementáció a kész init-zár ütközésénél eldobta a clear-t. */
    atomic_flag_test_and_set(&media_initializing);
    app_loxone_audio_media_clear(12);
    assert(media_generation == 12 && !media_slots[0].pending);
    app_loxone_audio_media_request(0, 12, "https://covers.example/test.png", 2, 112);
    assert(media_slots[0].pending && media_slots[0].version == 2);
    atomic_flag_clear(&media_initializing);
    app_loxone_audio_media_request(0, 11, "https://covers.example/old.png", 3, 112);
    assert(media_slots[0].version == 2);
    app_loxone_audio_media_clear(11);
    assert(media_generation == 12 && media_slots[0].pending && media_slots[0].version == 2);
}

static void ownership_test(void)
{
    media_worker(NULL);
    assert(!media_task && test_decodes == 1);
    lx_audio_image_t *held = app_loxone_audio_media_acquire(0, 12);
    assert(held && held->references == 2 && held->generation == 12);
    app_loxone_audio_media_request(1, 12, "https://covers.example/test.png", 2, 112);
    assert(media_slots[1].image == held && held->references == 3 && !media_task);
    app_loxone_audio_media_clear(13);
    assert(held->references == 1 && test_freed == 0);
    assert(!app_loxone_audio_media_acquire(0, 12));
    app_loxone_audio_media_release(held);
    assert(test_freed == 1);

    app_loxone_audio_media_request(0, 13, "https://covers.example/test.png", 2, 112);
    media_worker(NULL);
    app_loxone_audio_media_request(LX_AUDIO_MEDIA_SLOTS - 1, 13, "https://covers.example/test.png", 2, 112);
    held = app_loxone_audio_media_acquire(LX_AUDIO_MEDIA_SLOTS - 1, 13);
    assert(held && held->references == 3);
    app_loxone_audio_media_close_detail();
    assert(held->references == 2 && !app_loxone_audio_media_acquire(LX_AUDIO_MEDIA_SLOTS - 1, 13));
    app_loxone_audio_media_release(held);
    app_loxone_audio_media_clear(14);
    assert(test_freed == 2);
}

static void network_cancellation_test(void)
{
    app_loxone_audio_media_request(0, 14, "https://covers.example/late.png", 3, 112);
    test_cancel_generation = 15;
    unsigned decodes = test_decodes;
    media_worker(NULL);
    assert(media_generation == 15 && test_decodes == decodes && !media_slots[0].image);
    assert(!media_task);

    test_declared = LX_AUDIO_MEDIA_BYTES_MAX + 1;
    app_loxone_audio_media_request(0, 15, "https://covers.example/large.png", 4, 112);
    unsigned reads = test_reads;
    media_worker(NULL);
    assert(test_reads == reads && test_decodes == decodes);

    test_declared = 0;
    test_status = 302;
    test_redirect = "http://covers.example/insecure.png";
    unsigned connections = test_connections;
    app_loxone_audio_media_request(0, 15, "https://covers.example/redirect.png", 5, 112);
    media_worker(NULL);
    assert(test_connections == connections + 1 && test_reads == reads);
    assert(!media_slots[0].image && !media_task);
    test_redirect = NULL;
}

int main(void)
{
    initialize_and_generation_test();
    ownership_test();
    network_cancellation_test();
    app_loxone_audio_media_clear(16);
    free(media_slots);
    free(media_lock);
    puts("PASS: ESP cover worker initialization/generation, shared-image ownership, cancellation and HTTP bounds");
    return 0;
}
