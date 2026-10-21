/* Az app_ui.c része. SD-előtöltés a grafikus lock ELŐTT, minden widget a lock
 * alatt él. Bezáráskor a mentett témaválasztásból töltünk vissza, lapozás nélkül. */
#ifdef _MSC_VER
static volatile LONG home_requested;
static int home_request_get(void) { return (int)InterlockedCompareExchange(&home_requested, 0, 0); }
static void home_request_set(int value) { InterlockedExchange(&home_requested, value); }
#else
static atomic_int home_requested;
static int home_request_get(void) { return atomic_load(&home_requested); }
static void home_request_set(int value) { atomic_store(&home_requested, value); }
#endif
static int home_page;
static unsigned home_card_page, home_first_card, home_card_count;
static lv_obj_t *home_dots[LX_HOME_MAX_PAGES];
static bool home_ready_to_draw, home_night, home_connected, home_command_wait;
static unsigned home_revision, home_resource_generation;
static unsigned home_load_attempts;
static TEMPLATE_RAM lx_home_config_t home_config, home_next_config;
static TEMPLATE_RAM lx_home_values_t home_values, home_next_values;
static bool home_next_connected, home_next_wait;
static unsigned home_next_revision;
static char home_status[LX_HOME_ERROR_SIZE], home_next_status[LX_HOME_ERROR_SIZE];
static app_template_view_t home_assets;
static lv_image_dsc_t home_images[14];
static lv_draw_buf_t *home_buffers[14];
static lv_draw_buf_t *home_gray_buffers[14];
static lv_image_dsc_t home_gray_images[14];
static lv_obj_t *home_icons[12], *home_labels[12], *home_buttons[12], *home_switch_knobs[12], *home_message, *home_total;

static const lv_image_dsc_t *home_gray(unsigned index);

static bool home_is_open(void) { return home_request_get() != 0; }
static void home_close(lv_event_t *event)
{
    (void)event;
    home_request_set(0);
    if (event) lv_event_stop_bubbling(event);
}
static void home_open_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_pair_pad && s_cfg.data_recept_mode == APP_DATA_LOXONE && !s_diagnostics_requested)
        home_request_set(1);
}
static void home_cards_requested(void)
{
    if (!s_pair_pad && s_cfg.data_recept_mode == APP_DATA_LOXONE && !s_diagnostics_requested)
        home_request_set(2);
}
/* A kérés 1=fogyasztók, 2..9=Otthon-lap. Csak ez az atomikus kérés kerül
 * át az input callbackből; az SD-képek cseréjét a runtime végzi. */
static void home_gesture(lv_dir_t direction)
{
    int requested = home_request_get();
    unsigned pages = lx_home_page_count(&home_config);
    if (requested < 2 || pages < 2 || (direction != LV_DIR_LEFT && direction != LV_DIR_RIGHT))
        return;
    unsigned current = (unsigned)(requested - 2) % pages;
    unsigned next = direction == LV_DIR_LEFT ? (current + 1) % pages : (current + pages - 1) % pages;
    home_request_set(2 + (int)next);
}
static void home_release(void)
{
    for (unsigned i = 0; i < 14; i++)
    {
        lv_image_cache_drop(&home_images[i]);
        if (home_buffers[i])
        {
            lv_draw_buf_destroy(home_buffers[i]);
            home_buffers[i] = NULL;
        }
        if (home_gray_buffers[i])
        {
            lv_image_cache_drop(&home_gray_images[i]);
            lv_draw_buf_destroy(home_gray_buffers[i]);
            home_gray_buffers[i] = NULL;
        }
    }
    app_templates_view_free(&home_assets);
    memset(home_images, 0, sizeof(home_images));
    home_ready_to_draw = false;
}
static void home_reset_theme(void)
{
    clear();
    template_release_cache();
    app_templates_view_free(&template_view);
    app_templates_view_free(&template_pending);
    home_release();
    /* A beépített téma képei is maradhatnak a globális cache-ben. A PNG
     * dekóder az új képet még a cache helyfelszabadítása előtt foglalja le. */
    lv_image_cache_drop(NULL);
    template_node_count = 0;
}
/* Kész képpontokból épül az oldal: a rajzolás közben nincs PNG-dekódolás,
 * cache-kilökés vagy új nagy foglalás. A háttér RGB565-ben pontosan 2 048 000
 * bájt, az ikonok ARGB8888 alakban őrzik az átlátszóságukat. LVGL-lock alatt. */
static bool home_decode_images(void)
{
    const lv_image_decoder_args_t args = {.no_cache = true};
    for (unsigned i = 0; i < home_assets.asset_count; i++)
    {
        lv_image_decoder_dsc_t decoder;
        if (lv_image_decoder_open(&decoder, &home_images[i], &args) != LV_RESULT_OK)
        {
            ESP_LOGW("app_ui", "Home PNG decode failed: %s", home_assets.assets[i].name);
            return false;
        }
        const lv_draw_buf_t *source = decoder.decoded;
        bool valid = source && source->header.cf == LV_COLOR_FORMAT_ARGB8888 &&
                     source->header.w == home_assets.assets[i].width &&
                     source->header.h == home_assets.assets[i].height;
        lv_draw_buf_t *buffer = NULL;
        if (valid && i == 0)
        {
            buffer = lv_draw_buf_create(source->header.w, source->header.h,
                                        LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
            if (buffer)
            {
                for (unsigned y = 0; y < source->header.h; y++)
                {
                    const uint8_t *row = source->data + y * source->header.stride;
                    uint16_t *out = (uint16_t *)(buffer->data + y * buffer->header.stride);
                    for (unsigned x = 0; x < source->header.w; x++)
                    {
                        const uint8_t *pixel = row + 4 * x;
                        out[x] = (uint16_t)(((pixel[2] & 0xf8U) << 8) |
                                            ((pixel[1] & 0xfcU) << 3) | (pixel[0] >> 3));
                    }
                }
            }
        }
        else if (valid)
            buffer = lv_draw_buf_dup(source);
        lv_image_decoder_close(&decoder);
        if (!buffer)
        {
            ESP_LOGW("app_ui", "Home image buffer unavailable: %s", home_assets.assets[i].name);
            return false;
        }
        home_buffers[i] = buffer;
        lv_draw_buf_to_image(buffer, &home_images[i]);
    }
    return true;
}
static bool home_prepare(bool night)
{
    int requested = home_request_get();
    if (!requested && !home_page) return true;
    unsigned resource_generation = 0;
    bool snapshot = s_cfg.data_recept_mode == APP_DATA_LOXONE &&
                    app_loxone_home_snapshot(&home_next_config, &home_next_values,
                        &home_next_revision, &home_next_connected, &home_next_wait, home_next_status);
    bool sd_ok = app_storage_is_ok();
    bool resources = sd_ok && app_templates_home_info(NULL, &resource_generation);
    /* Hosszú webes telepítés alatt a nem blokkoló katalóguslekérdezés
     * foglalt lehet. A már előtöltött oldal ilyenkor megmarad, friss állapottal. */
    if (!resources && !resource_generation && requested && home_page && sd_ok && snapshot &&
        home_next_revision == home_revision)
        return true;
    bool available = snapshot && resources;
    unsigned pages = lx_home_page_count(&home_next_config);
    unsigned index = requested >= 2 ? (unsigned)(requested - 2) : 0;
    if (pages && index >= pages)
        index = pages - 1;
    int mode = requested == 1 ? 1 : 2;
    unsigned first = 0, count = home_next_config.consumer_count;
    bool page_valid = requested == 1 || lx_home_page_range(&home_next_config, index, &first, &count);
    if (requested >= 2 && page_valid && requested != 2 + (int)index)
        home_request_set(2 + (int)index);
    if (!requested || !available || !page_valid || (requested == 1 && !count))
    {
        if (!home_page)
        {
            home_request_set(0);
            return true;
        }
        if (bsp_display_lock(-1) == ESP_OK)
        {
            home_reset_theme(); home_page = 0;
            home_request_set(0);
            template_loading_screen();
            template_waiting_for_load = true;
            bsp_display_unlock();
            return template_loading_wait();
        }
        return false;
    }
    bool same_page = home_page == mode && home_card_page == index && home_night == night &&
                     home_revision == home_next_revision && home_resource_generation == resource_generation;
    if (same_page && home_ready_to_draw)
        return true;
    if (!same_page)
    {
        if (bsp_display_lock(-1) != ESP_OK) return false;
        home_reset_theme();
        template_loading_screen();
        home_page = mode;
        home_card_page = index;
        home_first_card = first;
        home_card_count = count;
        home_night = night;
        home_revision = home_next_revision;
        home_resource_generation = resource_generation;
        home_load_attempts = 0;
        bsp_display_unlock();
    }
    if (!template_loading_wait()) return false;
    const char *icons[12];
    for (unsigned i = 0; i < count; i++)
        icons[i] = requested == 1 ? home_next_config.consumers[i].icon : home_next_config.cards[first + i].icon;
    bool loaded = app_templates_home_load_for(icons, count, night, requested == 1, &home_assets);
    if (loaded)
    {
        for (unsigned i = 0; i < home_assets.asset_count; i++)
        {
            app_template_asset_t *asset = &home_assets.assets[i];
            home_images[i] = (lv_image_dsc_t){
                .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW_ALPHA,
                           .w = asset->width, .h = asset->height},
                .data = asset->bytes, .data_size = asset->size};
        }
        if (bsp_display_lock(-1) != ESP_OK)
        {
            app_templates_view_free(&home_assets);
            return true;
        }
        loaded = home_decode_images();
        if (loaded && requested == 1)
        {
            for (unsigned i = 2; loaded && i < home_assets.asset_count; i++)
            {
                loaded = home_gray(i) != NULL;
                if (!loaded)
                    ESP_LOGW("app_ui", "Home grayscale buffer unavailable: %s", home_assets.assets[i].name);
            }
        }
        if (!loaded)
            home_release();
        bsp_display_unlock();
    }
    if (!loaded)
    {
        /* Foglalt SD vagy sikertelen dekódolás esetén marad a betöltőkép.
         * Háttér nélküli oldal és ismétlődő widget-újraépítés nem jelenhet meg. */
        if (++home_load_attempts < 15)
            return true;
        app_events_add(APP_EVENT_WARNING, "A Loxone-oldal grafikai erőforrásai nem tölthetők be.");
        home_request_set(0);
        home_page = 0;
        template_loading_requested = true;
        template_waiting_for_load = true;
        return true;
    }
    home_ready_to_draw = true;
    return true;
}
static int home_icon_index(const char *id)
{
    char name[65]; snprintf(name, sizeof(name), "icon-%s.png", id);
    for (unsigned i = 2; i < home_assets.asset_count; i++)
        if (!strcmp(name, home_assets.assets[i].name)) return (int)i;
    return -1;
}
static const lv_image_dsc_t *home_gray(unsigned index)
{
    if (home_gray_buffers[index]) return &home_gray_images[index];
    /* Az eredeti, már ellenőrzött ARGB képpontokat másoljuk. Nem készül
     * ideiglenes LVGL-képernyő, és nincs új PNG-dekódolás a render alatt. */
    lv_draw_buf_t *buffer = lv_draw_buf_dup(home_buffers[index]);
    if (!buffer) return NULL;
    for (unsigned y = 0; y < 96; y++)
    {
        uint8_t *row = buffer->data + y * buffer->header.stride;
        for (unsigned x = 0; x < 96; x++)
        {
            uint8_t *pixel = row + 4 * x;
            uint8_t gray = (uint8_t)((29U * pixel[0] + 150U * pixel[1] + 77U * pixel[2]) >> 8);
            pixel[0] = pixel[1] = pixel[2] = gray;
        }
    }
    home_gray_buffers[index] = buffer;
    lv_draw_buf_to_image(buffer, &home_gray_images[index]);
    return &home_gray_images[index];
}
static void home_action(lv_event_t *event)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (s_pair_pad || home_request_get() != 2 + (int)home_card_page || !home_connected || home_command_wait ||
        index < home_first_card || index >= home_first_card + home_card_count || index >= home_config.card_count ||
        !home_values.command_supported[index]) return;
    const lx_home_card_t *card = &home_config.cards[index];
    lx_home_command_t action = LX_HOME_COMMAND_PULSE;
    if (card->type == LX_HOME_CARD_SWITCH)
    {
        if (!home_values.cards[index].valid) return;
        action = home_values.cards[index].value == 0 ? LX_HOME_COMMAND_ON : LX_HOME_COMMAND_OFF;
    }
    if (app_loxone_home_command(card->id, action) == ESP_OK)
    {
        home_command_wait = true;
        snprintf(home_status, sizeof(home_status), "%s", tr("Kérés küldése...", "Sending request..."));
    }
    else
        snprintf(home_status, sizeof(home_status), "%s", tr("A művelet most nem engedélyezett.", "This action is currently unavailable."));
    if (event) lv_event_stop_bubbling(event);
}
static void home_text(lv_obj_t *label, int width)
{
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
}
static void home_build(void)
{
    clear(); screen_style(s_screen, home_night);
    lv_obj_t *background = lv_image_create(s_screen);
    lv_image_set_src(background, &home_images[0]);
    lv_obj_set_pos(background, 0, 0);
    lv_obj_remove_flag(background, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *header = shape(s_screen, 24, 20, 1232, 80, home_night ? 0x17242B : 0xF8FBF4, 24);
    bg_opa_if_changed(header, home_night ? 242 : 244, 0);
    const char *page_title = home_config.page_count ? home_config.pages[home_card_page].title : tr("Otthon", "Home");
    lv_obj_t *page_heading = lbl(header, home_page == 1 ? tr("Ház külön mért fogyasztói", "Separately metered home consumers") : page_title,
                                 28, 10, APP_FONT_28, home_night ? 0xF2F7F6 : 0x071B32);
    lv_obj_set_width(page_heading, home_page == 1 ? 700 : 1080);
    lv_label_set_long_mode(page_heading, LV_LABEL_LONG_MODE_DOTS);
    lbl(header, home_page == 1 ? tr("Pillanatnyi fogyasztás", "Live consumption") :
        tr("Állapotok és vezérlés", "Status and controls"), 28, 46, APP_FONT_16, home_night ? 0xC0CDCE : 0x50615F);
    if (home_page == 1)
    {
        lv_obj_t *caption = lbl(header, tr("A ház teljes fogyasztása", "Total home consumption"),
                               780, 10, APP_FONT_16, home_night ? 0xC0CDCE : 0x50615F);
        lv_obj_set_width(caption, 350);
        lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    }
    home_total = lbl(header, "", 780, home_page == 1 ? 36 : 28,
                     APP_FONT_22, home_night ? 0x67DFD5 : 0x006C6C);
    lv_obj_set_width(home_total, 350);
    lv_obj_t *close = pairing_button(s_screen, "X", 1174, 32, 60, 56);
    lv_obj_add_flag(close, LV_OBJ_FLAG_GESTURE_BUBBLE);
    bg_color_if_changed(close, c(0x007D7D), 0);
    lv_obj_set_style_radius(close, 28, 0);
    lv_obj_add_event_cb(close, home_close, LV_EVENT_CLICKED, NULL);
    unsigned count = home_page == 1 ? home_config.consumer_count : home_card_count;
    memset(home_buttons, 0, sizeof(home_buttons));
    memset(home_switch_knobs, 0, sizeof(home_switch_knobs));
    for (unsigned i = 0; i < count; i++)
    {
        lx_home_card_rect_t rectangle; if (!lx_home_card_rect(count, i, &rectangle)) continue;
        lv_obj_t *card = shape(s_screen, rectangle.x, rectangle.y, rectangle.width, rectangle.height,
                                home_night ? 0x14252A : 0xF7FBF5, 20);
        bg_opa_if_changed(card, home_night ? 242 : 244, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        border_color_if_changed(card, c(home_night ? 0x367171 : 0x69B3AC), 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
        unsigned index = home_first_card + i;
        const char *name = home_page == 1 ? home_config.consumers[i].name : home_config.cards[index].name;
        const char *icon = home_page == 1 ? home_config.consumers[i].icon : home_config.cards[index].icon;
        int asset = home_icon_index(icon);
        home_icons[i] = lv_image_create(card); lv_obj_set_pos(home_icons[i], 98, 8);
        if (asset >= 0) lv_image_set_src(home_icons[i], &home_images[asset]);
        lv_obj_t *title = lbl(card, name, 10, 108, APP_FONT_20, home_night ? 0xEFF6F5 : 0x071B32);
        home_text(title, 272);
        bool control = home_page == 2 && home_config.cards[index].type != LX_HOME_CARD_VALUE;
        if (control)
        {
            bool is_switch = home_config.cards[index].type == LX_HOME_CARD_SWITCH;
            lv_obj_t *button = pairing_button(card, "", is_switch ? 62 : 22, 146, is_switch ? 104 : 248, 44);
            lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
            bg_color_if_changed(button, c(0x007D7D), 0);
            lv_obj_set_style_radius(button, 22, 0);
            lv_obj_set_style_pad_all(button, 0, 0);
            home_buttons[i] = button;
            if (is_switch)
            {
                home_switch_knobs[i] = shape(button, 6, 6, 32, 32, 0xFFFFFF, 16);
                lv_obj_remove_flag(home_switch_knobs[i], LV_OBJ_FLAG_CLICKABLE);
                home_labels[i] = lbl(card, "", 182, 154, APP_FONT_20, home_night ? 0x68E0D5 : 0x006B69);
            }
            else home_labels[i] = lv_obj_get_child(button, 0);
            lv_obj_add_event_cb(button, home_action, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        }
        else
        {
            home_labels[i] = lbl(card, "", 10, 146, APP_FONT_28, home_night ? 0x68E0D5 : 0x006B69);
            home_text(home_labels[i], 272);
        }
    }
    if (home_page == 2 && !count)
    {
        lv_obj_t *empty = shape(s_screen, 250, 330, 780, 100, home_night ? 0x14252A : 0xF7FBF5, 20);
        lv_obj_t *message = lbl(empty, tr("Ezen az oldalon még nincs kártya.", "This page has no cards yet."),
                                20, 35, APP_FONT_22, home_night ? 0xEFF6F5 : 0x071B32);
        home_text(message, 740);
    }
    /* A fedő lábléc a világos fotó sötét részletein is állandó kontrasztot ad. */
    lv_obj_t *footer = shape(s_screen, 16, 754, 1248, 40, home_night ? 0x14252A : 0xF8FBF4, 14);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(footer, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *logo_box = lv_obj_create(footer);
    lv_obj_remove_style_all(logo_box);
    lv_obj_set_size(logo_box, 32, 32);
    lv_obj_set_pos(logo_box, 8, 4);
    lv_obj_remove_flag(logo_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *logo = lv_image_create(logo_box);
    lv_image_set_src(logo, &home_images[1]);
    lv_image_set_pivot(logo, 0, 0);
    lv_image_set_scale(logo, 32 * 256 / home_assets.assets[1].width);
    lv_obj_set_pos(logo, 0, 0);
    unsigned pages = home_page == 2 ? lx_home_page_count(&home_config) : 0;
    home_message = lbl(footer, "", 64, 9, APP_FONT_16, home_night ? 0xFFFFFF : 0x071B32);
    lv_obj_set_width(home_message, pages > 1 ? 440 : 1136);
    lv_label_set_long_mode(home_message, LV_LABEL_LONG_MODE_DOTS);
    memset(home_dots, 0, sizeof(home_dots));
    if (pages > 1)
    {
        unsigned width = pages * 18 - 8;
        for (unsigned i = 0; i < pages; i++)
        {
            uint32_t color = i == home_card_page ? (home_night ? 0x68E0D5 : 0x006B69)
                                                : (home_night ? 0x637E80 : 0x91A29C);
            home_dots[i] = shape(footer, (1248 - width) / 2 + i * 18, 15, 10, 10, color, 5);
            lv_obj_remove_flag(home_dots[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(home_dots[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
        }
    }
    s_view = VIEW_HOME;
}
static const char *home_feedback(void)
{
    if (hu()) return home_status;
    static const char *const messages[][2] = {
        {"Kérés küldése...", "Sending request..."},
        {"A kapcsolat megszakadt; a művelet eredménye nem igazolható.", "Connection lost; the outcome could not be confirmed."},
        {"A művelet nem igazolható. Ellenőrizd a Loxone állapotát.", "The action could not be confirmed. Check the Loxone status."},
        {"Impulzus elküldve.", "Pulse sent."},
        {"Állapot-visszajelzésre várunk...", "Waiting for state feedback..."},
        {"Loxone-állapot visszaigazolva.", "Loxone state confirmed."},
        {"Nem érkezett megfelelő Loxone-állapot.", "No matching Loxone state received."},
    };
    for (unsigned i=0; i<sizeof(messages)/sizeof(messages[0]); i++)
        if (!strcmp(home_status,messages[i][0])) return messages[i][1];
    return home_status;
}
static bool home_render(const app_energy_sample_t *sample, bool sample_valid)
{
    if (!home_is_open()) return false;
    if (!home_ready_to_draw) return true;
    home_config = home_next_config; home_values = home_next_values;
    home_connected = home_next_connected; home_command_wait = home_next_wait;
    snprintf(home_status, sizeof(home_status), "%s", home_next_status);
    if (s_view != VIEW_HOME) home_build();
    unsigned count = home_page == 1 ? home_config.consumer_count : home_card_count;
    for (unsigned i = 0; i < count; i++)
    {
        unsigned index = home_first_card + i;
        lx_home_value_t value = home_page == 1 ? home_values.consumers[i] : home_values.cards[index];
        /* A fogyasztói mérés kW-os: az 1 W alatti zaj a feliratban,
         * ikonállapotban is nulla legyen. */
        if (home_page == 1 && value.valid && value.value < .001)
            value.value = 0;
        const lx_home_card_t *card = &home_config.cards[index];
        char text[128], number[96];
        if (home_page == 2 && card->type == LX_HOME_CARD_PULSE)
            snprintf(text, sizeof(text), "%s", tr("Indítás", "Start"));
        else if (!home_connected || !value.valid)
        {
            snprintf(text, sizeof(text), "%s", tr("Nincs adat", "No data"));
        }
        else if (home_page == 1)
        {
            if (value.value < 1) snprintf(text, sizeof(text), "%.0f W", value.value * 1000);
            else { fmt_dec(number, sizeof(number), value.value, 2); snprintf(text, sizeof(text), "%s kW", number); }
        }
        else if (card->type == LX_HOME_CARD_SWITCH)
            snprintf(text, sizeof(text), "%s", value.value == 1 ? tr("BE", "ON") : tr("KI", "OFF"));
        else
        {
            fmt_dec(number, sizeof(number), value.value, (int)card->decimals);
            snprintf(text, sizeof(text), "%s %s", number, card->unit);
        }
        label_text_if_changed(home_labels[i], text);
        if (home_buttons[i])
        {
            bool enabled = home_connected && !home_command_wait && home_values.command_supported[index] &&
                           (card->type == LX_HOME_CARD_PULSE || value.valid);
            if (home_switch_knobs[i])
            {
                bool on = home_connected && value.valid && value.value == 1;
                lv_obj_set_x(home_switch_knobs[i], on ? 66 : 6);
                bg_color_if_changed(home_buttons[i], c(on ? 0x007D7D : 0x667678), 0);
                text_color_if_changed(home_labels[i], c(on ? (home_night ? 0x68E0D5 : 0x006B69) :
                                                       (home_night ? 0xAFBCBC : 0x586765)), 0);
            }
            if (enabled) lv_obj_remove_state(home_buttons[i], LV_STATE_DISABLED);
            else lv_obj_add_state(home_buttons[i], LV_STATE_DISABLED);
        }
        else
        {
            uint32_t color = home_night ? 0x68E0D5 : 0x006B69;
            if (!home_connected || !value.valid || (home_page == 1 && value.value == 0)) color = home_night ? 0x9DABAC : 0x687474;
            if (home_page == 2 && home_connected && value.valid && ((card->warning_low_set && value.value < card->warning_low) ||
                (card->warning_high_set && value.value > card->warning_high))) color = (uint32_t)strtoul(card->warning_color + 1, NULL, 16);
            text_color_if_changed(home_labels[i], c(color), 0);
        }
        if (home_page == 1)
        {
            int index = home_icon_index(home_config.consumers[i].icon);
            if (index >= 0)
            {
                lv_obj_remove_flag(home_icons[i], LV_OBJ_FLAG_HIDDEN);
                const lv_image_dsc_t *image = &home_images[index];
                if (home_connected && value.valid && value.value == 0)
                {
                    const lv_image_dsc_t *gray = home_gray((unsigned)index);
                    if (gray) image = gray;
                    else lv_obj_add_flag(home_icons[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (lv_image_get_src(home_icons[i]) != image) lv_image_set_src(home_icons[i], image);
            }
        }
    }
    char text[128] = "";
    if (home_page == 1)
    {
        /* A fejléc a teljes ház PowerFull értékét mutatja, a külön
         * mért fogyasztók zajszűrt megjelenítésétől függetlenül. */
        if (home_connected && sample_valid && isfinite(sample->power_full_kw))
            fmt_power(text, sizeof(text), sample->power_full_kw);
        else
            snprintf(text, sizeof(text), "%s", tr("Nincs adat", "No data"));
    }
    label_text_if_changed(home_total, text);
    label_text_if_changed(home_message, !home_connected ? tr("Nincs Loxone-kapcsolat; a vezérlés tiltva.", "No Loxone connection; controls disabled.") :
                          home_status[0] ? home_feedback() : tr("Élő Loxone-adatok", "Live Loxone data"));
    return true;
}
static void home_bind_hotspot(lv_obj_t *parent, const cJSON *scene)
{
    const cJSON *area = cJSON_GetObjectItemCaseSensitive(scene, "home_hotspot");
    int x = tn(area,"x",0), y = tn(area,"y",0), w = tn(area,"w",0), h = tn(area,"h",0);
    if (!area)
    {
        /* A már telepített gyári csomagok OTA után is megnyithatók. Saját
         * illusztrált témánál az explicit home_hotspot a pontos megoldás. */
        if (!strcmp(template_view.id,"01-live-home")) { x=460; y=300; w=380; h=230; }
        else if (!strcmp(template_view.id,"09-solar-journey")) { x=970; y=310; w=245; h=220; }
        else for (unsigned i=0; i<template_node_count; i++)
        {
            template_node_t *node=&template_nodes[i];
            bool house=!strcmp(ts(node->spec,"file"),"flow_house.png");
            if (house || (!w && !strcmp(ts(node->spec,"type"),"value") && !strcmp(ts(node->spec,"source"),"home")))
            {
                x=lv_obj_get_x(node->obj); y=lv_obj_get_y(node->obj);
                w=lv_obj_get_width(node->obj); h=lv_obj_get_height(node->obj);
                if(house)break;
            }
        }
    }
    if (w <= 0 || h <= 0) return;
    lv_obj_t *hotspot = lv_obj_create(parent); lv_obj_remove_style_all(hotspot);
    lv_obj_set_pos(hotspot, x, y);
    lv_obj_set_size(hotspot, w, h);
    lv_obj_add_flag(hotspot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_remove_flag(hotspot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hotspot, home_open_clicked, LV_EVENT_CLICKED, NULL);
}
