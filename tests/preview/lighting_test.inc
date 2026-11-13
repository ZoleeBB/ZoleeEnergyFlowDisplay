/* A tényleges firmware-felületet járatjuk, nem külön látványtervet. */
#include "../../simulator/lighting_fixture.h"

static light_ui_row_t *lighting_test_row(unsigned index)
{
    for (unsigned i = 0; i < LIGHT_UI_ROWS; ++i)
        if (light_rows[i].bound && light_rows[i].index == index &&
            !lv_obj_has_flag(light_rows[i].root, LV_OBJ_FLAG_HIDDEN))
            return &light_rows[i];
    return NULL;
}

static void test_lighting_ui(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    app_sim_home_reset();
    app_sim_lighting_reset();
    lx_home_card_t *card = &app_sim_home_config.cards[0];
    card->type = LX_HOME_CARD_LIGHTING;
    strcpy(card->name, "Nappali világítás");
    strcpy(card->icon, "garden-light");
    ticks = 31000000;
    home_request_set(2);
    for (unsigned attempt = 0; attempt < 8 && s_view != VIEW_HOME; ++attempt)
    {
        assert(home_prepare(false));
        render_runtime(runtime, sample, true, ticks);
        ticks += 200000;
    }
    assert(s_view == VIEW_HOME && home_connected);
    lighting_card_update(0, 0, true);
    assert(!strcmp(lv_label_get_text(home_labels[0]), "TV nézés"));
    assert(lv_obj_get_height(lv_obj_get_parent(home_labels[0])) == LX_HOME_CARD_HEIGHT);
    for (unsigned i = 0; i < 3; ++i)
        assert(light_card_buttons[0][i]);
    app_sim_light_summary.label = LX_LIGHT_MANY_MOODS;
    app_sim_light_summary.active_count = 3;
    lighting_card_update(0, 0, true);
    assert(!strcmp(lv_label_get_text(home_labels[0]), "3 aktív világítási kép"));
    app_sim_light_summary.label = LX_LIGHT_CUSTOM;
    lighting_card_update(0, 0, true);
    assert(!strcmp(lv_label_get_text(home_labels[0]), "Egyedi beállítás"));
    app_sim_light_summary.label = LX_LIGHT_OFF;
    lighting_card_update(0, 0, true);
    assert(!strcmp(lv_label_get_text(home_labels[0]), "Kikapcsolva"));
    app_sim_light_summary.label = LX_LIGHT_UNKNOWN;
    lighting_card_update(0, 0, true);
    assert(!strcmp(lv_label_get_text(home_labels[0]), "Nincs adat"));
    app_sim_light_summary.label = LX_LIGHT_ONE_MOOD;
    lighting_card_update(0, 0, true);
    strcpy(app_sim_light_summary.name,
           "Egy nagyon hosszú világítási kép neve a nappali esti megvilágításához");
    lighting_card_update(0, 0, true);
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
    unsigned card_invalidations = 0;
    lv_display_add_event_cb(
        display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &card_invalidations);
    lighting_card_update(0, 0, true);
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
    assert(card_invalidations == 0);
    lv_display_remove_event_cb_with_user_data(display, count_invalidations, &card_invalidations);
    strcpy(app_sim_light_summary.name, "TV nézés");
    lighting_card_update(0, 0, true);
    save_frame("lighting-cards-day");

    const lx_home_command_t actions[] = {
        LX_HOME_COMMAND_LIGHT_PREV, LX_HOME_COMMAND_LIGHT_OFF, LX_HOME_COMMAND_LIGHT_NEXT};
    for (unsigned i = 0; i < 3; ++i)
    {
        home_command_wait = app_sim_home_pending = false;
        lv_obj_send_event(light_card_buttons[0][i], LV_EVENT_CLICKED, NULL);
        assert(app_sim_home_last_action == actions[i]);
    }
    home_command_wait = app_sim_home_pending = false;
    lv_obj_send_event(home_icons[0], LV_EVENT_CLICKED, NULL);
    assert(light_overlay && light_rows && !light_picker && !light_wheel_pixels);
    lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(light_panel) == 190 && lv_obj_get_y(light_panel) == 60);
    assert(lv_obj_get_width(light_panel) == 900 && lv_obj_get_height(light_panel) == 680);
    assert(lv_obj_get_child_count(light_content) == LIGHT_UI_ROWS);
    light_ui_row_t *row0 = lighting_test_row(0), *row1 = lighting_test_row(1),
                   *row2 = lighting_test_row(2);
    assert(row0 && row1 && row2);
    assert(lv_obj_check_type(row0->control, &lv_switch_class));
    assert(lv_obj_check_type(row1->control, &lv_slider_class));
    assert(!row2->control && row2->swatch);
    strcpy(app_sim_light_circuits[1].name,
           "Egy hosszabb világítási körnév a nappali mennyezeti részéhez");
    lighting_ui_update();
    assert(lv_label_get_long_mode(row1->name) == LV_LABEL_LONG_MODE_DOTS);
    assert(lv_obj_get_height(row1->root) == LIGHT_ROW_HEIGHT);
    lv_obj_update_layout(s_screen);
    assert(lv_obj_get_height(row1->name) == lv_font_get_line_height(APP_FONT_20));
    strcpy(app_sim_light_circuits[1].name, "Étkező\nvilágítás");
    lighting_ui_update();
    assert(!strchr(lv_label_get_text(row1->name), '\n'));
    assert(strchr(app_sim_light_circuits[1].name, '\n'));
    strcpy(app_sim_light_circuits[1].name,
           "Egy hosszabb világítási körnév a nappali mennyezeti részéhez");
    lighting_ui_update();

    unsigned sent = app_sim_light_commands;
    lv_obj_add_state(row0->control, LV_STATE_CHECKED);
    lv_obj_send_event(row0->control, LV_EVENT_VALUE_CHANGED, NULL);
    assert(app_sim_light_commands == sent + 1 && app_sim_light_last_index == 0);
    lv_obj_send_event(row1->control, LV_EVENT_PRESSED, NULL);
    lv_slider_set_value(row1->control, 73, LV_ANIM_OFF);
    lv_obj_send_event(row1->control, LV_EVENT_VALUE_CHANGED, NULL);
    assert(app_sim_light_circuits[1].actual.brightness == 73);
    app_sim_light_circuits[1].actual.brightness = 15;
    app_sim_light_circuits[0].actual.brightness = 0;
    lighting_ui_update();
    assert(lv_slider_get_value(row1->control) == 73);
    assert(!lv_obj_has_state(row0->control, LV_STATE_CHECKED));
    lv_obj_send_event(row1->control, LV_EVENT_RELEASED, NULL);
    assert(app_sim_light_circuits[1].actual.brightness == 73 && !light_touch);

    lv_obj_send_event(row2->root, LV_EVENT_CLICKED, NULL);
    lv_obj_update_layout(s_screen);
    assert(light_expanded == 2 && light_picker_row == row2 && light_wheel_pixels);
    assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(light_mode_buttons[0], 0), 0),
                       c(0xFFFFFF)));
    assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(light_mode_buttons[1], 0), 0),
                       c(light_ui_foreground())));
    assert(light_wheel_image.data_size == 192U * 192U * 2U);
    assert(!light_temperature || lv_obj_has_flag(light_temperature, LV_OBJ_FLAG_HIDDEN));
    unsigned brightness = app_sim_light_circuits[2].actual.brightness;
    lv_obj_send_event(light_mode_buttons[1], LV_EVENT_CLICKED, NULL);
    assert(app_sim_light_circuits[2].actual.mode == LX_LIGHT_MODE_WHITE);
    assert(app_sim_light_circuits[2].actual.brightness == brightness && !light_wheel_pixels);
    app_sim_light_circuits[2].actual.mode = LX_LIGHT_MODE_HSV;
    lighting_ui_update();
    assert(light_edit.mode == LX_LIGHT_MODE_HSV && light_wheel_pixels);
    lv_obj_send_event(light_brightness, LV_EVENT_PRESSED, NULL);
    app_sim_light_circuits[2].actual.mode = LX_LIGHT_MODE_WHITE;
    lighting_ui_update();
    assert(light_edit.mode == LX_LIGHT_MODE_HSV && light_wheel_pixels);
    lv_obj_send_event(light_brightness, LV_EVENT_RELEASED, NULL);
    app_sim_light_circuits[2].actual.mode = LX_LIGHT_MODE_WHITE;
    lighting_ui_update();
    assert(light_edit.mode == LX_LIGHT_MODE_WHITE && !light_wheel_pixels);
    lv_slider_set_value(light_temperature, 4483, LV_ANIM_OFF);
    lv_obj_send_event(light_temperature, LV_EVENT_VALUE_CHANGED, NULL);
    assert(app_sim_light_circuits[2].actual.kelvin == 4483);
    save_frame("lighting-white-day");
    lv_obj_send_event(light_mode_buttons[0], LV_EVENT_CLICKED, NULL);
    assert(light_wheel_pixels);
    save_frame("lighting-color-day");
    unsigned expanded_invalidations = 0;
    lv_display_add_event_cb(
        display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &expanded_invalidations);
    lighting_ui_update();
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
    assert(expanded_invalidations == 0);
    lv_display_remove_event_cb_with_user_data(
        display, count_invalidations, &expanded_invalidations);

    /* Valós inputkezelés: a tárcsa elengedése nem kapcsol másik kártyát. */
    lv_area_t wheel_area;
    lv_obj_get_coords(light_wheel, &wheel_area);
    sent = app_sim_light_commands;
    touch_sample_xy(wheel_area.x1 + 150, wheel_area.y1 + 90, true);
    touch_sample_xy(wheel_area.x1 + 160, wheel_area.y1 + 80, true);
    touch_sample_xy(wheel_area.x1 + 160, wheel_area.y1 + 80, false);
    assert(app_sim_light_commands > sent && !light_touch);
    assert(app_sim_light_circuits[2].actual.mode == LX_LIGHT_MODE_HSV);

    light_ui_row_t *row3 = lighting_test_row(3);
    assert(row3);
    lv_obj_send_event(row3->root, LV_EVENT_CLICKED, NULL);
    assert(light_expanded == 3 && light_picker_row == row3 && !light_wheel_pixels);
    assert(lv_obj_has_state(light_mode_buttons[0], LV_STATE_DISABLED));
    lv_obj_send_event(row3->root, LV_EVENT_CLICKED, NULL);
    assert(light_expanded == -1 && !light_picker && !light_wheel_pixels);
    ticks += 70000000;
    lighting_ui_update();
    assert(light_overlay); /* Nincs automatikus bezárási idő. */

    /* Stabil állapotban sem felirat, sem szín, sem csúszka nem rajzol újra. */
    save_frame("lighting-circuits-day");
    unsigned invalidations = 0;
    lv_display_add_event_cb(display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &invalidations);
    lighting_ui_update();
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
    assert(invalidations == 0);
    lv_display_remove_event_cb_with_user_data(display, count_invalidations, &invalidations);

    unsigned home_commands = app_sim_home_commands;
    touch_sample_xy(30, 200, true);
    touch_sample_xy(30, 200, false);
    assert(!light_overlay && !light_rows && !light_wheel_pixels);
    assert(app_sim_home_commands == home_commands);
    app_sim_light_count = LX_LIGHT_MAX_CIRCUITS;
    light_ui_open(0);
    assert(light_count == LX_LIGHT_MAX_CIRCUITS &&
           lv_obj_get_child_count(light_content) == LIGHT_UI_ROWS);
    lv_obj_update_layout(s_screen);
    lv_obj_scroll_to_y(light_body, 45 * LIGHT_ROW_PITCH, LV_ANIM_OFF);
    lighting_ui_update();
    assert(lighting_test_row(45) && !lighting_test_row(0));
    assert(lv_obj_get_child_count(light_content) == LIGHT_UI_ROWS);
    app_sim_home_connected = false;
    lighting_ui_update();
    assert(!light_overlay && !light_rows && !light_wheel_pixels);
    app_sim_home_connected = true;
    lighting_ui_update();
    assert(!light_overlay); /* Kapcsolat-visszatérés nem nyitja újra. */

    for (unsigned i = 0; i < 30; ++i)
    {
        light_ui_open(0);
        assert(light_overlay && light_rows);
        lv_obj_send_event(lighting_test_row(2)->root, LV_EVENT_CLICKED, NULL);
        assert(light_picker);
        lighting_ui_reset();
        assert(!light_overlay && !light_rows && !light_picker && !light_wheel_pixels);
    }
    home_night = true;
    home_build();
    app_loxone_home_visible(0);
    lighting_card_update(0, 0, true);
    light_ui_open(0);
    lv_obj_send_event(lighting_test_row(2)->root, LV_EVENT_CLICKED, NULL);
    save_frame("lighting-color-night");
    clear();
    assert(!light_overlay && !light_rows && !light_wheel_pixels);
    home_request_set(0);
    home_release();
    puts("PASS: actual lighting cards, 900x680 modal, 8 virtual rows, switches/dimmers/HSV/TW, "
         "touch, live sync, 0 idle redraws and repeated cleanup");
}
