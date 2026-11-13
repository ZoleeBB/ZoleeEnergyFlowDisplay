/* Kötött redőnykártya és egyszeri célpozíciót küldő kezelő. A csúszka saját
 * szerkesztési érték; a mért pozíciót kizárólag a Miniserver frissíti. */
#include "app_loxone_shading.h"

static const char *home_feedback(void);

typedef struct
{
    lv_obj_t *buttons[3], *safety;
} shading_ui_card_t;

static TEMPLATE_RAM shading_ui_card_t shading_cards[LX_HOME_MAX_CARDS];
static lv_obj_t *shading_overlay, *shading_panel, *shading_slider;
static lv_obj_t *shading_actual, *shading_target, *shading_motion, *shading_status;
static lv_obj_t *shading_apply, *shading_stop;
static unsigned shading_index, shading_revision, shading_page, shading_target_percent;
static unsigned shading_generation;
static char shading_card_id[LX_HOME_ID_SIZE];
static bool shading_submit_error, shading_submitted;

static bool shading_ui_is_open(void)
{
    return shading_overlay != NULL;
}

static void shading_ui_reset(void)
{
    /* Egy régi ablak sorban maradt eseménye az új ablakot sem vezérelheti. */
    if (++shading_generation == 0)
        ++shading_generation;
    if (shading_overlay)
        lv_obj_delete(shading_overlay);
    shading_overlay = shading_panel = shading_slider = NULL;
    shading_actual = shading_target = shading_motion = shading_status = NULL;
    shading_apply = shading_stop = NULL;
    shading_card_id[0] = 0;
    shading_revision = shading_index = shading_page = shading_target_percent = 0;
    shading_submit_error = shading_submitted = false;
}

static bool shading_position_valid(const lx_shading_summary_t *summary)
{
    return summary->position_valid && isfinite(summary->position_percent) &&
           summary->position_percent >= 0 && summary->position_percent <= 100;
}

static const char *shading_motion_text(const lx_shading_summary_t *summary)
{
    if (!summary->motion_known)
        return "";
    if (summary->moving_up && !summary->moving_down)
        return tr("Felfelé", "Moving up");
    if (summary->moving_down && !summary->moving_up)
        return tr("Lefelé", "Moving down");
    return summary->moving_up ? tr("Mozgás", "Moving") : tr("Áll", "Stopped");
}

static bool shading_modal_summary(lx_shading_summary_t *summary)
{
    if (!shading_overlay || lighting_ui_is_open() || s_pair_pad ||
        loxone_display_is_held() || !home_connected ||
        shading_revision != home_revision || shading_page != home_card_page ||
        home_request_get() != 2 + (int)shading_page ||
        shading_index >= home_config.card_count ||
        shading_index < home_first_card || shading_index >= home_first_card + home_card_count ||
        home_config.cards[shading_index].type != LX_HOME_CARD_SHADING ||
        strcmp(shading_card_id, home_config.cards[shading_index].id))
        return false;
    return app_loxone_shading_summary(shading_card_id, shading_revision, summary) &&
           summary->available && !summary->central && shading_position_valid(summary);
}

static void shading_ui_update(void)
{
    if (!shading_overlay)
        return;
    lx_shading_summary_t summary;
    if (!shading_modal_summary(&summary))
    {
        shading_ui_reset();
        return;
    }
    char text[96];
    snprintf(text, sizeof(text), tr("Aktuális zártság: %u%%", "Current closure: %u%%"),
             (unsigned)lround(summary.position_percent));
    label_text_if_changed(shading_actual, text);
    snprintf(text, sizeof(text), tr("Célérték: %u%%", "Target: %u%%"), shading_target_percent);
    label_text_if_changed(shading_target, text);
    const char *motion = shading_motion_text(&summary);
    if (summary.safety_active)
        snprintf(text, sizeof(text), "%s%s%s", motion, motion[0] ? " | " : "",
                 tr("Biztonsági mód aktív", "Safety mode active"));
    else
        snprintf(text, sizeof(text), "%s", motion);
    label_text_if_changed(shading_motion, text);
    bool allowed = summary.controllable && !home_command_wait;
    light_ui_enable(shading_slider, allowed);
    light_ui_enable(shading_apply, allowed);
    light_ui_enable(shading_stop, allowed);
    label_text_if_changed(shading_status,
        !summary.controllable ? tr("A vezérlés most nem engedélyezett.", "Control is currently unavailable.") :
        home_command_wait ? tr("Kérés küldése...", "Sending request...") :
        shading_submit_error ? tr("A kérés nem fogadható el. Ellenőrizd az állapotot.", "Request rejected. Check the current state.") :
        shading_submitted ? (home_status[0] ? home_feedback() :
            tr("A kérés eredménye még nem ismert.", "The request result is not yet known.")) :
        tr("A csúszkát a Beállítás gombbal érvényesítheted.", "Press Apply to send the slider value."));
}

static bool shading_modal_event_valid(lv_event_t *event, lx_shading_summary_t *summary)
{
    lv_event_stop_bubbling(event);
    if ((unsigned)(uintptr_t)lv_event_get_user_data(event) != shading_generation)
        return false;
    if (!shading_modal_summary(summary))
    {
        shading_ui_reset();
        return false;
    }
    return summary->controllable && !home_command_wait;
}

static void shading_slider_event(lv_event_t *event)
{
    lx_shading_summary_t summary;
    if (!shading_modal_event_valid(event, &summary))
        return;
    /* Nincs parancsküldés sem húzáskor, sem elengedéskor. */
    int percent = lv_slider_get_value(shading_slider);
    if (percent < 0 || percent > 100)
        return;
    shading_target_percent = (unsigned)percent;
    shading_submit_error = shading_submitted = false;
    shading_ui_update();
}

static void shading_apply_event(lv_event_t *event)
{
    lx_shading_summary_t summary;
    if (!shading_modal_event_valid(event, &summary))
        return;
    shading_submit_error = app_loxone_shading_position(
        shading_card_id, shading_revision, shading_target_percent) != ESP_OK;
    shading_submitted = !shading_submit_error;
    if (shading_submitted)
        home_command_wait = true;
    shading_ui_update();
}

static void shading_stop_event(lv_event_t *event)
{
    lx_shading_summary_t summary;
    if (!shading_modal_event_valid(event, &summary))
        return;
    shading_submit_error = app_loxone_shading_command(
        shading_card_id, shading_revision, LX_HOME_COMMAND_SHADING_STOP) != ESP_OK;
    shading_submitted = !shading_submit_error;
    if (shading_submitted)
        home_command_wait = true;
    shading_ui_update();
}

static void shading_close_event(lv_event_t *event)
{
    if (lv_event_get_target(event) != lv_event_get_current_target(event))
        return;
    lv_event_stop_bubbling(event);
    if ((unsigned)(uintptr_t)lv_event_get_user_data(event) != shading_generation)
        return;
    lv_indev_t *input = lv_indev_active();
    if (input)
        lv_indev_wait_release(input);
    shading_ui_reset();
}

static void shading_ui_open(unsigned index)
{
    if (shading_overlay || lighting_ui_is_open() || s_pair_pad ||
        loxone_display_is_held() || !home_connected || home_command_wait || home_page != 2 ||
        home_request_get() != 2 + (int)home_card_page || index >= home_config.card_count ||
        index < home_first_card || index >= home_first_card + home_card_count ||
        home_config.cards[index].type != LX_HOME_CARD_SHADING)
        return;
    lx_shading_summary_t summary;
    if (!app_loxone_shading_summary(home_config.cards[index].id, home_revision, &summary) ||
        !summary.available || !summary.controllable || summary.central || !shading_position_valid(&summary))
        return;
    shading_ui_reset();
    shading_index = index;
    shading_revision = home_revision;
    shading_page = home_card_page;
    shading_target_percent = (unsigned)lround(summary.position_percent);
    snprintf(shading_card_id, sizeof(shading_card_id), "%s", home_config.cards[index].id);
    void *generation = (void *)(uintptr_t)shading_generation;
    shading_overlay = shape(s_screen, 0, 0, 1280, 800, 0x000000, 0);
    bg_opa_if_changed(shading_overlay, 150, 0);
    lv_obj_remove_flag(shading_overlay, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(shading_overlay, shading_close_event, LV_EVENT_CLICKED, generation);
    shading_panel = shape(shading_overlay, 260, 170, 760, 460, home_night ? 0x14272D : 0xFBFDF9, 24);
    lv_obj_remove_flag(shading_panel, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lbl(shading_panel, home_config.cards[index].name, 28, 22,
                          APP_FONT_28, light_ui_foreground());
    lv_obj_set_size(title, 620, lv_font_get_line_height(APP_FONT_28));
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *close = pairing_button(shading_panel, "X", 680, 12, 56, 56);
    lv_obj_remove_flag(close, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(close, shading_close_event, LV_EVENT_CLICKED, generation);
    lv_obj_t *icon = lv_image_create(shading_panel);
    lv_image_set_src(icon, &home_shading_image);
    lv_obj_set_pos(icon, 30, 94);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    shading_actual = lbl(shading_panel, "", 150, 98, APP_FONT_28, light_ui_foreground());
    shading_target = lbl(shading_panel, "", 150, 142, APP_FONT_28, home_night ? 0x68E0D5 : 0x006B69);
    shading_motion = lbl(shading_panel, "", 150, 186, APP_FONT_16, light_ui_muted());
    lv_obj_set_width(shading_actual, 570);
    lv_obj_set_width(shading_target, 570);
    lv_obj_set_width(shading_motion, 570);
    shading_slider = lv_slider_create(shading_panel);
    lv_obj_set_pos(shading_slider, 52, 260);
    lv_obj_set_size(shading_slider, 656, 16);
    lv_slider_set_range(shading_slider, 0, 100);
    lv_slider_set_value(shading_slider, (int)shading_target_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(shading_slider, c(home_night ? 0x49656A : 0xC5D6CE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(shading_slider, c(0x008C83), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(shading_slider, c(0x42CFC0), LV_PART_KNOB);
    lv_obj_set_style_pad_all(shading_slider, 10, LV_PART_KNOB);
    lv_obj_set_ext_click_area(shading_slider, 18);
    lv_obj_remove_flag(shading_slider, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(shading_slider, shading_slider_event, LV_EVENT_VALUE_CHANGED, generation);
    lv_obj_t *minimum = lbl(shading_panel, tr("0% · Nyitva", "0% · Open"), 36, 296,
                            APP_FONT_20, light_ui_muted());
    lv_obj_set_width(minimum, 300);
    lv_obj_t *maximum = lbl(shading_panel, tr("100% · Zárva", "100% · Closed"), 424, 296,
                            APP_FONT_20, light_ui_muted());
    lv_obj_set_width(maximum, 300);
    lv_obj_set_style_text_align(maximum, LV_TEXT_ALIGN_RIGHT, 0);
    shading_stop = pairing_button(shading_panel, "Stop", 52, 350, 180, 56);
    shading_apply = pairing_button(shading_panel, tr("Beállítás", "Apply"), 248, 350, 460, 56);
    lv_obj_remove_flag(shading_stop, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_remove_flag(shading_apply, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(shading_stop, shading_stop_event, LV_EVENT_CLICKED, generation);
    lv_obj_add_event_cb(shading_apply, shading_apply_event, LV_EVENT_CLICKED, generation);
    shading_status = lbl(shading_panel, "", 28, 426, APP_FONT_16, light_ui_muted());
    lv_obj_set_size(shading_status, 704, lv_font_get_line_height(APP_FONT_16));
    lv_label_set_long_mode(shading_status, LV_LABEL_LONG_MODE_CLIP);
    shading_ui_update();
}

static void shading_card_open_event(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    shading_ui_open((unsigned)(uintptr_t)lv_event_get_user_data(event));
}

static void shading_card_command_event(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    unsigned encoded = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    unsigned index = encoded / 4, operation = encoded % 4;
    if (shading_ui_is_open() || lighting_ui_is_open() || s_pair_pad ||
        loxone_display_is_held() || !home_connected || home_command_wait || home_page != 2 ||
        home_request_get() != 2 + (int)home_card_page || index >= home_config.card_count ||
        index < home_first_card || index >= home_first_card + home_card_count ||
        home_config.cards[index].type != LX_HOME_CARD_SHADING || operation > 2)
        return;
    lx_shading_summary_t summary;
    if (!app_loxone_shading_summary(home_config.cards[index].id, home_revision, &summary) ||
        !summary.available || !summary.controllable)
        return;
    lx_home_command_t action = operation == 0 ? LX_HOME_COMMAND_SHADING_UP :
                               operation == 1 ? LX_HOME_COMMAND_SHADING_STOP : LX_HOME_COMMAND_SHADING_DOWN;
    if (app_loxone_shading_command(home_config.cards[index].id, home_revision, action) == ESP_OK)
    {
        home_command_wait = true;
        snprintf(home_status, sizeof(home_status), "%s", tr("Kérés küldése...", "Sending request..."));
    }
}

static void shading_card_build(lv_obj_t *card, lv_obj_t *title, unsigned slot, unsigned index)
{
    shading_ui_card_t *ui = &shading_cards[slot];
    lv_image_set_src(home_icons[slot], &home_shading_image);
    lv_obj_set_pos(home_icons[slot], 98, 3);
    lv_obj_remove_flag(home_icons[slot], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_icons[slot], shading_card_open_event, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    lv_obj_set_y(title, 98);
    lv_obj_set_height(title, lv_font_get_line_height(APP_FONT_20));
    home_labels[slot] = lbl(card, "", 10, 124, APP_FONT_16, light_ui_muted());
    home_text(home_labels[slot], 272);
    lv_obj_set_height(home_labels[slot], lv_font_get_line_height(APP_FONT_16));
    lv_label_set_long_mode(home_labels[slot], LV_LABEL_LONG_MODE_CLIP);
    ui->safety = lbl(card, tr("Védelem", "Safety"), 200, 14, APP_FONT_14,
                     home_night ? 0xFFD07A : 0x9B5A00);
    lv_obj_set_width(ui->safety, 82);
    lv_obj_remove_flag(ui->safety, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ui->safety, LV_OBJ_FLAG_HIDDEN);
    const char *captions[] = {tr("Fel", "Up"), "Stop", tr("Le", "Down")};
    for (unsigned button = 0; button < 3; ++button)
    {
        lv_obj_t *object = pairing_button(card, captions[button], 16 + (int)button * 92, 146, 76, 44);
        bg_color_if_changed(object, c(0x007D7D), 0);
        lv_obj_set_style_radius(object, 20, 0);
        lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_state(object, LV_STATE_DISABLED);
        lv_obj_add_event_cb(object, shading_card_command_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(index * 4 + button));
        ui->buttons[button] = object;
    }
}

static void shading_card_update(unsigned slot, unsigned index, bool live)
{
    shading_ui_card_t *ui = &shading_cards[slot];
    lx_shading_summary_t summary;
    bool current = app_loxone_shading_summary(home_config.cards[index].id, home_revision, &summary);
    char text[96];
    if (!current || !summary.available)
        snprintf(text, sizeof(text), "%s", tr("Nincs adat", "No data"));
    else if (summary.central)
        snprintf(text, sizeof(text), "%s", tr("Csoportos vezérlés", "Group control"));
    else if (!shading_position_valid(&summary))
        snprintf(text, sizeof(text), "%s", tr("Zártság: nincs adat", "Closure: no data"));
    else
    {
        const char *motion = shading_motion_text(&summary);
        snprintf(text, sizeof(text), tr("%u%% zárva%s%s", "%u%% closed%s%s"),
                 (unsigned)lround(summary.position_percent), motion[0] ? " | " : "", motion);
    }
    label_text_if_changed(home_labels[slot], text);
    light_ui_hidden(ui->safety, !current || !summary.safety_active);
    bool enabled = live && current && summary.available && summary.controllable && !home_command_wait;
    for (unsigned i = 0; i < 3; ++i)
        light_ui_enable(ui->buttons[i], enabled);
    bool popup = enabled && !summary.central && shading_position_valid(&summary);
    if (popup)
        lv_obj_add_flag(home_icons[slot], LV_OBJ_FLAG_CLICKABLE);
    else
        lv_obj_remove_flag(home_icons[slot], LV_OBJ_FLAG_CLICKABLE);
}
