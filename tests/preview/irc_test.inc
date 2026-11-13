/* A valódi IRC-kártya renderje és eseménykezelése a közös LVGL-környezetben. */
#include "../../simulator/irc_fixture.h"

static void irc_test_open(bool night, app_runtime_state_t runtime, app_energy_sample_t sample)
{
    sample.night_mode = night;
    for (unsigned attempt = 0; attempt < 12; ++attempt)
    {
        assert(home_prepare(night));
        render_runtime(runtime, sample, true, ticks);
        ticks += 200000;
        if (s_view == VIEW_HOME && home_night == night)
            break;
    }
    assert(s_view == VIEW_HOME && home_ready_to_draw && home_night == night);
}

static void test_irc_ui(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    app_sim_home_reset();
    const char *names[] = {"Nappali", "Hálószoba", "Dolgozószoba"};
    for (unsigned i = 0; i < 3; ++i)
    {
        lx_home_card_t *card = &app_sim_home_config.cards[i];
        card->type = LX_HOME_CARD_IRC;
        strcpy(card->name, names[i]);
        /* A renderer régi/importált téves ikonnál is a kötött grafikát használja. */
        strcpy(card->icon, "does-not-exist");
    }
    app_sim_irc_summaries[1].target_c = 24;
    app_sim_irc_summaries[1].actual_c = 25.3;
    app_sim_irc_summaries[1].direction = LX_IRC_DIRECTION_COOLING;
    app_sim_irc_summaries[2].direction = LX_IRC_DIRECTION_UNKNOWN;
    ticks = 31000000;
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    irc_card_update(0, 0, true);
    assert(!strcmp(lv_label_get_text(irc_cards[0].target), "22,5°C"));
    assert(!strcmp(lv_label_get_text(home_labels[0]), "Szoba: 21,8 °C"));
    assert(!lv_obj_has_flag(irc_cards[0].heat, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(irc_cards[0].cool, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(irc_cards[1].heat, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(irc_cards[1].cool, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(irc_cards[2].heat, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(irc_cards[2].cool, LV_OBJ_FLAG_HIDDEN));
    assert(home_irc_buffer && home_irc_image.header.w == 96 && home_irc_image.header.h == 96);
    assert(home_irc_buffer->data_size <= 96U * 96U * 4U);
    for (unsigned i = 0; i < 3; ++i)
    {
        assert(lv_image_get_src(home_icons[i]) == &home_irc_image);
        assert(!lv_obj_has_flag(home_icons[i], LV_OBJ_FLAG_CLICKABLE));
        assert(lv_obj_get_height(lv_obj_get_parent(home_labels[i])) == LX_HOME_CARD_HEIGHT);
        assert(irc_cards[i].buttons[0] && irc_cards[i].buttons[1]);
        assert(!light_card_buttons[i][0] && !home_buttons[i]);
    }
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
    unsigned invalidations = 0;
    lv_display_add_event_cb(display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &invalidations);
    for (unsigned iteration = 0; iteration < 4; ++iteration)
    {
        for (unsigned i = 0; i < 3; ++i)
            irc_card_update(i, i, true);
        lv_obj_update_layout(s_screen);
        lv_refr_now(display);
    }
    assert(invalidations == 0);
    lv_display_remove_event_cb_with_user_data(display, count_invalidations, &invalidations);
    save_frame("irc-cards-day");

    /* Egy érintés egy natív lépés; a célérték csak külön állapoteseményre változik. */
    const lx_home_command_t actions[] = {LX_HOME_COMMAND_IRC_DECREASE, LX_HOME_COMMAND_IRC_INCREASE};
    for (unsigned i = 0; i < 2; ++i)
    {
        home_command_wait = app_sim_home_pending = false;
        unsigned before = app_sim_home_commands;
        lv_obj_send_event(irc_cards[0].buttons[i], LV_EVENT_CLICKED, NULL);
        assert(app_sim_home_commands == before + 1 && app_sim_home_last_action == actions[i]);
        assert(!strcmp(lv_label_get_text(irc_cards[0].target), "22,5°C"));
    }
    home_command_wait = app_sim_home_pending = false;
    app_sim_irc_summaries[0].target_c = 23;
    irc_card_update(0, 0, true);
    assert(!strcmp(lv_label_get_text(irc_cards[0].target), "23,0°C"));
    app_sim_irc_summaries[0].controllable = false;
    irc_card_update(0, 0, true);
    unsigned before = app_sim_home_commands;
    assert(lv_obj_has_state(irc_cards[0].buttons[0], LV_STATE_DISABLED));
    lv_obj_send_event(irc_cards[0].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before);
    app_sim_irc_summaries[0].target_valid = false;
    app_sim_irc_summaries[0].actual_valid = false;
    app_sim_irc_summaries[0].direction = LX_IRC_DIRECTION_UNKNOWN;
    irc_card_update(0, 0, true);
    assert(!strcmp(lv_label_get_text(irc_cards[0].target), "--,-°C"));
    assert(!strcmp(lv_label_get_text(home_labels[0]), "Szoba: nincs adat"));
    assert(lv_obj_has_flag(irc_cards[0].heat, LV_OBJ_FLAG_HIDDEN));
    save_frame("irc-cards-unavailable");

    app_sim_irc_reset();
    app_sim_irc_summaries[1].target_c = 24;
    app_sim_irc_summaries[1].actual_c = 25.3;
    app_sim_irc_summaries[1].direction = LX_IRC_DIRECTION_COOLING;
    app_sim_irc_summaries[2].direction = LX_IRC_DIRECTION_UNKNOWN;
    irc_test_open(true, runtime, sample);
    save_frame("irc-cards-night");

    /* A legnagyobb oldalon is csak egy dekódolt termosztátkép él. */
    home_request_set(0);
    assert(home_prepare(true));
    assert(!home_irc_buffer);
    app_sim_home_config.page_count = 1;
    app_sim_home_config.card_count = app_sim_home_config.pages[0].card_count = LX_HOME_MAX_CARDS;
    for (unsigned i = 0; i < LX_HOME_MAX_CARDS; ++i)
    {
        lx_home_card_t *card = &app_sim_home_config.cards[i];
        snprintf(card->id, sizeof(card->id), "irc-%u", i);
        snprintf(card->name, sizeof(card->name), "Helyiség %u", i + 1);
        strcpy(card->icon, "not-an-sd-icon");
        card->type = LX_HOME_CARD_IRC;
    }
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    assert(home_assets.asset_count == 2);
    for (unsigned i = 0; i < LX_HOME_MAX_CARDS; ++i)
        assert(lv_image_get_src(home_icons[i]) == &home_irc_image);
    save_frame("irc-cards-twelve");
    home_request_set(0);
    assert(home_prepare(false));
    assert(!home_irc_buffer);
    puts("PASS: IRC real UI +/- only, fixed shared icon, external feedback, lock/no-data gates, zero idle invalidations and 12-card page");
}
