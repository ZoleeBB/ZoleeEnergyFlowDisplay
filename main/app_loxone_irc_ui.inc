/* Az Otthon-felület kompakt IRC-kártyája. A számokat a Miniserver adja;
 * gombnyomásra csak natív relatív parancs megy, helyi célértéket nem találunk ki. */
typedef struct
{
    lv_obj_t *target, *heat, *cool, *buttons[2];
} irc_ui_card_t;

static TEMPLATE_RAM irc_ui_card_t irc_cards[LX_HOME_MAX_CARDS];

static lv_obj_t *irc_mode_container(lv_obj_t *card)
{
    lv_obj_t *object = lv_obj_create(card);
    lv_obj_remove_style_all(object);
    /* A jel közepe az ikon LCD-je alatti, 64–80. sor közötti keretrészre esik. */
    lv_obj_set_pos(object, 139, 68);
    lv_obj_set_size(object, 14, 14);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_HIDDEN);
    return object;
}

static void irc_mode_line(lv_obj_t *parent, const lv_point_precise_t *points,
                          unsigned count, uint32_t color)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, count);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, c(color), 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
}

static void irc_card_command_event(lv_event_t *event)
{
    unsigned encoded = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    unsigned index = encoded / 2, operation = encoded % 2;
    if (home_modal_is_open() || s_pair_pad || loxone_display_is_held() ||
        !home_connected || home_command_wait ||
        home_request_get() != 2 + (int)home_card_page || index >= home_config.card_count ||
        home_config.cards[index].type != LX_HOME_CARD_IRC)
        return;
    lx_irc_summary_t summary;
    if (!app_loxone_irc_summary(home_config.cards[index].id, home_revision, &summary) ||
        !summary.controllable)
        return;
    lx_home_command_t action = operation ? LX_HOME_COMMAND_IRC_INCREASE :
                                          LX_HOME_COMMAND_IRC_DECREASE;
    if (app_loxone_home_command(home_config.cards[index].id, action) == ESP_OK)
    {
        home_command_wait = true;
        snprintf(home_status, sizeof(home_status), "%s", tr("Kérés küldése...", "Sending request..."));
    }
    lv_event_stop_bubbling(event);
}

static void irc_card_build(lv_obj_t *card, lv_obj_t *title, unsigned slot, unsigned index)
{
    irc_ui_card_t *ui = &irc_cards[slot];
    /* A mentett ikonazonosító sem írhatja felül a kötött kijelzőgeometriát. */
    int icon = home_icon_index("thermostat-irc");
    if (icon >= 0)
        lv_image_set_src(home_icons[slot], &home_images[icon]);
    lv_obj_set_pos(home_icons[slot], 98, 3);
    lv_obj_remove_flag(home_icons[slot], LV_OBJ_FLAG_CLICKABLE);
    ui->target = lbl(card, "--.-°C", 108, 36, APP_FONT_20, 0x233B36);
    lv_obj_set_size(ui->target, 76, 25);
    if (icon < 0)
    {
        /* Régi csomagnál tiszta szöveg: más termosztát eltérő LCD-jére nem rajzolunk. */
        lv_obj_set_pos(ui->target, 28, 30);
        lv_obj_set_size(ui->target, 236, 36);
        lv_obj_set_style_text_font(ui->target, APP_FONT_28, 0);
        text_color_if_changed(ui->target, c(home_night ? 0xEFF6F5 : 0x071B32), 0);
    }
    lv_obj_set_style_text_align(ui->target, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ui->target, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_remove_flag(ui->target, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_y(title, 98);
    lv_obj_set_height(title, lv_font_get_line_height(APP_FONT_20));
    home_labels[slot] = lbl(card, "", 10, 124, APP_FONT_16, light_ui_muted());
    home_text(home_labels[slot], 272);

    /* A jel az IRC működési iránya, nem a szelepnyitás vagy a hőigény becslése. */
    static const lv_point_precise_t heat0[] = {{2, 12}, {4, 9}, {2, 6}, {4, 2}};
    static const lv_point_precise_t heat1[] = {{6, 12}, {8, 9}, {6, 6}, {8, 2}};
    static const lv_point_precise_t heat2[] = {{10, 12}, {12, 9}, {10, 6}, {12, 2}};
    static const lv_point_precise_t cold0[] = {{7, 1}, {7, 13}};
    static const lv_point_precise_t cold1[] = {{2, 4}, {12, 10}};
    static const lv_point_precise_t cold2[] = {{2, 10}, {12, 4}};
    ui->heat = irc_mode_container(card);
    irc_mode_line(ui->heat, heat0, 4, 0xD57817);
    irc_mode_line(ui->heat, heat1, 4, 0xD57817);
    irc_mode_line(ui->heat, heat2, 4, 0xD57817);
    ui->cool = irc_mode_container(card);
    irc_mode_line(ui->cool, cold0, 2, 0x147FCA);
    irc_mode_line(ui->cool, cold1, 2, 0x147FCA);
    irc_mode_line(ui->cool, cold2, 2, 0x147FCA);
    for (unsigned button = 0; button < 2; ++button)
    {
        lv_obj_t *object = pairing_button(card, button ? "+" : "-",
                                          28 + (int)button * 124, 146, 112, 44);
        bg_color_if_changed(object, c(0x007D7D), 0);
        lv_obj_set_style_radius(object, 20, 0);
        lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_state(object, LV_STATE_DISABLED);
        lv_obj_add_event_cb(object, irc_card_command_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(index * 2 + button));
        ui->buttons[button] = object;
    }
}

static void irc_card_update(unsigned slot, unsigned index, bool live)
{
    irc_ui_card_t *ui = &irc_cards[slot];
    lx_irc_summary_t summary;
    bool current = app_loxone_irc_summary(home_config.cards[index].id, home_revision, &summary);
    char number[24], text[64];
    if (current && summary.target_valid)
    {
        fmt_dec(number, sizeof(number), summary.target_c, 1);
        snprintf(text, sizeof(text), "%s°C", number);
    }
    else
        snprintf(text, sizeof(text), "%s", hu() ? "--,-°C" : "--.-°C");
    label_text_if_changed(ui->target, text);
    if (current && summary.actual_valid)
    {
        fmt_dec(number, sizeof(number), summary.actual_c, 1);
        snprintf(text, sizeof(text), tr("Szoba: %s °C", "Room: %s °C"), number);
    }
    else
        snprintf(text, sizeof(text), "%s", tr("Szoba: nincs adat", "Room: no data"));
    label_text_if_changed(home_labels[slot], text);
    light_ui_hidden(ui->heat, !current || summary.direction != LX_IRC_DIRECTION_HEATING);
    light_ui_hidden(ui->cool, !current || summary.direction != LX_IRC_DIRECTION_COOLING);
    bool enabled = live && current && summary.controllable && !home_command_wait;
    light_ui_enable(ui->buttons[0], enabled);
    light_ui_enable(ui->buttons[1], enabled);
}
