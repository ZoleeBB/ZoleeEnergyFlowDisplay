/* A tényleges ESP-oldali Audio futtató: kézi idő, determinisztikus WS/RTOS. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include "../main/app_loxone_audio_runtime.c"

struct test_audio_semaphore { bool binary, held; };
typedef struct { unsigned capacity, count, head, tail; size_t size; unsigned char *data; bool with_caps; } test_queue_t;
static int64_t test_now = 1000000;
static unsigned test_transports, test_transport_attempts, test_native_sends, test_task_creations;
static unsigned test_volume, test_media_generation;
static bool test_fail_allocation, test_fail_queue, test_cancel_before_enqueue;
static bool test_fail_semaphore, test_ota_active, test_exit_when_idle;
static unsigned test_heap_allocations, test_semaphores_created, test_semaphores_deleted;
static void (*test_init_hook)(void);
static jmp_buf test_runner_exit;
static bool test_transport_busy;
enum { TEST_NATIVE_NONE, TEST_NATIVE_INIT, TEST_NATIVE_AUTH, TEST_NATIVE_SEND, TEST_NATIVE_STOP };
static unsigned test_native_wait_point, test_native_wait_checks;
static void test_miniserver_during_native_wait(unsigned point);
static char test_native_command[128];
static unsigned test_native_buffer_size, test_native_command_length, test_auth_command_length;
static bool test_native_init_oom, test_auth_oom;
static int test_native_send_failure;
static void (*test_native_sleep_hook)(void);
static void (*test_native_stop_hook)(void);
static void (*test_native_send_hook)(const char *command);
static void (*test_audio_lock_hook)(void);
static void (*test_native_event_callback)(void *, esp_event_base_t, int32_t, void *);
static void *test_native_event_argument;
static bool test_native_start_error, test_native_stop_disconnect;
static unsigned test_native_starts;
static unsigned test_caps_queues_created, test_caps_queues_deleted;
static void (*test_queue_receive_hook)(bool received);
static audio_connection_t *test_receive_allocation;
static size_t test_receive_peak;
static bool test_native_callback_active;
static unsigned test_web_events;
static char test_web_event[APP_EVENT_TEXT_MAX];

void app_events_add(app_event_level_t level, const char *format, ...)
{
    assert(level == APP_EVENT_WARNING);
    assert((!audio_lock || !audio_lock->held) && !test_native_callback_active);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(test_web_event, sizeof(test_web_event), format, arguments);
    va_end(arguments);
    assert(!strstr(test_web_event, "test.jwt") && !strstr(test_web_event, "test-session"));
    assert(!strstr(test_web_event, "unit-test-auth") && !strstr(test_web_event, "192.168."));
    assert(!strstr(test_web_event, "Tesztzene") && !strstr(test_web_event, "roomfav/play"));
    assert(!strstr(test_web_event, "PRIVATE-AUTH") && !strstr(test_web_event, "PRIVATE-RESULT"));
    ++test_web_events;
}

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    ++test_heap_allocations;
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (test_receive_allocation)
    {
        size_t owned = atomic_load(&test_receive_allocation->received_bytes);
        assert(owned <= AUDIO_RX_BYTES_MAX);
        if (owned > test_receive_peak)
            test_receive_peak = owned;
    }
    if (test_fail_allocation)
    {
        test_fail_allocation = false;
        return NULL;
    }
    return calloc(count, size);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    if (test_init_hook)
    {
        void (*hook)(void) = test_init_hook;
        test_init_hook = NULL;
        hook();
    }
    if (test_fail_semaphore)
    {
        test_fail_semaphore = false;
        return NULL;
    }
    ++test_semaphores_created;
    return calloc(1, sizeof(struct test_audio_semaphore));
}
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    SemaphoreHandle_t result = xSemaphoreCreateMutex();
    if (result)
        result->binary = true;
    return result;
}
void vSemaphoreDelete(SemaphoreHandle_t lock)
{
    assert(lock && !lock->held);
    ++test_semaphores_deleted;
    free(lock);
}
int xSemaphoreTake(SemaphoreHandle_t lock, TickType_t timeout)
{
    if (lock->binary && timeout == portMAX_DELAY && test_exit_when_idle)
        longjmp(test_runner_exit, 1);
    if (lock->binary && timeout && test_native_sleep_hook)
        test_native_sleep_hook();
    if (!lock->binary)
    {
        if (lock == audio_lock && test_audio_lock_hook)
            test_audio_lock_hook();
        assert(!lock->held);
        lock->held = true;
    }
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t lock)
{
    if (!lock->binary)
    {
        assert(lock->held);
        lock->held = false;
    }
    return pdTRUE;
}
BaseType_t xTaskCreateWithCaps(TaskFunction_t function, const char *name, uint32_t stack,
                              void *argument, UBaseType_t priority, TaskHandle_t *handle, unsigned caps)
{
    (void)function; (void)name; (void)argument; (void)priority;
    (void)stack; (void)caps;
    ++test_task_creations;
    /* A panelen a legnagyobb belső blokk 7936 B. Az Audio saját taskjának
     * minden foglalását tiltjuk; a meglévő main-feladatnak így is futnia kell. */
    *handle = NULL;
    return 0;
}
void vTaskDeleteWithCaps(TaskHandle_t handle) { (void)handle; }
void vTaskDelay(TickType_t ticks) { test_now += (int64_t)ticks * 1000; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle) { (void)handle; return 4096; }
bool app_ota_operation_active(void) { return test_ota_active; }
int64_t esp_timer_get_time(void) { return test_now; }
void esp_fill_random(void *output, size_t length) { memset(output, 42, length); }

QueueHandle_t xQueueCreate(unsigned count, size_t size)
{
    if (test_fail_queue)
    {
        test_fail_queue = false;
        return NULL;
    }
    test_queue_t *q = calloc(1, sizeof(*q));
    assert(q);
    q->capacity = count; q->size = size; q->data = calloc(count, size);
    assert(q->data);
    return q;
}
int xQueueSend(QueueHandle_t queue, const void *data, TickType_t timeout)
{
    assert(timeout == 0); /* A WS callback nem várhat a workerre. */
    test_queue_t *q = queue;
    if (q->count == q->capacity) return 0;
    memcpy(q->data + q->tail * q->size, data, q->size);
    q->tail = (q->tail + 1) % q->capacity; ++q->count;
    return 1;
}
int xQueueReceive(QueueHandle_t queue, void *data, TickType_t timeout)
{
    (void)timeout;
    test_queue_t *q = queue;
    if (!q->count)
    {
        if (test_queue_receive_hook)
            test_queue_receive_hook(false);
        return 0;
    }
    memcpy(data, q->data + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->capacity; --q->count;
    if (test_queue_receive_hook)
        test_queue_receive_hook(true);
    return 1;
}
void vQueueDelete(QueueHandle_t queue)
{
    test_queue_t *q = queue;
    assert(!q->with_caps);
    free(q->data); free(q);
}

QueueHandle_t xQueueCreateWithCaps(UBaseType_t count, UBaseType_t size, UBaseType_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(count == AUDIO_RX_QUEUE_LENGTH && size == sizeof(audio_message_t));
    test_queue_t *queue = xQueueCreate(count, size);
    if (queue)
    {
        queue->with_caps = true;
        ++test_caps_queues_created;
    }
    return queue;
}

void vQueueDeleteWithCaps(QueueHandle_t handle)
{
    test_queue_t *queue = handle;
    assert(queue->with_caps);
    ++test_caps_queues_deleted;
    queue->with_caps = false;
    vQueueDelete(handle);
}

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *options)
{
    assert(!strcmp(options->subprotocol, "remotecontrol"));
    assert(options->task_stack == 4096 && options->task_stack <= 7936);
    assert(options->buffer_size == LX_AUDIO_AUTH_WS_BUFFER_SIZE);
    test_native_buffer_size = (unsigned)options->buffer_size;
    test_miniserver_during_native_wait(TEST_NATIVE_INIT);
    if (test_native_init_oom)
    {
        test_native_init_oom = false;
        return NULL;
    }
    return (void *)1;
}
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t handle, int event,
    void (*callback)(void *, esp_event_base_t, int32_t, void *), void *argument)
{
    (void)handle; (void)event;
    test_native_event_callback = callback;
    test_native_event_argument = argument;
    return ESP_OK;
}
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t handle)
{
    (void)handle;
    ++test_native_starts;
    if (test_native_start_error)
    {
        test_native_start_error = false;
        test_native_event_callback(test_native_event_argument, NULL, WEBSOCKET_EVENT_ERROR, NULL);
    }
    return ESP_OK;
}
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t handle)
{
    (void)handle;
    test_miniserver_during_native_wait(TEST_NATIVE_STOP);
    if (test_native_stop_hook)
    {
        void (*hook)(void) = test_native_stop_hook;
        test_native_stop_hook = NULL;
        hook();
    }
    if (test_native_stop_disconnect)
        test_native_event_callback(test_native_event_argument, NULL, WEBSOCKET_EVENT_DISCONNECTED, NULL);
    return ESP_OK;
}
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t handle) { (void)handle; return ESP_OK; }
int esp_websocket_client_send_text(esp_websocket_client_handle_t handle, const char *command, int length, TickType_t timeout)
{
    (void)handle; (void)timeout;
    assert(!audio_lock->held);
    test_miniserver_during_native_wait(TEST_NATIVE_SEND);
    assert(length >= 0 && (size_t)length == strlen(command));
    test_native_command_length = (unsigned)length;
    size_t copied = (size_t)length < sizeof(test_native_command) - 1 ?
        (size_t)length : sizeof(test_native_command) - 1;
    memcpy(test_native_command, command, copied);
    test_native_command[copied] = 0;
    ++test_native_sends;
    if (test_native_send_failure)
    {
        int failure = test_native_send_failure;
        test_native_send_failure = 0;
        return failure < 0 ? -1 : length - 1;
    }
    if (test_native_send_hook)
        test_native_send_hook(command);
    return length;
}
void mbedtls_platform_zeroize(void *data, size_t length) { memset(data, 0, length); }
char *lx_audio_auth_create(const char *key, const char *user, const char *jwt, const char *session,
    int (*random_bytes)(void *, unsigned char *, size_t), void *context)
{
    (void)key; (void)user; (void)jwt; (void)session; (void)random_bytes; (void)context;
    test_miniserver_during_native_wait(TEST_NATIVE_AUTH);
    if (test_auth_oom)
    {
        test_auth_oom = false;
        return NULL;
    }
    if (test_auth_command_length)
    {
        const char *prefix = "secure/authenticate/PRIVATE-AUTH/";
        assert(test_auth_command_length >= strlen(prefix));
        char *command = malloc((size_t)test_auth_command_length + 1);
        assert(command);
        memset(command, 'A', test_auth_command_length);
        memcpy(command, prefix, strlen(prefix));
        command[test_auth_command_length] = 0;
        return command;
    }
    return strdup("unit-test-auth");
}
void lx_audio_auth_free(char *command) { free(command); }
void app_loxone_audio_media_clear(unsigned generation) { test_media_generation = generation; }
void app_loxone_audio_media_close_detail(void) {}
esp_err_t app_loxone_audio_transport(const char *id, unsigned revision, lx_audio_action_t action,
                                    unsigned value, unsigned edit_generation)
{
    assert(!audio_lock->held);
    assert(!strcmp(id, "audio-one") && revision == 7);
    ++test_transport_attempts;
    if (test_transport_busy)
        return ESP_ERR_INVALID_STATE;
    if (test_cancel_before_enqueue)
    {
        test_cancel_before_enqueue = false;
        app_loxone_audio_cancel_volume(id, revision);
    }
    if (!app_loxone_audio_queued_allowed(id, revision, action, value, edit_generation))
        return ESP_ERR_INVALID_STATE;
    ++test_transports;
    test_volume = value;
    return ESP_OK;
}

static void state(const char *suffix, double value)
{
    char uuid[LX_UUID_LEN];
    snprintf(uuid, sizeof(uuid), "40000000-0000-0000-00000000000000%s", suffix);
    app_loxone_audio_state(uuid, true, value, NULL, 0, test_now);
}

static void prepare_page_with_session(bool session)
{
    const char *json = "{\"mediaServer\":{\"server\":{\"type\":2,\"host\":\"192.168.1.40:7091\"}},"
        "\"controls\":{\"40000000-0000-0000-0000000000000001\":{\"type\":\"AudioZoneV2\","
        "\"name\":\"Audio\",\"details\":{\"server\":\"server\",\"playerid\":3},\"states\":{"
        "\"volume\":\"40000000-0000-0000-0000000000000011\","
        "\"isLocked\":\"40000000-0000-0000-0000000000000012\","
        "\"jLocked\":\"40000000-0000-0000-0000000000000013\","
        "\"maxVolume\":\"40000000-0000-0000-0000000000000014\"}}}}";
    cJSON *catalog = lx_catalog_parse(json, strlen(json));
    lx_home_config_t *config = calloc(1, sizeof(*config));
    assert(config && catalog);
    config->card_count = 1;
    config->cards[0].type = LX_HOME_CARD_AUDIO;
    strcpy(config->cards[0].id, "audio-one");
    strcpy(config->cards[0].control_uuid, "40000000-0000-0000-0000000000000001");
    if (session)
        app_loxone_audio_session("user", "test.jwt", true);
    app_loxone_audio_page(config, catalog, 0, 7);
    free(config); cJSON_Delete(catalog);
    assert(audio_page && test_media_generation == audio_page->generation);
    assert(test_task_creations == 0);
    assert(!app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY));
    state("12", 0); state("14", 60); state("11", 25);
    assert(!app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY));
    app_loxone_audio_state("40000000-0000-0000-0000000000000013", false, 0, "", 0, test_now);
    assert(app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY) == session);
    assert(!app_loxone_audio_allowed("audio-one", 6, LX_AUDIO_PLAY));
    assert(!app_loxone_audio_allowed("other", 7, LX_AUDIO_PLAY));
}

static void prepare_page(void) { prepare_page_with_session(true); }

static void run_until_idle(void)
{
    assert(!atomic_load(&audio_runner_active));
    test_exit_when_idle = true;
    if (setjmp(test_runner_exit) == 0)
    {
        app_loxone_audio_run();
        assert(!"A main Audio futtató nem térhet vissza");
    }
    test_exit_when_idle = false;
    atomic_store(&audio_runner_active, false);
}

static void volume_test(void)
{
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 10, false) == ESP_OK);
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 20, false) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transports == 1 && test_volume == 20);
    test_now += 50000;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 30, false) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transports == 1);
    test_now += 50000;
    app_loxone_audio_poll(test_now);
    assert(test_transports == 2 && test_volume == 30);
    test_now += 10000;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 40, true) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transports == 3 && test_volume == 40);
    assert(audio_page->slots[0].track.volume == 25); /* Kérés != visszajelzés. */
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 61, true) != ESP_OK);
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 60, true) == ESP_OK);
    assert(app_loxone_audio_value_allowed("audio-one", 7, LX_AUDIO_VOLUME, 60));
    state("14", 50);
    assert(!app_loxone_audio_value_allowed("audio-one", 7, LX_AUDIO_VOLUME, 60));
    assert(app_loxone_audio_value_allowed("audio-one", 7, LX_AUDIO_VOLUME, 50));
    app_loxone_audio_poll(test_now);
    assert(test_transports == 3 && !audio_page->slots[0].volume_dirty);
    state("12", 1);
    assert(!app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY));
    state("12", 0);
    const char *locked = "{\"locked\":1}";
    app_loxone_audio_state("40000000-0000-0000-0000000000000013", false, 0, locked, strlen(locked), test_now);
    assert(!app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY));
    app_loxone_audio_state("40000000-0000-0000-0000000000000013", false, 0, "", 0, test_now);
    assert(app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY));
}

static audio_connection_t *connection_new(void)
{
    audio_connection_t *connection = calloc(1, sizeof(*connection));
    assert(connection);
    connection->generation = audio_page->generation;
    connection->credential_generation = audio_credentials->generation;
    connection->ws = (void *)1;
    connection->port = 7091;
    strcpy(connection->host, "192.168.1.40");
    connection->phase = 4;
    connection->favorites_slot = -1;
    connection->messages = xQueueCreateWithCaps(AUDIO_RX_QUEUE_LENGTH, sizeof(audio_message_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    audio_page->slots[0].native_ready = true;
    return connection;
}

static void volume_cancellation_test(void)
{
    unsigned sent = test_transports;
    unsigned generation = audio_page->slots[0].edit_generation;
    unsigned actual = audio_page->slots[0].track.volume;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 45, true) == ESP_OK);
    app_loxone_audio_cancel_volume("other", 7);
    app_loxone_audio_cancel_volume("audio-one", 6);
    assert(audio_page->slots[0].volume_dirty && audio_page->slots[0].edit_generation == generation);
    app_loxone_audio_cancel_volume("audio-one", 7);
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent && !audio_page->slots[0].volume_dirty);
    assert(audio_page->slots[0].track.volume == actual);
    assert(!app_loxone_audio_queued_allowed("audio-one", 7, LX_AUDIO_VOLUME, 45, generation));
    assert(!app_loxone_audio_queued_allowed("audio-one", 7, LX_AUDIO_VOLUME_UP, 0, generation));
    assert(!app_loxone_audio_queued_allowed("audio-one", 7, LX_AUDIO_VOLUME_DOWN, 0, generation));
    assert(app_loxone_audio_queued_allowed("audio-one", 7, LX_AUDIO_PLAY, 0, 0));

    /* A worker már kivette a célt, de a modal a közös sorba vétel előtt zárul. */
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 42, true) == ESP_OK);
    test_cancel_before_enqueue = true;
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent && !audio_page->slots[0].volume_dirty);
    assert(audio_page->slots[0].track.volume == actual);
    test_cancel_before_enqueue = true;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME_UP, 0, true) != ESP_OK);
    assert(test_transports == sent);
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 40, true) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent + 1 && test_volume == 40);
    app_loxone_audio_cancel_volume("audio-one", 7);
    assert(audio_page->slots[0].track.volume == actual);
}

static void connection_delete(audio_connection_t *connection)
{
    audio_connection_close(connection);
    assert(atomic_load(&connection->received_bytes) == 0);
    vQueueDeleteWithCaps(connection->messages);
    free(connection);
}

static void message_reply(audio_connection_t *connection, const char *reply)
{
    audio_message_t message = {.data = (char *)reply, .length = strlen(reply), .received_at = test_now};
    audio_message(connection, &message);
}

static void test_miniserver_during_native_wait(unsigned point)
{
    if (test_native_wait_point != point)
        return;
    test_native_wait_point = TEST_NATIVE_NONE;
    ++test_native_wait_checks;
    assert(!audio_lock->held);
    unsigned sent = test_transports;
    unsigned actual = audio_page->slots[0].track.volume;
    lx_audio_summary_t summary;
    /* A natív művelet még nem tért vissza. E közben érkezik a GUI új húzása,
     * és fut a másik feladatban a Miniserver normál 100 ms-os köre. */
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 14, false) == ESP_OK);
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 35, false) == ESP_OK);
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 44, true) == ESP_OK);
    test_now += 50000;
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent + 1 && test_volume == 44);
    assert(!audio_page->slots[0].volume_dirty);
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(summary.volume_pending && summary.desired_volume == 44 && summary.track.volume == actual);
    state("11", 44);
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(!summary.volume_pending && !summary.volume_failed && summary.track.volume == 44);
}

static void native_waiting_volume_test(void)
{
    audio_connection_t *connection = connection_new();
    unsigned checks = test_native_wait_checks;
    test_native_wait_point = TEST_NATIVE_INIT;
    audio_connection_open(connection, 0);
    assert(test_native_wait_checks == ++checks);
    test_native_wait_point = TEST_NATIVE_AUTH;
    connection->phase = 2;
    message_reply(connection, "{\"getkey_result\":[{\"pubkey\":\"fixture public key\"}]}");
    assert(test_native_wait_checks == ++checks);
    test_native_wait_point = TEST_NATIVE_SEND;
    assert(audio_send(connection, "audio/3/status"));
    assert(test_native_wait_checks == ++checks);
    test_native_wait_point = TEST_NATIVE_STOP;
    audio_connection_close(connection);
    assert(test_native_wait_checks == ++checks);
    connection_delete(connection);
}

static void volume_feedback_and_backpressure_test(void)
{
    lx_audio_summary_t summary;
    unsigned sent = test_transports;
    audio_page->slots[0].status = LX_AUDIO_AUTH_FAILED;
    audio_page->slots[0].native_ready = false;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 40, true) == ESP_OK);
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(summary.volume_pending && summary.desired_volume == 40 && summary.track.volume == 44);
    app_loxone_audio_poll(test_now);
    assert(test_transports == ++sent);
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(summary.volume_pending && !summary.volume_failed && summary.status == LX_AUDIO_AUTH_FAILED);
    state("11", 30);
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(summary.volume_pending && summary.desired_volume == 40 && summary.track.volume == 30);
    state("11", 40);
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(!summary.volume_pending && !summary.volume_failed && summary.track.volume == 40);

    /* Foglalt MS-sornál egy célérték marad, a köztes húzások nem torlódnak.
     * A végső, már elfogadott értéket a futtató nem küldi el újra. */
    test_transport_busy = true;
    unsigned attempts = test_transport_attempts;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 15, true) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transport_attempts == ++attempts && test_transports == sent);
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 21, false) == ESP_OK);
    test_now += 50000;
    app_loxone_audio_poll(test_now);
    assert(test_transport_attempts == attempts);
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 34, false) == ESP_OK);
    test_now += 50000;
    test_transport_busy = false;
    app_loxone_audio_poll(test_now);
    assert(test_transport_attempts == ++attempts && test_transports == ++sent && test_volume == 34);
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(summary.volume_pending && summary.desired_volume == 34 && summary.track.volume == 40);
    test_now += AUDIO_VOLUME_CONFIRM_US;
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(!summary.volume_pending && summary.volume_failed && summary.track.volume == 40);
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent && test_transport_attempts == attempts);
    state("11", 34);
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(!summary.volume_pending && !summary.volume_failed && summary.track.volume == 34);
    state("11", 40);

    /* Tartós sorfoglaltság és a friss válasz hiánya külön sem tarthatja
     * végtelen ideig a régi célt. A lejárt szerkesztés generációja is érvénytelen. */
    test_transport_busy = true;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 25, true) == ESP_OK);
    unsigned edit = audio_page->slots[0].edit_generation;
    app_loxone_audio_poll(test_now);
    test_now += AUDIO_VOLUME_CONFIRM_US;
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(!summary.volume_pending && summary.volume_failed && summary.track.volume == 40);
    test_transport_busy = false;
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent);
    assert(!app_loxone_audio_queued_allowed("audio-one", 7, LX_AUDIO_VOLUME, 25, edit));
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 44, true) == ESP_OK);
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(summary.volume_pending && !summary.volume_failed);
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent + 1);
    state("11", 44);
}

static void observed_radio_event_test(void)
{
    audio_connection_t *connection = connection_new();
    audio_page->slots[0].endpoint.player = 5;
    message_reply(connection, "{\"audio_event\":[{\"playerid\":5,\"station\":\"95.8 Sláger FM\","
        "\"title\":\"\",\"artist\":\"\",\"album\":\"\",\"mode\":\"stop\",\"volume\":6,"
        "\"coverurl\":\"https://cdn-profiles.tunein.com/s25966/images/logoq.jpg?t=637069206200000000\","
        "\"clientState\":\"on\"}]}");
    lx_audio_summary_t summary;
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(summary.native_ready && !strcmp(summary.track.station, "95.8 Sláger FM"));
    assert(!summary.track.title[0] && summary.track.mode == LX_AUDIO_MODE_STOP);
    assert(summary.track.volume_known && summary.track.volume == 6);
    assert(!strcmp(summary.track.cover,
        "https://cdn-profiles.tunein.com/s25966/images/logoq.jpg?t=637069206200000000"));
    assert(summary.track.version != 0);

    /* Natív visszajelzés is nyugta, de csak a megfelelő zóna tényleges
     * volume mezője. A borító/állomás változása nem friss hangerőérték. */
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 6, true) == ESP_OK);
    app_loxone_audio_poll(test_now);
    message_reply(connection, "{\"audio_event\":[{\"playerid\":5,\"station\":\"95.8 Sláger FM\"}]}");
    assert(app_loxone_audio_summary("audio-one", 7, &summary) && summary.volume_pending);
    message_reply(connection, "{\"audio_event\":[{\"playerid\":4,\"volume\":6},{\"playerid\":5,\"title\":\"\"}]}");
    assert(app_loxone_audio_summary("audio-one", 7, &summary) && summary.volume_pending);
    message_reply(connection, "{\"audio_event\":[{\"playerid\":5,\"volume\":5}]}");
    assert(app_loxone_audio_summary("audio-one", 7, &summary) && summary.volume_pending && summary.track.volume == 5);
    message_reply(connection, "{\"audio_event\":[{\"playerid\":5,\"volume\":6}]}");
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(!summary.volume_pending && !summary.volume_failed && summary.track.volume == 6);
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 40, false) == ESP_OK);
    message_reply(connection, "{\"audio_event\":[{\"playerid\":5,\"volume\":40}]}");
    assert(app_loxone_audio_summary("audio-one", 7, &summary) && summary.volume_pending);
    test_now += AUDIO_VOLUME_INTERVAL_US;
    app_loxone_audio_poll(test_now);
    message_reply(connection, "{\"audio_event\":[{\"playerid\":5,\"station\":\"95.8 Sláger FM\"}]}");
    assert(app_loxone_audio_summary("audio-one", 7, &summary) && summary.volume_pending);
    message_reply(connection, "{\"status_result\":[{\"playerid\":5,\"volume\":40}]}");
    assert(app_loxone_audio_summary("audio-one", 7, &summary));
    assert(!summary.volume_pending && !summary.volume_failed && summary.track.volume == 40);
    audio_page->slots[0].endpoint.player = 3;
    connection_delete(connection);
}

static void favorites_test(void)
{
    audio_connection_t *connection = connection_new();
    assert(app_loxone_audio_favorites_request("audio-one", 7, 0) == ESP_OK);
    audio_native_poll(connection);
    assert(!strcmp(test_native_command, "audio/cfg/getroomfavs/3/0/32"));
    const char *reply = "{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":2,\"items\":["
        "{\"id\":901,\"slot\":0,\"name\":\"Jazz\"},{\"id\":1357,\"slot\":1,\"name\":\"Radio\"}]}]}";
    message_reply(connection, reply);
    assert(audio_page->favorites.ready && audio_page->favorites.count == 2);
    unsigned generation = audio_page->favorites.generation;
    assert(app_loxone_audio_favorite("audio-one", 7, 1, generation) != ESP_OK);
    assert(app_loxone_audio_favorite("audio-one", 7, 1357, generation + 1) != ESP_OK);
    assert(app_loxone_audio_favorite("audio-one", 7, 1357, generation) == ESP_OK);
    audio_native_poll(connection);
    assert(!strcmp(test_native_command, "audio/3/roomfav/play/1357/noshuffle"));
    connection_delete(connection);
}

static void favorites_lifecycle_test(void)
{
    audio_connection_t *connection = connection_new();
    audio_page->count = 2;
    audio_page->slots[1] = audio_page->slots[0];
    strcpy(audio_page->slots[1].id, "audio-two");
    audio_page->slots[1].endpoint.player = 4;

    assert(app_loxone_audio_favorites_request("audio-one", 7, 0) == ESP_OK);
    audio_native_poll(connection);
    unsigned sent = test_native_sends;
    app_loxone_audio_detail_close();
    assert(app_loxone_audio_favorites_request("audio-two", 7, 0) == ESP_OK);
    audio_native_poll(connection);
    assert(test_native_sends == sent && connection->favorites_player == 3);
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":1,"
        "\"items\":[{\"id\":901,\"name\":\"Old zone\"}]}]}");
    assert(!audio_page->favorites.ready && !audio_page->favorites.failed);
    assert(audio_page->favorites_dirty && connection->favorites_slot == -1);
    audio_native_poll(connection);
    assert(test_native_sends == sent + 1 && !strcmp(test_native_command, "audio/cfg/getroomfavs/4/0/32"));
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":4,\"start\":0,\"totalitems\":71,"
        "\"items\":[{\"id\":1024,\"name\":\"New zone\"},{\"id\":1500,\"slot\":9,\"name\":\"Second\"}]}]}");
    assert(audio_page->favorites.ready && audio_page->favorites.player == 4);
    assert(audio_page->favorites.count == 2 && audio_page->favorites.total == 71);
    assert(app_loxone_audio_favorites_request("audio-two", 7, 2) == ESP_OK);
    audio_native_poll(connection);
    assert(!strcmp(test_native_command, "audio/cfg/getroomfavs/4/2/32"));

    /* Kapcsolatvesztéskor az olvasás újrakérhető, a lejátszás nem. */
    audio_connection_close(connection);
    assert(audio_page->favorites_dirty && !audio_page->favorites.failed);
    assert(!audio_page->slots[1].native_ready);
    audio_connection_open(connection, 1);
    connection->phase = 4;
    audio_page->slots[1].native_ready = true;
    audio_native_poll(connection);
    assert(!strcmp(test_native_command, "audio/cfg/getroomfavs/4/2/32"));

    /* Időtúllépés után a régi socket már nem fogadhat késői választ. */
    audio_favorites_timeout(connection, connection->favorites_deadline + 1);
    assert(connection->dead && audio_page->favorites.failed);
    esp_websocket_event_data_t late = {.op_code = 1, .fin = true, .data_ptr = "{}", .data_len = 2, .payload_len = 2};
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &late);
    assert(((test_queue_t *)connection->messages)->count == 0);
    connection_delete(connection);

    connection = connection_new();
    assert(app_loxone_audio_favorites_request("audio-one", 7, 0) == ESP_OK);
    audio_native_poll(connection);
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":1,"
        "\"items\":[{\"id\":901,\"name\":\"Jazz\"}]}]}");
    unsigned generation = audio_page->favorites.generation;
    assert(app_loxone_audio_favorite("audio-one", 7, 901, generation) == ESP_OK);
    audio_connection_close(connection);
    assert(!audio_page->favorite_queued);
    assert(app_loxone_audio_favorite("audio-one", 7, 901, generation) != ESP_OK);
    audio_connection_open(connection, 0);
    connection->phase = 4;
    audio_page->slots[0].native_ready = true;
    assert(app_loxone_audio_favorite("audio-one", 7, 901, generation) == ESP_OK);
    sent = test_native_sends;
    app_loxone_audio_hide();
    audio_native_poll(connection);
    assert(test_native_sends == sent);
    assert(!app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY));
    connection_delete(connection);
}

static void init_reentry(void)
{
    assert(app_loxone_audio_init() == ESP_ERR_INVALID_STATE);
    assert(!atomic_load(&audio_ready) && !audio_lock && !audio_wake && !audio_credentials);
    app_loxone_audio_hide(); /* A félkész futtatót publikus olvasó sem érheti el. */
}

static void init_failure_test(void)
{
    test_fail_allocation = true;
    assert(app_loxone_audio_init() == ESP_ERR_NO_MEM);
    assert(!atomic_load(&audio_ready) && !audio_lock && !audio_wake && !audio_credentials);
    assert(!app_loxone_audio_session("user", "test.jwt", true));
    assert(test_semaphores_created == test_semaphores_deleted);
    unsigned attempts = test_heap_allocations;
    assert(app_loxone_audio_init() == ESP_ERR_NO_MEM && test_heap_allocations == attempts);
    test_now += 1000000;
    test_fail_semaphore = true;
    assert(app_loxone_audio_init() == ESP_ERR_NO_MEM);
    assert(test_semaphores_created == test_semaphores_deleted);
    test_now += 1000000;
    test_init_hook = init_reentry;
    assert(app_loxone_audio_init() == ESP_OK);
    assert(atomic_load(&audio_ready) && audio_lock && audio_wake && audio_credentials);
    attempts = test_heap_allocations;
    assert(app_loxone_audio_init() == ESP_OK && test_heap_allocations == attempts);
}

static void controller_allocation_failure_test(void)
{
    /* A kapcsolat/sor OOM-ja megőrzi a session/page modellt és az MS
     * hangerőutat. Újrapróba ugyanazon main-vermen, legkorábban 5 s után. */
    prepare_page();
    audio_controller_t controller = {0};
    test_fail_allocation = true;
    atomic_store(&audio_last_web_error_ms, 0);
    assert(audio_controller_step(&controller, test_now) == pdMS_TO_TICKS(50));
    assert(strstr(test_web_event, "ok=connection-memory"));
    assert(!controller.connection && audio_page && !test_task_creations);
    assert(audio_page->slots[0].status == LX_AUDIO_UNAVAILABLE);
    unsigned allocations = test_heap_allocations;
    unsigned sent = test_transports;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 12, true) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent + 1 && test_volume == 12);
    test_now += AUDIO_RESOURCE_RETRY_US - 1;
    audio_controller_step(&controller, test_now);
    assert(test_heap_allocations == allocations && !controller.connection);
    ++test_now;
    audio_controller_step(&controller, test_now);
    assert(controller.connection && controller.connection->ws);
    audio_controller_close(&controller);

    prepare_page();
    test_fail_queue = true;
    atomic_store(&audio_last_web_error_ms, 0);
    audio_controller_step(&controller, test_now);
    assert(strstr(test_web_event, "ok=receive-queue-memory"));
    assert(!controller.connection && audio_page && !test_task_creations);
    assert(audio_page->slots[0].status == LX_AUDIO_UNAVAILABLE);
    sent = test_transports;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 18, true) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent + 1 && test_volume == 18);
    test_now += AUDIO_RESOURCE_RETRY_US;
    audio_controller_step(&controller, test_now);
    assert(controller.connection && controller.connection->ws);
    audio_controller_close(&controller);
}

static void controller_lifecycle_test(void)
{
    audio_controller_t controller = {0};
    app_loxone_audio_session(NULL, NULL, false);
    prepare_page_with_session(false);
    unsigned starts = test_native_starts;
    assert(audio_controller_step(&controller, test_now) == portMAX_DELAY);
    assert(!controller.connection && audio_page && test_native_starts == starts);
    app_loxone_audio_session("user", "test.jwt", true);
    audio_controller_step(&controller, test_now);
    assert(controller.connection && test_native_starts == starts + 1);
    unsigned generation = controller.connection->generation;

    /* Hide csak a modell érvényességét váltja; WS stop saját main-körben. */
    app_loxone_audio_hide();
    assert(!audio_page && controller.connection->ws);
    prepare_page();
    audio_controller_step(&controller, test_now);
    assert(controller.connection->generation != generation && test_native_starts == starts + 2);
    unsigned credential = controller.connection->credential_generation;
    app_loxone_audio_session("user", "refreshed.jwt", true);
    audio_controller_step(&controller, test_now);
    assert(controller.connection->credential_generation != credential && test_native_starts == starts + 3);

    /* OTA foglalásra bontunk, de az oldal/session és MS vezérlés marad.
     * A foglalás vége ugyanazt a main-futtatót élesíti újra. */
    test_ota_active = true;
    audio_controller_step(&controller, test_now);
    assert(!controller.connection && audio_page && audio_credentials->live);
    audio_controller_step(&controller, test_now);
    assert(test_native_starts == starts + 3);
    test_ota_active = false;
    audio_controller_step(&controller, test_now);
    assert(controller.connection && test_native_starts == starts + 4);
    app_loxone_audio_hide();
    test_native_stop_hook = prepare_page;
    assert(audio_controller_step(&controller, test_now) == portMAX_DELAY);
    assert(!controller.connection && audio_page); /* Stop közben újra megnyílt. */
    audio_controller_step(&controller, test_now);
    assert(controller.connection && test_native_starts == starts + 5);
    app_loxone_audio_hide();
    assert(audio_controller_step(&controller, test_now) == portMAX_DELAY);
    assert(!controller.connection && !audio_page);
    assert(!test_task_creations);
    prepare_page();
}

static unsigned controller_handshake_stage;

static void controller_handshake_tick(void)
{
    audio_connection_t *connection = test_native_event_argument;
    const char *reply = NULL;
    switch (controller_handshake_stage++)
    {
        case 0:
            assert(connection->phase == 1);
            reply = "LWSS V 17.3.11.12 | ~API:1.6~ | Session-Token: test-session";
            break;
        case 1:
            assert(connection->phase == 2);
            reply = "{\"getkey_result\":[{\"pubkey\":\"fixture public key\"}]}";
            break;
        case 2:
            assert(connection->phase == 3);
            reply = "{\"command\":\"unit-test-auth\",\"authenticate_result\":\"authentication successful\"}";
            break;
        case 3:
            assert(connection->phase == 4 && audio_page->slots[0].native_ready);
            reply = "{\"audio_event\":[{\"playerid\":3,\"station\":\"Rádió Teszt\","
                "\"coverurl\":\"https://cdn.example/radio.png\"}]}";
            break;
        case 4:
            assert(!strcmp(audio_page->slots[0].track.station, "Rádió Teszt"));
            assert(!strcmp(audio_page->slots[0].track.cover, "https://cdn.example/radio.png"));
            app_loxone_audio_hide();
            break;
        default:
            assert(!"A futtatónak már üresjáratban kell várnia");
    }
    if (reply)
    {
        esp_websocket_event_data_t part = {.op_code = 1, .fin = true, .data_ptr = reply,
            .data_len = (int)strlen(reply), .payload_len = (int)strlen(reply)};
        test_native_event_callback(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    }
    test_now += 50000;
}

static void controller_main_handshake_test(void)
{
    prepare_page();
    unsigned starts = test_native_starts;
    controller_handshake_stage = 0;
    test_native_sleep_hook = controller_handshake_tick;
    run_until_idle();
    test_native_sleep_hook = NULL;
    assert(controller_handshake_stage == 5 && !audio_page);
    assert(test_native_starts == starts + 1 && test_task_creations == 0);
    prepare_page();
}

#include "test_audio_native.inc"
#include "test_audio_receive.inc"

static const char *startup_replies[] = {
    "LWSS V 17.3.11.12 | ~API:1.6~ | Session-Token: test-session",
    "{\"getkey_result\":[{\"pubkey\":\"fixture public key\"}]}",
    "{\"authenticate_result\":\"authentication successful\"}",
};

static void startup_connected(audio_controller_t *controller)
{
    unsigned logs = test_web_events;
    test_native_callback_active = true;
    audio_receive(controller->connection, NULL, WEBSOCKET_EVENT_CONNECTED, NULL);
    test_native_callback_active = false;
    assert(test_web_events == logs); /* A callback csak állapotot ad át. */
}

static void startup_advance(audio_controller_t *controller, unsigned phase)
{
    assert(phase >= 1 && phase <= 4);
    audio_connection_t *connection = controller->connection;
    while (connection->phase < phase)
    {
        assert(connection->phase > 0 && !connection->dead);
        native_receive_text(connection, startup_replies[connection->phase - 1]);
        audio_controller_step(controller, test_now);
    }
    assert(connection->phase == phase && connection->ws);
}

static audio_connection_t *startup_interleave_connection;
static bool startup_interleave_after_pop;

static void startup_enqueue_during_drain(bool received)
{
    assert(received == startup_interleave_after_pop);
    test_queue_receive_hook = NULL;
    audio_connection_t *connection = startup_interleave_connection;
    test_now = connection->deadline;
    native_receive_text(connection, startup_replies[2]);
    /* Az on-time válasz a kiürítettnek látott vagy épp feldolgozott sorba
     * érkezik, de a main végső időellenőrzésére már elmúlt a határidő. */
    test_now += 1000;
}

static void controller_deadline_interleave_test(void)
{
    for (unsigned refill = 0; refill <= 1; ++refill)
    {
        prepare_page();
        audio_controller_t controller = {0};
        audio_controller_step(&controller, test_now);
        startup_connected(&controller);
        audio_controller_step(&controller, test_now);
        startup_advance(&controller, 3);
        audio_connection_t *connection = controller.connection;
        test_now = connection->deadline - 1;
        if (refill)
            for (unsigned i = 0; i < AUDIO_RX_QUEUE_LENGTH; ++i)
                native_receive_text(connection, "{}");
        startup_interleave_connection = connection;
        startup_interleave_after_pop = refill != 0;
        test_queue_receive_hook = startup_enqueue_during_drain;
        unsigned logs = test_web_events;
        audio_controller_step(&controller, test_now);
        assert(!test_queue_receive_hook);
        assert(connection->ws && !connection->dead && connection->phase == 3);
        assert(((test_queue_t *)connection->messages)->count == 1);
        assert(test_web_events == logs && !controller.retry);
        audio_controller_step(&controller, test_now);
        assert(connection->phase == 4 && audio_page->slots[0].native_ready);
        assert(!((test_queue_t *)connection->messages)->count);
        assert(!atomic_load(&connection->received_bytes));
        audio_controller_close(&controller);
    }
    startup_interleave_connection = NULL;
    prepare_page();
    puts("PASS: timely concurrent auth survives empty-queue observation and bounded 32-message drain refill across the deadline");
}

static void controller_queued_deadline_test(void)
{
    prepare_page();
    audio_controller_t controller = {0};
    unsigned starts = test_native_starts, logs = test_web_events;
    audio_controller_step(&controller, test_now);
    startup_connected(&controller);
    audio_controller_step(&controller, test_now);
    audio_connection_t *connection = controller.connection;
    for (unsigned phase = 1; phase <= 3; ++phase)
    {
        int64_t deadline = connection->deadline;
        /* A greeting és auth még előtte, a key pontosan a határon érkezik.
         * A main csak később kap CPU-t: az érkezési időnek kell döntenie. */
        test_now = deadline - (phase == 2 ? 0 : 1);
        native_receive_text(connection, startup_replies[phase - 1]);
        assert(((test_queue_t *)connection->messages)->count == 1);
        test_now = deadline + 50000;
        audio_controller_step(&controller, test_now);
        assert(connection->ws && !connection->dead && connection->phase == phase + 1);
        assert(!((test_queue_t *)connection->messages)->count);
        assert(!atomic_load(&connection->received_bytes));
        if (phase < 3)
            assert(connection->deadline == test_now + AUDIO_TIMEOUT_US);
    }
    assert(audio_page->slots[0].native_ready && audio_page->slots[0].status == LX_AUDIO_READY);
    assert(test_native_starts == starts + 1 && test_web_events == logs);
    audio_controller_close(&controller);

    /* A ténylegesen késő válasz egyik fázisban sem mentheti meg a lejárt
     * próbát; a FIFO előbb feldolgozása nem korlátlan türelmi idő. */
    for (unsigned phase = 1; phase <= 3; ++phase)
    {
        prepare_page();
        audio_controller_step(&controller, test_now);
        startup_connected(&controller);
        audio_controller_step(&controller, test_now);
        startup_advance(&controller, phase);
        connection = controller.connection;
        test_now = connection->deadline + 1;
        native_receive_text(connection, startup_replies[phase - 1]);
        atomic_store(&audio_last_web_error_ms, 0);
        audio_controller_step(&controller, test_now);
        assert(!connection->ws && !connection->phase && !audio_page->slots[0].native_ready);
        assert(strstr(test_web_event, "ok=handshake-timeout"));
        assert(controller.retry == test_now + 1000000LL);
        audio_controller_close(&controller);
    }

    prepare_page();
    audio_controller_step(&controller, test_now);
    startup_connected(&controller);
    audio_controller_step(&controller, test_now);
    connection = controller.connection;
    int64_t deadline = connection->deadline;
    const char *greeting = startup_replies[0];
    esp_websocket_event_data_t part = {.op_code = 1, .fin = true, .data_ptr = greeting,
        .data_len = 7, .payload_len = (int)strlen(greeting)};
    test_now = deadline - 10000;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(!((test_queue_t *)connection->messages)->count);
    test_now = deadline + 1;
    part.payload_offset = part.data_len;
    part.data_ptr += part.data_len;
    part.data_len = part.payload_len - part.payload_offset;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    audio_controller_step(&controller, test_now);
    assert(!connection->ws && !connection->phase && !audio_page->slots[0].native_ready);
    audio_controller_close(&controller);
    prepare_page();
    puts("PASS: on-time queued greeting/key/auth survive delayed main scheduling; late complete and fragmented replies expire");
}

static void controller_stage_deadline_test(void)
{
    prepare_page();
    audio_controller_t controller = {0};
    unsigned starts = test_native_starts;
    int64_t began = test_now;
    audio_controller_step(&controller, test_now);
    audio_connection_t *connection = controller.connection;
    test_now = began + 6000000LL;
    startup_connected(&controller);
    audio_controller_step(&controller, test_now);
    assert(connection->deadline == test_now + AUDIO_TIMEOUT_US);
    test_now = began + 6500000LL;
    startup_advance(&controller, 2);
    assert(connection->deadline == test_now + AUDIO_TIMEOUT_US);
    test_now = began + 7500000LL;
    startup_advance(&controller, 3);
    assert(connection->deadline == test_now + AUDIO_TIMEOUT_US);
    /* Régen 8 s-nál bezárult, hiába haladt a kapcsolat. */
    test_now = began + 8500000LL;
    startup_advance(&controller, 4);
    assert(audio_page->slots[0].native_ready && test_native_starts == starts + 1);
    audio_controller_close(&controller);

    prepare_page();
    audio_controller_step(&controller, test_now);
    connection = controller.connection;
    int64_t connect_deadline = connection->deadline;
    test_now = connect_deadline - 1;
    startup_connected(&controller);
    int64_t connected_at = test_now;
    test_now = connect_deadline + 50000;
    audio_controller_step(&controller, test_now);
    assert(connection->ws && connection->phase == 1 && !connection->dead);
    assert(connection->deadline == connected_at + AUDIO_TIMEOUT_US);
    int64_t greeting_deadline = connection->deadline;
    test_now = greeting_deadline - 1000;
    startup_connected(&controller); /* Ismételt callback nem új haladás. */
    native_receive_text(connection, "{}");
    audio_controller_step(&controller, test_now);
    assert(connection->deadline == greeting_deadline);
    test_now = greeting_deadline + 1;
    audio_controller_step(&controller, test_now);
    assert(!connection->ws && !connection->phase);
    audio_controller_close(&controller);

    prepare_page();
    audio_controller_step(&controller, test_now);
    connection = controller.connection;
    test_now = connection->deadline + 1;
    startup_connected(&controller); /* A már elkésett CONNECTED sem újít. */
    audio_controller_step(&controller, test_now);
    assert(!connection->ws && !connection->phase && !audio_page->slots[0].native_ready);
    audio_controller_close(&controller);
    prepare_page();
    puts("PASS: real progress gets a full next-stage budget; repeated CONNECTED/noise and late transport completion cannot prolong startup");
}

static void startup_fail_and_retry(audio_controller_t *controller, int64_t delay)
{
    audio_connection_t *connection = controller->connection;
    unsigned starts = test_native_starts;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_ERROR, NULL);
    audio_controller_step(controller, test_now);
    assert(!connection->ws && !audio_page->slots[0].native_ready);
    assert(controller->retry == test_now + delay);
    assert(audio_page->slots[0].status == LX_AUDIO_UNAVAILABLE);
    test_now = controller->retry - 1;
    audio_controller_step(controller, test_now);
    assert(!connection->ws && test_native_starts == starts);
    ++test_now;
    audio_controller_step(controller, test_now);
    assert(connection->ws && connection->phase == 1 && test_native_starts == starts + 1);
    assert(!audio_page->slots[0].native_ready && audio_page->slots[0].status == LX_AUDIO_CONNECTING);
}

static void controller_retry_policy_test(void)
{
    prepare_page();
    /* Másik szerver hibajelzése nem válhat kapcsolódássá az első nyitásakor. */
    audio_page->count = 2;
    audio_page->slots[1] = audio_page->slots[0];
    strcpy(audio_page->slots[1].id, "audio-other-server");
    strcpy(audio_page->slots[1].endpoint.host, "192.168.1.41");
    audio_page->slots[1].status = LX_AUDIO_AUTH_FAILED;
    audio_controller_t controller = {0};
    audio_controller_step(&controller, test_now);
    const int64_t delays[] = {1000000LL, 2000000LL, 5000000LL, 5000000LL};
    for (unsigned i = 0; i < sizeof(delays) / sizeof(delays[0]); ++i)
    {
        startup_fail_and_retry(&controller, delays[i]);
        assert(audio_page->slots[1].status == LX_AUDIO_AUTH_FAILED);
    }
    assert(controller.transient_failures <= 3);

    startup_connected(&controller);
    audio_controller_step(&controller, test_now);
    startup_advance(&controller, 4);
    assert(controller.transient_failures == 0);
    startup_fail_and_retry(&controller, 1000000LL);
    startup_fail_and_retry(&controller, 2000000LL);

    /* Az auth-elutasítás nem gyors hálózati újrapróba, és nem siker: az
     * ezt követő hibák is a kímélő 5 s-on maradnak READY/generációváltásig. */
    startup_connected(&controller);
    audio_controller_step(&controller, test_now);
    startup_advance(&controller, 3);
    native_receive_text(controller.connection, "{\"authenticate_result\":\"authentication rejected\"}");
    audio_controller_step(&controller, test_now);
    assert(controller.retry == test_now + AUDIO_RESOURCE_RETRY_US);
    assert(controller.transient_failures == 3);
    assert(audio_page->slots[0].status == LX_AUDIO_AUTH_FAILED);
    test_now = controller.retry;
    audio_controller_step(&controller, test_now);
    assert(audio_page->slots[0].status == LX_AUDIO_CONNECTING);
    startup_fail_and_retry(&controller, 5000000LL);

    /* Másik oldal/session új próbafolyamat. A régi várakozást és
     * hibasorozatot nem örökölheti; a Miniserver-hitelesítést nem bontjuk. */
    audio_receive(controller.connection, NULL, WEBSOCKET_EVENT_ERROR, NULL);
    audio_controller_step(&controller, test_now);
    assert(!controller.connection->ws && controller.retry == test_now + 5000000LL);
    prepare_page();
    audio_controller_step(&controller, test_now);
    assert(controller.transient_failures == 0 && controller.connection->ws && !controller.retry);
    startup_fail_and_retry(&controller, 1000000LL);
    startup_fail_and_retry(&controller, 2000000LL);
    audio_receive(controller.connection, NULL, WEBSOCKET_EVENT_ERROR, NULL);
    audio_controller_step(&controller, test_now);
    assert(!controller.connection->ws && controller.retry == test_now + 5000000LL);
    assert(app_loxone_audio_session("user", "renewed-test.jwt", true));
    audio_controller_step(&controller, test_now);
    assert(controller.transient_failures == 0 && controller.connection->ws && !controller.retry);
    startup_fail_and_retry(&controller, 1000000LL);
    audio_controller_close(&controller);

    prepare_page();
    test_native_init_oom = true;
    audio_controller_step(&controller, test_now);
    audio_controller_step(&controller, test_now);
    assert(controller.connection && !controller.connection->ws);
    assert(controller.retry == test_now + AUDIO_RESOURCE_RETRY_US);
    assert(controller.transient_failures == 3 && !test_task_creations);
    audio_controller_close(&controller);
    prepare_page();
    puts("PASS: transient retries back off 1/2/5 s; success/page/session reset, resource/auth failures remain 5 s, and active retry is shown as connecting");
}

static void controller_startup_diagnostic_test(void)
{
    for (unsigned connected = 0; connected <= 1; ++connected)
    {
        prepare_page();
        audio_controller_t controller = {0};
        audio_controller_step(&controller, test_now);
        if (connected)
        {
            startup_connected(&controller);
            audio_controller_step(&controller, test_now);
        }
        test_now = controller.connection->deadline + 1;
        atomic_store(&audio_last_web_error_ms, 0);
        audio_controller_step(&controller, test_now);
        assert(strstr(test_web_event, "ok=handshake-timeout; phase=1; text=0; binary=0; JSON=0;"));
        assert(strstr(test_web_event, connected ? "ws=1; ms=8000;" : "ws=0; ms=8000;"));
        const char *suffix = "tx=0; auth=none/0.";
        size_t length = strlen(test_web_event), suffix_length = strlen(suffix);
        assert(length < APP_EVENT_TEXT_MAX && length >= suffix_length);
        assert(!strcmp(test_web_event + length - suffix_length, suffix));
        audio_controller_close(&controller);
    }
    prepare_page();
    puts("PASS: phase-1 diagnostics distinguish absent transport from absent greeting; bounded event includes timing and auth suffix without secrets");
}

static void native_auth_transport_boundary_test(void)
{
    const unsigned lengths[] = {2112, LX_AUDIO_AUTH_COMMAND_MAX};
    const char *key = "{\"getkey_result\":[{\"pubkey\":\"fixture public key\"}]}";
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
    {
        prepare_page();
        audio_connection_t *connection = connection_new();
        audio_connection_open(connection, 0);
        assert(test_native_buffer_size > LX_AUDIO_AUTH_COMMAND_MAX);
        assert(!connection->auth_command_bytes && !connection->auth_result_kind);
        audio_page->slots[0].native_ready = false;
        connection->phase = 2;
        test_auth_command_length = lengths[i];
        message_reply(connection, key);
        assert(!connection->dead && connection->phase == 3);
        assert(test_native_command_length == lengths[i]);
        assert(connection->auth_command_bytes == lengths[i]);
        assert(strlen(test_native_command) == sizeof(test_native_command) - 1);
        message_reply(connection, "{\"authenticate_result\":\"authentication successful\"}");
        assert(connection->phase == 4 && audio_page->slots[0].native_ready);
        connection_delete(connection);
    }

    /* A teljes parancs átadását ellenőrizzük: rövid/negatív send_text,
     * auth-foglalási hiba és a kemény hosszkorlát nem válhat sikerré. */
    for (unsigned failure = 0; failure < 4; ++failure)
    {
        prepare_page();
        audio_connection_t *connection = connection_new();
        audio_connection_open(connection, 0);
        audio_page->slots[0].native_ready = false;
        connection->phase = 2;
        unsigned sent = test_native_sends;
        test_auth_command_length = failure == 3 ? LX_AUDIO_AUTH_COMMAND_MAX + 1 : 2112;
        test_native_send_failure = failure == 0 ? 1 : failure == 1 ? -1 : 0;
        test_auth_oom = failure == 2;
        message_reply(connection, key);
        assert(connection->dead && !audio_page->slots[0].native_ready);
        assert(test_native_sends == sent + (failure < 2 ? 1U : 0U));
        const char *reason = atomic_load(&connection->failure_reason);
        assert(reason && !strcmp(reason, failure < 2 ? "send" :
            failure == 2 ? "authentication-envelope" : "send-limit"));
        connection_delete(connection);
    }
    test_auth_command_length = 0;
    prepare_page();
    audio_connection_t *connection = connection_new();
    unsigned sent = test_native_sends;
    test_native_init_oom = true;
    audio_connection_open(connection, 0);
    assert(connection->dead && !connection->ws && test_native_sends == sent);
    assert(!strcmp(atomic_load(&connection->failure_reason), "websocket-start"));
    connection_delete(connection);
    connection = connection_new();
    assert(!audio_send(connection, "") && test_native_sends == sent);
    assert(!strcmp(atomic_load(&connection->failure_reason), "send-limit"));
    connection_delete(connection);
    prepare_page();
    puts("PASS: actual runtime config covers 2112/max auth; short/failed send, OOM and over-limit stay errors");
}

static void native_auth_diagnostic_test(void)
{
    const struct { const char *json, *kind; unsigned bytes; } cases[] = {
        {"\"PRIVATE-RESULT-token\"", "string", 20},
        {"{\"PRIVATE-RESULT\":true}", "object", 0},
        {"[\"PRIVATE-RESULT\"]", "array", 0},
        {"true", "bool", 0},
        {"123", "number", 0},
        {"null", "null", 0},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        prepare_page();
        audio_connection_t *connection = connection_new();
        connection->phase = 3;
        connection->auth_command_bytes = LX_AUDIO_AUTH_COMMAND_MAX;
        audio_page->slots[0].native_ready = false;
        char reply[192];
        snprintf(reply, sizeof(reply), "{\"command\":\"PRIVATE-AUTH\",\"authenticate_result\":%s}", cases[i].json);
        message_reply(connection, reply);
        assert(connection->dead && connection->phase == 3 && !audio_page->slots[0].native_ready);
        assert(!strcmp(connection->auth_result_kind, cases[i].kind));
        assert(connection->auth_result_bytes == cases[i].bytes);
        atomic_store(&audio_last_web_error_ms, 0);
        audio_web_error(connection, NULL);
        char suffix[64];
        snprintf(suffix, sizeof(suffix), "; tx=%u; auth=%s/%u.",
            LX_AUDIO_AUTH_COMMAND_MAX, cases[i].kind, cases[i].bytes);
        size_t length = strlen(test_web_event), suffix_length = strlen(suffix);
        assert(length < 128 && length >= suffix_length);
        assert(!strcmp(test_web_event + length - suffix_length, suffix));
        connection_delete(connection);
    }

    /* A panel JSON=1 számlálója nem az auth-válasz típusa. Egy korábbi
     * szintetikus, nem JSON üzenet után is a pontos sikeres string dönt. */
    prepare_page();
    audio_connection_t *connection = connection_new();
    connection->phase = 1;
    audio_page->slots[0].native_ready = false;
    const char *greeting = "LWSS V 17.3.11.12 | ~API:1.6~ | Session-Token: test-session";
    const char *messages[] = {greeting, greeting,
        "{\"command\":\"audio/cfg/getkey/full\",\"getkey_result\":[{\"pubkey\":\"fixture public key\"}]}",
        "{\"command\":\"PRIVATE-AUTH\",\"authenticate_result\":\"authentication successful\"}"};
    for (unsigned i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i)
    {
        native_receive_text(connection, messages[i]);
        audio_message_t message;
        assert(xQueueReceive(connection->messages, &message, 0) == pdTRUE);
        audio_message(connection, &message);
        audio_message_release(connection, &message);
    }
    assert(!connection->dead && connection->phase == 4 && audio_page->slots[0].native_ready);
    assert(atomic_load(&connection->text_frames) == 4 && connection->invalid_json == 1);
    assert(!strcmp(connection->auth_result_kind, "string"));
    assert(connection->auth_result_bytes == strlen("authentication successful"));
    connection_delete(connection);
    prepare_page();
    puts("PASS: strict auth success and type/length-only diagnostics; prior invalid JSON is harmless; no secret echo");
}

int main(void)
{
    init_failure_test();
    prepare_page();
    volume_test();
    volume_cancellation_test();
    native_waiting_volume_test();
    volume_feedback_and_backpressure_test();
    observed_radio_event_test();
    native_startup_metadata_test();
    native_fragment_test();
    native_status_backpressure_test();
    native_receive_burst_test();
    native_receive_limits_test();
    native_web_diagnostic_test();
    native_worker_retry_test();
    native_favorite_close_race_test();
    native_favorites_response_routing_test();
    favorites_test();
    favorites_lifecycle_test();
    controller_allocation_failure_test();
    controller_lifecycle_test();
    controller_main_handshake_test();
    controller_deadline_interleave_test();
    controller_queued_deadline_test();
    controller_stage_deadline_test();
    controller_retry_policy_test();
    controller_startup_diagnostic_test();
    native_auth_transport_boundary_test();
    native_auth_diagnostic_test();
    app_loxone_audio_session(NULL, NULL, false);
    assert(!audio_credentials->jwt[0] && !audio_credentials->live);
    assert(!audio_page); /* Task nélküli hibás natív ág sem tarthat rejtett modellt. */
    free(audio_page); audio_page = NULL;
    free(audio_credentials); audio_credentials = NULL;
    vSemaphoreDelete(audio_lock); vSemaphoreDelete(audio_wake);
    assert(test_semaphores_created == test_semaphores_deleted && !test_task_creations);
    assert(test_caps_queues_created == test_caps_queues_deleted);
    puts("PASS: ESP Audio main-stack controller without task allocations, init/OOM/session/OTA recovery, native-wait independent volume and bounded protocol lifecycle");
    return 0;
}
