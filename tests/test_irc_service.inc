/* Valódi IRC-katalógus, nyers delta-cache, látható modell és titkosított
 * parancssor együtt. A Miniserver csak kapcsolódáskor küld kezdőállapotot. */
static bool irc_initial_sent;

static void irc_test_numeric(unsigned id, double value)
{
    uint8_t packet[24] = {0};
    packet[3] = 0x20;
    packet[15] = (uint8_t)id;
    memcpy(packet + 16, &value, sizeof(value));
    assert(update_values(2, packet, sizeof(packet)));
}

static void irc_test_text(unsigned id, const char *text)
{
    size_t length = strlen(text);
    size_t size = 36 + ((length + 3) & ~(size_t)3);
    uint8_t *packet = calloc(1, size);
    assert(packet);
    packet[3] = 0x20;
    packet[15] = (uint8_t)id;
    for (unsigned i = 0; i < 4; ++i)
        packet[32 + i] = (uint8_t)(length >> (8 * i));
    memcpy(packet + 36, text, length);
    assert(update_values(3, packet, size));
    free(packet);
}

static void irc_test_initial(void)
{
    if (irc_initial_sent)
        return;
    irc_initial_sent = true;
    irc_test_numeric(201, 22.5);
    irc_test_numeric(202, 21.8);
    irc_test_numeric(203, 1);
    irc_test_numeric(204, 0);
    irc_test_text(205, "");
}

static void irc_service_test(lx_config_t *energy)
{
    static const char structure[] =
        "{\"msInfo\":{\"tempUnit\":0},\"controls\":{"
        "\"20000000-0000-0000-0000000000000001\":{"
        "\"name\":\"Nappali IRC\",\"type\":\"IRoomControllerV2\","
        "\"uuidAction\":\"20000000-0000-0000-0000000000000002\","
        "\"isSecured\":false,\"states\":{"
        "\"tempTarget\":\"20000000-0000-0000-00000000000000c9\","
        "\"tempActual\":\"20000000-0000-0000-00000000000000ca\","
        "\"currentMode\":\"20000000-0000-0000-00000000000000cb\","
        "\"operatingMode\":\"20000000-0000-0000-00000000000000cc\","
        "\"jLocked\":\"20000000-0000-0000-00000000000000cd\"}}}}";
    cJSON *original_catalog = s_catalog;
    s_catalog = lx_catalog_parse(structure, strlen(structure));
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
        snprintf(card->id, sizeof(card->id), "irc-%u", i);
        strcpy(card->control_uuid, "20000000-0000-0000-0000000000000001");
        card->type = LX_HOME_CARD_IRC;
    }
    strcpy(s_home->cards[2].id, "room-value");
    s_home->cards[2].type = LX_HOME_CARD_VALUE;
    strcpy(s_home->cards[2].source.uuid, "20000000-0000-0000-00000000000000ca");
    s_home->cards[2].source.scale = 1;
    s_home_live = true;
    s_dead = s_home_busy = s_command_pending = false;
    s_session_config = energy;
    irc_test_active = true;
    irc_initial_sent = false;
    mark_response();
    app_loxone_home_visible(-1);
    lighting_poll(esp_timer_get_time());
    unsigned subscriptions = lighting_initial_count;
    assert(!s_light_context && !s_light_detail);
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) != ESP_OK);
    cJSON *ack = reply("jdev/sps/enablebinstatusupdate", false);
    assert(ack && irc_initial_sent);
    cJSON_Delete(ack);
    assert(lighting_initial_count == subscriptions + 1 && !s_light_context);
    assert(s_light_cache.count == 5 && s_light_cache.capacity == 16);
    assert(lighting_cache_find("20000000-0000-0000-00000000000000cd")->seen);
    assert(!s_home_values.cards[2].valid);

    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    assert(s_light_context && s_light_initial_ready && s_light_context->count == 2);
    assert(!s_light_context->blocks[0] && !s_light_context->blocks[1]);
    assert(s_light_context->irc_blocks[0] == s_light_context->irc_blocks[1]);
    lx_irc_summary_t summary;
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary));
    assert(summary.available && summary.target_valid && summary.target_c == 22.5);
    assert(summary.actual_valid && summary.actual_c == 21.8 && summary.controllable);
    assert(summary.direction == LX_IRC_DIRECTION_HEATING);
    assert(!app_loxone_irc_summary("irc-0", s_home_revision + 1, &summary));
    assert(!summary.target_valid && !summary.controllable);
    assert(!app_loxone_irc_summary("missing", s_home_revision, &summary));
    assert(!app_loxone_light_open("irc-0", s_home_revision));
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_LIGHT_NEXT) != ESP_OK);
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_PULSE) != ESP_OK);

    /* A teljes Otthon-pillanatkép nem bővül IRC-adattömbbel. Csak a látható
     * kártya parancsképességét adja át; a hőfok külön kis pillanatkép. */
    lx_home_config_t *snapshot = calloc(1, sizeof(*snapshot));
    lx_home_values_t *values = calloc(1, sizeof(*values));
    unsigned revision;
    bool connected, pending;
    char message[LX_HOME_ERROR_SIZE];
    assert(snapshot && values);
    assert(app_loxone_home_snapshot(snapshot, values, &revision, &connected, &pending, message));
    assert(connected && !pending && revision == s_home_revision);
    assert(values->command_supported[0] && values->command_supported[1]);
    assert(!values->command_supported[2]);
    free(snapshot);
    free(values);

    const lx_home_command_t actions[] = {LX_HOME_COMMAND_IRC_DECREASE, LX_HOME_COMMAND_IRC_INCREASE};
    const char *commands[] = {
        "jdev/sps/io/20000000-0000-0000-0000000000000002/decreasetemperature/0.500000",
        "jdev/sps/io/20000000-0000-0000-0000000000000002/increasetemperature/0.500000"
    };
    for (unsigned i = 0; i < 2; ++i)
    {
        unsigned sent = irc_command_count;
        assert(app_loxone_home_command("irc-0", actions[i]) == ESP_OK);
        assert(app_loxone_home_command("irc-1", actions[i]) != ESP_OK);
        home_command_poll(esp_timer_get_time());
        assert(irc_command_count == sent + 1 && !s_command_pending);
        assert(!strcmp(last_command, commands[i]));
        assert(strstr(s_command_status, "Hőfokléptetés") && !strstr(s_command_status, "Impulzus"));
        assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary));
        assert(summary.target_c == 22.5); /* ACK nem hőmérséklet-visszajelzés. */
    }
    irc_test_numeric(201, 23.0);
    irc_test_numeric(202, 22.1);
    irc_test_numeric(203, 2);
    assert(app_loxone_irc_summary("irc-1", s_home_revision, &summary));
    assert(summary.target_c == 23.0 && summary.actual_c == 22.1);
    assert(summary.direction == LX_IRC_DIRECTION_COOLING);
    /* A hibás célérték vagy lock soha nem marad vezérelhető régi állapot. */
    irc_test_numeric(201, NAN);
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary));
    assert(!summary.target_valid && !summary.controllable && summary.actual_valid);
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) != ESP_OK);
    irc_test_numeric(201, 23.0);
    irc_test_text(205, "{\"locked\":2,\"reason\":\"Teszt\"}");
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) != ESP_OK);
    irc_test_text(205, "{}");
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary) && !summary.controllable);
    irc_test_numeric(205, 0);
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary) && !summary.controllable);
    irc_test_text(205, "");
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary) && summary.controllable);

    /* Lapozáskor nincs új állapotfeliratkozás. A rejtett IRC kizárólag
     * nyers értékeket tárol, a normál értékkártya ugyanazt a cache-t használja. */
    app_loxone_home_visible(1);
    lighting_poll(esp_timer_get_time());
    assert(s_light_context && !s_light_context->irc_blocks[0]);
    assert(s_home_values.cards[2].valid && s_home_values.cards[2].value == 22.1);
    assert(!app_loxone_irc_summary("irc-0", s_home_revision, &summary));
    irc_test_numeric(201, 24.0);
    irc_test_numeric(202, 22.7);
    irc_test_text(205, "{\"locked\":1}");
    assert(s_home_values.cards[2].value == 22.7);
    app_loxone_home_visible(-1);
    lighting_poll(esp_timer_get_time());
    assert(!s_light_context && !s_light_detail);
    irc_test_numeric(201, 24.5);
    irc_test_text(205, "");
    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary));
    assert(summary.target_c == 24.5 && summary.actual_c == 22.7 && summary.controllable);
    assert(lighting_initial_count == subscriptions + 1 && s_light_cache.count == 5);
    irc_test_numeric(230, 77);
    assert(s_light_cache.count == 5);
    assert(!lighting_cache_find("20000000-0000-0000-00000000000000e6"));

    unsigned sent = irc_command_count;
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) == ESP_OK);
    app_loxone_home_visible(-1);
    app_loxone_home_visible(0); /* Visszalapozás sem éleszti fel a régi kérést. */
    lighting_poll(esp_timer_get_time());
    home_command_poll(esp_timer_get_time());
    assert(irc_command_count == sent && !s_command_pending);
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) == ESP_OK);
    clock_us += 2000001LL;
    home_command_poll(esp_timer_get_time());
    assert(irc_command_count == sent && !s_command_pending);
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) == ESP_OK);
    s_home_busy = true;
    home_command_poll(esp_timer_get_time());
    assert(irc_command_count == sent && !s_command_pending);
    s_home_busy = false;
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) == ESP_OK);
    irc_test_text(205, "{\"locked\":1}");
    home_command_poll(esp_timer_get_time());
    assert(irc_command_count == sent && !s_command_pending);
    irc_test_text(205, "");

    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) == ESP_OK);
    lx_home_config_t *replacement = calloc(1, sizeof(*replacement));
    assert(replacement);
    *replacement = *s_home;
    home_store_error = false;
    unsigned old_revision = s_home_revision;
    save_home_request(replacement);
    free(replacement);
    assert(!app_loxone_irc_summary("irc-0", old_revision, &summary));
    home_command_poll(esp_timer_get_time());
    assert(irc_command_count == sent && !s_command_pending);
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary) && !summary.controllable);
    irc_test_numeric(201, 22.0);
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) != ESP_OK);
    irc_test_text(205, "");
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) == ESP_OK);
    invalidate(true);
    assert(!s_light_context && !s_light_cache.count && !uxQueueMessagesWaiting(s_home_commands));
    s_home_live = true;
    mark_response();
    lighting_poll(esp_timer_get_time());
    home_command_poll(esp_timer_get_time());
    assert(irc_command_count == sent && !s_command_pending);
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary) && !summary.controllable);
    irc_test_numeric(201, 22.0);
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) != ESP_OK);
    irc_test_text(205, "");
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary) && summary.controllable);

    /* Elutasítás és hiányzó nyugta után sincs újraküldés: egy natív lépés
     * nem idempotens. A célt továbbra is kizárólag valódi delta írhatja. */
    reject_home_command = true;
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(irc_command_count == ++sent && !s_command_pending);
    home_command_poll(esp_timer_get_time());
    assert(irc_command_count == sent);
    reject_home_command = false;
    irc_drop_command_reply = true;
    assert(app_loxone_home_command("irc-0", LX_HOME_COMMAND_IRC_INCREASE) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(irc_command_count == ++sent && !s_command_pending);
    assert(strstr(s_command_status, "nem igazolható"));
    irc_drop_command_reply = false;
    for (unsigned i = 0; i < 3; ++i)
        home_command_poll(esp_timer_get_time());
    assert(irc_command_count == sent);
    assert(app_loxone_irc_summary("irc-0", s_home_revision, &summary) && summary.target_c == 22.0);

    invalidate(true);
    app_loxone_home_visible(-1);
    assert(!s_light_cache.entries && !s_light_context && !s_light_detail);
    irc_test_active = false;
    cJSON_Delete(s_catalog);
    s_catalog = original_catalog;
    free(s_home);
    s_home = NULL;
    s_session_config = NULL;
    puts("PASS: IRC initial hidden snapshot/empty lock, five-state shared cache, duplicate card models, external feedback, native +/- only, stale queue gates, reconnect lock reset and no retry after missing acknowledgement");
}
