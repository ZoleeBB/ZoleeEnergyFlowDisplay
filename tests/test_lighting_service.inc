/* A valódi kliens belső szolgáltatása, szimulált Miniserverrel.
 * Az inicializálás ugyanúgy a soros WebSocket-válasz alatt érkezik. */
static double lighting_initial_brightness = 42;

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
    lighting_test_numeric(101, lighting_initial_brightness);
    lighting_test_numeric(102, 1);
    lighting_test_text(103, "hsv(120,80,60)");
    lighting_test_text(104, "temp(35,3500)");
    lighting_test_text(5, "[{\"id\":1,\"name\":\"TV nézés\"}]");
    lighting_test_text(4, "[1]");
}

static void lighting_service_test(lx_config_t *energy)
{
    cJSON *original_catalog = s_catalog;
    char *source = load("tests/fixtures/loxone-lighting.json");
    s_catalog = lx_catalog_parse(source, strlen(source));
    free(source);
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
    mark_response();
    app_loxone_home_visible(-1);
    lighting_poll(esp_timer_get_time());
    unsigned initial = lighting_initial_count;
    assert(!s_light_context && !s_light_detail);
    lighting_test_numeric(101, 17);
    assert(s_home_values.consumers[0].valid && s_home_values.consumers[0].value == 17);
    assert(!s_home_values.cards[2].valid);
    assert(app_loxone_light_open("lighting-0", s_home_revision) == 0);

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
    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    assert(lighting_initial_count == initial + 1);

    unsigned handle = app_loxone_light_open("lighting-0", s_home_revision);
    assert(handle);
    app_loxone_light_info_t info;
    assert(app_loxone_light_info(handle, &info) && !info.ready);
    clock_us += 300000;
    lighting_poll(esp_timer_get_time());
    assert(lighting_initial_count == initial + 2);
    assert(app_loxone_light_info(handle, &info) && info.ready && info.circuit_count == 6);
    lx_light_circuit_t circuit;
    assert(app_loxone_light_circuit(handle, 0, &circuit) && circuit.actual.brightness == 42);
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
    value.brightness = 99;
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    app_loxone_light_close(handle);
    assert(app_loxone_light_set(handle, 0, &value) != ESP_OK);
    lighting_poll(esp_timer_get_time());
    assert(!s_light_detail && s_light_context);
    assert(lighting_command_count == commands + 2 && lighting_initial_count == initial + 2);

    handle = app_loxone_light_open("lighting-0", s_home_revision);
    clock_us += 300000;
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_light_set(handle, 0, &value) == ESP_OK);
    app_loxone_home_visible(1);
    assert(!app_loxone_light_info(handle, &info));
    lighting_initial_brightness = 67;
    clock_us += 300000;
    lighting_poll(esp_timer_get_time());
    assert(!s_light_detail && s_light_context && !s_light_context->blocks[0]);
    assert(s_home_values.cards[2].valid && s_home_values.cards[2].value == 67);
    assert(lighting_command_count == commands + 2);
    assert(!app_loxone_light_summary("lighting-0", s_home_revision, &summary));
    app_loxone_home_visible(-1);
    lighting_poll(esp_timer_get_time());
    lighting_test_numeric(101, 23);
    assert(!s_light_context && !s_light_detail && !s_home_values.cards[2].valid);
    assert(s_home_values.consumers[0].valid && s_home_values.consumers[0].value == 23);

    app_loxone_home_visible(0);
    clock_us += 300000;
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_home_command("lighting-0", LX_HOME_COMMAND_LIGHT_NEXT) == ESP_OK);
    app_loxone_home_visible(-1);
    home_command_poll(esp_timer_get_time());
    assert(lighting_command_count == commands + 2 && !s_command_pending);
    app_loxone_home_visible(0);
    clock_us += 300000;
    lighting_poll(esp_timer_get_time());
    handle = app_loxone_light_open("lighting-0", s_home_revision);
    clock_us += 300000;
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
    /* Mentés után a korábbi ablakazonosító nem nyithat meg másik blokkot. */
    handle = app_loxone_light_open("lighting-0", s_home_revision);
    lx_home_config_t *replacement = calloc(1, sizeof(*replacement));
    assert(replacement);
    *replacement = *s_home;
    home_store_error = false;
    save_home_request(replacement);
    free(replacement);
    assert(!app_loxone_light_info(handle, &info));
    assert(!s_light_modal);
    invalidate(true);
    app_loxone_home_visible(-1);
    lighting_test_active = false;
    cJSON_Delete(s_catalog);
    s_catalog = original_catalog;
    free(s_home);
    s_home = NULL;
    s_session_config = NULL;
    puts("PASS: actual lighting worker, compact shared cards, resnapshot, rate, visible-only values, cancellation and reconnect");
}
