/* A tényleges widget összes helyi percét, adathiányát és megjelenését vizsgáljuk. */
static void test_orbit_dashboard(const app_energy_sample_t *original)
{
    if (strcmp(template_view.id, "06-energy-orbit") || tn(template_view.document, "version", 0) < 13)
        return;
    time_t saved_epoch = epoch;
    bool synced = template_clock_synced;
    char language[sizeof(s_cfg.project.language)];
    snprintf(language, sizeof(language), "%s", s_cfg.project.language);
    struct tm local = {.tm_year=126, .tm_mon=5, .tm_mday=21, .tm_isdst=-1};
    time_t midnight = mktime(&local);
    app_energy_sample_t sample = *original;
    sample.sun_position_valid = true;
    sample.sunrise_minute = 430;
    sample.sunset_minute = 1065;
    template_clock_synced = true;
    template_node_t *timeline = NULL;
    unsigned flows = 0;
    for (unsigned i = 0; i < template_node_count; ++i)
    {
        template_node_t *node = &template_nodes[i];
        if (!strcmp(ts(node->spec, "type"), "solar_timeline"))
            timeline = node;
        if (!strcmp(ts(node->spec, "type"), "flow"))
        {
            ++flows;
            assert(node->point_count == 97 && node->flow_path);
            const char *source = ts(node->spec, "source");
            assert(!strcmp(source, "pv") || !strcmp(source, "grid") || !strcmp(source, "battery"));
            bool reverse = cJSON_IsTrue(cJSON_GetObjectItem(node->spec, "reverse"));
            assert(reverse == !strcmp(source, "battery"));
            double power = node->power;
            for (int sign = -1; sign <= 1; sign += 2)
            {
                node->power = sign;
                node->phase = .5f;
                template_animate(.01f);
                assert(((sign > 0) != reverse) ? node->phase > .5f : node->phase < .5f);
            }
            node->power = power;
        }
    }
    assert(flows == 3 && timeline);
    int width = tn(timeline->spec, "w", 0), span = width - 80;
    int previous = -1;
    for (int minute = 0; minute < 1440; ++minute)
    {
        epoch = midnight + minute * 60;
        solar_timeline_update(timeline, &sample);
        lv_obj_update_layout(s_screen);
        int x = lv_obj_get_x(timeline->detail);
        assert(x == 24 + (int)lround(minute * (double)span / 1440));
        assert(x >= previous && x >= 0 && x + 32 <= width);
        previous = x;
        const void *icon = lv_image_get_src(lv_obj_get_child(timeline->detail, 0));
        assert(icon == weather_asset(minute >= 430 && minute < 1065 ? WEATHER_SUNNY : WEATHER_NIGHT));
    }
    assert(lv_obj_get_x(timeline->dots[0]) == 40 + (int)lround(430. * span / 1440));
    assert(lv_obj_get_width(timeline->dots[0]) ==
           (int)lround(1065. * span / 1440) - (int)lround(430. * span / 1440));
    epoch = midnight;
    solar_timeline_update(timeline, &sample);
    lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(timeline->detail) == 24);
    const int bad[][2] = {{-1, 100}, {200, 200}, {900, 800}, {0, 1440}};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        sample.sunrise_minute = bad[i][0];
        sample.sunset_minute = bad[i][1];
        solar_timeline_update(timeline, &sample);
        assert(lv_obj_has_flag(timeline->dots[0], LV_OBJ_FLAG_HIDDEN));
        assert(strchr(lv_label_get_text(timeline->axis[0]), '-'));
    }
    sample.sunrise_minute = 720;
    sample.sunset_minute = 721;
    solar_timeline_update(timeline, &sample);
    lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(timeline->axis[0]) + 192 <= lv_obj_get_x(timeline->axis[1]));
    assert(lv_obj_get_x(timeline->axis[0]) >= 16 && lv_obj_get_x(timeline->axis[1]) + 192 <= width - 16);
    sample.sun_position_valid = false;
    solar_timeline_update(timeline, &sample);
    assert(lv_obj_has_flag(timeline->dots[0], LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(lv_obj_get_child(timeline->detail, 0), LV_OBJ_FLAG_HIDDEN));
    template_clock_synced = false;
    solar_timeline_update(timeline, &sample);
    assert(lv_obj_has_flag(timeline->detail, LV_OBJ_FLAG_HIDDEN));
    template_clock_synced = true;
    epoch = 0;
    solar_timeline_update(timeline, &sample);
    assert(lv_obj_has_flag(timeline->detail, LV_OBJ_FLAG_HIDDEN));
    sample.sun_position_valid = true;
    sample.sunrise_minute = 430;
    sample.sunset_minute = 1065;
    for (unsigned english = 0; english < 2; ++english)
    {
        snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", english ? "en" : "hu");
        template_build(sample.night_mode);
        sample.power_pv_kw = sample.power_full_kw = 15;
        sample.power_grid_kw = sample.power_bat_kw = -15;
        sample.soc_bat_pct = 100;
        epoch = midnight + (sample.night_mode ? 23 * 3600 : 14 * 3600 + 30 * 60);
        template_update(&sample, &fixture);
        lv_obj_update_layout(s_screen);
        for (unsigned i = 0; i < template_node_count; ++i)
        {
            template_node_t *node = &template_nodes[i];
            if (strcmp(ts(node->spec, "type"), "value") && strcmp(ts(node->spec, "type"), "label"))
                continue;
            lv_point_t size;
            lv_text_get_size(&size, lv_label_get_text(node->obj), lv_obj_get_style_text_font(node->obj, 0),
                             0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            assert(size.x <= lv_obj_get_width(node->obj) && size.y <= lv_obj_get_height(node->obj));
        }
        /* A látványképen kiegyensúlyozott, szintetikus tesztadatok legyenek. */
        sample.power_pv_kw = sample.night_mode ? 0 : 5;
        sample.power_full_kw = sample.night_mode ? .5 : 1;
        sample.power_bat_kw = sample.night_mode ? .5 : -2;
        sample.power_grid_kw = sample.night_mode ? 0 : -2;
        sample.soc_bat_pct = 84;
        template_update(&sample, &fixture);
        char name[80];
        snprintf(name, sizeof(name), "orbit-timeline-%s-%s", english ? "en" : "hu",
                 sample.night_mode ? "dark" : "light");
        save_frame(name);
    }
    snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", language);
    epoch = saved_epoch;
    template_clock_synced = synced;
    template_build(original->night_mode);
    template_update(original, &fixture);
    puts("PASS: energy courtyard, signed flows, every local minute, midnight, invalid/missing sun, unsynced clock, short daylight, HU/EN limits");
}
