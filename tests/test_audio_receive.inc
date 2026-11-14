/* A WS-task rövid sorozata a worker futása előtt is teljes egészében
 * megérkezhet. A sikeres auth nem veszhet el a mögötte érkező zónáktól. */
static void native_receive_text(audio_connection_t *connection, const char *text)
{
    esp_websocket_event_data_t part = {.op_code = 1, .fin = true, .data_ptr = text,
        .data_len = (int)strlen(text), .payload_len = (int)strlen(text)};
    test_native_callback_active = true;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    test_native_callback_active = false;
}

static void native_receive_burst_test(void)
{
    const unsigned bursts[] = {4, 12};
    unsigned failures = 0;
    for (unsigned b = 0; b < sizeof(bursts) / sizeof(bursts[0]); ++b)
    {
        prepare_page();
        if (bursts[b] == 12)
        {
            audio_page->count = 12;
            for (unsigned i = 0; i < audio_page->count; ++i)
            {
                if (i)
                    audio_page->slots[i] = audio_page->slots[0];
                audio_page->slots[i].endpoint.player = i + 1;
            }
        }
        audio_connection_t *connection = connection_new();
        connection->phase = 3;
        for (unsigned i = 0; i < audio_page->count; ++i)
            audio_page->slots[i].native_ready = false;
        native_receive_text(connection, "{\"authenticate_result\":\"authentication successful\"}");
        for (unsigned player = 1; player <= bursts[b]; ++player)
        {
            char message[128];
            snprintf(message, sizeof(message), "{\"audio_event\":[{\"playerid\":%u,\"title\":\"Zóna %u\","
                "\"volume\":6,\"mode\":\"play\"}]}", player, player);
            native_receive_text(connection, message);
        }
        audio_message_t message;
        while (xQueueReceive(connection->messages, &message, 0) == pdTRUE)
        {
            audio_message(connection, &message);
            audio_message_release(connection, &message);
        }
        bool valid = !connection->dead && connection->phase == 4;
        for (unsigned i = 0; i < audio_page->count; ++i)
        {
            char title[32];
            snprintf(title, sizeof(title), "Zóna %u", audio_page->slots[i].endpoint.player);
            valid &= audio_page->slots[i].native_ready && !strcmp(audio_page->slots[i].track.title, title);
        }
        if (!valid)
        {
            fprintf(stderr, "FAIL: auth + %u events: phase=%u dead=%u\n", bursts[b],
                connection->phase, (unsigned)connection->dead);
            ++failures;
        }
        connection_delete(connection);
    }
    assert(!failures);
    prepare_page();
}

static void native_receive_limits_test(void)
{
    audio_connection_t *connection = connection_new();
    assert(((test_queue_t *)connection->messages)->with_caps);
    assert(((test_queue_t *)connection->messages)->capacity == 32);
    for (unsigned i = 0; i < AUDIO_RX_QUEUE_LENGTH; ++i)
        native_receive_text(connection, "{}");
    assert(!connection->dead && atomic_load(&connection->received_bytes) == 32U * 2048U);
    native_receive_text(connection, "{}");
    assert(connection->dead && !strcmp(atomic_load(&connection->failure_reason), "receive-queue-full"));
    assert(atomic_load(&connection->received_bytes) == 32U * 2048U);
    connection_delete(connection); /* A sikertelen 33. elem és a 32 várakozó is elengedve. */

    connection = connection_new();
    char *large = malloc(LX_AUDIO_JSON_MAX + 1U);
    assert(large);
    memset(large, ' ', LX_AUDIO_JSON_MAX);
    large[LX_AUDIO_JSON_MAX] = 0;
    assert(AUDIO_RX_BYTES_MAX == 204800U);
    for (unsigned i = 0; i < 4; ++i)
        native_receive_text(connection, large);
    assert(!connection->dead && atomic_load(&connection->received_bytes) == AUDIO_RX_BYTES_MAX);
    audio_message_t held;
    assert(xQueueReceive(connection->messages, &held, 0) == pdTRUE);
    assert(held.capacity == 51200U && held.length == LX_AUDIO_JSON_MAX);
    /* A worker által kivett, de még fel nem szabadított elem továbbra is tulajdon. */
    assert(atomic_load(&connection->received_bytes) == AUDIO_RX_BYTES_MAX);
    native_receive_text(connection, "{}");
    assert(connection->dead && !strcmp(atomic_load(&connection->failure_reason), "receive-byte-limit"));
    assert(((test_queue_t *)connection->messages)->count == 3);
    assert(atomic_load(&connection->received_bytes) == AUDIO_RX_BYTES_MAX);
    audio_message_release(connection, &held);
    assert(!held.data && !held.capacity && atomic_load(&connection->received_bytes) == 153600U);
    connection_delete(connection);
    free(large);

    connection = connection_new();
    char *fragmented = malloc(5001);
    assert(fragmented);
    memset(fragmented, 'x', 5000);
    fragmented[5000] = 0;
    esp_websocket_event_data_t part = {.op_code = 1, .fin = true, .data_ptr = fragmented,
        .data_len = 1000, .payload_len = 5000};
    test_receive_allocation = connection;
    test_receive_peak = 0;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(connection->capacity == 2048U && atomic_load(&connection->received_bytes) == 2048U);
    part.payload_offset = 1000;
    part.data_ptr = fragmented + 1000;
    part.data_len = 2000;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(connection->capacity == 4096U && atomic_load(&connection->received_bytes) == 4096U);
    assert(test_receive_peak == 2048U + 4096U);
    part.payload_offset = 3000;
    part.data_ptr = fragmented + 3000;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(!connection->dead && test_receive_peak == 4096U + 6144U);
    test_receive_allocation = NULL;
    assert(!connection->rx && atomic_load(&connection->received_bytes) == 6144U);
    assert(xQueueReceive(connection->messages, &held, 0) == pdTRUE);
    assert(held.capacity == 6144U && held.length == 5000 && !strcmp(held.data, fragmented));
    audio_message_release(connection, &held);
    assert(atomic_load(&connection->received_bytes) == 0);
    connection_delete(connection);

    connection = connection_new();
    part.payload_offset = 0;
    part.data_ptr = fragmented;
    part.data_len = 1000;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    test_fail_allocation = true;
    part.payload_offset = 1000;
    part.data_ptr = fragmented + 1000;
    part.data_len = 2000;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(connection->dead && !test_fail_allocation);
    assert(!strcmp(atomic_load(&connection->failure_reason), "receive-memory"));
    assert(connection->capacity == 2048U && atomic_load(&connection->received_bytes) == 2048U);
    connection_delete(connection); /* Részleges frame és sikertelen bővítés. */
    free(fragmented);

    connection = connection_new();
    test_fail_allocation = true;
    native_receive_text(connection, "{}");
    assert(connection->dead && !test_fail_allocation && !connection->rx);
    assert(atomic_load(&connection->received_bytes) == 0);
    connection_delete(connection);
    prepare_page();
}

static void native_intentional_stop_tick(void)
{
    test_now += 50000;
    audio_page->live = false;
}

static void native_web_diagnostic_test(void)
{
    int64_t saved_now = test_now;
    atomic_store(&audio_last_web_error_ms, 0);
    audio_connection_t *connection = connection_new();
    connection->phase = 2;
    strcpy(connection->session, "test-session");
    strcpy(audio_page->slots[0].track.title, "Tesztzene");
    atomic_store(&connection->text_frames, 7);
    atomic_store(&connection->binary_frames, 1);
    connection->invalid_json = 3;
    unsigned before = test_web_events;
    test_native_callback_active = true;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_ERROR, NULL);
    test_native_callback_active = false;
    assert(test_web_events == before); /* Callback csak jelezhet. */
    audio_web_error(connection, NULL);
    assert(test_web_events == before + 1);
    assert(strstr(test_web_event, "ok=transport; phase=2; text=7; binary=1; JSON=3"));
    audio_connection_t *other = connection_new();
    audio_web_error(other, NULL);
    assert(test_web_events == before + 1); /* Másik kapcsolat sem kerüli meg a korlátot. */
    test_now += 29999000;
    audio_web_error(other, NULL);
    assert(test_web_events == before + 1);
    test_now += 1000;
    audio_web_error(other, NULL);
    assert(test_web_events == before + 2);
    /* A hosszú csend nem teheti újra jövőbelivé a régi határidőt. */
    test_now += 25LL * 24 * 60 * 60 * 1000000;
    audio_web_error(other, NULL);
    assert(test_web_events == before + 3);

    /* Az eltelt idő a 32 bites milliszekundum-számláló átfordulásán is jó. */
    atomic_store(&audio_last_web_error_ms, 0);
    test_now = ((int64_t)UINT32_MAX - 9999) * 1000;
    audio_web_error(other, NULL);
    assert(test_web_events == before + 4);
    test_now += 29999000;
    audio_web_error(other, NULL);
    assert(test_web_events == before + 4);
    test_now += 1000;
    audio_web_error(other, NULL);
    assert(test_web_events == before + 5);

    /* Az induláskori nulla időpont sem enged át ismételt hibasorokat. */
    atomic_store(&audio_last_web_error_ms, 0);
    test_now = 0;
    audio_web_error(other, NULL);
    audio_web_error(other, NULL);
    assert(test_web_events == before + 6);
    connection_delete(connection);
    connection_delete(other);
    assert(test_web_events == before + 6);

    /* A szándékos oldalezárást a valódi worker sem naplózza hálózati hibának. */
    prepare_page();
    test_now = saved_now;
    atomic_store(&audio_last_web_error_ms, 0);
    test_native_sleep_hook = native_intentional_stop_tick;
    test_native_stop_disconnect = true;
    audio_worker(NULL);
    test_native_sleep_hook = NULL;
    test_native_stop_disconnect = false;
    assert(test_web_events == before + 6 && !audio_page && !audio_task);
    prepare_page();
}
