/* A deklaratív solar_timeline widget. Minden helyi óraidő ugyanarra a
 * 00:00–24:00 skálára kerül; nem napkelte/napnyugta közé nyújtjuk a napot.
 * Nincs új idő-/hálózati adatforrás, task, kép vagy dinamikus pályapuffer. */
static int solar_timeline_x(int minute, int span)
{
    return (minute * span + 720) / 1440;
}

static int solar_timeline_clamp(int x, int low, int high)
{
    return x < low ? low : x > high ? high : x;
}

static void solar_timeline_build(template_node_t *node, bool night)
{
    const cJSON *spec = node->spec;
    int width = tn(spec, "w", 1264), height = tn(spec, "h", 136);
    int span = width - 80;
    uint32_t ink = tc(spec, "color", night ? TEXT_DARK : TEXT_LIGHT);
    uint32_t track = tc(spec, "muted", night ? 0x52657E : 0x8A9DB4);
    node->obj = lv_obj_create(s_dash);
    lv_obj_remove_style_all(node->obj);
    lv_obj_set_pos(node->obj, tn(spec, "x", 8), tn(spec, "y", 508));
    lv_obj_set_size(node->obj, width, height);
    lv_obj_set_style_radius(node->obj, tn(spec, "radius", 18), 0);
    bg_color_if_changed(node->obj, c(tc(spec, "background", night ? 0x202B36 : 0xFFF9EF)), 0);
    bg_opa_if_changed(node->obj, tn(spec, "opacity", 250), 0);
    lbl(node->obj, tr("A nap állása", "Solar timeline"), 24, 7, APP_FONT_20, ink);
    node->axis[2] = lbl(node->obj, "", width - 176, 7, APP_FONT_20, ink);
    lv_obj_set_size(node->axis[2], 160, 28);
    node->dots[3] = shape(node->obj, 40, 80, span, 16, track, 8);
    node->dots[0] = shape(node->obj, 40, 80, 1, 16,
                          tc(spec, "positive_color", 0xFFD15B), 0);
    for (int i = 0; i < 5; ++i)
    {
        int x = 40 + solar_timeline_x(i * 360, span);
        shape(node->obj, x, 97, 1, 6, track, 0);
        char text[8];
        snprintf(text, sizeof(text), "%02d:00", i * 6);
        lv_obj_t *caption = lbl(node->obj, text, x - 40, 107, APP_FONT_16, ink);
        lv_obj_set_size(caption, 80, 22);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    }
    for (int i = 0; i < 2; ++i)
    {
        node->axis[i] = lbl(node->obj, "", 16, 40, APP_FONT_16, ink);
        lv_obj_set_size(node->axis[i], 192, 22);
        lv_obj_set_style_text_align(node->axis[i], LV_TEXT_ALIGN_CENTER, 0);
        node->dots[i + 1] = shape(node->obj, 40, 67, 2, 31, ink, 0);
    }
    node->detail = lv_obj_create(node->obj);
    lv_obj_remove_style_all(node->detail);
    lv_obj_set_size(node->detail, 32, 32);
    lv_obj_set_style_radius(node->detail, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *icon = lv_image_create(node->detail);
    lv_image_set_src(icon, weather_asset(WEATHER_SUNNY));
    lv_image_set_pivot(icon, 0, 0);
    lv_image_set_scale(icon, 64);
    lv_obj_remove_flag(node->detail, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    /* Az első frissítésig se látszódjon kitalált napkelte vagy időpont. */
    lv_obj_add_flag(node->detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(node->dots[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(node->dots[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(node->dots[2], LV_OBJ_FLAG_HIDDEN);
}

static void solar_timeline_update(template_node_t *node, const app_energy_sample_t *sample)
{
    int width = tn(node->spec, "w", 1264), span = width - 80;
    int sunrise = sample->sunrise_minute, sunset = sample->sunset_minute;
    bool events = sample->sun_position_valid && sunrise >= 0 && sunrise < sunset && sunset < 1440;
    time_t epoch = time(NULL);
    struct tm local = {0};
    bool clock = template_clock_synced && localtime_r(&epoch, &local) && local.tm_year >= 120;
    int minute = local.tm_hour * 60 + local.tm_min;
    weather_visibility(node->detail, clock);
    weather_visibility(node->dots[0], events);
    weather_visibility(node->dots[1], events);
    weather_visibility(node->dots[2], events);
    char text[64];
    if (clock)
        snprintf(text, sizeof(text), "%s %02d:%02d", tr("Most:", "Now:"), local.tm_hour, local.tm_min);
    else
        snprintf(text, sizeof(text), "%s -", tr("Idő:", "Time:"));
    label_text_if_changed(node->axis[2], text);
    uint32_t track = tc(node->spec, "muted", sample->night_mode ? 0x52657E : 0x8A9DB4);
    bg_color_if_changed(node->dots[3], c(events ? track : (sample->night_mode ? 0x596462 : 0xB7BFBC)), 0);
    int positions[2] = {16, width - 208};
    if (events)
    {
        int rise_x = 40 + solar_timeline_x(sunrise, span);
        int set_x = 40 + solar_timeline_x(sunset, span);
        lv_obj_set_pos(node->dots[0], rise_x, 80);
        lv_obj_set_width(node->dots[0], set_x - rise_x);
        lv_obj_set_pos(node->dots[1], rise_x, 67);
        lv_obj_set_pos(node->dots[2], set_x, 67);
        positions[0] = solar_timeline_clamp(rise_x - 96, 16, width - 208);
        positions[1] = solar_timeline_clamp(set_x - 96, 16, width - 208);
        /* Rövid nappalnál is két külön felirat marad; a jelölővonalak
         * változatlanul a pontos időpontokon állnak. */
        if (positions[1] < positions[0] + 200)
        {
            positions[0] = solar_timeline_clamp((rise_x + set_x) / 2 - 196, 16, width - 408);
            positions[1] = positions[0] + 200;
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        const char *caption = i ? tr("Napnyugta", "Sunset") : tr("Napkelte", "Sunrise");
        int value = i ? sunset : sunrise;
        if (events)
            snprintf(text, sizeof(text), "%s %02d:%02d", caption, value / 60, value % 60);
        else
            snprintf(text, sizeof(text), "%s: -", caption);
        label_text_if_changed(node->axis[i], text);
        lv_obj_set_x(node->axis[i], positions[i]);
    }
    if (clock)
    {
        lv_obj_set_pos(node->detail, 40 + solar_timeline_x(minute, span) - 16, 72);
        lv_obj_t *icon = lv_obj_get_child(node->detail, 0);
        weather_visibility(icon, events);
        bg_opa_if_changed(node->detail, events ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        bg_color_if_changed(node->detail, c(track), 0);
        if (events)
        {
            bool day = minute >= sunrise && minute < sunset;
            const lv_image_dsc_t *asset = weather_asset(day ? WEATHER_SUNNY : WEATHER_NIGHT);
            if (lv_image_get_src(icon) != asset)
                lv_image_set_src(icon, asset);
        }
    }
}
