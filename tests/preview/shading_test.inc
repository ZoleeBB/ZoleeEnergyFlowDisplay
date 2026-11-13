/* A valódi LVGL-kártyát és a pozíciókezelőt járatjuk mindkét pixelszélességgel. */
#include "../../simulator/shading_fixture.h"

static void shading_test_update(const app_energy_sample_t *sample)
{
    assert(home_prepare(home_night));
    assert(home_render(sample, true));
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
}

static void shading_test_ack(const app_energy_sample_t *sample, const char *status)
{
    app_sim_home_pending = false;
    snprintf(app_sim_home_status, sizeof(app_sim_home_status), "%s", status);
    shading_test_update(sample);
}

static void shading_test_idle(const app_energy_sample_t *sample)
{
    shading_test_update(sample);
    unsigned invalidations = 0;
    lv_display_add_event_cb(display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &invalidations);
    for (unsigned iteration = 0; iteration < 4; ++iteration)
        shading_test_update(sample);
    assert(invalidations == 0);
    lv_display_remove_event_cb_with_user_data(display, count_invalidations, &invalidations);
}

static void shading_test_configure(void)
{
    app_sim_home_reset();
    const char *names[] = {"Nappali redőny", "Összes árnyékoló", "Hálószoba",
                          "Dolgozószoba", "Vendégszoba", "Terasz árnyékoló"};
    strcpy(app_sim_home_config.pages[0].title, "Árnyékolás és otthon");
    for (unsigned i = 0; i < 6; ++i)
    {
        lx_home_card_t *card = &app_sim_home_config.cards[i];
        card->type = LX_HOME_CARD_SHADING;
        strcpy(card->name, names[i]);
        /* Importált, hibás ikonazonosító sem változtathatja meg a fix grafikát. */
        strcpy(card->icon, "not-an-sd-icon");
    }
    app_sim_shading_summaries[0].moving_up = true;
    app_sim_shading_summaries[1].central = true;
    app_sim_shading_summaries[1].position_valid = false;
    app_sim_shading_summaries[2].position_percent = 100;
    app_sim_shading_summaries[3].position_percent = 15;
    app_sim_shading_summaries[3].moving_down = true;
    app_sim_shading_summaries[4].position_valid = false;
    app_sim_shading_summaries[4].motion_known = false;
    app_sim_shading_summaries[5].position_percent = 0;
    app_sim_shading_summaries[5].safety_active = true;
    lx_home_card_t *irc = &app_sim_home_config.cards[6];
    irc->type = LX_HOME_CARD_IRC;
    strcpy(irc->name, "Nappali termosztát");
    strcpy(irc->icon, "not-an-sd-icon");
    lx_home_card_t *light = &app_sim_home_config.cards[7];
    light->type = LX_HOME_CARD_LIGHTING;
    strcpy(light->name, "Nappali világítás");
    strcpy(light->icon, "garden-light");
}

static void shading_test_card_commands(const app_energy_sample_t *sample)
{
    const lx_home_command_t actions[] = {LX_HOME_COMMAND_SHADING_UP,
        LX_HOME_COMMAND_SHADING_STOP, LX_HOME_COMMAND_SHADING_DOWN};
    for (unsigned card = 0; card < 2; ++card)
        for (unsigned button = 0; button < 3; ++button)
        {
            unsigned before = app_sim_home_commands;
            double position = app_sim_shading_summaries[card].position_percent;
            lv_obj_send_event(shading_cards[card].buttons[button], LV_EVENT_CLICKED, NULL);
            assert(app_sim_home_commands == before + 1);
            assert(app_sim_home_last_action == actions[button]);
            assert(!strcmp(app_sim_home_last_id, home_config.cards[card].id));
            assert(app_sim_shading_summaries[card].position_percent == position);
            shading_test_update(sample);
            assert(lv_obj_has_state(shading_cards[card].buttons[1], LV_STATE_DISABLED));
            lv_obj_send_event(shading_cards[card].buttons[1], LV_EVENT_CLICKED, NULL);
            assert(app_sim_home_commands == before + 1);
            shading_test_ack(sample, "Redőnyparancs elküldve.");
            assert(!lv_obj_has_state(shading_cards[card].buttons[1], LV_STATE_DISABLED));
        }
    /* A központi blokk egy esetleges téves pozícióértéket sem rajzol ki. */
    app_sim_shading_summaries[1].position_valid = true;
    shading_test_update(sample);
    lv_obj_send_event(home_icons[1], LV_EVENT_CLICKED, NULL);
    assert(!shading_overlay && !strchr(lv_label_get_text(home_labels[1]), '%'));
    app_sim_shading_summaries[1].position_valid = false;

    unsigned before = app_sim_home_commands;
    app_sim_shading_summaries[0].controllable = false;
    shading_test_update(sample);
    assert(lv_obj_has_state(shading_cards[0].buttons[0], LV_STATE_DISABLED));
    lv_obj_send_event(shading_cards[0].buttons[0], LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(home_icons[0], LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before && !shading_overlay);
    app_sim_shading_summaries[0].controllable = true;
    app_sim_shading_summaries[0].available = false;
    shading_test_update(sample);
    assert(!strcmp(lv_label_get_text(home_labels[0]), "Nincs adat"));
    lv_obj_send_event(shading_cards[0].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before);
    app_sim_shading_summaries[0].available = true;
    /* Hiányzó pozíció mellett is használható a három iránygomb, popup nincs. */
    assert(!lv_obj_has_state(shading_cards[4].buttons[0], LV_STATE_DISABLED));
    lv_obj_send_event(home_icons[4], LV_EVENT_CLICKED, NULL);
    assert(!shading_overlay);
    lv_obj_send_event(shading_cards[4].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before + 1);
    shading_test_ack(sample, "Redőnyparancs elküldve.");
    /* A védelmi információ önmagában nem jogosultságtiltás. */
    assert(!lv_obj_has_flag(shading_cards[5].safety, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_state(shading_cards[5].buttons[1], LV_STATE_DISABLED));
}

static void shading_test_position(const app_energy_sample_t *sample)
{
    lv_obj_send_event(home_icons[0], LV_EVENT_CLICKED, NULL);
    assert(shading_overlay && shading_slider && !light_overlay);
    assert(shading_target_percent == 45 && lv_slider_get_value(shading_slider) == 45);
    assert(!strcmp(lv_label_get_text(shading_actual), "Aktuális zártság: 45%"));
    lv_obj_update_layout(s_screen);
    assert(lv_obj_get_width(shading_panel) == 760 && lv_obj_get_height(shading_panel) == 460);
    unsigned before = app_sim_home_commands, positions = app_sim_shading_positions;
    const unsigned targets[] = {0, 1, 73, 99, 100};
    for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
    {
        lv_slider_set_value(shading_slider, targets[i], LV_ANIM_OFF);
        lv_obj_send_event(shading_slider, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_send_event(shading_slider, LV_EVENT_RELEASED, NULL);
        assert(shading_target_percent == targets[i]);
        assert(app_sim_home_commands == before && app_sim_shading_positions == positions);
    }
    /* A valós érintési útvonalon a húzás és elengedés sem küld hálózati kérést. */
    lv_area_t area;
    lv_obj_get_coords(shading_slider, &area);
    touch_sample_xy(area.x1 + 80, (area.y1 + area.y2) / 2, true);
    touch_sample_xy(area.x2 - 80, (area.y1 + area.y2) / 2, true);
    touch_sample_xy(area.x2 - 80, (area.y1 + area.y2) / 2, false);
    assert(shading_overlay && app_sim_home_commands == before);
    lv_slider_set_value(shading_slider, 73, LV_ANIM_OFF);
    lv_obj_send_event(shading_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(shading_target_percent == 73);
    app_sim_shading_summaries[0].position_percent = 38;
    shading_test_update(sample);
    assert(lv_slider_get_value(shading_slider) == 73 && shading_target_percent == 73);
    assert(!strcmp(lv_label_get_text(shading_actual), "Aktuális zártság: 38%"));
    assert(!strcmp(lv_label_get_text(shading_target), "Célérték: 73%"));
    shading_test_idle(sample);
    save_frame("shading-position-day");

    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before + 1 && app_sim_shading_positions == positions + 1);
    assert(app_sim_home_last_action == LX_HOME_COMMAND_SHADING_POSITION && app_sim_shading_last_percent == 73);
    assert(app_sim_shading_summaries[0].position_percent == 38);
    assert(!strcmp(lv_label_get_text(shading_status), "Kérés küldése..."));
    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before + 1);
    shading_test_ack(sample, "Redőnyparancs elküldve.");
    assert(!strcmp(lv_label_get_text(shading_status), "Redőnyparancs elküldve."));
    assert(!lv_obj_has_state(shading_stop, LV_STATE_DISABLED));
    assert(app_sim_shading_summaries[0].moving_up);
    lv_obj_send_event(shading_stop, LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before + 2 && app_sim_home_last_action == LX_HOME_COMMAND_SHADING_STOP);
    assert(app_sim_shading_summaries[0].moving_up && app_sim_shading_summaries[0].position_percent == 38);
    /* A worker hibás/hiányzó ACK-ja nem maradhat sikeres küldésnek feliratozva. */
    shading_test_ack(sample, "A művelet nem igazolható. Ellenőrizd a Loxone állapotát.");
    assert(!strcmp(lv_label_get_text(shading_status), app_sim_home_status));
    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    shading_test_ack(sample, "HTTP 403: a Loxone elutasította a kérést.");
    assert(!strcmp(lv_label_get_text(shading_status), app_sim_home_status));
    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    shading_test_ack(sample, "");
    assert(!strcmp(lv_label_get_text(shading_status), "A kérés eredménye még nem ismert."));
    shading_test_idle(sample);

    /* A feloldás után is a tényleges állapotból rajzolunk; a tervezet megmarad. */
    app_sim_shading_summaries[0].moving_up = false;
    app_sim_shading_summaries[0].position_percent = 32;
    shading_test_update(sample);
    assert(!strcmp(lv_label_get_text(shading_actual), "Aktuális zártság: 32%"));
    assert(!strcmp(lv_label_get_text(shading_motion), "Áll"));
    assert(shading_target_percent == 73);
    app_sim_shading_summaries[0].controllable = false;
    shading_test_update(sample);
    before = app_sim_home_commands;
    assert(lv_obj_has_state(shading_apply, LV_STATE_DISABLED));
    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(shading_stop, LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before);
    app_sim_shading_summaries[0].controllable = true;
    shading_test_update(sample);
    /* Az ablakon kívül, egy alatta lévő gomb helyén érintve csak bezárás történik. */
    lv_obj_get_coords(shading_cards[0].buttons[0], &area);
    touch_sample_xy((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2, true);
    touch_sample_xy((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2, false);
    assert(!shading_overlay && app_sim_home_commands == before);
}

static void shading_test_guards(const app_energy_sample_t *sample)
{
    unsigned before = app_sim_home_commands;
    assert(app_loxone_shading_command(home_config.cards[0].id, home_revision + 1,
                                     LX_HOME_COMMAND_SHADING_UP) == ESP_ERR_INVALID_STATE);
    assert(app_loxone_shading_position(home_config.cards[0].id, home_revision + 1, 50) == ESP_ERR_INVALID_STATE);
    assert(app_sim_home_commands == before);
    shading_ui_open(0);
    assert(shading_overlay);
    unsigned old_generation = shading_generation;
    lv_obj_t *stale_event = lv_obj_create(s_screen);
    lv_obj_add_event_cb(stale_event, shading_apply_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)old_generation);
    shading_ui_reset();
    shading_ui_open(0);
    lv_obj_send_event(stale_event, LV_EVENT_CLICKED, NULL);
    assert(shading_overlay && app_sim_home_commands == before);
    lv_obj_delete(stale_event);
    ++home_revision;
    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    assert(!shading_overlay && app_sim_home_commands == before);
    --home_revision;
    shading_ui_open(0);
    home_config.cards[0].id[0] = 'X';
    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    assert(!shading_overlay && app_sim_home_commands == before);
    shading_test_update(sample);
    shading_ui_open(0);
    home_request_set(3);
    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    assert(!shading_overlay && app_sim_home_commands == before);
    home_request_set(2);
    shading_ui_open(0);
    app_sim_shading_summaries[0].position_valid = false;
    shading_ui_update();
    assert(!shading_overlay);
    app_sim_shading_summaries[0].position_valid = true;
    shading_ui_open(0);
    app_sim_home_connected = false;
    shading_ui_update();
    assert(!shading_overlay);
    shading_test_update(sample);
    lv_obj_send_event(shading_cards[0].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before);
    app_sim_home_connected = true;
    shading_test_update(sample);
    assert(!shading_overlay);

    /* A másik modal és a háttérkártyák nem fogadhatnak el átkattintást. */
    shading_ui_open(0);
    light_ui_open(7);
    assert(shading_overlay && !light_overlay);
    lv_obj_send_event(irc_cards[6].buttons[0], LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(light_card_buttons[7][0], LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(shading_cards[1].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before);
    shading_ui_reset();
    light_ui_open(7);
    assert(light_overlay);
    shading_ui_open(0);
    lv_obj_send_event(shading_cards[0].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(!shading_overlay && app_sim_home_commands == before);
    lighting_ui_reset();

    /* A runtime ugyanazzal a visszavonási kapuval ad elsőbbséget a kaputelefonnak. */
    shading_ui_open(0);
    app_sim_intercom = (app_intercom_snapshot_t){.ringing = true, .connected = true, .revision = 900};
    assert(intercom_prepare());
    home_controls_suspend();
    intercom_render();
    assert(intercom_overlay && !shading_overlay && !light_overlay);
    assert(app_loxone_shading_command(home_config.cards[0].id, home_revision,
                                     LX_HOME_COMMAND_SHADING_UP) == ESP_ERR_INVALID_STATE);
    app_sim_intercom.ringing = false;
    assert(!intercom_prepare());
    intercom_render();
    assert(!intercom_overlay);
    shading_test_update(sample);

    shading_ui_open(0);
    loxone_display_set_held(true);
    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    assert(!shading_overlay && app_sim_home_commands == before);
    lv_obj_send_event(shading_cards[0].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before);
    loxone_display_set_held(false);
    shading_ui_open(0);
    s_pair_pad = lv_obj_create(s_screen);
    lv_obj_send_event(shading_apply, LV_EVENT_CLICKED, NULL);
    assert(!shading_overlay && app_sim_home_commands == before);
    lv_obj_delete(s_pair_pad);
    s_pair_pad = NULL;
    for (unsigned i = 0; i < 30; ++i)
    {
        shading_ui_open(0);
        assert(shading_overlay);
        lv_obj_send_event(lv_obj_get_child(shading_panel, 1), LV_EVENT_CLICKED, NULL);
        assert(!shading_overlay && !shading_slider && !shading_apply && !shading_stop);
    }
    assert(app_sim_home_commands == before);
}

static void test_shading_ui(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    shading_test_configure();
    ticks = 31000000;
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    assert(home_assets.asset_count == 3 && home_shading_buffer && home_irc_buffer);
    assert(home_shading_buffer->data_size == 96U * 96U * 4U);
    assert(home_shading_image.header.w == 96 && home_shading_image.header.h == 96);
    for (unsigned i = 0; i < 6; ++i)
    {
        assert(lv_image_get_src(home_icons[i]) == &home_shading_image);
        assert(lv_obj_get_height(lv_obj_get_parent(home_labels[i])) == LX_HOME_CARD_HEIGHT);
        assert(lv_obj_get_width(lv_obj_get_parent(home_labels[i])) == LX_HOME_CARD_WIDTH);
        assert(!home_buttons[i] && !light_card_buttons[i][0] && !irc_cards[i].buttons[0]);
        for (unsigned button = 0; button < 3; ++button)
            assert(shading_cards[i].buttons[button]);
    }
    assert(!strcmp(lv_label_get_text(home_labels[0]), "45% zárva | Felfelé"));
    assert(!strcmp(lv_label_get_text(home_labels[1]), "Csoportos vezérlés"));
    assert(!strcmp(lv_label_get_text(home_labels[2]), "100% zárva | Áll"));
    assert(!strcmp(lv_label_get_text(home_labels[3]), "15% zárva | Lefelé"));
    assert(!strcmp(lv_label_get_text(home_labels[4]), "Zártság: nincs adat"));
    assert(!lv_obj_has_flag(home_icons[1], LV_OBJ_FLAG_CLICKABLE));
    shading_test_idle(&sample);
    save_frame("shading-cards-day");
    shading_test_card_commands(&sample);
    shading_test_position(&sample);
    shading_test_guards(&sample);

    app_sim_shading_summaries[0].position_percent = 45;
    app_sim_shading_summaries[0].moving_up = true;
    app_sim_home_status[0] = 0;
    irc_test_open(true, runtime, sample);
    shading_test_idle(&sample);
    save_frame("shading-cards-night");
    shading_ui_open(0);
    assert(shading_overlay);
    shading_test_idle(&sample);
    /* Lapváltás a megosztott képet és a megnyitott ablakot is elengedi. */
    home_request_set(3);
    irc_test_open(true, runtime, sample);
    assert(!shading_overlay && !home_shading_buffer && !home_irc_buffer);
    home_request_set(2);
    irc_test_open(true, runtime, sample);
    assert(home_shading_buffer && home_irc_buffer);
    shading_ui_open(0);
    clear();
    assert(!shading_overlay && !shading_slider && !shading_apply && !shading_stop);
    home_request_set(0);
    assert(home_prepare(true));
    assert(!home_shading_buffer && !home_irc_buffer);

    /* Tizenkét kártya ugyanazt a képpuffert használja, SD-s egyedi ikon nélkül. */
    app_sim_home_config.page_count = 1;
    app_sim_home_config.card_count = app_sim_home_config.pages[0].card_count = LX_HOME_MAX_CARDS;
    strcpy(app_sim_home_config.pages[0].title, "A ház árnyékolói");
    for (unsigned i = 0; i < LX_HOME_MAX_CARDS; ++i)
    {
        lx_home_card_t *card = &app_sim_home_config.cards[i];
        snprintf(card->id, sizeof(card->id), "shading-%u", i);
        snprintf(card->name, sizeof(card->name), "Helyiség %u", i + 1);
        strcpy(card->icon, "not-an-sd-icon");
        card->type = LX_HOME_CARD_SHADING;
        app_sim_shading_summaries[i].position_percent = (i * 17) % 101;
    }
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    assert(home_assets.asset_count == 2 && home_shading_buffer && !home_irc_buffer);
    assert(home_shading_buffer->data_size == 36864);
    for (unsigned i = 0; i < LX_HOME_MAX_CARDS; ++i)
        assert(lv_image_get_src(home_icons[i]) == &home_shading_image);
    shading_test_idle(&sample);
    save_frame("shading-cards-twelve");
    home_request_set(0);
    assert(home_prepare(false));
    assert(!home_shading_buffer && !home_irc_buffer && !shading_overlay);
    puts("PASS: shading actual UI Up/Stop/Down, central/no-data, 1% draft with explicit Apply, ACK errors, "
         "no invented feedback, stale/lock/modal/touch gates, zero idle invalidations and shared 12-card icon cleanup");
}
