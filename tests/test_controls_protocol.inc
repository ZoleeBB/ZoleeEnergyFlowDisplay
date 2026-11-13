/* A valódi parancssor és titkosított MS-kérés a típusos vezérlőkkel. */
static void controls_protocol_number(lx_controls_block_t *block, unsigned state, double value)
{
    lighting_event_locked(block->states[state], true, value, NULL, 0, esp_timer_get_time());
}

static void controls_protocol_test(lx_config_t *energy)
{
    char *fixture = load("tests/fixtures/loxone-controls.json");
    cJSON *original = s_catalog;
    s_catalog = lx_catalog_parse(fixture, strlen(fixture));
    free(fixture);
    s_home = calloc(1, sizeof(*s_home));
    assert(s_home && s_catalog);
    s_home->page_count = 1;
    s_home->card_count = s_home->pages[0].card_count = 7;
    for (unsigned i = 0; i < 7; ++i)
    {
        lx_home_card_t *card = &s_home->cards[i];
        snprintf(card->id, sizeof(card->id), "control-%u", i);
        snprintf(card->control_uuid, sizeof(card->control_uuid),
                 "40000000-0000-0000-000000000000000%u", i + 1);
        card->type = (lx_home_card_type_t)(LX_HOME_CARD_DIMMER + i);
        card->allow_permanent = true;
    }
    assert(home_resolve_icons_locked());
    assert(!strcmp(s_home->cards[5].icon, "gate-double"));
    assert(!home_resolve_icons_locked());
    s_home_live = true;
    s_dead = s_home_busy = s_command_pending = false;
    s_session_config = energy;
    controls_test_active = true;
    mark_response();
    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    for (unsigned i = 0; i < 7; ++i)
    {
        lx_controls_block_t *b = s_light_context->controls_blocks[i];
        assert(b);
        lighting_event_locked(b->states[LX_CONTROL_LOCK], false, 0, "", 0, esp_timer_get_time());
        controls_protocol_number(b, LX_CONTROL_ERROR, 0);
    }
    lx_controls_block_t *dimmer = s_light_context->controls_blocks[0];
    controls_protocol_number(dimmer, LX_CONTROL_MIN, 0);
    controls_protocol_number(dimmer, LX_CONTROL_MAX, 100);
    controls_protocol_number(dimmer, LX_CONTROL_STEP, 1);
    controls_protocol_number(dimmer, LX_CONTROL_VALUE, 25);
    assert(app_loxone_controls_set("control-0", s_home_revision, 65) == ESP_OK);
    assert(dimmer->dirty);
    assert(app_loxone_controls_command("control-0", s_home_revision, LX_CONTROLS_OFF) == ESP_OK);
    assert(!dimmer->dirty);
    unsigned sent = controls_command_count;
    home_command_poll(esp_timer_get_time());
    controls_poll(esp_timer_get_time());
    assert(controls_command_count == sent + 1 && !s_command_pending && strstr(last_command, "/off"));
    assert(dimmer->values[LX_CONTROL_VALUE] == 25);
    s_home->cards[3].allow_permanent = false;
    controls_protocol_number(s_light_context->controls_blocks[3], LX_CONTROL_DELAY, -1);
    assert(app_loxone_controls_command("control-3", s_home_revision, LX_CONTROLS_ON) != ESP_OK);
    assert(app_loxone_controls_command("control-3", s_home_revision, LX_CONTROLS_OFF) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(!s_command_pending && strstr(last_command, "/off"));
    assert(app_loxone_controls_command("control-4", s_home_revision, LX_CONTROLS_ON) != ESP_OK);
    assert(app_loxone_controls_set("control-5", s_home_revision, 25) != ESP_OK);
    assert(app_loxone_controls_command("control-5", s_home_revision, LX_CONTROLS_OPEN) != ESP_OK);
    lx_controls_block_t *gate = s_light_context->controls_blocks[5];
    controls_protocol_number(gate, LX_CONTROL_PREVENT_OPEN, 0);
    controls_protocol_number(gate, LX_CONTROL_PREVENT_CLOSE, 0);
    assert(app_loxone_controls_command("control-5", s_home_revision, LX_CONTROLS_OPEN) == ESP_OK);
    /* A biztonsági delta az elküldés előtt újra érvényesül. */
    controls_protocol_number(gate, LX_CONTROL_PREVENT_OPEN, 1);
    sent = controls_command_count;
    home_command_poll(esp_timer_get_time());
    assert(controls_command_count == sent && !s_command_pending);
    assert(app_loxone_controls_command("control-5", s_home_revision, LX_CONTROLS_STOP) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(controls_command_count == sent + 1 && !s_command_pending && strstr(last_command, "/stop"));
    assert(app_loxone_controls_set("control-6", s_home_revision, 53) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(!s_command_pending && strstr(last_command, "/moveToPosition/53.000000"));
    assert(app_loxone_controls_set("control-2", s_home_revision, -100) != ESP_OK);
    assert(app_loxone_controls_set("control-2", s_home_revision, 22.5) == ESP_OK);
    sent = controls_command_count;
    app_loxone_home_visible(-1);
    app_loxone_home_visible(0);
    lighting_poll(esp_timer_get_time());
    home_command_poll(esp_timer_get_time());
    assert(controls_command_count == sent && !s_command_pending);
    assert(app_loxone_controls_set("control-2", s_home_revision + 1, 22.5) != ESP_OK);
    reject_home_command = true;
    assert(app_loxone_controls_set("control-2", s_home_revision, 22.5) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(controls_command_count == sent + 1 && !s_command_pending);
    reject_home_command = false;
    controls_drop_reply = true;
    assert(app_loxone_controls_command("control-5", s_home_revision, LX_CONTROLS_STOP) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(controls_command_count == sent + 2 && !s_command_pending);
    controls_drop_reply = false;
    for (unsigned i = 0; i < 3; ++i)
        home_command_poll(esp_timer_get_time());
    assert(controls_command_count == sent + 2);
    invalidate(true);
    app_loxone_home_visible(-1);
    controls_test_active = false;
    cJSON_Delete(s_catalog);
    s_catalog = original;
    free(s_home);
    s_home = NULL;
    s_session_config = NULL;
    puts("PASS: typed-control encrypted queue, permanent-off option, dimmer Off cancels drag, direction lock recheck, stale revision/page rejection and no ambiguous replay");
}
