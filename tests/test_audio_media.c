/* A tényleges ESP borító-worker életciklusa: determinisztikus RTOS/HTTP. */
#include <assert.h>
#include <stdarg.h>
#include "../main/app_loxone_audio_media.c"

struct test_audio_semaphore { bool held; };
struct test_audio_http { esp_http_client_config_t configuration; unsigned position; };
static unsigned test_decodes, test_freed, test_reads, test_connections;
static unsigned test_cancel_generation;
static int test_status = 200;
static int64_t test_declared = 4;
static const char *test_redirect;
static bool test_task_failure;
static unsigned test_task_starts;
static size_t test_allocation_failure;
static bool test_http_init_failure, test_read_failure, test_decode_failure, test_close_detail;
static esp_err_t test_open_result = ESP_OK;
static int64_t test_now = 1000000;
static unsigned test_error_logs, test_recovery_logs;
static char test_last_log[APP_EVENT_TEXT_MAX];

void app_events_add(app_event_level_t level, const char *format, ...)
{
    assert(!media_lock || !media_lock->held);
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(test_last_log, sizeof(test_last_log), format, arguments);
    va_end(arguments);
    assert(length > 0 && (size_t)length < sizeof(test_last_log));
    assert(!strstr(test_last_log, "http:") && !strstr(test_last_log, "https:") &&
           !strstr(test_last_log, "covers.example") && !strstr(test_last_log, "tunein.com"));
    if (level == APP_EVENT_WARNING)
        ++test_error_logs;
    else
    {
        assert(level == APP_EVENT_INFO);
        ++test_recovery_logs;
    }
}

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (test_allocation_failure == count * size)
    {
        test_allocation_failure = 0;
        return NULL;
    }
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
    assert(stack >= 8192 && (caps == MALLOC_CAP_RTCRAM || caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    ++test_task_starts;
    if (test_task_failure)
        return 0;
    *handle = (void *)1;
    return pdPASS;
}
void vTaskDeleteWithCaps(TaskHandle_t handle) { (void)handle; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle) { (void)handle; return 4096; }
int64_t esp_timer_get_time(void) { return test_now; }
esp_err_t esp_crt_bundle_attach(void *configuration) { (void)configuration; return ESP_OK; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *configuration)
{
    assert(!media_lock->held);
    assert(configuration->disable_auto_redirect && configuration->timeout_ms == 2000);
    assert(configuration->crt_bundle_attach == esp_crt_bundle_attach);
    assert(lx_audio_cover_url(configuration->url));
    ++test_connections;
    if (test_http_init_failure)
        return NULL;
    esp_http_client_handle_t http = calloc(1, sizeof(*http));
    assert(http);
    http->configuration = *configuration;
    return http;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t http, int write_length)
{
    (void)http;
    assert(write_length == 0);
    return test_open_result;
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
    if (test_close_detail)
    {
        test_close_detail = false;
        app_loxone_audio_media_close_detail();
    }
    if (test_cancel_generation)
    {
        assert(!app_loxone_audio_media_idle());
        unsigned generation = test_cancel_generation;
        test_cancel_generation = 0;
        app_loxone_audio_media_clear(generation);
        assert(!app_loxone_audio_media_idle());
    }
    if (test_read_failure)
        return -1;
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
    if (test_decode_failure)
        return NULL;
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
    assert(app_loxone_audio_media_idle());
    /* Clear az első inicializálás közben: a generáció nem veszhet el. */
    atomic_flag_test_and_set(&media_initializing);
    app_loxone_audio_media_clear(11);
    assert(!media_slots && !media_lock && atomic_load(&media_requested_generation) == 11);
    atomic_flag_clear(&media_initializing);
    app_loxone_audio_media_request(0, 11, "https://covers.example/test.png", 1, 112);
    assert(media_generation == 11 && media_slots[0].pending);
    assert(!app_loxone_audio_media_idle());

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
    assert(!media_task && test_decodes == 1 && app_loxone_audio_media_idle());
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
    assert(!media_task && app_loxone_audio_media_idle());

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

    /* Sikertelen task-foglalás sem tarthatja fogva a kamera indulási kapuját. */
    test_task_failure = true;
    app_loxone_audio_media_request(0, 15, "https://covers.example/no-task.png", 6, 112);
    assert(!media_task && app_loxone_audio_media_idle());
    test_task_failure = false;
}

static void same_cover_retry_test(void)
{
    const char *url = "http://cdn-profiles.tunein.com/s15879/images/logoq.png?t=1";
    app_loxone_audio_media_clear(16);
    test_declared = 4;
    test_status = 503;
    unsigned connections = test_connections;
    app_loxone_audio_media_request(0, 16, url, 1, 112);
    media_worker(NULL);
    assert(test_connections == connections + 1 && !media_task && !media_slots[0].image);
    assert(app_loxone_audio_media_idle());
    test_status = 200;
    for (unsigned i = 0; i < 9; ++i)
    {
        test_now += 200000;
        app_loxone_audio_media_request(0, 16, url, 1, 112);
        assert(!media_task && test_connections == connections + 1);
    }
    test_now += 200000;
    app_loxone_audio_media_request(0, 16, url, 1, 112);
    assert(media_task);
    media_worker(NULL);
    assert(test_connections == connections + 2 && media_slots[0].image);
    for (unsigned i = 0; i < 20; ++i)
    {
        test_now += 200000;
        app_loxone_audio_media_request(0, 16, url, 1, 112);
        assert(!media_task && test_connections == connections + 2);
    }
}

static void bounded_retry_test(void)
{
    const char *url = "https://covers.example/temporary.png";
    app_loxone_audio_media_clear(17);
    test_status = 503;
    unsigned connections = test_connections;
    app_loxone_audio_media_request(0, 17, url, 1, 112);
    media_worker(NULL);
    for (unsigned attempt = 1; attempt < 3; ++attempt)
    {
        unsigned ticks = attempt == 1 ? 10 : 25;
        for (unsigned tick = 1; tick < ticks; ++tick)
        {
            test_now += 200000;
            app_loxone_audio_media_request(0, 17, url, 1, 112);
            assert(!media_task && test_connections == connections + attempt);
        }
        test_now += 200000;
        app_loxone_audio_media_request(0, 17, url, 1, 112);
        assert(media_task);
        media_worker(NULL);
    }
    assert(test_connections == connections + 3 && app_loxone_audio_media_idle());
    test_now += 60000000;
    app_loxone_audio_media_request(0, 17, url, 1, 112);
    assert(!media_task && test_connections == connections + 3);
    /* Az új tartalom új próbakeretet kap, az azonos kép nem. */
    test_status = 200;
    app_loxone_audio_media_request(0, 17, url, 2, 112);
    assert(media_task);
    media_worker(NULL);
    assert(media_slots[0].image && test_connections == connections + 4);
}

static void cancelled_retry_test(void)
{
    const char *url = "https://covers.example/cancelled.png";
    unsigned detail = LX_AUDIO_MEDIA_SLOTS - 1;
    app_loxone_audio_media_clear(18);
    test_status = 503;
    app_loxone_audio_media_request(0, 18, url, 1, 112);
    media_worker(NULL);
    app_loxone_audio_media_clear(19);
    test_now += 10000000;
    unsigned connections = test_connections;
    app_loxone_audio_media_request(0, 18, url, 1, 112);
    assert(!media_task && test_connections == connections && !media_slots[0].pending);

    app_loxone_audio_media_request(detail, 19, url, 1, 256);
    media_worker(NULL);
    app_loxone_audio_media_close_detail();
    test_now += 10000000;
    test_status = 200;
    connections = test_connections;
    app_loxone_audio_media_request(0, 19, "https://covers.example/current.png", 1, 112);
    media_worker(NULL);
    assert(test_connections == connections + 1 && !media_slots[detail].image && !media_slots[detail].pending);

    /* Bezárás az éppen folyó olvasásban: a későbbi hiba sem élesztheti újra. */
    test_close_detail = test_read_failure = true;
    app_loxone_audio_media_request(detail, 19, url, 2, 256);
    media_worker(NULL);
    test_read_failure = false;
    test_now += 10000000;
    connections = test_connections;
    app_loxone_audio_media_request(0, 19, "https://covers.example/current.png", 1, 112);
    assert(!media_task && test_connections == connections && !media_slots[detail].pending);
}

static void resource_retry_test(void)
{
    const char *url = "https://covers.example/recover.png";
    /* Task, job, HTTP-puffer, kliens, TLS/open, fejléc, olvasás, 408 és 429. */
    for (unsigned failure = 0; failure < 9; ++failure)
    {
        unsigned generation = 20 + failure;
        app_loxone_audio_media_clear(generation);
        test_status = failure == 7 ? 408 : failure == 8 ? 429 : 200;
        test_declared = failure == 5 ? -1 : 4;
        test_task_failure = failure == 0;
        test_allocation_failure = failure == 1 ? sizeof(media_job_t) :
            failure == 2 ? LX_AUDIO_MEDIA_BYTES_MAX : 0;
        test_http_init_failure = failure == 3;
        test_open_result = failure == 4 ? ESP_FAIL : ESP_OK;
        test_read_failure = failure == 6;
        app_loxone_audio_media_request(0, generation, url, 1, 112);
        if (media_task)
            media_worker(NULL);
        assert(!media_slots[0].image && !media_task && app_loxone_audio_media_idle());

        test_task_failure = test_http_init_failure = test_read_failure = false;
        test_allocation_failure = 0;
        test_open_result = ESP_OK;
        test_declared = 4;
        test_status = 200;
        unsigned connections = test_connections, starts = test_task_starts;
        for (unsigned i = 0; i < 9; ++i)
        {
            test_now += 200000;
            app_loxone_audio_media_request(0, generation, url, 1, 112);
            assert(!media_task && test_connections == connections && test_task_starts == starts);
        }
        test_now += 200000;
        app_loxone_audio_media_request(0, generation, url, 1, 112);
        assert(media_task);
        media_worker(NULL);
        assert(media_slots[0].image && test_connections == connections + 1);
    }

    /* Tartós taskhiányból sem lehet 200 ms-os létrehozás/törlés ciklus. */
    app_loxone_audio_media_clear(29);
    test_task_failure = true;
    unsigned starts = test_task_starts;
    app_loxone_audio_media_request(0, 29, url, 1, 112);
    test_now += 2000000;
    app_loxone_audio_media_request(0, 29, url, 1, 112);
    test_now += 5000000;
    app_loxone_audio_media_request(0, 29, url, 1, 112);
    assert(test_task_starts == starts + 6 && app_loxone_audio_media_idle());
    test_now += 60000000;
    app_loxone_audio_media_request(0, 29, url, 1, 112);
    assert(test_task_starts == starts + 6 && !media_task);
    test_task_failure = false;
}

static void permanent_failure_test(void)
{
    const char *url = "https://covers.example/permanent.png";
    /* Hitelesítés, hiányzó kép, bemeneti korlát és sikertelen dekódolás. */
    for (unsigned failure = 0; failure < 4; ++failure)
    {
        unsigned generation = 30 + failure;
        app_loxone_audio_media_clear(generation);
        test_status = failure == 0 ? 401 : failure == 1 ? 404 : 200;
        test_declared = failure == 2 ? LX_AUDIO_MEDIA_BYTES_MAX + 1 : 4;
        test_decode_failure = failure == 3;
        app_loxone_audio_media_request(0, generation, url, 1, 112);
        media_worker(NULL);
        assert(!media_slots[0].image && !media_task);
        unsigned connections = test_connections, starts = test_task_starts;
        test_status = 200;
        test_declared = 4;
        test_decode_failure = false;
        for (unsigned i = 0; i < 10; ++i)
        {
            test_now += 10000000;
            app_loxone_audio_media_request(0, generation, url, 1, 112);
            assert(!media_task && test_connections == connections && test_task_starts == starts);
        }
    }

    /* Az ismeretlen/chunked hossz teljes HTTP 200 válasza megmarad. */
    app_loxone_audio_media_clear(34);
    test_declared = 0;
    app_loxone_audio_media_request(0, 34, url, 2, 112);
    media_worker(NULL);
    assert(media_slots[0].image);
    test_declared = 4;
}

static void bounded_diagnostics_test(void)
{
    const char *url = "https://covers.example/private-title.png?token=secret";
    app_loxone_audio_media_clear(35);
    test_now += 10000000;
    test_status = 503;
    unsigned errors = test_error_logs;
    app_loxone_audio_media_request(0, 35, url, 1, 112);
    media_worker(NULL);
    assert(test_error_logs == errors + 1);
    assert(strstr(test_last_log, "hely=1;") && strstr(test_last_log, "ok=http;") &&
           strstr(test_last_log, "HTTP=503;") && strstr(test_last_log, "próba=1/3;") &&
           strstr(test_last_log, "újra=2 s."));
    assert(!strstr(test_last_log, "secret") && !strstr(test_last_log, "private-title"));

    /* Másik kártya és a 2/5 másodperces újrapróbák sem töltik meg a naplót. */
    app_loxone_audio_media_request(1, 35, url, 1, 112);
    media_worker(NULL);
    test_now += 2000000;
    app_loxone_audio_media_request(0, 35, url, 1, 112);
    media_worker(NULL);
    test_now += 5000000;
    app_loxone_audio_media_request(0, 35, url, 1, 112);
    media_worker(NULL);
    assert(test_error_logs == errors + 1);

    test_now += 3000000;
    app_loxone_audio_media_request(2, 35, url, 1, 112);
    media_worker(NULL);
    assert(test_error_logs == errors + 2 && strstr(test_last_log, "hely=3;"));
    unsigned recoveries = test_recovery_logs;
    test_status = 200;
    test_now += 2000000;
    app_loxone_audio_media_request(2, 35, url, 1, 112);
    media_worker(NULL);
    assert(test_recovery_logs == recoveries + 1 && strstr(test_last_log, "helyreállt"));
    app_loxone_audio_media_request(2, 35, url, 1, 112);
    assert(test_recovery_logs == recoveries + 1);

    /* Esedékes naplóablakban sem hiba a részlet bezárása vagy az oldalváltás. */
    test_now += 10000000;
    errors = test_error_logs;
    test_close_detail = test_read_failure = true;
    app_loxone_audio_media_request(LX_AUDIO_MEDIA_SLOTS - 1, 35, url, 2, 256);
    media_worker(NULL);
    assert(test_error_logs == errors);
    test_read_failure = false;
    test_cancel_generation = 36;
    app_loxone_audio_media_request(3, 35, url, 3, 112);
    media_worker(NULL);
    assert(test_error_logs == errors && app_loxone_audio_media_idle());
}

int main(void)
{
    initialize_and_generation_test();
    ownership_test();
    network_cancellation_test();
    same_cover_retry_test();
    bounded_retry_test();
    cancelled_retry_test();
    resource_retry_test();
    permanent_failure_test();
    bounded_diagnostics_test();
    app_loxone_audio_media_clear(37);
    free(media_slots);
    free(media_lock);
    puts("PASS: ESP cover worker lifecycle, bounded same-URL retry, cancellation and HTTP bounds");
    return 0;
}
