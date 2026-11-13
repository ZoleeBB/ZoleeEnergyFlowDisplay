/* A valódi árnyékolási katalógus, közös nyers cache, látható modell és
 * titkosított parancssor együtt, kizárólag a kapcsolat kezdeti snapshotjával. */
static bool shading_initial_sent;

static void shading_test_numeric(unsigned id, double value)
{
    uint8_t packet[24] = {0};
    packet[3] = 0x30;
    packet[15] = (uint8_t)id;
    memcpy(packet + 16, &value, sizeof(value));
    assert(update_values(2, packet, sizeof(packet)));
}

static void shading_test_text(unsigned id, const char *text)
{
    size_t length = strlen(text);
    size_t size = 36 + ((length + 3) & ~(size_t)3);
    uint8_t *packet = calloc(1, size);
    assert(packet);
    packet[3] = 0x30;
    packet[15] = (uint8_t)id;
    for (unsigned i = 0; i < 4; ++i)
        packet[32 + i] = (uint8_t)(length >> (8 * i));
    memcpy(packet + 36, text, length);
    assert(update_values(3, packet, size));
    free(packet);
}

static void shading_test_initial(void)
{
    if (shading_initial_sent)
        return;
    shading_initial_sent = true;
    shading_test_numeric(201, .45);
    shading_test_numeric(202, 0);
    shading_test_numeric(203, 1);
    shading_test_numeric(204, 0);
    shading_test_numeric(205, 0);
    shading_test_numeric(206, 2);
    shading_test_text(207, "");
}

static esp_err_t shading_test_command(const char *id, lx_home_command_t action)
{
    return app_loxone_shading_command(id, s_home_revision, action);
}

static void shading_service_test(lx_config_t *energy)
{
    static const char structure[] =
        "{\"controls\":{\"30000000-0000-0000-0000000000000001\":{"
        "\"name\":\"Nappali redőny\",\"type\":\"Jalousie\","
        "\"uuidAction\":\"30000000-0000-0000-0000000000000002\","
        "\"details\":{\"isAutomatic\":true},\"states\":{"
        "\"position\":\"30000000-0000-0000-00000000000000c9\","
        "\"up\":\"30000000-0000-0000-00000000000000ca\","
        "\"down\":\"30000000-0000-0000-00000000000000cb\","
        "\"locked\":\"30000000-0000-0000-00000000000000cc\","
        "\"safetyActive\":\"30000000-0000-0000-00000000000000cd\","
        "\"deviceState\":\"30000000-0000-0000-00000000000000ce\","
        "\"jLocked\":\"30000000-0000-0000-00000000000000cf\"}},"
        "\"30000000-0000-0000-0000000000000003\":{"
        "\"name\":\"Összes redőny\",\"type\":\"CentralJalousie\","
        "\"uuidAction\":\"30000000-0000-0000-0000000000000004\","
        "\"details\":{\"controls\":[{\"uuid\":\"30000000-0000-0000-0000000000000001\"}]}}}}";
    cJSON *original_catalog = s_catalog;
    s_catalog = lx_catalog_parse(structure, strlen(structure));
    assert(s_catalog);
    s_home = calloc(1, sizeof(*s_home));
    assert(s_home);
    s_home->page_count = 2;
    s_home->card_count = 4;
    s_home->pages[0].card_count = 3;
    s_home->pages[1].first_card = 3;
    s_home->pages[1].card_count = 1;
    for (unsigned i = 0; i < 3; ++i)
    {
        lx_home_card_t *card = &s_home->cards[i];
        snprintf(card->id, sizeof(card->id), "shading-%u", i);
        strcpy(card->control_uuid, i == 2 ? "30000000-0000-0000-0000000000000003" :
                                            "30000000-0000-0000-0000000000000001");
        card->type = LX_HOME_CARD_SHADING;
    }
    strcpy(s_home->cards[3].id, "position-value");
    s_home->cards[3].type = LX_HOME_CARD_VALUE;
    strcpy(s_home->cards[3].source.uuid, "30000000-0000-0000-00000000000000c9");
    s_home->cards[3].source.scale = 1;
    s_home_live = true;
    s_dead = s_home_busy = s_command_pending = false;
    s_session_config = energy;
    shading_test_active = true;
    shading_initial_sent = false;
    mark_response();
    app_loxone_home_visible(-1);
    lighting_poll(esp_timer_get_time());
    unsigned subscriptions = lighting_initial_count;
    assert(!s_light_context && !s_light_detail);
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_UP) != ESP_OK);
    cJSON *ack = reply("jdev/sps/enablebinstatusupdate", false);
    assert(ack && shading_initial_sent);
    cJSON_Delete(ack);
    assert(lighting_initial_count == subscriptions + 1 && !s_light_context);
    assert(s_light_cache.count == 7 && s_light_cache.capacity == 16);
    assert(lighting_cache_find("30000000-0000-0000-00000000000000cf")->seen);
    assert(!s_home_values.cards[3].valid);

    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    assert(s_light_context && s_light_initial_ready && s_light_context->count == 3);
    assert(!s_light_context->blocks[0] && !s_light_context->irc_blocks[0]);
    assert(s_light_context->shading_blocks[0] == s_light_context->shading_blocks[1]);
    assert(s_light_context->shading_blocks[2] != s_light_context->shading_blocks[0]);
    lx_shading_summary_t summary;
    assert(app_loxone_shading_summary("shading-0", s_home_revision, &summary));
    assert(summary.available && summary.controllable && !summary.central);
    assert(summary.position_valid && summary.position_percent == 45);
    assert(summary.motion_known && summary.moving_down && !summary.moving_up);
    assert(app_loxone_shading_summary("shading-2", s_home_revision, &summary));
    assert(summary.available && summary.controllable && summary.central);
    assert(!summary.position_valid && !summary.motion_known);
    assert(!app_loxone_shading_summary("shading-0", s_home_revision + 1, &summary));
    assert(!summary.position_valid && !summary.controllable);
    assert(!app_loxone_shading_summary("missing", s_home_revision, &summary));
    assert(!app_loxone_light_open("shading-0", s_home_revision));
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_IRC_INCREASE) != ESP_OK);
    assert(app_loxone_home_command("shading-0", LX_HOME_COMMAND_SHADING_POSITION) == ESP_ERR_INVALID_ARG);
    assert(app_loxone_shading_command("shading-0", s_home_revision, LX_HOME_COMMAND_SHADING_POSITION) == ESP_ERR_INVALID_ARG);
    assert(app_loxone_shading_command("shading-0", s_home_revision + 1, LX_HOME_COMMAND_SHADING_UP) != ESP_OK);
    assert(app_loxone_shading_position("shading-2", s_home_revision, 50) != ESP_OK);
    assert(app_loxone_shading_position("position-value", s_home_revision, 50) != ESP_OK);
    assert(app_loxone_shading_position("shading-0", s_home_revision, 101) == ESP_ERR_INVALID_ARG);
    assert(app_loxone_shading_position("shading-0", s_home_revision + 1, 50) != ESP_OK);

    lx_home_config_t *snapshot = calloc(1, sizeof(*snapshot));
    lx_home_values_t *values = calloc(1, sizeof(*values));
    unsigned revision;
    bool connected, pending;
    char message[LX_HOME_ERROR_SIZE];
    assert(snapshot && values);
    assert(app_loxone_home_snapshot(snapshot, values, &revision, &connected, &pending, message));
    assert(connected && !pending && revision == s_home_revision);
    assert(values->command_supported[0] && values->command_supported[1] && values->command_supported[2]);
    assert(!values->command_supported[3]);
    free(snapshot);
    free(values);

    const lx_home_command_t actions[] = {
        LX_HOME_COMMAND_SHADING_UP, LX_HOME_COMMAND_SHADING_STOP, LX_HOME_COMMAND_SHADING_DOWN
    };
    const char *tails[] = {"FullUp", "stop", "FullDown"};
    for (unsigned card = 0; card <= 2; card += 2)
    {
        char id[LX_HOME_ID_SIZE];
        snprintf(id, sizeof(id), "shading-%u", card);
        for (unsigned i = 0; i < 3; ++i)
        {
            unsigned sent = shading_command_count;
            assert(shading_test_command(id, actions[i]) == ESP_OK);
            assert(shading_test_command(id, actions[i]) != ESP_OK);
            home_command_poll(esp_timer_get_time());
            assert(shading_command_count == sent + 1 && !s_command_pending);
            char expected[96];
            snprintf(expected, sizeof(expected), "jdev/sps/io/30000000-0000-0000-000000000000000%u/%s",
                     card == 2 ? 4 : 2, tails[i]);
            assert(!strcmp(last_command, expected));
            assert(strstr(s_command_status, "Redőnyparancs"));
        }
    }
    const unsigned targets[] = {0, 37, 100};
    for (unsigned i = 0; i < 3; ++i)
    {
        assert(app_loxone_shading_position("shading-0", s_home_revision, targets[i]) == ESP_OK);
        home_command_poll(esp_timer_get_time());
        assert(!s_command_pending);
        char expected[96];
        snprintf(expected, sizeof(expected), "jdev/sps/io/30000000-0000-0000-0000000000000002/manualPosition/%u",
                 targets[i]);
        assert(!strcmp(last_command, expected));
        assert(app_loxone_shading_summary("shading-1", s_home_revision, &summary));
        assert(summary.position_percent == 45 && summary.moving_down);
        /* A nyugta után mozgás közben is leállítható; nem várunk végállásra. */
        assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_STOP) == ESP_OK);
        home_command_poll(esp_timer_get_time());
        assert(!s_command_pending && strstr(last_command, "/stop"));
    }

    /* Pozíció nélkül a három alapgomb megmarad. A védelemjelzés önmagában
     * nem jogosultsági zár, a locked/jLocked és offline állapot viszont igen. */
    shading_test_numeric(201, NAN);
    assert(app_loxone_shading_summary("shading-0", s_home_revision, &summary));
    assert(summary.available && summary.controllable && !summary.position_valid);
    assert(app_loxone_shading_position("shading-0", s_home_revision, 50) != ESP_OK);
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_UP) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    shading_test_numeric(201, .6);
    shading_test_numeric(205, 1);
    assert(app_loxone_shading_summary("shading-0", s_home_revision, &summary));
    assert(summary.safety_active && summary.controllable);
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_STOP) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    shading_test_numeric(204, 1);
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_STOP) != ESP_OK);
    shading_test_numeric(204, NAN);
    assert(app_loxone_shading_position("shading-0", s_home_revision, 50) != ESP_OK);
    shading_test_numeric(204, 0);
    shading_test_text(207, "{\"locked\":1}");
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_DOWN) != ESP_OK);
    shading_test_text(207, "{}");
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_UP) != ESP_OK);
    shading_test_text(207, "");
    shading_test_numeric(206, 1);
    assert(app_loxone_shading_summary("shading-0", s_home_revision, &summary) && !summary.available);
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_STOP) != ESP_OK);
    shading_test_numeric(206, 2);

    app_loxone_home_visible(1);
    lighting_poll(esp_timer_get_time());
    assert(s_light_context && !s_light_context->shading_blocks[0]);
    assert(s_home_values.cards[3].valid && s_home_values.cards[3].value == .6);
    shading_test_numeric(201, .7);
    assert(s_home_values.cards[3].value == .7);
    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_shading_summary("shading-0", s_home_revision, &summary));
    assert(summary.position_percent == 70 && summary.controllable);
    assert(lighting_initial_count == subscriptions + 1 && s_light_cache.count == 7);

    unsigned sent = shading_command_count;
    assert(app_loxone_shading_position("shading-0", s_home_revision, 50) == ESP_OK);
    shading_test_numeric(201, NAN);
    home_command_poll(esp_timer_get_time());
    assert(shading_command_count == sent && !s_command_pending);
    shading_test_numeric(201, .7);
    assert(app_loxone_shading_position("shading-0", s_home_revision, 50) == ESP_OK);
    shading_test_numeric(204, 1);
    home_command_poll(esp_timer_get_time());
    assert(shading_command_count == sent && !s_command_pending);
    shading_test_numeric(204, 0);
    assert(app_loxone_shading_position("shading-0", s_home_revision, 50) == ESP_OK);
    app_loxone_home_visible(-1);
    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    home_command_poll(esp_timer_get_time());
    assert(shading_command_count == sent && !s_command_pending);
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_DOWN) == ESP_OK);
    clock_us += 2000001LL;
    home_command_poll(esp_timer_get_time());
    assert(shading_command_count == sent && !s_command_pending);

    assert(app_loxone_shading_position("shading-0", s_home_revision, 50) == ESP_OK);
    lx_home_config_t *replacement = calloc(1, sizeof(*replacement));
    assert(replacement);
    *replacement = *s_home;
    home_store_error = false;
    unsigned old_revision = s_home_revision;
    save_home_request(replacement);
    free(replacement);
    home_command_poll(esp_timer_get_time());
    assert(shading_command_count == sent && !s_command_pending);
    assert(!app_loxone_shading_summary("shading-0", old_revision, &summary));
    assert(app_loxone_shading_position("shading-0", old_revision, 50) != ESP_OK);
    assert(app_loxone_shading_command("shading-0", old_revision, LX_HOME_COMMAND_SHADING_STOP) != ESP_OK);
    lighting_poll(esp_timer_get_time());
    assert(app_loxone_shading_summary("shading-0", s_home_revision, &summary) && !summary.controllable);
    shading_initial_sent = false;
    shading_test_initial();
    assert(app_loxone_shading_position("shading-0", s_home_revision, 50) == ESP_OK);
    invalidate(true);
    assert(!s_light_context && !s_light_cache.count && !uxQueueMessagesWaiting(s_home_commands));
    s_home_live = true;
    mark_response();
    lighting_poll(esp_timer_get_time());
    home_command_poll(esp_timer_get_time());
    assert(shading_command_count == sent && !s_command_pending);
    assert(app_loxone_shading_summary("shading-0", s_home_revision, &summary) && !summary.controllable);
    shading_test_numeric(201, .45);
    shading_test_numeric(204, 0);
    shading_test_numeric(206, 2);
    assert(app_loxone_shading_position("shading-0", s_home_revision, 50) != ESP_OK);
    shading_test_text(207, "");

    /* Nincs automatikus újraküldés sem visszautasítás, sem hiányzó nyugta után. */
    reject_home_command = true;
    assert(app_loxone_shading_position("shading-0", s_home_revision, 50) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(shading_command_count == ++sent && !s_command_pending);
    home_command_poll(esp_timer_get_time());
    assert(shading_command_count == sent);
    reject_home_command = false;
    shading_drop_command_reply = true;
    assert(shading_test_command("shading-0", LX_HOME_COMMAND_SHADING_DOWN) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(shading_command_count == ++sent && !s_command_pending);
    assert(strstr(s_command_status, "nem igazolható"));
    shading_drop_command_reply = false;
    for (unsigned i = 0; i < 3; ++i)
        home_command_poll(esp_timer_get_time());
    assert(shading_command_count == sent);
    assert(app_loxone_shading_summary("shading-0", s_home_revision, &summary) && summary.position_percent == 45);

    invalidate(true);
    app_loxone_home_visible(-1);
    assert(!s_light_cache.entries && !s_light_context && !s_light_detail);
    shading_test_active = false;
    cJSON_Delete(s_catalog);
    s_catalog = original_catalog;
    free(s_home);
    s_home = NULL;
    s_session_config = NULL;
    puts("PASS: shading hidden snapshot/shared cache, automatic + central commands, explicit 0/37/100 percent, moving Stop after ACK, lock/offline gates, stale queue and popup rejection, reconnect and no retry");
}
