/* A pillanatképek a grafikus lock előtt készülnek; a modell és a bezárás
 * kizárólag LVGL-lock alatt él. Rejtett oldalon sincs új widget/kép/kapcsolat. */
static TEMPLATE_RAM app_assistant_consumers_t assistant_consumers;
static app_assistant_night_t assistant_night;
static app_assistant_t assistant_state;
static lv_obj_t *assistant_box, *assistant_title, *assistant_detail;

static void assistant_reset_widgets(void)
{
    assistant_box = assistant_title = assistant_detail = NULL;
}

static void assistant_prepare(void)
{
    memset(&assistant_consumers, 0, sizeof(assistant_consumers));
    memset(&assistant_night, 0, sizeof(assistant_night));
    if (s_cfg.data_recept_mode == APP_DATA_LOXONE)
    {
        (void)app_loxone_assistant_snapshot(&assistant_consumers);
        (void)app_stats_get_night_reference(&assistant_night);
    }
}

static void assistant_close(lv_event_t *event)
{
    app_assistant_dismiss(&assistant_state, esp_timer_get_time());
    if (assistant_box)
        lv_obj_add_flag(assistant_box, LV_OBJ_FLAG_HIDDEN);
    lv_event_stop_bubbling(event);
}

static bool assistant_area(int *x, int *y, int *w, int *h)
{
    *x = 78;
    *y = 18;
    *w = 348;
    *h = 46;
    if (!template_is_active())
        return true;
    const cJSON *scene = cJSON_GetObjectItemCaseSensitive(template_view.document,
                                                        s_last_night ? "dark" : "light");
    const cJSON *area = cJSON_GetObjectItemCaseSensitive(scene, "assistant_area");
    if (area)
    {
        int values[4];
        const char *keys[] = {"x", "y", "w", "h"};
        for (unsigned i = 0; i < 4; ++i)
        {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(area, keys[i]);
            if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
                value->valuedouble < 0 || value->valuedouble > 1280 ||
                floor(value->valuedouble) != value->valuedouble)
                return false;
            values[i] = value->valueint;
        }
        if (values[2] < 320 || values[3] < 46 || values[0] + values[2] > W ||
            values[1] + values[3] > H || values[3] > 100)
            return false;
        *x = values[0];
        *y = values[1];
        *w = values[2];
        *h = values[3];
        return true;
    }
    /* A gyári témák csak a dekoratív címsávjukat adják át a javaslat idejére.
     * Óra, időjárás, energiaadat és érinthető házterület szabadon marad. */
    *x = 332;
    *y = 16;
    *w = 465;
    *h = 72;
    if (!strcmp(template_view.id, "01-live-home"))
    {
        *x = 296;
        *w = 490;
        *h = 62;
        return true;
    }
    if (!strcmp(template_view.id, "09-solar-journey"))
    {
        *x = 480;
        *w = 458;
        *h = 70;
        return true;
    }
    const char *known[] = {"02-energy-paths", "03-instruments", "04-daily-balance",
                           "05-calm-home", "06-energy-orbit", "07-energy-ledger", "08-statistics"};
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
        if (!strcmp(template_view.id, known[i]))
            return true;
    /* Ismeretlen külső téma csak a szerző által megadott területre rajzolhat. */
    return false;
}

static void assistant_render(void)
{
    int x, y, width, height;
    bool visible = s_cfg.data_recept_mode == APP_DATA_LOXONE && s_view == VIEW_DASHBOARD &&
                   !home_is_open() && !s_pair_pad && !s_diagnostics_requested &&
                   assistant_state.result.kind != APP_ASSISTANT_NONE &&
                   assistant_area(&x, &y, &width, &height);
    if (!visible)
    {
        if (assistant_box && !lv_obj_has_flag(assistant_box, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(assistant_box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!assistant_box)
    {
        assistant_box = lv_obj_create(s_screen);
        lv_obj_remove_style_all(assistant_box);
        lv_obj_remove_flag(assistant_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_style_radius(assistant_box, 10, 0);
        lv_obj_set_style_bg_opa(assistant_box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(assistant_box, 1, 0);
        assistant_title = lbl(assistant_box, "", 10, 4, APP_FONT_16, TEXT_LIGHT);
        assistant_detail = lbl(assistant_box, "", 10, 25, APP_FONT_12, TEXT_LIGHT);
        lv_obj_set_height(assistant_title, 20);
        lv_obj_set_height(assistant_detail, 16);
        lv_obj_t *close = lv_button_create(assistant_box);
        lv_obj_remove_style_all(close);
        lv_obj_set_size(close, 44, 44);
        lv_obj_align(close, LV_ALIGN_RIGHT_MID, -1, 0);
        lv_obj_remove_flag(close, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(close, assistant_close, LV_EVENT_CLICKED, NULL);
        lv_obj_t *text = lbl(close, LV_SYMBOL_CLOSE, 0, 0, LV_FONT_DEFAULT, TEXT_LIGHT);
        lv_obj_center(text);
    }
    lv_obj_set_pos(assistant_box, x, y);
    lv_obj_set_size(assistant_box, width, height);
    lv_obj_t *close = lv_obj_get_child(assistant_box, 2);
    lv_obj_align(close, LV_ALIGN_RIGHT_MID, -1, 0);
    uint32_t foreground = s_last_night ? 0xE9F5E9 : 0x18452A;
    bg_color_if_changed(assistant_box, c(s_last_night ? 0x183326 : 0xE1F3D5), 0);
    border_color_if_changed(assistant_box, c(s_last_night ? 0x5A986D : 0x80AE63), 0);
    text_color_if_changed(assistant_title, c(foreground), 0);
    text_color_if_changed(assistant_detail, c(foreground), 0);
    text_color_if_changed(lv_obj_get_child(close, 0), c(foreground), 0);
    char title[160], detail[160];
    const app_assistant_result_t *result = &assistant_state.result;
    char primary[24], secondary[24];
    if (result->kind == APP_ASSISTANT_SOLAR)
    {
        snprintf(title, sizeof(title), tr("Most érdemes: %s", "Good time for: %s"), result->name);
        fmt_power(primary, sizeof(primary), result->surplus_kw);
        fmt_power(secondary, sizeof(secondary), result->required_kw);
        snprintf(detail, sizeof(detail), tr("Akku 100%% · többlet %s · igény %s",
                                            "Battery 100%% · spare %s · needs %s"), primary, secondary);
    }
    else
    {
        snprintf(title, sizeof(title), "%s", tr("Magasabb éjszakai fogyasztás", "Higher overnight consumption"));
        fmt_power(primary, sizeof(primary), result->current_kw);
        fmt_power(secondary, sizeof(secondary), result->baseline_kw);
        snprintf(detail, sizeof(detail), tr("Előző óra %s · szokásos %s", "Last hour %s · usual %s"), primary, secondary);
    }
    fit_text(assistant_title, title, width - 62, APP_FONT_16);
    fit_text(assistant_detail, detail, width - 62, APP_FONT_12);
    if (lv_obj_has_flag(assistant_box, LV_OBJ_FLAG_HIDDEN))
        lv_obj_remove_flag(assistant_box, LV_OBJ_FLAG_HIDDEN);
}
