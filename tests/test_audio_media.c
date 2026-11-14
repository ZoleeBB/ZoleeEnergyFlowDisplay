/* A tényleges ESP borító-job életciklusa: determinisztikus executor/RTOS/HTTP. */
#include <assert.h>
#include <stdarg.h>
#include "freertos/idf_additions.h"
#include "../main/app_loxone_audio_media.c"

struct test_audio_semaphore { bool held; };
struct test_audio_http { esp_http_client_config_t configuration; unsigned position; };
static unsigned test_decodes, test_freed, test_reads, test_connections;
static unsigned test_cancel_generation;
static int test_status = 200;
static int64_t test_declared = 4;
static const char *test_redirect;
static bool test_post_failure;
static unsigned test_background_posts;
static void (*test_background_callback)(void *);
static void *test_background_argument;
static bool test_callback_active, test_ota_active;
static unsigned test_callbacks, test_task_starts, test_task_deletes;
static unsigned test_http_live;
static unsigned test_job_allocations;
static unsigned test_ota_during;
static void (*test_read_hook)(void);
static void (*test_before_callback)(void);
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
    if (count * size == sizeof(media_job_t) || count * size == LX_AUDIO_MEDIA_BYTES_MAX)
        assert(test_callback_active && !app_loxone_audio_media_idle());
    if (count * size == sizeof(media_job_t))
        ++test_job_allocations;
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
    (void)function; (void)name; (void)stack; (void)argument;
    (void)priority; (void)handle; (void)caps;
    ++test_task_starts;
    assert(!"A borító nem hozhat létre külön taskot.");
    return 0;
}
void vTaskDeleteWithCaps(TaskHandle_t handle)
{
    (void)handle;
    ++test_task_deletes;
    assert(!"A borító nem törölhet taskot.");
}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle) { (void)handle; return 4096; }
int64_t esp_timer_get_time(void) { return test_now; }
esp_err_t esp_crt_bundle_attach(void *configuration) { (void)configuration; return ESP_OK; }

bool app_ota_operation_active(void) { return test_ota_active; }

bool app_ota_background_post(void (*run)(void *), void *argument)
{
    assert(!media_lock || !media_lock->held);
    assert(run && !argument);
    ++test_background_posts;
    if (test_post_failure)
        return false;
    assert(!test_background_callback ||
           (test_background_callback == run && test_background_argument == argument));
    test_background_callback = run;
    test_background_argument = argument;
    return true;
}

static bool test_background_run(void)
{
    if (!test_background_callback || test_ota_active)
        return false;
    void (*run)(void *) = test_background_callback;
    void *argument = test_background_argument;
    test_background_callback = NULL;
    test_background_argument = NULL;
    assert(!test_callback_active);
    if (test_before_callback)
    {
        void (*hook)(void) = test_before_callback;
        test_before_callback = NULL;
        hook();
    }
    test_callback_active = true;
    ++test_callbacks;
    run(argument);
    test_callback_active = false;
    assert(app_loxone_audio_media_idle() && test_http_live == 0);
    return true;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *configuration)
{
    assert(test_callback_active && !media_lock->held && !app_loxone_audio_media_idle());
    assert(configuration->disable_auto_redirect && configuration->timeout_ms == 2000);
    assert(configuration->crt_bundle_attach == esp_crt_bundle_attach);
    assert(lx_audio_cover_url(configuration->url));
    ++test_connections;
    if (test_http_init_failure)
        return NULL;
    esp_http_client_handle_t http = calloc(1, sizeof(*http));
    assert(http);
    http->configuration = *configuration;
    ++test_http_live;
    return http;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t http, int write_length)
{
    (void)http;
    assert(write_length == 0);
    if (test_ota_during == 1)
        test_ota_active = true;
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
    if (test_ota_during == 2)
        test_ota_active = true;
    if (test_read_hook)
    {
        void (*hook)(void) = test_read_hook;
        test_read_hook = NULL;
        hook();
    }
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
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t http)
{
    assert(!app_loxone_audio_media_idle() && test_http_live);
    --test_http_live;
    free(http);
    if (test_ota_during == 3)
        test_ota_active = true;
    return ESP_OK;
}

lx_audio_image_t *lx_audio_image_decode(const uint8_t *data, size_t length, unsigned size)
{
    assert(test_callback_active && !media_lock->held && !app_loxone_audio_media_idle());
    assert(length == 4 && !memcmp(data, "test", 4));
    ++test_decodes;
    if (test_ota_during == 4)
        test_ota_active = true;
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
    assert(test_background_callback && app_loxone_audio_media_idle());

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
    assert(test_background_run());
    assert(!test_background_callback && test_decodes == 1 && app_loxone_audio_media_idle());
    lx_audio_image_t *held = app_loxone_audio_media_acquire(0, 12);
    assert(held && held->references == 2 && held->generation == 12);
    app_loxone_audio_media_request(1, 12, "https://covers.example/test.png", 2, 112);
    assert(media_slots[1].image == held && held->references == 3 && !test_background_callback);
    app_loxone_audio_media_clear(13);
    assert(held->references == 1 && test_freed == 0);
    assert(!app_loxone_audio_media_acquire(0, 12));
    app_loxone_audio_media_release(held);
    assert(test_freed == 1);

    app_loxone_audio_media_request(0, 13, "https://covers.example/test.png", 2, 112);
    assert(test_background_run());
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
    assert(test_background_run());
    assert(media_generation == 15 && test_decodes == decodes && !media_slots[0].image);
    assert(!test_background_callback && app_loxone_audio_media_idle());

    test_declared = LX_AUDIO_MEDIA_BYTES_MAX + 1;
    app_loxone_audio_media_request(0, 15, "https://covers.example/large.png", 4, 112);
    unsigned reads = test_reads;
    assert(test_background_run());
    assert(test_reads == reads && test_decodes == decodes);

    test_declared = 0;
    test_status = 302;
    test_redirect = "http://covers.example/insecure.png";
    unsigned connections = test_connections;
    app_loxone_audio_media_request(0, 15, "https://covers.example/redirect.png", 5, 112);
    assert(test_background_run());
    assert(test_connections == connections + 1 && test_reads == reads);
    assert(!media_slots[0].image && !test_background_callback);
    test_redirect = NULL;

    /* Sikertelen beadás sem tarthatja fogva a kamera indulási kapuját. */
    test_post_failure = true;
    app_loxone_audio_media_request(0, 15, "https://covers.example/no-task.png", 6, 112);
    assert(!test_background_callback && app_loxone_audio_media_idle());
    test_post_failure = false;
}

static void same_cover_retry_test(void)
{
    const char *url = "http://cdn-profiles.tunein.com/s15879/images/logoq.png?t=1";
    app_loxone_audio_media_clear(16);
    test_declared = 4;
    test_status = 503;
    unsigned connections = test_connections;
    app_loxone_audio_media_request(0, 16, url, 1, 112);
    assert(test_background_run());
    assert(test_connections == connections + 1 && !test_background_callback && !media_slots[0].image);
    assert(app_loxone_audio_media_idle());
    test_status = 200;
    for (unsigned i = 0; i < 9; ++i)
    {
        test_now += 200000;
        app_loxone_audio_media_request(0, 16, url, 1, 112);
        assert(!test_background_callback && test_connections == connections + 1);
    }
    test_now += 200000;
    app_loxone_audio_media_request(0, 16, url, 1, 112);
    assert(test_background_callback);
    assert(test_background_run());
    assert(test_connections == connections + 2 && media_slots[0].image);
    for (unsigned i = 0; i < 20; ++i)
    {
        test_now += 200000;
        app_loxone_audio_media_request(0, 16, url, 1, 112);
        assert(!test_background_callback && test_connections == connections + 2);
    }
}

static void bounded_retry_test(void)
{
    const char *url = "https://covers.example/temporary.png";
    app_loxone_audio_media_clear(17);
    test_status = 503;
    unsigned connections = test_connections;
    app_loxone_audio_media_request(0, 17, url, 1, 112);
    assert(test_background_run());
    for (unsigned attempt = 1; attempt < 3; ++attempt)
    {
        unsigned ticks = attempt == 1 ? 10 : 25;
        for (unsigned tick = 1; tick < ticks; ++tick)
        {
            test_now += 200000;
            app_loxone_audio_media_request(0, 17, url, 1, 112);
            assert(!test_background_callback && test_connections == connections + attempt);
        }
        test_now += 200000;
        app_loxone_audio_media_request(0, 17, url, 1, 112);
        assert(test_background_callback);
        assert(test_background_run());
    }
    assert(test_connections == connections + 3 && app_loxone_audio_media_idle());
    test_now += 60000000;
    app_loxone_audio_media_request(0, 17, url, 1, 112);
    assert(!test_background_callback && test_connections == connections + 3);
    /* Az új tartalom új próbakeretet kap, az azonos kép nem. */
    test_status = 200;
    app_loxone_audio_media_request(0, 17, url, 2, 112);
    assert(test_background_callback);
    assert(test_background_run());
    assert(media_slots[0].image && test_connections == connections + 4);
}

static void cancelled_retry_test(void)
{
    const char *url = "https://covers.example/cancelled.png";
    unsigned detail = LX_AUDIO_MEDIA_SLOTS - 1;
    app_loxone_audio_media_clear(18);
    test_status = 503;
    app_loxone_audio_media_request(0, 18, url, 1, 112);
    assert(test_background_run());
    app_loxone_audio_media_clear(19);
    test_now += 10000000;
    unsigned connections = test_connections;
    app_loxone_audio_media_request(0, 18, url, 1, 112);
    assert(!test_background_callback && test_connections == connections && !media_slots[0].pending);

    app_loxone_audio_media_request(detail, 19, url, 1, 256);
    assert(test_background_run());
    app_loxone_audio_media_close_detail();
    test_now += 10000000;
    test_status = 200;
    connections = test_connections;
    app_loxone_audio_media_request(0, 19, "https://covers.example/current.png", 1, 112);
    assert(test_background_run());
    assert(test_connections == connections + 1 && !media_slots[detail].image && !media_slots[detail].pending);

    /* Bezárás az éppen folyó olvasásban: a későbbi hiba sem élesztheti újra. */
    test_close_detail = test_read_failure = true;
    app_loxone_audio_media_request(detail, 19, url, 2, 256);
    assert(test_background_run());
    test_read_failure = false;
    test_now += 10000000;
    connections = test_connections;
    app_loxone_audio_media_request(0, 19, "https://covers.example/current.png", 1, 112);
    assert(!test_background_callback && test_connections == connections && !media_slots[detail].pending);
}

static void resource_retry_test(void)
{
    const char *url = "https://covers.example/recover.png";
    /* Beadás, job, HTTP-puffer, kliens, TLS/open, fejléc, olvasás, 408 és 429. */
    for (unsigned failure = 0; failure < 9; ++failure)
    {
        unsigned generation = 20 + failure;
        app_loxone_audio_media_clear(generation);
        test_status = failure == 7 ? 408 : failure == 8 ? 429 : 200;
        test_declared = failure == 5 ? -1 : 4;
        test_post_failure = failure == 0;
        test_allocation_failure = failure == 1 ? sizeof(media_job_t) :
            failure == 2 ? LX_AUDIO_MEDIA_BYTES_MAX : 0;
        test_http_init_failure = failure == 3;
        test_open_result = failure == 4 ? ESP_FAIL : ESP_OK;
        test_read_failure = failure == 6;
        app_loxone_audio_media_request(0, generation, url, 1, 112);
        if (test_background_callback)
            assert(test_background_run());
        assert(!media_slots[0].image && !test_background_callback && app_loxone_audio_media_idle());

        test_post_failure = test_http_init_failure = test_read_failure = false;
        test_allocation_failure = 0;
        test_open_result = ESP_OK;
        test_declared = 4;
        test_status = 200;
        unsigned connections = test_connections, starts = test_background_posts;
        for (unsigned i = 0; i < 9; ++i)
        {
            test_now += 200000;
            app_loxone_audio_media_request(0, generation, url, 1, 112);
            assert(!test_background_callback && test_connections == connections && test_background_posts == starts);
        }
        test_now += 200000;
        app_loxone_audio_media_request(0, generation, url, 1, 112);
        assert(test_background_callback);
        assert(test_background_run());
        assert(media_slots[0].image && test_connections == connections + 1);
    }

    /* Tartós ütemezési hibából sem lehet 200 ms-os beadási ciklus. */
    app_loxone_audio_media_clear(29);
    test_post_failure = true;
    unsigned starts = test_background_posts;
    app_loxone_audio_media_request(0, 29, url, 1, 112);
    test_now += 2000000;
    app_loxone_audio_media_request(0, 29, url, 1, 112);
    test_now += 5000000;
    app_loxone_audio_media_request(0, 29, url, 1, 112);
    assert(test_background_posts == starts + 3 && app_loxone_audio_media_idle());
    test_now += 60000000;
    app_loxone_audio_media_request(0, 29, url, 1, 112);
    assert(test_background_posts == starts + 3 && !test_background_callback);
    test_post_failure = false;
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
        assert(test_background_run());
        assert(!media_slots[0].image && !test_background_callback);
        unsigned connections = test_connections, starts = test_background_posts;
        test_status = 200;
        test_declared = 4;
        test_decode_failure = false;
        for (unsigned i = 0; i < 10; ++i)
        {
            test_now += 10000000;
            app_loxone_audio_media_request(0, generation, url, 1, 112);
            assert(!test_background_callback && test_connections == connections && test_background_posts == starts);
        }
    }

    /* Az ismeretlen/chunked hossz teljes HTTP 200 válasza megmarad. */
    app_loxone_audio_media_clear(34);
    test_declared = 0;
    app_loxone_audio_media_request(0, 34, url, 2, 112);
    assert(test_background_run());
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
    assert(test_background_run());
    assert(test_error_logs == errors + 1);
    assert(strstr(test_last_log, "hely=1;") && strstr(test_last_log, "ok=http;") &&
           strstr(test_last_log, "HTTP=503;") && strstr(test_last_log, "próba=1/3;") &&
           strstr(test_last_log, "újra=2 s."));
    assert(!strstr(test_last_log, "secret") && !strstr(test_last_log, "private-title"));

    /* Másik kártya és a 2/5 másodperces újrapróbák sem töltik meg a naplót. */
    app_loxone_audio_media_request(1, 35, url, 1, 112);
    assert(test_background_run());
    test_now += 2000000;
    app_loxone_audio_media_request(0, 35, url, 1, 112);
    assert(test_background_run());
    test_now += 5000000;
    app_loxone_audio_media_request(0, 35, url, 1, 112);
    assert(test_background_run());
    assert(test_error_logs == errors + 1);

    test_now += 3000000;
    app_loxone_audio_media_request(2, 35, url, 1, 112);
    assert(test_background_run());
    assert(test_error_logs == errors + 2 && strstr(test_last_log, "hely=3;"));
    unsigned recoveries = test_recovery_logs;
    test_status = 200;
    test_now += 2000000;
    app_loxone_audio_media_request(2, 35, url, 1, 112);
    assert(test_background_run());
    assert(test_recovery_logs == recoveries + 1 && strstr(test_last_log, "helyreállt"));
    app_loxone_audio_media_request(2, 35, url, 1, 112);
    assert(test_recovery_logs == recoveries + 1);

    /* Esedékes naplóablakban sem hiba a részlet bezárása vagy az oldalváltás. */
    test_now += 10000000;
    errors = test_error_logs;
    test_close_detail = test_read_failure = true;
    app_loxone_audio_media_request(LX_AUDIO_MEDIA_SLOTS - 1, 35, url, 2, 256);
    assert(test_background_run());
    assert(test_error_logs == errors);
    test_read_failure = false;
    test_cancel_generation = 36;
    app_loxone_audio_media_request(3, 35, url, 3, 112);
    assert(test_background_run());
    assert(test_error_logs == errors && app_loxone_audio_media_idle());
}

static void executor_single_image_test(void)
{
    app_loxone_audio_media_clear(40);
    test_status = 200;
    test_declared = 4;
    unsigned connections = test_connections, decodes = test_decodes, callbacks = test_callbacks;
    unsigned detail = LX_AUDIO_MEDIA_SLOTS - 1;
    app_loxone_audio_media_request(0, 40, "https://covers.example/first.png", 1, 112);
    app_loxone_audio_media_request(1, 40, "https://covers.example/second.png", 1, 112);
    app_loxone_audio_media_request(detail, 40, "https://covers.example/detail.png", 1, 256);
    for (unsigned i = 0; i < 10; ++i)
        app_loxone_audio_media_request(0, 40, "https://covers.example/first.png", 1, 112);
    assert(test_connections == connections && test_decodes == decodes);
    assert(test_background_callback && app_loxone_audio_media_idle());
    assert(test_background_run());
    assert(test_connections == connections + 1 && test_decodes == decodes + 1);
    assert(media_slots[detail].image && media_slots[detail].image->width == 256);
    assert(!media_slots[0].image && !media_slots[1].image && test_background_callback);
    assert(test_background_run());
    assert(test_connections == connections + 2 && test_decodes == decodes + 2);
    assert(media_slots[0].image && !media_slots[1].image && test_background_callback);
    assert(test_background_run());
    assert(test_connections == connections + 3 && test_decodes == decodes + 3);
    assert(media_slots[1].image && !test_background_callback && test_callbacks == callbacks + 3);
    app_loxone_audio_media_request(2, 40, "https://covers.example/first.png", 1, 112);
    assert(media_slots[2].image == media_slots[0].image && !test_background_callback);
    assert(test_connections == connections + 3 && app_loxone_audio_media_idle());
}

static void enqueue_during_read(void)
{
    assert(test_callback_active && !app_loxone_audio_media_idle());
    app_loxone_audio_media_request(1, media_generation, "https://covers.example/arrived.png", 1, 112);
    assert(test_background_callback);
}

static void replace_generation_during_read(void)
{
    assert(!app_loxone_audio_media_idle());
    unsigned next = media_generation + 1;
    app_loxone_audio_media_clear(next);
    assert(!app_loxone_audio_media_idle());
    app_loxone_audio_media_request(0, next, "https://covers.example/new-page.png", 2, 112);
    /* A későn érkező régi clear az azonos callback új ébresztését sem törli. */
    app_loxone_audio_media_clear(next - 1);
    assert(test_background_callback && media_generation == next);
}

static void executor_handoff_test(void)
{
    app_loxone_audio_media_clear(41);
    unsigned connections = test_connections;
    app_loxone_audio_media_request(0, 41, "https://covers.example/running.png", 1, 112);
    test_read_hook = enqueue_during_read;
    assert(test_background_run());
    assert(test_connections == connections + 1 && media_slots[0].image);
    assert(test_background_callback && media_slots[1].pending && !media_slots[1].image);
    assert(test_background_run());
    assert(test_connections == connections + 2 && media_slots[1].image && !test_background_callback);

    app_loxone_audio_media_clear(42);
    app_loxone_audio_media_request(0, 42, "https://covers.example/old-page.png", 1, 112);
    test_read_hook = replace_generation_during_read;
    unsigned decodes = test_decodes;
    assert(test_background_run());
    assert(media_generation == 43 && !media_slots[0].image && test_decodes == decodes);
    assert(test_background_callback && app_loxone_audio_media_idle());
    assert(test_background_run());
    assert(media_slots[0].image && media_slots[0].image->generation == 43);
    assert(!test_background_callback);

    /* A kamera a még el sem indított, már érvénytelen munkára nem várhat. */
    app_loxone_audio_media_clear(44);
    app_loxone_audio_media_request(0, 44, "https://covers.example/queued.png", 1, 112);
    app_loxone_audio_media_clear(45);
    connections = test_connections;
    unsigned allocations = test_job_allocations;
    assert(test_background_callback && app_loxone_audio_media_idle());
    assert(test_background_run());
    assert(test_connections == connections && test_job_allocations == allocations);
    assert(!test_background_callback && app_loxone_audio_media_idle());
}

static void ota_begins_before_callback(void)
{
    test_ota_active = true;
}

static void ota_deferral_test(void)
{
    /* TLS/open, törzsolvasás, letöltés utáni dekóderkapu, publikálási kapu,
     * valamint OTA-val átfedő dekóderhiba: mind halasztás, nem végleges hiba. */
    for (unsigned phase = 1; phase <= 5; ++phase)
    {
        unsigned generation = 50 + phase;
        app_loxone_audio_media_clear(generation);
        test_now += 10000000;
        unsigned errors = test_error_logs, recoveries = test_recovery_logs;
        unsigned decodes = test_decodes;
        test_ota_during = phase == 5 ? 4 : phase;
        test_decode_failure = phase == 5;
        app_loxone_audio_media_request(0, generation, "https://covers.example/ota.png", 1, 112);
        assert(test_background_run());
        assert(test_ota_active && !media_slots[0].image && media_slots[0].pending);
        assert(media_slots[0].attempts == 0 && !media_slots[0].retry_at);
        assert(test_error_logs == errors && test_recovery_logs == recoveries);
        assert(test_background_callback && app_loxone_audio_media_idle());
        assert(test_decodes == decodes + (phase >= 4));
        assert(!test_background_run());
        test_ota_during = 0;
        test_decode_failure = test_ota_active = false;
        /* Nincs új UI-kérés: az OTA vége után a megtartott beadás folytatódik. */
        assert(test_background_run());
        assert(media_slots[0].image && media_slots[0].attempts == 1);
        assert(!test_background_callback && !media_slots[0].pending);
        assert(test_error_logs == errors && test_recovery_logs == recoveries);
    }

    app_loxone_audio_media_clear(56);
    app_loxone_audio_media_request(0, 56, "https://covers.example/queued-ota.png", 1, 112);
    unsigned allocations = test_job_allocations, connections = test_connections;
    test_ota_active = true;
    assert(!test_background_run() && app_loxone_audio_media_idle());
    test_ota_active = false;
    /* A slot átvétele és a callback kezdete közötti OTA-foglalás is védett. */
    test_before_callback = ota_begins_before_callback;
    assert(test_background_run());
    assert(test_job_allocations == allocations && test_connections == connections);
    assert(media_slots[0].pending && !media_slots[0].attempts && test_background_callback);
    assert(app_loxone_audio_media_idle());
    test_ota_active = false;
    assert(test_background_run());
    assert(media_slots[0].image && !test_background_callback);
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
    executor_single_image_test();
    executor_handoff_test();
    ota_deferral_test();
    app_loxone_audio_media_clear(57);
    assert(test_task_starts == 0 && test_task_deletes == 0 && test_http_live == 0);
    assert(!test_background_callback && app_loxone_audio_media_idle());
    free(media_slots);
    free(media_lock);
    puts("PASS: ESP cover one-image executor, no task allocation/deletion, OTA deferral, generation handoff, ownership, bounded retry and HTTP limits");
    return 0;
}
