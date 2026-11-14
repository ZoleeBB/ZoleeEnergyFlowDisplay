/* Valódi LVGL-események: külön célérték, részletes lista, tilalmak és
 * változatlan állapotnál nulla újrarajzolás. A hálózatot fixture helyettesíti. */
#include "../../simulator/controls_fixture.h"

static void controls_test_tick(const app_energy_sample_t *sample)
{
    assert(home_prepare(home_night));
    assert(home_render(sample, true));
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
}

static void controls_test_idle(const app_energy_sample_t *sample)
{
    controls_test_tick(sample);
    unsigned invalidations = 0;
    lv_display_add_event_cb(display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &invalidations);
    for (unsigned i = 0; i < 4; ++i)
        controls_test_tick(sample);
    assert(invalidations == 0);
    lv_display_remove_event_cb_with_user_data(display, count_invalidations, &invalidations);
}

static void controls_test_ack(const app_energy_sample_t *sample)
{
    app_sim_home_pending = false;
    snprintf(app_sim_home_status, sizeof(app_sim_home_status), "%s", "Vezérlési kérés elküldve.");
    controls_test_tick(sample);
}

static void test_controls_ui(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    app_sim_home_reset();
    app_sim_home_config.page_count = 1;
    app_sim_home_config.card_count = app_sim_home_config.pages[0].card_count = 7;
    const char *names[] = {"Olvasólámpa", "Összes világítás", "Hőmérsékleti érték", "Lépcsőházi időzítés",
                          "Ajtók és ablakok", "Kerti kapu", "Motoros ablak"};
    const char *icons[] = {"dimmer", "central-lighting", "slider", "timed-switch", "window-monitor", "gate-double", "motor-window"};
    for (unsigned i = 0; i < 7; ++i)
    {
        lx_home_card_t *card = &app_sim_home_config.cards[i];
        card->type = (lx_home_card_type_t)(LX_HOME_CARD_DIMMER + i);
        snprintf(card->name, sizeof(card->name), "%s", names[i]);
        snprintf(card->icon, sizeof(card->icon), "%s", icons[i]);
        card->allow_permanent = true;
    }
    app_sim_controls_summaries[2].minimum = -20;
    app_sim_controls_summaries[2].maximum = 60;
    app_sim_controls_summaries[2].step = .5;
    app_sim_controls_summaries[2].value = 21.5;
    app_sim_controls_summaries[2].decimals = 1;
    strcpy(app_sim_controls_summaries[2].unit, "°C");
    app_sim_controls_summaries[5].motion = -1;
    ticks = 31000000;
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    for (unsigned i = 0; i < 7; ++i)
    {
        assert(controls_cards[i].count >= 1 && controls_cards[i].buttons[0]);
        assert(lv_obj_get_height(lv_obj_get_parent(home_labels[i])) == LX_HOME_CARD_HEIGHT);
        assert(lv_obj_get_width(lv_obj_get_parent(home_labels[i])) == LX_HOME_CARD_WIDTH);
    }
    controls_test_idle(&sample);
    save_frame("controls-cards-day");

    /* Dimmer: húzás közben küld, a valódi értéket közben nem hamisítja. */
    lv_obj_send_event(home_icons[0], LV_EVENT_CLICKED, NULL);
    assert(controls_overlay && controls_slider && controls_decrease && controls_increase && !controls_apply);
    unsigned before = app_sim_controls_sets;
    app_sim_controls_summaries[0].step = 5;
    lv_obj_send_event(controls_increase, LV_EVENT_CLICKED, NULL);
    assert(app_sim_controls_sets == before + 1 && app_sim_controls_last_value == 50);
    assert(controls_target_value == 50 && lv_slider_get_value(controls_slider) == 5000);
    lv_obj_send_event(controls_decrease, LV_EVENT_CLICKED, NULL);
    assert(app_sim_controls_sets == before + 2 && app_sim_controls_last_value == 45);
    lv_slider_set_value(controls_slider, 10000, LV_ANIM_OFF);
    lv_obj_send_event(controls_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(controls_target_value == 100 && lv_obj_has_state(controls_increase, LV_STATE_DISABLED));
    before = app_sim_controls_sets;
    lv_obj_send_event(controls_increase, LV_EVENT_CLICKED, NULL);
    assert(app_sim_controls_sets == before);
    lv_slider_set_value(controls_slider, 0, LV_ANIM_OFF);
    lv_obj_send_event(controls_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(controls_target_value == 0 && lv_obj_has_state(controls_decrease, LV_STATE_DISABLED));
    before = app_sim_controls_sets;
    lv_obj_send_event(controls_decrease, LV_EVENT_CLICKED, NULL);
    assert(app_sim_controls_sets == before);
    app_sim_controls_summaries[0].step = 1;
    lv_slider_set_value(controls_slider, 7300, LV_ANIM_OFF);
    lv_obj_send_event(controls_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(app_sim_controls_sets == before + 1 && app_sim_controls_last_value == 73);
    assert(app_sim_controls_summaries[0].value == 45);
    app_sim_controls_summaries[0].value = 51;
    controls_test_tick(&sample);
    assert(controls_target_value == 73 && lv_slider_get_value(controls_slider) == 7300);
    assert(strstr(lv_label_get_text(controls_actual), "51"));
    controls_test_idle(&sample);
    save_frame("controls-dimmer-day");
    controls_ui_reset();

    /* Slider: a húzás és az elengedés sem küld a Beállítás gomb nélkül. */
    lv_obj_send_event(controls_cards[2].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(controls_overlay && controls_slider && controls_apply && controls_decrease && controls_increase);
    before = app_sim_controls_sets;
    lv_obj_send_event(controls_increase, LV_EVENT_CLICKED, NULL);
    assert(controls_target_value == 22 && app_sim_controls_sets == before);
    lv_obj_send_event(controls_decrease, LV_EVENT_CLICKED, NULL);
    assert(controls_target_value == 21.5 && app_sim_controls_sets == before);
    lv_slider_set_value(controls_slider, 0, LV_ANIM_OFF);
    lv_obj_send_event(controls_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(controls_target_value == -20 && lv_obj_has_state(controls_decrease, LV_STATE_DISABLED));
    lv_obj_send_event(controls_increase, LV_EVENT_CLICKED, NULL);
    assert(controls_target_value == -19.5 && app_sim_controls_sets == before);
    assert(app_sim_controls_summaries[2].value == 21.5);
    lv_slider_set_value(controls_slider, 6200, LV_ANIM_OFF);
    lv_obj_send_event(controls_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(controls_slider, LV_EVENT_RELEASED, NULL);
    assert(app_sim_controls_sets == before && controls_target_value == 29.5);
    controls_test_idle(&sample);
    save_frame("controls-slider-day");
    lv_obj_send_event(controls_apply, LV_EVENT_CLICKED, NULL);
    assert(app_sim_controls_sets == before + 1 && app_sim_controls_last_value == 29.5);
    controls_test_ack(&sample);
    app_sim_home_status[0] = 0;
    controls_test_tick(&sample);
    assert(strstr(lv_label_get_text(controls_note), "még nem ismert"));
    controls_ui_reset();

    /* Tartós be csak a popupban; letiltva is működik a Ki. */
    app_sim_home_config.cards[3].allow_permanent = false;
    controls_test_tick(&sample);
    lv_obj_send_event(home_icons[3], LV_EVENT_CLICKED, NULL);
    assert(controls_overlay && controls_permanent && lv_obj_has_flag(controls_permanent, LV_OBJ_FLAG_HIDDEN));
    assert(!controls_slider && controls_off);
    before = app_sim_home_commands;
    lv_obj_send_event(controls_permanent, LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before);
    lv_obj_send_event(controls_off, LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before + 1 && app_sim_home_last_action == LX_HOME_COMMAND_CONTROL_OFF);
    controls_test_ack(&sample);
    save_frame("controls-timer-disabled-day");
    controls_ui_reset();
    app_sim_home_config.cards[3].allow_permanent = true;
    controls_test_tick(&sample);
    lv_obj_send_event(home_icons[3], LV_EVENT_CLICKED, NULL);
    assert(controls_permanent && !lv_obj_has_flag(controls_permanent, LV_OBJ_FLAG_HIDDEN));
    save_frame("controls-timer-day");
    controls_ui_reset();

    /* A teljes lista lapozható, az oldalszám nem rögzíti nyolc kedvencre/sorra. */
    lv_obj_send_event(controls_cards[4].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(controls_overlay && controls_rows[0] && controls_next);
    const uint16_t expected[] = {2, 4, 8, 10, 1, 7, 13, 5, 11, 0, 3, 6, 9, 12};
    assert(!memcmp(controls_window_order, expected, sizeof(expected)));
    assert(controls_list_start == 0 && strstr(lv_label_get_text(controls_rows[0]), "Ablak 3"));
    unsigned orders = app_sim_controls_orders;
    controls_test_idle(&sample);
    assert(app_sim_controls_orders == orders);
    app_sim_controls_windows[0].state = 4;
    ++app_sim_controls_summaries[4].list_revision;
    controls_test_tick(&sample);
    assert(app_sim_controls_orders == orders + 1 && controls_window_order[0] == 0);
    assert(strstr(lv_label_get_text(controls_rows[0]), "Ablak 1"));
    app_sim_controls_windows[0].state = 1;
    ++app_sim_controls_summaries[4].list_revision;
    controls_test_tick(&sample);
    assert(!memcmp(controls_window_order, expected, sizeof(expected)));
    assert(!strcmp(app_sim_controls_windows[0].name, "Ablak 1") && app_sim_controls_windows[0].state == 1);
    assert(app_sim_controls_summaries[4].counts[0] == 2 && app_sim_controls_summaries[4].counts[1] == 7);
    save_frame("controls-monitor-day");
    lv_obj_send_event(controls_next, LV_EVENT_CLICKED, NULL);
    assert(controls_list_start == 6 && strstr(lv_label_get_text(controls_rows[0]), "Ablak 14"));
    lv_obj_send_event(controls_next, LV_EVENT_CLICKED, NULL);
    assert(controls_list_start == 12 && strstr(lv_label_get_text(controls_rows[0]), "Ablak 10"));
    assert(lv_obj_has_state(controls_next, LV_STATE_DISABLED));
    assert(lv_obj_has_flag(controls_rows[2], LV_OBJ_FLAG_HIDDEN));
    controls_test_idle(&sample);
    save_frame("controls-monitor-last-page");
    lx_controls_window_t saved_row = app_sim_controls_windows[12];
    memset(app_sim_controls_windows[12].name, 'W', sizeof(app_sim_controls_windows[12].name) - 1);
    memset(app_sim_controls_windows[12].room, 'W', sizeof(app_sim_controls_windows[12].room) - 1);
    memset(app_sim_controls_windows[12].place, 'W', sizeof(app_sim_controls_windows[12].place) - 1);
    controls_test_idle(&sample); /* DOTS által rövidített sor sem írható újra körönként. */
    app_sim_controls_windows[12] = saved_row;
    controls_ui_reset();

    /* Az irányzár külön érvényesül, Stop a két tiltástól független. */
    app_sim_controls_summaries[5].open_allowed = false;
    controls_test_tick(&sample);
    assert(lv_obj_has_state(controls_cards[5].buttons[0], LV_STATE_DISABLED));
    assert(!lv_obj_has_state(controls_cards[5].buttons[1], LV_STATE_DISABLED));
    before = app_sim_home_commands;
    lv_obj_send_event(controls_cards[5].buttons[0], LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before);
    lv_obj_send_event(controls_cards[5].buttons[1], LV_EVENT_CLICKED, NULL);
    assert(app_sim_home_commands == before + 1 && app_sim_home_last_action == LX_HOME_COMMAND_CONTROL_STOP);
    controls_test_ack(&sample);

    lv_obj_send_event(home_icons[6], LV_EVENT_CLICKED, NULL);
    assert(controls_overlay && controls_slider && controls_apply && controls_off);
    lv_slider_set_value(controls_slider, 2700, LV_ANIM_OFF);
    lv_obj_send_event(controls_slider, LV_EVENT_VALUE_CHANGED, NULL);
    before = app_sim_controls_sets;
    app_sim_controls_summaries[6].close_allowed = false;
    controls_test_tick(&sample);
    assert(lv_obj_has_state(controls_apply, LV_STATE_DISABLED));
    lv_obj_send_event(controls_apply, LV_EVENT_CLICKED, NULL);
    assert(app_sim_controls_sets == before);
    app_sim_controls_summaries[6].close_allowed = true;
    controls_test_tick(&sample);
    assert(!lv_obj_has_state(controls_apply, LV_STATE_DISABLED));
    lv_obj_send_event(controls_apply, LV_EVENT_CLICKED, NULL);
    assert(app_sim_controls_sets == before + 1 && app_sim_controls_last_value == 27);
    controls_test_ack(&sample);
    assert(app_sim_controls_summaries[6].value == 45);
    save_frame("controls-window-day");
    memset(app_sim_controls_summaries[6].reason, 'W', sizeof(app_sim_controls_summaries[6].reason) - 1);
    controls_test_idle(&sample);
    app_sim_controls_summaries[6].reason[0] = 0;
    controls_ui_reset();

    /* A régi ablak eseménye és a megszakadt kapcsolat nem vezérelhet. */
    controls_ui_open(2);
    unsigned old_generation = controls_generation;
    controls_ui_reset();
    controls_ui_open(2);
    lv_obj_t *stale = lv_obj_create(s_screen);
    lv_obj_add_event_cb(stale, controls_modal_command, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)((old_generation << 4) | LX_CONTROLS_SET));
    lv_obj_add_event_cb(stale, controls_step_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)((old_generation << 4) | 1U));
    before = app_sim_controls_sets;
    lv_obj_send_event(stale, LV_EVENT_CLICKED, NULL);
    assert(app_sim_controls_sets == before && controls_overlay);
    lv_obj_delete(stale);
    app_sim_home_connected = false;
    controls_test_tick(&sample);
    assert(!controls_overlay);
    app_sim_home_connected = true;
    controls_test_tick(&sample);
    irc_test_open(true, runtime, sample);
    controls_test_idle(&sample);
    save_frame("controls-cards-night");
    home_request_set(0);
    assert(home_prepare(true));

    /* Kép nélküli régi csomagból is elérhető minden részletes kezelő. */
    home_missing_card_icons = true;
    home_request_set(2);
    irc_test_open(false, runtime, sample);
    assert(home_assets.asset_count == 2);
    const unsigned popup_cards[] = {0, 2, 3, 4, 6};
    for (unsigned i = 0; i < sizeof(popup_cards) / sizeof(popup_cards[0]); ++i)
    {
        lv_obj_t *target = home_icons[popup_cards[i]];
        assert(lv_obj_get_width(target) == 96 && lv_obj_get_height(target) >= 80);
        lv_obj_send_event(target, LV_EVENT_CLICKED, NULL);
        assert(controls_overlay);
        controls_ui_reset();
    }
    save_frame("controls-cards-legacy-pack");
    home_request_set(0);
    assert(home_prepare(false));
    home_missing_card_icons = false;
    puts("PASS: typed controls real LVGL events, precise +/- steps, target/actual separation, directional locks, popup guards, stable monitor priority index and full pagination, old-pack detail fallback and zero idle invalidations");
}
