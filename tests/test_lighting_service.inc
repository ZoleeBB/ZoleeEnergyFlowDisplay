/* A valódi kliens és a valódi állapotmodell. A teszt-Miniserver egy
 * kapcsolaton csak egyszer küld kezdőállapotot, az üres jLocked-ot is. */
static double lighting_initial_brightness = 42;
static bool lighting_initial_sent;

static void lighting_test_numeric(unsigned id, double value)
{
    uint8_t packet[24] = {0};
    packet[3] = 0x10;
    packet[15] = (uint8_t)id;
    memcpy(packet + 16, &value, sizeof(value));
    assert(update_values(2, packet, sizeof(packet)));
}

static void lighting_test_text(unsigned id, const char *text)
{
    size_t length = strlen(text);
    size_t size = 36 + ((length + 3) & ~(size_t)3);
    uint8_t *packet = calloc(1, size);
    assert(packet);
    packet[3] = 0x10;
    packet[15] = (uint8_t)id;
    for (unsigned i = 0; i < 4; ++i)
        packet[32 + i] = (uint8_t)(length >> (8 * i));
    memcpy(packet + 36, text, length);
    assert(update_values(3, packet, size));
    free(packet);
}

static void lighting_test_initial(void)
{
    if (lighting_initial_sent)
        return; /* Ismételt enablebinstatusupdate: csak ACK, nem új snapshot. */
    lighting_initial_sent = true;
    lighting_test_numeric(101, lighting_initial_brightness);
    lighting_test_numeric(102, 1);
    lighting_test_text(103, "hsv(120,80,60)");
    lighting_test_text(104, "temp(35,3500)");
    lighting_test_text(5, "[{\"id\":1,\"name\":\"TV nézés\"}]");
    lighting_test_text(4, "[1]");
    lighting_test_text(9, "");
    lighting_test_text(10, "");
}

static void lighting_service_test(lx_config_t *energy)
{
    cJSON *original_catalog = s_catalog;
    char *source = load("tests/fixtures/loxone-lighting.json");
    cJSON *structure = cJSON_Parse(source);
    free(source);
    assert(structure);
    cJSON *control = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(structure, "controls"),
        "10000000-0000-0000-0000000000000001");
    assert(control);
    /* Valós blokkoknál a szülő és a kör saját zárolásállapota is szerepelhet.
     * Egy fényerőváltozás ezeket az üres, változatlan állapotokat nem ismétli. */
    assert(cJSON_AddStringToObject(cJSON_GetObjectItemCaseSensitive(control, "states"),
                                   "jLocked", "10000000-0000-0000-0000000000000009"));
    cJSON *dimmer = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(control, "subControls"), "AI1");
    assert(cJSON_AddStringToObject(cJSON_GetObjectItemCaseSensitive(dimmer, "states"),
                                   "jLocked", "10000000-0000-0000-000000000000000a"));
    source = cJSON_PrintUnformatted(structure);
    assert(source);
    s_catalog = lx_catalog_parse(source, strlen(source));
    free(source);
    cJSON_Delete(structure);
    assert(s_catalog);
    s_home = calloc(1, sizeof(*s_home));
    assert(s_home);
    s_home->page_count = 2;
    s_home->card_count = 3;
    s_home->pages[0].card_count = 2;
    s_home->pages[1].first_card = 2;
    s_home->pages[1].card_count = 1;
    for (unsigned i = 0; i < 2; ++i)
    {
        lx_home_card_t *card = &s_home->cards[i];
        snprintf(card->id, sizeof(card->id), "lighting-%u", i);
        strcpy(card->control_uuid, "10000000-0000-0000-0000000000000001");
        card->type = LX_HOME_CARD_LIGHTING;
    }
    strcpy(s_home->cards[2].id, "hidden-value");
    s_home->cards[2].type = LX_HOME_CARD_VALUE;
    strcpy(s_home->cards[2].source.uuid, "10000000-0000-0000-0000000000000065");
    s_home->cards[2].source.scale = 1;
    s_home->consumer_count = 1;
    s_home->consumers[0].source = s_home->cards[2].source;
    s_home_live = true;
    s_dead = s_home_busy = s_command_pending = false;
    s_session_config = energy;
    lighting_test_active = true;
    lighting_initial_sent = false;
    mark_response();
    app_loxone_home_visible(-1);
    lighting_poll(esp_timer_get_time());
    unsigned initial = lighting_initial_count;
    assert(!s_light_context && !s_light_detail);
    lighting_test_numeric(101, 17);
    assert(s_home_values.consumers[0].valid && s_home_values.consumers[0].value == 17);
    assert(!s_home_values.cards[2].valid);
    assert(app_loxone_light_open("lighting-0", s_home_revision) == 0);
    /* A valódi kapcsolatindításkor jön a teljes tábla, a UI még zárva van. */
    cJSON *ack = reply("jdev/sps/enablebinstatusupdate", false);
    assert(ack);
    cJSON_Delete(ack);
    assert(lighting_initial_count == initial + 1 && !s_light_context && !s_light_detail);
    assert(!s_home_values.cards[2].valid);

    app_loxone_home_visible(0);
    clock_us += 300000;
    mark_response();
    lighting_poll(esp_timer_get_time());
    assert(lighting_initial_count == initial + 1 && s_light_initial_ready);
    assert(s_light_context && s_light_context->count == 2);
    assert(s_light_context->blocks[0] == s_light_context->blocks[1]);
    assert(s_light_context->blocks[0]->observations && !s_light_context->blocks[0]->circuits);
    assert(!s_light_detail && !s_home_values.cards[2].valid);
    lx_light_summary_t summary;
    assert(app_loxone_light_summary("lighting-0", s_home_revision, &summary));
    assert(summary.label == LX_LIGHT_ONE_MOOD && !strcmp(summary.name, "TV nézés"));
    assert(summary.controllable);
    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    assert(lighting_initial_count == initial + 1);
    const lx_home_command_t actions[] = {LX_HOME_COMMAND_LIGHT_PREV,
        LX_HOME_COMMAND_LIGHT_OFF, LX_HOME_COMMAND_LIGHT_NEXT};
    const char *commands_text[] = {"/minus", "/changeTo/0", "/plus"};
    for (unsigned i = 0; i < 3; ++i)
    {
        unsigned sent = lighting_command_count;
        assert(app_loxone_home_command("lighting-0", actions[i]) == ESP_OK);
        home_command_poll(esp_timer_get_time());
        assert(lighting_command_count == sent + 1 && !s_command_pending);
        assert(strstr(last_command, commands_text[i]));
    }

    unsigned handle = app_loxone_light_open("lighting-0", s_home_revision);
    assert(handle);
    app_loxone_light_info_t info;
    assert(app_loxone_light_info(handle, &info) && !info.ready);
    clock_us += 300000;
    lighting_poll(esp_timer_get_time());
    assert(lighting_initial_count == initial + 1);
    assert(app_loxone_light_info(handle, &info) && info.ready && info.circuit_count == 6);
    lx_light_circuit_t circuit;
    assert(app_loxone_light_circuit(handle, 0, &circuit));
    assert(circuit.actual.valid && circuit.actual.brightness == 42 && !circuit.blocked);
    lx_light_value_t value = circuit.actual;
    value.brightness = 60;
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    value.brightness = 70;
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    value.brightness = 80;
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    unsigned commands = lighting_command_count;
    lighting_poll(esp_timer_get_time());
    assert(lighting_command_count == commands + 1 && strstr(last_command, "/AI1/80"));
    clock_us += 10000;
    value.brightness = 90;
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    lighting_poll(esp_timer_get_time());
    assert(lighting_command_count == commands + 1);
    clock_us += 100000;
    lighting_poll(esp_timer_get_time());
    assert(lighting_command_count == commands + 2 && strstr(last_command, "/AI1/90"));
    lighting_test_numeric(101, 60);
    assert(app_loxone_light_circuit(handle, 0, &circuit) && circuit.actual.brightness == 90);
    lighting_test_numeric(101, 90);
    assert(app_loxone_light_circuit(handle, 0, &circuit) && !circuit.pending);
    lighting_test_text(103, "temp(75,4000)");
    assert(app_loxone_light_circuit(handle, 2, &circuit) && circuit.actual.mode == LX_LIGHT_MODE_WHITE);

    /* A javítás nem kerüli meg a valódi zárolást vagy a hibás zárolásadatot. */
    value.brightness = 99;
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    lighting_test_text(9, "{\"locked\":2,\"reason\":\"Teszt\"}");
    assert(app_loxone_light_set(handle, 0, &value) != ESP_OK);
    assert(app_loxone_home_command("lighting-0", LX_HOME_COMMAND_LIGHT_NEXT) != ESP_OK);
    lighting_poll(esp_timer_get_time());
    assert(lighting_command_count == commands + 2);
    lighting_test_text(9, "");
    lighting_test_text(10, "{}");
    assert(app_loxone_light_set(handle, 0, &value) != ESP_OK);
    lighting_test_text(10, "");
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    app_loxone_light_close(handle);
    assert(app_loxone_light_set(handle, 0, &value) != ESP_OK);
    lighting_poll(esp_timer_get_time());
    assert(!s_light_detail && s_light_context);
    assert(lighting_command_count == commands + 2 && lighting_initial_count == initial + 1);

    /* Zárt ablakban változik az érték, majd újranyitáskor nincs új WS-esemény. */
    lighting_test_numeric(101, 67);
    handle = app_loxone_light_open("lighting-0", s_home_revision);
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_light_circuit(handle, 0, &circuit));
    assert(circuit.actual.brightness == 67 && !circuit.blocked);
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    app_loxone_home_visible(1);
    assert(!app_loxone_light_info(handle, &info));
    lighting_poll(esp_timer_get_time());
    assert(!s_light_detail && s_light_context && !s_light_context->blocks[0]);
    assert(s_home_values.cards[2].valid && s_home_values.cards[2].value == 67);
    assert(lighting_command_count == commands + 2 && lighting_initial_count == initial + 1);
    assert(!app_loxone_light_summary("lighting-0", s_home_revision, &summary));
    app_loxone_home_visible(-1);
    lighting_poll(esp_timer_get_time());
    lighting_test_numeric(101, 23);
    assert(!s_light_context && !s_light_detail && !s_home_values.cards[2].valid);
    assert(s_home_values.consumers[0].valid && s_home_values.consumers[0].value == 23);
    app_loxone_home_visible(1);
    lighting_poll(esp_timer_get_time());
    assert(s_home_values.cards[2].valid && s_home_values.cards[2].value == 23);

    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_home_command("lighting-0", LX_HOME_COMMAND_LIGHT_NEXT) == ESP_OK);
    app_loxone_home_visible(-1);
    home_command_poll(esp_timer_get_time());
    assert(lighting_command_count == commands + 2 && !s_command_pending);
    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    handle = app_loxone_light_open("lighting-0", s_home_revision);
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    invalidate(true);
    assert(!s_light_detail && !s_light_context && !s_light_modal);
    assert(!app_loxone_light_info(handle, &info));
    s_home_live = true;
    mark_response();
    lighting_poll(esp_timer_get_time());
    assert(s_light_context && !s_light_detail && !s_light_modal);
    assert(lighting_command_count == commands + 2);
    /* Külön reprodukció: fényerő már van, de az új kapcsolat zárolásállapota
     * még hiányzik. A régi kapcsolat feloldott állapota nem használható. */
    lighting_test_numeric(101, 42);
    handle = app_loxone_light_open("lighting-0", s_home_revision);
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_light_circuit(handle, 0, &circuit));
    assert(circuit.actual.valid && circuit.blocked);
    assert(app_loxone_light_set(handle, 0, &value) != ESP_OK);
    lighting_test_text(9, "");
    assert(app_loxone_light_set(handle, 0, &value) != ESP_OK);
    lighting_test_text(10, "");
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    app_loxone_light_close(handle);
    lighting_poll(esp_timer_get_time());
    assert(lighting_command_count == commands + 2 && lighting_initial_count == initial + 1);

#ifdef LIGHT_CACHE_MAX_STATES
    /* Csak a konfigurált körök vannak jelen; a korlátos nyers tároló nem
     * tarthat meg idegen UUID-t, és memóriahiánynál sem adhat régi lockot. */
    assert(s_light_cache.count == 11 && s_light_cache.capacity == 16);
    assert(s_light_cache.text_bytes < 2048);
    lighting_test_numeric(200, 88);
    assert(!lighting_cache_find("10000000-0000-0000-00000000000000c8"));
    char *large = malloc(LX_LIGHT_TEXT_MAX + 1);
    assert(large);
    memset(large, 'x', LX_LIGHT_TEXT_MAX);
    large[LX_LIGHT_TEXT_MAX] = 0;
    lighting_test_text(9, large);
    lighting_test_text(10, large);
    assert(s_light_cache.failed && s_light_cache.text_bytes <= LIGHT_CACHE_TEXT_BUDGET);
    assert(!lighting_cache_find("10000000-0000-0000-000000000000000a")->seen);
    free(large);
    lighting_test_text(9, "");
    lighting_test_text(10, "");
    assert(lighting_cache_find("10000000-0000-0000-000000000000000a")->seen);
#endif

    handle = app_loxone_light_open("lighting-0", s_home_revision);
    lx_home_config_t *replacement = calloc(1, sizeof(*replacement));
    assert(replacement);
    *replacement = *s_home;
    home_store_error = false;
    save_home_request(replacement);
    free(replacement);
    assert(!app_loxone_light_info(handle, &info) && !s_light_modal);
    invalidate(true);
    app_loxone_home_visible(-1);
#ifdef LIGHT_CACHE_MAX_STATES
    assert(!s_light_cache.entries && !s_light_cache.count && !s_light_cache.text_bytes);
#endif
    lighting_test_active = false;
    cJSON_Delete(s_catalog);
    s_catalog = original_catalog;
    free(s_home);
    s_home = NULL;
    s_session_config = NULL;
    puts("PASS: initial states before UI, empty parent/child locks, no resubscription, card/dimmer commands, hidden deltas, bounded cache, fail-closed and reconnect");
}
