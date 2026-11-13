/* A tényleges ESP-oldali Audio futtató: kézi idő, determinisztikus WS/RTOS. */
#include <assert.h>
#include "../main/app_loxone_audio_runtime.c"

struct test_audio_semaphore { bool binary, held; };
typedef struct { unsigned capacity, count, head, tail; size_t size; unsigned char *data; } test_queue_t;
static int64_t test_now = 1000000;
static unsigned test_transports, test_native_sends, test_task_creations;
static unsigned test_volume, test_media_generation;
static bool test_fail_allocation, test_fail_queue;
static char test_native_command[128];

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
    (void)timeout;
    if (!lock->binary)
    {
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
    assert(stack >= 8192 && caps == MALLOC_CAP_RTCRAM);
    ++test_task_creations;
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
{ assert(!strcmp(options->subprotocol, "remotecontrol")); return (void *)1; }
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t handle, int event,
    void (*callback)(void *, esp_event_base_t, int32_t, void *), void *argument)
{ (void)handle; (void)event; (void)callback; (void)argument; return ESP_OK; }
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t handle) { (void)handle; return ESP_OK; }
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t handle) { (void)handle; return ESP_OK; }
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t handle) { (void)handle; return ESP_OK; }
int esp_websocket_client_send_text(esp_websocket_client_handle_t handle, const char *command, int length, TickType_t timeout)
{
    (void)handle; (void)timeout;
    assert(!audio_lock->held);
    snprintf(test_native_command, sizeof(test_native_command), "%s", command);
    ++test_native_sends;
    return length;
}
void mbedtls_platform_zeroize(void *data, size_t length) { memset(data, 0, length); }
char *lx_audio_auth_create(const char *key, const char *user, const char *jwt, const char *session,
    int (*random_bytes)(void *, unsigned char *, size_t), void *context)
{
    (void)key; (void)user; (void)jwt; (void)session; (void)random_bytes; (void)context;
    return strdup("unit-test-auth");
}
void lx_audio_auth_free(char *command) { free(command); }
void app_loxone_audio_media_clear(unsigned generation) { test_media_generation = generation; }
void app_loxone_audio_media_close_detail(void) {}
esp_err_t app_loxone_audio_transport(const char *id, unsigned revision, lx_audio_action_t action, unsigned value)
{
    assert(!audio_lock->held);
    assert(!strcmp(id, "audio-one") && revision == 7 && action == LX_AUDIO_VOLUME);
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
    assert(audio_page && audio_task && test_media_generation == audio_page->generation);
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
    audio_volume_poll(test_now);
    assert(test_transports == 1 && test_volume == 20);
    test_now += 50000;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 30, false) == ESP_OK);
    audio_volume_poll(test_now);
    assert(test_transports == 1);
    test_now += 50000;
    audio_volume_poll(test_now);
    assert(test_transports == 2 && test_volume == 30);
    test_now += 10000;
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 40, true) == ESP_OK);
    audio_volume_poll(test_now);
    assert(test_transports == 3 && test_volume == 40);
    assert(audio_page->slots[0].track.volume == 25); /* Kérés != visszajelzés. */
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 61, true) != ESP_OK);
    assert(app_loxone_audio_command("audio-one", 7, LX_AUDIO_VOLUME, 60, true) == ESP_OK);
    assert(app_loxone_audio_value_allowed("audio-one", 7, LX_AUDIO_VOLUME, 60));
    state("14", 50);
    assert(!app_loxone_audio_value_allowed("audio-one", 7, LX_AUDIO_VOLUME, 60));
    assert(app_loxone_audio_value_allowed("audio-one", 7, LX_AUDIO_VOLUME, 50));
    audio_volume_poll(test_now);
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
    assert(!strcmp(test_native_command, "audio/3/roomfav/play/1357"));
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
    /* A régi kód új taskot indított minden sikertelen foglalás után. */
    audio_page->live = true;
    test_fail_allocation = true;
    unsigned created = test_task_creations;
    audio_worker(NULL);
    assert(!audio_task && !audio_page && test_task_creations == created);

    prepare_page();
    test_fail_queue = true;
    created = test_task_creations;
    audio_worker(NULL);
    assert(!audio_task && !audio_page && test_task_creations == created);
}

int main(void)
{
    prepare_page();
    volume_test();
    favorites_test();
    favorites_lifecycle_test();
    worker_allocation_failure_test();
    app_loxone_audio_session(NULL, NULL, false);
    assert(!audio_credentials->jwt[0] && !audio_credentials->live);
    free(audio_page); audio_page = NULL;
    free(audio_credentials); audio_credentials = NULL;
    free(audio_lock); free(audio_wake);
    puts("PASS: ESP Audio runtime guards, volume limits, favorites IDs/correlation/reconnect and OOM lifecycle");
    return 0;
}
