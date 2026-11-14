/* A nyitáskor, akár a greeting előtt érkező natív snapshot nem veszhet el.
 * A meta csak sikeres auth után látszhat; a korábbi volume/mode nem írhatja
 * vissza a Miniserver közben érkezett frissebb visszajelzését. */
static void native_startup_metadata_test(void)
{
    prepare_page();
    audio_connection_t *connection = connection_new();
    connection->phase = 1;
    audio_page->slots[0].native_ready = false;
    strcpy(audio_page->slots[0].track.title, "Korábbi meta");
    audio_page->slots[0].track.mode = LX_AUDIO_MODE_PLAY;
    message_reply(connection, "{\"audio_event\":[{\"playerid\":3,\"title\":\"Első cím\","
        "\"artist\":\"Előadó\",\"album\":\"Album\",\"station\":\"\",\"volume\":2,\"mode\":\"stop\","
        "\"coverurl\":\"https://cdn.example/initial.jpg\"}]}");
    assert(!strcmp(audio_page->slots[0].track.title, "Korábbi meta"));
    assert(!audio_page->slots[0].native_ready && audio_page->slots[0].track.volume == 25);
    message_reply(connection, "LWSS V 17.3.11.12 | ~API:1.6~ | Session-Token: test-session");
    assert(connection->phase == 2 && !strcmp(test_native_command, "audio/cfg/getkey/full"));
    message_reply(connection, "{\"audio_event\":[{\"playerid\":3,\"title\":\"Következő cím\"}]}");
    message_reply(connection, "{\"getkey_result\":[{\"pubkey\":\"fixture public key\"}]}");
    assert(connection->phase == 3);
    message_reply(connection, "{\"audio_event\":[{\"playerid\":3,\"station\":\"Rádió Teszt\"}]}");
    state("11", 41);
    assert(!strcmp(audio_page->slots[0].track.title, "Korábbi meta"));
    message_reply(connection, "{\"authenticate_result\":\"authentication successful\"}");
    assert(connection->phase == 4 && audio_page->slots[0].native_ready);
    assert(!connection->staged_tracks && !connection->staged_mask);
    assert(!strcmp(audio_page->slots[0].track.title, "Következő cím"));
    assert(!strcmp(audio_page->slots[0].track.artist, "Előadó"));
    assert(!strcmp(audio_page->slots[0].track.album, "Album"));
    assert(!strcmp(audio_page->slots[0].track.station, "Rádió Teszt"));
    assert(!strcmp(audio_page->slots[0].track.cover, "https://cdn.example/initial.jpg"));
    assert(audio_page->slots[0].track.version != 0);
    assert(audio_page->slots[0].track.volume == 41 && audio_page->slots[0].track.mode == LX_AUDIO_MODE_PLAY);
    connection_delete(connection);

    connection = connection_new();
    connection->phase = 1;
    audio_page->slots[0].native_ready = false;
    message_reply(connection, "{\"audio_event\":[{\"playerid\":3,\"title\":\"Elutasított meta\"}]}");
    connection->phase = 3;
    message_reply(connection, "{\"authenticate_result\":\"authentication rejected\"}");
    assert(connection->dead && !audio_page->slots[0].native_ready);
    assert(!connection->staged_tracks && !connection->staged_mask);
    assert(!strcmp(audio_page->slots[0].track.title, "Következő cím"));
    connection_delete(connection);

    connection = connection_new();
    connection->phase = 1;
    audio_page->slots[0].native_ready = false;
    message_reply(connection, "{\"audio_event\":[{\"playerid\":3,\"title\":\"Régi generáció\"}]}");
    ++audio_page->generation;
    connection->phase = 3;
    message_reply(connection, "{\"authenticate_result\":\"authentication successful\"}");
    assert(!audio_page->slots[0].native_ready);
    assert(!connection->staged_tracks && !connection->staged_mask);
    assert(!strcmp(audio_page->slots[0].track.title, "Következő cím"));
    connection_delete(connection);

    connection = connection_new();
    connection->phase = 1;
    audio_page->slots[0].native_ready = false;
    test_fail_allocation = true;
    message_reply(connection, "{\"audio_event\":[{\"playerid\":3,\"title\":\"Nincs puffer\"}]}");
    assert(connection->dead && !audio_page->slots[0].native_ready && !test_fail_allocation);
    assert(!connection->staged_tracks && !connection->staged_mask);
    assert(!strcmp(audio_page->slots[0].track.title, "Következő cím"));
    connection_delete(connection);
    prepare_page();
}

static void native_fragment_test(void)
{
    audio_connection_t *connection = connection_new();
    connection->phase = 1;
    audio_page->slots[0].native_ready = false;
    const char *greeting = "LWSS V 17.3.11.12 | ~API:1.6~ | Session-Token: test-session";
    /* Az első WS frame két driverdarab, majd valódi continuation frame.
     * A közbeékelt Ping nem törheti meg a szöveg összerakását. */
    esp_websocket_event_data_t part = {.op_code = 1, .fin = false, .data_ptr = greeting,
        .data_len = 7, .payload_len = 18, .payload_offset = 0};
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    part.data_ptr = greeting + 7;
    part.data_len = 11;
    part.payload_offset = 7;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(!connection->dead && ((test_queue_t *)connection->messages)->count == 0);
    esp_websocket_event_data_t ping = {.op_code = 9, .fin = true};
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &ping);
    part.op_code = 0;
    part.fin = true;
    part.payload_offset = 0;
    part.data_ptr = greeting + 18;
    part.data_len = part.payload_len = (int)strlen(greeting) - 18;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    audio_message_t message;
    assert(xQueueReceive(connection->messages, &message, 0) == pdTRUE);
    assert(!strcmp(message.data, greeting));
    audio_message(connection, &message);
    audio_message_release(connection, &message);
    assert(connection->phase == 2 && !connection->rx && !connection->rx_opcode);
    assert(atomic_load(&connection->text_frames) == 1);

    const char *key = "{\"getkey_result\":[{\"pubkey\":\"fixture public key\"}]}";
    part = (esp_websocket_event_data_t){.op_code = 1, .fin = true, .data_ptr = key,
        .data_len = 19, .payload_len = (int)strlen(key)};
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(((test_queue_t *)connection->messages)->count == 0);
    part.payload_offset = part.data_len;
    part.data_ptr = key + part.payload_offset;
    part.data_len = part.payload_len - part.payload_offset;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(xQueueReceive(connection->messages, &message, 0) == pdTRUE);
    audio_message(connection, &message);
    audio_message_release(connection, &message);
    assert(connection->phase == 3 && !connection->dead);

    /* Bináris üzenet continuation vége sem válhat szöveges auth-válasszá. */
    const char *auth = "{\"authenticate_result\":\"authentication successful\"}";
    part = (esp_websocket_event_data_t){.op_code = 2, .fin = false, .data_ptr = auth,
        .data_len = 10, .payload_len = 10};
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    part.op_code = 0;
    part.fin = true;
    part.data_ptr = auth + 10;
    part.data_len = part.payload_len = (int)strlen(auth) - 10;
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(((test_queue_t *)connection->messages)->count == 0);
    assert(!connection->dead && !connection->rx_opcode && atomic_load(&connection->binary_frames) == 1);
    part = (esp_websocket_event_data_t){.op_code = 1, .fin = true, .data_ptr = auth,
        .data_len = (int)strlen(auth), .payload_len = (int)strlen(auth)};
    audio_receive(connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    assert(xQueueReceive(connection->messages, &message, 0) == pdTRUE);
    audio_message(connection, &message);
    audio_message_release(connection, &message);
    assert(connection->phase == 4 && audio_page->slots[0].native_ready);
    connection_delete(connection);
}

static audio_connection_t *native_fast_connection;
static unsigned native_fast_replies, native_fast_peak;

static void native_fast_status_reply(const char *command)
{
    unsigned player = 0;
    int end = 0;
    if (sscanf(command, "audio/%u/status%n", &player, &end) != 1 || command[end])
        return;
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"status_result\":[{\"playerid\":%u,\"volume\":25}]}", player);
    esp_websocket_event_data_t part = {.op_code = 1, .fin = true, .data_ptr = reply,
        .data_len = (int)strlen(reply), .payload_len = (int)strlen(reply)};
    audio_receive(native_fast_connection, NULL, WEBSOCKET_EVENT_DATA, &part);
    unsigned count = ((test_queue_t *)native_fast_connection->messages)->count;
    if (count > native_fast_peak)
        native_fast_peak = count;
    ++native_fast_replies;
}

static void native_status_backpressure_test(void)
{
    prepare_page();
    audio_page->count = LX_HOME_MAX_CARDS;
    for (unsigned i = 0; i < audio_page->count; ++i)
    {
        if (i)
            audio_page->slots[i] = audio_page->slots[0];
        snprintf(audio_page->slots[i].id, sizeof(audio_page->slots[i].id), "audio-%u", i);
        audio_page->slots[i].endpoint.player = i + 1;
    }
    audio_connection_t *connection = connection_new();
    connection->phase = 3;
    native_fast_connection = connection;
    native_fast_replies = native_fast_peak = 0;
    test_native_send_hook = native_fast_status_reply;
    /* A magasabb prioritású WS-task válasza a send visszatérése előtt jön.
     * Tizenkét kártya sem áraszthatja el a négy elemű fogadási sort. */
    message_reply(connection, "{\"authenticate_result\":\"authentication successful\"}");
    assert(!connection->dead);
    for (unsigned i = 0; i < LX_HOME_MAX_CARDS + 1; ++i)
    {
        audio_message_t message;
        while (xQueueReceive(connection->messages, &message, 0) == pdTRUE)
        {
            audio_message(connection, &message);
            audio_message_release(connection, &message);
        }
        audio_native_poll(connection);
        assert(!connection->dead);
    }
    assert(native_fast_replies == LX_HOME_MAX_CARDS && native_fast_peak <= 1);
    test_native_send_hook = NULL;
    native_fast_connection = NULL;
    connection_delete(connection);
    prepare_page();
}

static unsigned native_retry_ticks, native_retry_start_count;
static int64_t native_retry_began;

static void native_retry_tick(void)
{
    test_now += 50000;
    ++native_retry_ticks;
    unsigned attempts = test_native_starts - native_retry_start_count;
    if (attempts > 1)
        assert(test_now - native_retry_began >= 5000000LL);
    if (attempts > 1 || native_retry_ticks >= 140)
        audio_page->live = false;
}

static void native_worker_retry_test(void)
{
    prepare_page();
    native_retry_ticks = 0;
    native_retry_began = test_now;
    native_retry_start_count = test_native_starts;
    test_native_sleep_hook = native_retry_tick;
    test_native_start_error = true;
    test_native_stop_disconnect = true;
    audio_worker(NULL);
    test_native_sleep_hook = NULL;
    test_native_stop_disconnect = false;
    /* Az első hiba lezárása után egyszeri 5 s szünet, majd valódi új open.
     * A dead jelző nem tolhatja minden worker-körben tovább a határidőt. */
    assert(test_native_starts == native_retry_start_count + 2);
    assert(native_retry_ticks < 140 && !audio_page && !audio_task);
    prepare_page();
}

static unsigned native_guard_locks;

static void native_close_before_send_guard(void)
{
    if (++native_guard_locks == 2)
    {
        test_audio_lock_hook = NULL;
        app_loxone_audio_detail_close();
    }
}

static void native_favorite_close_race_test(void)
{
    audio_connection_t *connection = connection_new();
    assert(app_loxone_audio_favorites_request("audio-one", 7, 0) == ESP_OK);
    audio_native_poll(connection);
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":1,"
        "\"items\":[{\"id\":901,\"name\":\"Jazz\"}]}]}");
    assert(app_loxone_audio_favorite("audio-one", 7, 901, audio_page->favorites.generation) == ESP_OK);
    unsigned sent = test_native_sends;
    native_guard_locks = 0;
    test_audio_lock_hook = native_close_before_send_guard;
    audio_native_poll(connection);
    test_audio_lock_hook = NULL;
    assert(native_guard_locks == 2 && audio_page->detail < 0);
    assert(test_native_sends == sent && !connection->dead && !audio_page->favorite_queued);
    connection_delete(connection);
    prepare_page();
}

static void native_favorites_response_routing_test(void)
{
    prepare_page();
    audio_connection_t *connection = connection_new();
    assert(app_loxone_audio_favorites_request("audio-one", 7, 0) == ESP_OK);
    audio_native_poll(connection);
    int64_t deadline = connection->favorites_deadline;
    unsigned request = connection->favorites_request;
    /* Másik zóna saját válasza nem fogyaszthatja el a mi kérésünket. */
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":2,\"start\":0,\"totalitems\":1,"
        "\"items\":[{\"id\":8,\"name\":\"Másik zóna\"}]}],\"command\":\"audio/cfg/getroomfavs/2/0/32\"}");
    assert(connection->favorites_slot == 0 && !audio_page->favorites.failed);
    assert(connection->favorites_deadline == deadline && connection->favorites_request == request);
    /* Azonos zóna régi lapja, eltérő parancs és egyéb listák sem nyugták. */
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":32,\"totalitems\":33,"
        "\"items\":[{\"id\":9,\"name\":\"Régi lap\"}]}],\"command\":\"audio/cfg/getroomfavs/3/32/32\"}");
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":0,"
        "\"items\":[]}],\"command\":\"audio/cfg/getroomfavs/3/0/16\"}");
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":0,"
        "\"items\":[]}],\"command\":17}");
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":4,\"start\":0,\"totalitems\":0,\"items\":[]}]}");
    message_reply(connection, "{\"recent_result\":{\"items\":[],\"ts\":1794608911},\"command\":\"audio/3/recent\"}");
    message_reply(connection, "{\"getservicefolder_result\":[{\"id\":\"0\",\"start\":0,\"totalitems\":0,\"items\":[]}],"
        "\"command\":\"audio/cfg/getservicefolder/loxoneradio/noUser/root/0/50\"}");
    assert(connection->favorites_slot == 0 && !audio_page->favorites.failed);
    assert(connection->favorites_deadline == deadline && connection->favorites_request == request);
    /* Több csoportból az egyértelmű saját: numerikus string is szabályos. */
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":2,\"start\":0,\"totalitems\":0,\"items\":[]},"
        "{\"id\":\"3\",\"items\":[{\"id\":\"1357\",\"name\":\"Saját kedvenc\"}],\"start\":0,\"totalitems\":33}],"
        "\"command\":\"audio/cfg/getroomfavs/3/0/32\"}");
    assert(connection->favorites_slot < 0 && audio_page->favorites.ready && !audio_page->favorites.failed);
    assert(audio_page->favorites.count == 1 && audio_page->favorites.items[0].id == 1357);
    assert(audio_page->favorites.generation == request);

    assert(app_loxone_audio_favorites_request("audio-one", 7, 32) == ESP_OK);
    audio_native_poll(connection);
    deadline = connection->favorites_deadline;
    /* Command nélkül kizárólag az id+start pár rendelhet választ a kéréshez. */
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":1,"
        "\"items\":[{\"id\":7,\"name\":\"Régi első lap\"}]}]}");
    assert(connection->favorites_slot == 0 && connection->favorites_deadline == deadline);
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":32,\"totalitems\":33,"
        "\"items\":[{\"id\":2468,\"name\":\"Utolsó lap\"}]}]}");
    assert(connection->favorites_slot < 0 && audio_page->favorites.ready && !audio_page->favorites.failed);
    assert(audio_page->favorites.start == 32 && audio_page->favorites.items[0].id == 2468);

    assert(app_loxone_audio_favorites_request("audio-one", 7, 0) == ESP_OK);
    audio_native_poll(connection);
    /* Biztosan saját, de hibás válasz: a hiányzó lapozási adat nem található ki. */
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"items\":[]}],"
        "\"command\":\"audio/cfg/getroomfavs/3/0/32\"}");
    assert(connection->favorites_slot < 0 && audio_page->favorites.failed && !audio_page->favorites.ready);
    assert(app_loxone_audio_favorites_request("audio-one", 7, 0) == ESP_OK);
    audio_native_poll(connection);
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":0,\"items\":[]},"
        "{\"id\":3,\"start\":0,\"totalitems\":0,\"items\":[]}]}");
    assert(connection->favorites_slot < 0 && audio_page->favorites.failed && !audio_page->favorites.ready);

    assert(app_loxone_audio_favorites_request("audio-one", 7, 0) == ESP_OK);
    audio_native_poll(connection);
    app_loxone_audio_detail_close();
    assert(app_loxone_audio_favorites_request("audio-one", 7, 32) == ESP_OK);
    unsigned current = audio_page->favorites_requested;
    /* A régi ablak saját válaszát elfogyasztjuk, de nem írjuk az új ablakba. */
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":0,\"items\":[]}],"
        "\"command\":\"audio/cfg/getroomfavs/3/0/32\"}");
    assert(connection->favorites_slot < 0 && !audio_page->favorites.ready && !audio_page->favorites.failed);
    assert(audio_page->favorites_dirty && audio_page->favorites_requested == current);
    audio_native_poll(connection);
    assert(!strcmp(test_native_command, "audio/cfg/getroomfavs/3/32/32"));
    message_reply(connection, "{\"getroomfavs_result\":[{\"id\":3,\"start\":32,\"totalitems\":33,"
        "\"items\":[{\"id\":2468,\"name\":\"Aktuális ablak\"}]}],\"command\":\"audio/cfg/getroomfavs/3/32/32\"}");
    assert(audio_page->favorites.ready && audio_page->favorites.generation == current);
    connection_delete(connection);
    prepare_page();
}
