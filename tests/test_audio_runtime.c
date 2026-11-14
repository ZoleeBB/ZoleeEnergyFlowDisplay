/* A tényleges ESP-oldali Audio futtató: kézi idő, determinisztikus WS/RTOS. */
#include <assert.h>
#include "../main/app_loxone_audio_runtime.c"

struct test_audio_semaphore { bool binary, held; };
typedef struct { unsigned capacity, count, head, tail; size_t size; unsigned char *data; } test_queue_t;
static int64_t test_now = 1000000;
static unsigned test_transports, test_transport_attempts, test_native_sends, test_task_creations;
static unsigned test_volume, test_media_generation;
static bool test_fail_allocation, test_fail_queue, test_fail_task, test_cancel_before_enqueue;
static bool test_transport_busy;
enum { TEST_NATIVE_NONE, TEST_NATIVE_INIT, TEST_NATIVE_AUTH, TEST_NATIVE_SEND, TEST_NATIVE_STOP };
static unsigned test_native_wait_point, test_native_wait_checks;
static void test_miniserver_during_native_wait(unsigned point);
static char test_native_command[128];
static void (*test_native_sleep_hook)(void);
static void (*test_native_send_hook)(const char *command);
static void (*test_audio_lock_hook)(void);
static void (*test_native_event_callback)(void *, esp_event_base_t, int32_t, void *);
static void *test_native_event_argument;
static bool test_native_start_error, test_native_stop_disconnect;
static unsigned test_native_starts;

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (test_fail_allocation)
    {
        test_fail_allocation = false;
        return NULL;
    }
    return calloc(count, size);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1, sizeof(struct test_audio_semaphore)); }
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    SemaphoreHandle_t result = xSemaphoreCreateMutex();
    result->binary = true;
    return result;
}
int xSemaphoreTake(SemaphoreHandle_t lock, TickType_t timeout)
{
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
    assert(stack >= 8192 && (caps == MALLOC_CAP_RTCRAM || caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    ++test_task_creations;
    if (test_fail_task)
    {
        *handle = NULL;
        return 0;
    }
    *handle = (void *)1;
    return pdPASS;
}
void vTaskDeleteWithCaps(TaskHandle_t handle) { (void)handle; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle) { (void)handle; return 4096; }
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
    (void)timeout;
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
    if (!q->count) return 0;
    memcpy(data, q->data + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->capacity; --q->count;
    return 1;
}
void vQueueDelete(QueueHandle_t queue)
{
    test_queue_t *q = queue;
    free(q->data); free(q);
}

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *options)
{
    assert(!strcmp(options->subprotocol, "remotecontrol"));
    test_miniserver_during_native_wait(TEST_NATIVE_INIT);
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
    snprintf(test_native_command, sizeof(test_native_command), "%s", command);
    ++test_native_sends;
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

static void prepare_page(void)
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
    app_loxone_audio_session("user", "test.jwt", true);
    app_loxone_audio_page(config, catalog, 0, 7);
    free(config); cJSON_Delete(catalog);
    assert(audio_page && test_media_generation == audio_page->generation);
    assert(test_fail_task || audio_task);
    assert(!app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY));
    state("12", 0); state("14", 60); state("11", 25);
    assert(!app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY));
    app_loxone_audio_state("40000000-0000-0000-0000000000000013", false, 0, "", 0, test_now);
    assert(app_loxone_audio_allowed("audio-one", 7, LX_AUDIO_PLAY));
    assert(!app_loxone_audio_allowed("audio-one", 6, LX_AUDIO_PLAY));
    assert(!app_loxone_audio_allowed("other", 7, LX_AUDIO_PLAY));
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
    connection->messages = xQueueCreate(4, sizeof(audio_message_t));
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
    vQueueDelete(connection->messages);
    free(connection);
}

static void message_reply(audio_connection_t *connection, const char *reply)
{
    audio_message_t message = {.data = (char *)reply, .length = strlen(reply)};
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

static void worker_allocation_failure_test(void)
{
    /* Natív memóriahiány nem indíthat task-hurkot és nem törölheti a
     * továbbra is élő Miniserver-alapvezérlés oldalmodelljét. */
    audio_page->live = true;
    test_fail_allocation = true;
    unsigned created = test_task_creations;
    audio_worker(NULL);
    assert(!audio_task && audio_page && test_task_creations == created);
    assert(audio_page->slots[0].status == LX_AUDIO_UNAVAILABLE);
    unsigned sent = test_transports;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 12, true) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent + 1 && test_volume == 12);

    prepare_page();
    test_fail_queue = true;
    created = test_task_creations;
    audio_worker(NULL);
    assert(!audio_task && audio_page && test_task_creations == created);
    assert(audio_page->slots[0].status == LX_AUDIO_UNAVAILABLE);
    sent = test_transports;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 18, true) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent + 1 && test_volume == 18);

    test_fail_task = true;
    prepare_page();
    test_fail_task = false;
    assert(!audio_task && audio_page && test_task_creations == created + 2);
    assert(audio_page->slots[0].status == LX_AUDIO_UNAVAILABLE);
    sent = test_transports;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 22, true) == ESP_OK);
    app_loxone_audio_poll(test_now);
    assert(test_transports == sent + 1 && test_volume == 22);
}

#include "test_audio_native.inc"

int main(void)
{
    prepare_page();
    volume_test();
    volume_cancellation_test();
    native_waiting_volume_test();
    volume_feedback_and_backpressure_test();
    observed_radio_event_test();
    native_startup_metadata_test();
    native_fragment_test();
    native_status_backpressure_test();
    native_worker_retry_test();
    native_favorite_close_race_test();
    native_favorites_response_routing_test();
    favorites_test();
    favorites_lifecycle_test();
    worker_allocation_failure_test();
    app_loxone_audio_session(NULL, NULL, false);
    assert(!audio_credentials->jwt[0] && !audio_credentials->live);
    assert(!audio_page); /* Task nélküli hibás natív ág sem tarthat rejtett modellt. */
    free(audio_page); audio_page = NULL;
    free(audio_credentials); audio_credentials = NULL;
    free(audio_lock); free(audio_wake);
    puts("PASS: ESP Audio native-wait independent volume, bounded feedback/coalescing, radio event and lifecycle guards");
    return 0;
}
