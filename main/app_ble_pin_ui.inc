/* A meglévő LVGL érintőbemenetet használó, gesztusokat blokkoló PIN-modal. */
static void pairing_mask_update(void)
{
    char mask[24];
    unsigned length = (unsigned)strlen(s_pair_digits);
    snprintf(mask,
             sizeof(mask),
             "%c %c %c %c %c %c",
             length > 0 ? '*' : '_',
             length > 1 ? '*' : '_',
             length > 2 ? '*' : '_',
             length > 3 ? '*' : '_',
             length > 4 ? '*' : '_',
             length > 5 ? '*' : '_');
    label_text_if_changed(s_pair_mask, mask);
}

static void pairing_key(lv_event_t *event)
{
    uintptr_t key = (uintptr_t)lv_event_get_user_data(event);
    size_t length = strlen(s_pair_digits);
    if (key <= 9 && length < 6)
    {
        s_pair_digits[length] = (char)('0' + key);
        s_pair_digits[length + 1] = 0;
    }
    else if (key == 10 && length)
    {
        s_pair_digits[length - 1] = 0;
    }
    else if (key == 11 && length == 6)
    {
        (void)app_ble_pairing_submit(s_pair_ui_token, s_pair_digits);
        memset(s_pair_digits, 0, sizeof(s_pair_digits));
    }
    else if (key == 12)
    {
        (void)app_ble_pairing_submit(s_pair_ui_token, NULL);
        memset(s_pair_digits, 0, sizeof(s_pair_digits));
    }
    pairing_mask_update();
}

static lv_obj_t *pairing_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, c(0x284C68), 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, APP_FONT_22, 0);
    lv_obj_set_style_text_color(label, c(0xFFFFFF), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static bool pairing_ui_tick(void)
{
    app_ble_pairing_status_t status;
    app_ble_pairing_status(&status);
    if (!status.pin_required)
    {
        if (s_pair_pad)
        {
            lv_obj_delete(s_pair_pad);
            s_pair_pad = s_pair_mask = s_pair_countdown = NULL;
            memset(s_pair_digits, 0, sizeof(s_pair_digits));
        }
        return false;
    }
    if (!s_pair_pad || s_pair_ui_token != status.token)
    {
        if (s_pair_pad)
            lv_obj_delete(s_pair_pad);
        s_pair_ui_token = status.token;
        memset(s_pair_digits, 0, sizeof(s_pair_digits));
        s_pair_pad = lv_obj_create(s_screen);
        lv_obj_set_pos(s_pair_pad, 0, 0);
        lv_obj_set_size(s_pair_pad, W, H);
        lv_obj_set_style_pad_all(s_pair_pad, 0, 0);
        lv_obj_set_style_radius(s_pair_pad, 0, 0);
        lv_obj_set_style_border_width(s_pair_pad, 0, 0);
        lv_obj_set_style_bg_color(s_pair_pad, c(0x101B2C), 0);
        lv_obj_remove_flag(s_pair_pad, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lbl(s_pair_pad, tr("Master párosítása", "Pair with master"), 80, 36, APP_FONT_28, 0xFFFFFF);
        lv_obj_t *instructions =
            lbl(s_pair_pad,
                tr("A master diagnosztikáján engedélyezd a párosítást, majd írd be az ott látható "
                   "6 számjegyű PIN-t.",
                   "Enable pairing in the master diagnostics, then enter its 6-digit PIN."),
                80,
                92,
                APP_FONT_18,
                0xC9D7E7);
        lv_obj_set_width(instructions, W - 160);
        app_runtime_state_t rt;
        app_state_get_runtime(&rt);
        char peer[80];
        snprintf(peer, sizeof(peer), "Master: %s", rt.ble_peer[0] ? rt.ble_peer : "ZoleeEnergy");
        lbl(s_pair_pad, peer, 80, 155, APP_FONT_18, 0xC9D7E7);
        s_pair_mask = lbl(s_pair_pad, "", 440, 210, APP_FONT_28, 0xFFFFFF);
        lv_obj_set_width(s_pair_mask, 392);
        lv_obj_set_style_text_align(s_pair_mask, LV_TEXT_ALIGN_CENTER, 0);
        for (unsigned i = 0; i < 12; i++)
        {
            unsigned key = i < 9 ? i + 1 : i == 9 ? 10 : i == 10 ? 0 : 11;
            char digit[2] = {(char)('0' + key), 0};
            const char *text = key == 10 ? tr("Törlés", "Delete") : key == 11 ? "OK" : digit;
            lv_obj_t *button =
                pairing_button(s_pair_pad, text, 440 + (i % 3) * 136, 280 + (i / 3) * 88, 120, 72);
            lv_obj_add_event_cb(button, pairing_key, LV_EVENT_CLICKED, (void *)(uintptr_t)key);
        }
        s_pair_countdown = lbl(s_pair_pad, "", 440, 646, APP_FONT_16, 0xC9D7E7);
        lv_obj_t *cancel = pairing_button(s_pair_pad, tr("Mégsem", "Cancel"), 500, 700, 280, 56);
        lv_obj_add_event_cb(cancel, pairing_key, LV_EVENT_CLICKED, (void *)(uintptr_t)12);
        pairing_mask_update();
    }
    char countdown[80];
    snprintf(countdown,
             sizeof(countdown),
             tr("Hátralévő idő: %u mp", "Time remaining: %u s"),
             status.seconds);
    label_text_if_changed(s_pair_countdown, countdown);
    return true;
}

static void pairing_window_clicked(lv_event_t *event)
{
    (void)event;
    app_ble_pairing_open();
}
