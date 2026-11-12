/* Az Otthon-felület része. A logikai körlista teljes, de legfeljebb nyolc
 * sornak van LVGL-kezelője. Egyetlen, igény szerint foglalt RGB565 színtárcsa
 * élhet a PSRAM-ban; bezáráskor minden felugró erőforrás felszabadul. */
#define LIGHT_UI_ROWS 8
#define LIGHT_ROW_HEIGHT 84
#define LIGHT_ROW_PITCH 94
#define LIGHT_ROW_EXTRA 260
#define LIGHT_ROW_WIDTH 828
#define LIGHT_WHEEL_SIZE 192

typedef struct
{
    lv_obj_t *root, *name, *level, *control, *swatch, *arrow;
    unsigned index;
    lx_light_type_t type;
    bool bound;
} light_ui_row_t;

static lv_obj_t *light_overlay, *light_panel, *light_body, *light_content, *light_status;
static lv_obj_t *light_picker, *light_wheel, *light_marker, *light_white_preview, *light_white_caption;
static lv_obj_t *light_mode_buttons[2], *light_brightness, *light_brightness_label;
static lv_obj_t *light_temperature, *light_temperature_label, *light_temperature_caption;
static lv_obj_t *light_card_buttons[LX_HOME_MAX_CARDS][3];
static light_ui_row_t *light_rows, *light_picker_row;
static unsigned light_handle, light_count;
static int light_expanded = -1, light_touch_index = -1;
static bool light_touch, light_syncing, light_refreshing, light_ui_error;
static bool light_rows_failed, light_wheel_failed;
static lx_light_value_t light_edit;
static uint16_t *light_wheel_pixels;
static lv_image_dsc_t light_wheel_image;

static void lighting_ui_refresh_rows(void);
static void lighting_ui_sync_picker(const lx_light_circuit_t *circuit);

static uint32_t light_ui_background(void) { return home_night ? 0x20343A : 0xF0F6F1; }
static uint32_t light_ui_foreground(void) { return home_night ? 0xEFF6F5 : 0x142B35; }
static uint32_t light_ui_muted(void) { return home_night ? 0xABC3C5 : 0x52686A; }

static void *light_ui_alloc(size_t count, size_t size)
{
#ifdef APP_TEMPLATE_DESKTOP
    return calloc(count, size);
#else
    return heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

static bool lighting_ui_is_open(void) { return light_overlay != NULL; }

static void light_ui_enable(lv_obj_t *object, bool enabled)
{
    if (!object) return;
    if (enabled) lv_obj_remove_state(object, LV_STATE_DISABLED);
    else lv_obj_add_state(object, LV_STATE_DISABLED);
}

static void light_ui_hidden(lv_obj_t *object, bool hidden)
{
    if (!object) return;
    if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *light_ui_label(lv_obj_t *parent, const char *text, int x, int y, int width)
{
    lv_obj_t *label = lbl(parent, text, x, y, APP_FONT_20, light_ui_foreground());
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return label;
}

/* A tárcsa teljes fényerejű színmintát mutat; a valódi fényerőt a külön
 * csúszka adja meg. Az előnézet nem próbálja meg az RGBW-keverést helyettesíteni. */
static uint32_t light_ui_hsv(unsigned hue, unsigned saturation, unsigned brightness)
{
    double sector = (hue % 360) / 60.0;
    unsigned part = (unsigned)sector;
    double fraction = sector - part;
    double value = brightness / 100.0, sat = saturation / 100.0;
    double p = value * (1 - sat), q = value * (1 - sat * fraction);
    double t = value * (1 - sat * (1 - fraction));
    double red, green, blue;
    switch (part)
    {
        case 0: red = value; green = t; blue = p; break;
        case 1: red = q; green = value; blue = p; break;
        case 2: red = p; green = value; blue = t; break;
        case 3: red = p; green = q; blue = value; break;
        case 4: red = t; green = p; blue = value; break;
        default: red = value; green = p; blue = q; break;
    }
    return ((uint32_t)lround(red * 255) << 16) | ((uint32_t)lround(green * 255) << 8) |
           (uint32_t)lround(blue * 255);
}

static uint32_t light_ui_color(const lx_light_value_t *value)
{
    if (value->mode == LX_LIGHT_MODE_WHITE)
        return value->kelvin < 4000 ? 0xFFE0AF : value->kelvin < 5500 ? 0xFFF3DF : 0xDCEAFF;
    return light_ui_hsv(value->hue, value->saturation, 100);
}

static void light_ui_wheel_free(void)
{
    if (light_wheel)
    {
        lv_obj_delete(light_wheel);
        light_wheel = light_marker = NULL;
    }
    if (light_wheel_pixels)
    {
        lv_image_cache_drop(&light_wheel_image);
        free(light_wheel_pixels);
        light_wheel_pixels = NULL;
        memset(&light_wheel_image, 0, sizeof(light_wheel_image));
    }
}

static void light_ui_picker_free(void)
{
    light_ui_wheel_free();
    if (light_picker) lv_obj_delete(light_picker);
    light_picker = NULL;
    light_picker_row = NULL;
    light_wheel_failed = false;
    light_white_preview = light_white_caption = NULL;
    light_brightness = light_brightness_label = NULL;
    light_temperature = light_temperature_label = light_temperature_caption = NULL;
    light_mode_buttons[0] = light_mode_buttons[1] = NULL;
    light_touch = false;
    light_touch_index = -1;
    if (light_body) lv_obj_add_flag(light_body, LV_OBJ_FLAG_SCROLLABLE);
}

static void lighting_ui_reset(void)
{
    unsigned handle = light_handle;
    light_handle = 0;
    light_ui_picker_free();
    if (light_overlay) lv_obj_delete(light_overlay);
    light_overlay = light_panel = light_body = light_content = light_status = NULL;
    free(light_rows);
    light_rows = NULL;
    light_count = 0;
    light_expanded = -1;
    light_syncing = light_refreshing = light_ui_error = light_rows_failed = false;
    app_loxone_light_close(handle);
}

static void light_ui_close_event(lv_event_t *event)
{
    if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
    lv_event_stop_bubbling(event);
    /* Az elengedés már nem juthat az ablak mögötti világításgombra. */
    lv_indev_t *input = lv_indev_active();
    if (input) lv_indev_wait_release(input);
    lighting_ui_reset();
}

static bool light_ui_allowed(const lx_light_circuit_t *circuit)
{
    return circuit->actual.valid && !circuit->blocked && !circuit->locked && circuit->lock_known &&
           circuit->type != LX_LIGHT_UNSUPPORTED;
}

static void light_ui_submit(unsigned index, const lx_light_value_t *value)
{
    light_ui_error = app_loxone_light_set(light_handle, index, value) != ESP_OK;
    if (light_ui_error && light_status)
        label_text_if_changed(light_status, tr("A vezérlés most nem engedélyezett.", "Control is currently unavailable."));
}

static void light_ui_touch_end(void)
{
    light_touch = false;
    light_touch_index = -1;
    if (light_body) lv_obj_add_flag(light_body, LV_OBJ_FLAG_SCROLLABLE);
}

static void light_ui_slider_event(lv_event_t *event)
{
    if (light_syncing || !light_handle) return;
    light_ui_row_t *row = lv_event_get_user_data(event);
    if (!row || !row->bound) return;
    lv_obj_t *target = lv_event_get_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    lx_light_circuit_t circuit;
    if (!app_loxone_light_circuit(light_handle, row->index, &circuit) || !light_ui_allowed(&circuit))
    {
        light_ui_touch_end();
        return;
    }
    if (code == LV_EVENT_PRESSED)
    {
        light_touch = true;
        light_touch_index = (int)row->index;
        if (row != light_picker_row) light_edit = circuit.actual;
        lv_obj_remove_flag(light_body, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_RELEASED)
    {
        lx_light_value_t next = light_touch ? light_edit : circuit.actual;
        if (target == light_temperature)
        {
            next.mode = LX_LIGHT_MODE_WHITE;
            next.kelvin = (unsigned)lv_slider_get_value(target);
        }
        else next.brightness = (unsigned)lv_slider_get_value(target);
        light_edit = next;
        light_ui_submit(row->index, &next);
        if (row == light_picker_row) lighting_ui_sync_picker(&circuit);
        char value[24];
        snprintf(value, sizeof(value), "%u%%", next.brightness);
        label_text_if_changed(row->level, value);
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        light_ui_touch_end();
    lv_event_stop_bubbling(event);
}

static lv_obj_t *light_ui_slider(lv_obj_t *parent, int x, int y, int width, light_ui_row_t *row)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, width, 14);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_set_style_bg_color(slider, c(home_night ? 0x49656A : 0xC5D6CE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, c(0x008C83), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, c(0x42CFC0), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 16);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(slider, light_ui_slider_event, LV_EVENT_PRESSED, row);
    lv_obj_add_event_cb(slider, light_ui_slider_event, LV_EVENT_VALUE_CHANGED, row);
    lv_obj_add_event_cb(slider, light_ui_slider_event, LV_EVENT_RELEASED, row);
    lv_obj_add_event_cb(slider, light_ui_slider_event, LV_EVENT_PRESS_LOST, row);
    return slider;
}

static void light_ui_switch_event(lv_event_t *event)
{
    if (light_syncing) return;
    light_ui_row_t *row = lv_event_get_user_data(event);
    lx_light_circuit_t circuit;
    if (!row || !app_loxone_light_circuit(light_handle, row->index, &circuit) || !light_ui_allowed(&circuit)) return;
    circuit.actual.brightness = lv_obj_has_state(row->control, LV_STATE_CHECKED) ? 100 : 0;
    light_ui_submit(row->index, &circuit.actual);
    lv_event_stop_bubbling(event);
}

static void light_ui_mode_event(lv_event_t *event)
{
    if (!light_picker_row || light_touch) return;
    lx_light_circuit_t circuit;
    if (!app_loxone_light_circuit(light_handle, light_picker_row->index, &circuit) || !light_ui_allowed(&circuit)) return;
    lx_light_mode_t mode = (lx_light_mode_t)(uintptr_t)lv_event_get_user_data(event);
    if ((mode == LX_LIGHT_MODE_HSV && !circuit.rgb) || (mode == LX_LIGHT_MODE_WHITE && !circuit.white)) return;
    light_edit = circuit.actual;
    light_edit.mode = mode;
    light_ui_submit(light_picker_row->index, &light_edit);
    lighting_ui_sync_picker(&circuit);
    lv_event_stop_bubbling(event);
}

static void light_ui_wheel_event(lv_event_t *event)
{
    if (!light_picker_row || !light_wheel || light_syncing) return;
    lx_light_circuit_t circuit;
    if (!app_loxone_light_circuit(light_handle, light_picker_row->index, &circuit) || !light_ui_allowed(&circuit))
    {
        light_ui_touch_end();
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED)
    {
        light_touch = true;
        light_touch_index = (int)light_picker_row->index;
        lv_obj_remove_flag(light_body, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_indev_t *input = lv_indev_active();
    if (input && light_touch && (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED))
    {
        lv_point_t point;
        lv_area_t area;
        lv_indev_get_point(input, &point);
        lv_obj_get_coords(light_wheel, &area);
        double dx = point.x - area.x1 - LIGHT_WHEEL_SIZE / 2.0;
        double dy = point.y - area.y1 - LIGHT_WHEEL_SIZE / 2.0;
        double hue = atan2(-dy, dx) * 180.0 / 3.14159265358979323846;
        if (hue < 0) hue += 360;
        unsigned saturation = (unsigned)lround(hypot(dx, dy) * 100 / (LIGHT_WHEEL_SIZE / 2 - 2));
        light_edit.mode = LX_LIGHT_MODE_HSV;
        light_edit.hue = (unsigned)lround(hue) % 360;
        light_edit.saturation = saturation > 100 ? 100 : saturation;
        light_ui_submit(light_picker_row->index, &light_edit);
        lighting_ui_sync_picker(&circuit);
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        light_ui_touch_end();
    lv_event_stop_bubbling(event);
}

static bool light_ui_wheel_create(void)
{
    if (light_wheel) return true;
    if (light_wheel_failed) return false;
    light_wheel_pixels = light_ui_alloc(LIGHT_WHEEL_SIZE * LIGHT_WHEEL_SIZE, sizeof(*light_wheel_pixels));
    if (!light_wheel_pixels) { light_wheel_failed = true; return false; }
    for (int y = 0; y < LIGHT_WHEEL_SIZE; ++y)
        for (int x = 0; x < LIGHT_WHEEL_SIZE; ++x)
        {
            double dx = x - LIGHT_WHEEL_SIZE / 2.0, dy = y - LIGHT_WHEEL_SIZE / 2.0;
            double saturation = hypot(dx, dy) * 100 / (LIGHT_WHEEL_SIZE / 2 - 2);
            double hue = atan2(-dy, dx) * 180.0 / 3.14159265358979323846;
            if (hue < 0) hue += 360;
            uint32_t rgb = saturation > 100 ? light_ui_background() :
                light_ui_hsv((unsigned)hue, (unsigned)saturation, 100);
            light_wheel_pixels[y * LIGHT_WHEEL_SIZE + x] = (uint16_t)(
                ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
        }
    light_wheel_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    light_wheel_image.header.cf = LV_COLOR_FORMAT_RGB565;
    light_wheel_image.header.w = light_wheel_image.header.h = LIGHT_WHEEL_SIZE;
    light_wheel_image.header.stride = LIGHT_WHEEL_SIZE * 2;
    light_wheel_image.data_size = LIGHT_WHEEL_SIZE * LIGHT_WHEEL_SIZE * 2;
    light_wheel_image.data = (const uint8_t *)light_wheel_pixels;
    light_wheel = lv_image_create(light_picker);
    lv_image_set_src(light_wheel, &light_wheel_image);
    lv_obj_set_pos(light_wheel, 26, 34);
    lv_obj_add_flag(light_wheel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(light_wheel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(light_wheel, light_ui_wheel_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(light_wheel, light_ui_wheel_event, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(light_wheel, light_ui_wheel_event, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(light_wheel, light_ui_wheel_event, LV_EVENT_PRESS_LOST, NULL);
    light_marker = shape(light_wheel, 0, 0, 16, 16, 0xFFFFFF, 8);
    lv_obj_set_style_border_width(light_marker, 2, 0);
    lv_obj_set_style_border_color(light_marker, c(0x132B31), 0);
    lv_obj_remove_flag(light_marker, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

static void light_ui_picker_create(light_ui_row_t *row, const lx_light_circuit_t *circuit)
{
    light_ui_picker_free();
    light_picker_row = row;
    light_edit = circuit->actual;
    light_picker = shape(row->root, 0, LIGHT_ROW_HEIGHT, LIGHT_ROW_WIDTH, LIGHT_ROW_EXTRA, light_ui_background(), 0);
    lv_obj_remove_flag(light_picker, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    light_mode_buttons[0] = pairing_button(light_picker, tr("Színes", "Color"), 260, 8, 172, 42);
    light_mode_buttons[1] = pairing_button(light_picker, tr("Fehérfény", "White"), 444, 8, 172, 42);
    for (unsigned i = 0; i < 2; ++i)
    {
        lv_obj_set_style_text_font(light_mode_buttons[i], APP_FONT_20, 0);
        lv_obj_remove_flag(light_mode_buttons[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(light_mode_buttons[i], light_ui_mode_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(i ? LX_LIGHT_MODE_WHITE : LX_LIGHT_MODE_HSV));
    }
    light_white_preview = shape(light_picker, 47, 55, 150, 150, 0xFFE0AF, 75);
    lv_obj_remove_flag(light_white_preview, LV_OBJ_FLAG_CLICKABLE);
    light_white_caption = light_ui_label(light_white_preview, "", 0, 62, 150);
    lv_obj_set_style_text_align(light_white_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(light_white_caption, c(0x193037), 0);
    light_temperature_caption = light_ui_label(light_picker, tr("Színhőmérséklet", "Color temperature"), 260, 74, 400);
    light_temperature = light_ui_slider(light_picker, 260, 124, 360, row);
    lv_slider_set_range(light_temperature, (int)circuit->min_kelvin, (int)circuit->max_kelvin);
    light_temperature_label = light_ui_label(light_picker, "", 644, 110, 155);
    light_ui_label(light_picker, tr("Fényerő", "Brightness"), 260, 172, 400);
    light_brightness = light_ui_slider(light_picker, 260, 216, 390, row);
    light_brightness_label = light_ui_label(light_picker, "", 674, 202, 110);
    lighting_ui_sync_picker(circuit);
}

static void lighting_ui_sync_picker(const lx_light_circuit_t *circuit)
{
    if (!light_picker) return;
    bool white = light_edit.mode == LX_LIGHT_MODE_WHITE || !circuit->rgb;
    bool allowed = light_ui_allowed(circuit);
    light_syncing = true;
    light_ui_enable(light_mode_buttons[0], allowed && circuit->rgb);
    light_ui_enable(light_mode_buttons[1], allowed && circuit->white);
    bg_color_if_changed(light_mode_buttons[0], c(!white ? 0x007D7D : light_ui_background()), 0);
    bg_color_if_changed(light_mode_buttons[1], c(white ? 0x007D7D : light_ui_background()), 0);
    light_ui_hidden(light_temperature, !white);
    light_ui_hidden(light_temperature_label, !white);
    light_ui_hidden(light_temperature_caption, !white);
    light_ui_hidden(light_white_preview, !white);
    if (white)
    {
        light_ui_wheel_free();
        bg_color_if_changed(light_white_preview, c(light_ui_color(&light_edit)), 0);
        char kelvin[24];
        snprintf(kelvin, sizeof(kelvin), "%u K", light_edit.kelvin);
        label_text_if_changed(light_white_caption, kelvin);
        label_text_if_changed(light_temperature_label, kelvin);
        if (lv_slider_get_value(light_temperature) != (int)light_edit.kelvin)
            lv_slider_set_value(light_temperature, (int)light_edit.kelvin, LV_ANIM_OFF);
    }
    else if (light_ui_wheel_create())
    {
        double angle = light_edit.hue * 3.14159265358979323846 / 180;
        double radius = (LIGHT_WHEEL_SIZE / 2 - 2) * light_edit.saturation / 100.0;
        lv_obj_set_pos(light_marker, LIGHT_WHEEL_SIZE / 2 + (int)lround(cos(angle) * radius) - 8,
                       LIGHT_WHEEL_SIZE / 2 - (int)lround(sin(angle) * radius) - 8);
        light_ui_enable(light_wheel, allowed);
    }
    else light_ui_error = true;
    light_ui_enable(light_temperature, allowed && circuit->white);
    light_ui_enable(light_brightness, allowed);
    if (lv_slider_get_value(light_brightness) != (int)light_edit.brightness)
        lv_slider_set_value(light_brightness, (int)light_edit.brightness, LV_ANIM_OFF);
    char percent[24];
    snprintf(percent, sizeof(percent), "%u%%", light_edit.brightness);
    label_text_if_changed(light_brightness_label, light_edit.valid ? percent : tr("Nincs adat", "No data"));
    light_syncing = false;
}

static int light_ui_row_y(unsigned index)
{
    return (int)index * LIGHT_ROW_PITCH + (light_expanded >= 0 && (int)index > light_expanded ? LIGHT_ROW_EXTRA : 0);
}

static void light_ui_expand_event(lv_event_t *event)
{
    light_ui_row_t *row = lv_event_get_user_data(event);
    if (!row || !row->bound || row->type != LX_LIGHT_COLOR || light_touch) return;
    light_ui_picker_free();
    light_expanded = light_expanded == (int)row->index ? -1 : (int)row->index;
    lv_obj_set_height(light_content, (int)light_count * LIGHT_ROW_PITCH + (light_expanded >= 0 ? LIGHT_ROW_EXTRA : 0));
    lighting_ui_refresh_rows();
    lv_event_stop_bubbling(event);
}

static void light_ui_bind(light_ui_row_t *row, unsigned index, const lx_light_circuit_t *circuit)
{
    if (row->bound && row->index == index && row->type == circuit->type) return;
    if (row == light_picker_row) light_ui_picker_free();
    if (row->control) lv_obj_delete(row->control);
    if (row->swatch) lv_obj_delete(row->swatch);
    if (row->arrow) lv_obj_delete(row->arrow);
    row->control = row->swatch = row->arrow = NULL;
    row->index = index;
    row->type = circuit->type;
    row->bound = true;
    lv_obj_remove_flag(row->root, LV_OBJ_FLAG_CLICKABLE);
    light_ui_hidden(row->level, circuit->type == LX_LIGHT_SWITCH);
    lv_obj_set_pos(row->level, 708, 27);
    lv_obj_set_width(row->level, 104);
    if (circuit->type == LX_LIGHT_SWITCH)
    {
        row->control = lv_switch_create(row->root);
        lv_obj_set_pos(row->control, 742, 24);
        lv_obj_set_size(row->control, 64, 36);
        lv_obj_set_ext_click_area(row->control, 8);
        lv_obj_set_style_bg_color(row->control, c(0x008C83), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_remove_flag(row->control, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(row->control, light_ui_switch_event, LV_EVENT_VALUE_CHANGED, row);
    }
    else if (circuit->type == LX_LIGHT_DIMMER)
        row->control = light_ui_slider(row->root, 420, 36, 260, row);
    else if (circuit->type == LX_LIGHT_COLOR)
    {
        lv_obj_add_flag(row->root, LV_OBJ_FLAG_CLICKABLE);
        row->swatch = shape(row->root, 640, 24, 36, 36, 0xFFFFFF, 18);
        lv_obj_remove_flag(row->swatch, LV_OBJ_FLAG_CLICKABLE);
        row->arrow = light_ui_label(row->root, ">", 796, 26, 24);
        lv_obj_set_pos(row->level, 690, 27);
        lv_obj_set_width(row->level, 100);
    }
    else
    {
        lv_obj_set_pos(row->level, 520, 27);
        lv_obj_set_width(row->level, 292);
    }
}

static void lighting_ui_refresh_rows(void)
{
    if (!light_rows || !light_content || light_refreshing) return;
    light_refreshing = true;
    int scroll = lv_obj_get_scroll_y(light_body);
    unsigned first = 0;
    while (first < light_count)
    {
        int height = LIGHT_ROW_HEIGHT + ((int)first == light_expanded ? LIGHT_ROW_EXTRA : 0);
        if (light_ui_row_y(first) + height > scroll) break;
        ++first;
    }
    for (unsigned slot = 0; slot < LIGHT_UI_ROWS; ++slot)
    {
        light_ui_row_t *row = &light_rows[slot];
        unsigned index = first + slot;
        if (index >= light_count)
        {
            if (row == light_picker_row) light_ui_picker_free();
            light_ui_hidden(row->root, true);
            continue;
        }
        lx_light_circuit_t circuit;
        if (!app_loxone_light_circuit(light_handle, index, &circuit)) continue;
        light_ui_bind(row, index, &circuit);
        light_ui_hidden(row->root, false);
        lv_obj_set_y(row->root, light_ui_row_y(index));
        lv_obj_set_height(row->root, LIGHT_ROW_HEIGHT + ((int)index == light_expanded ? LIGHT_ROW_EXTRA : 0));
        label_text_if_changed(row->name, circuit.name);
        bool allowed = light_ui_allowed(&circuit);
        bool editing = light_touch && light_touch_index == (int)index;
        const lx_light_value_t *value = editing ? &light_edit : &circuit.actual;
        char percent[24];
        snprintf(percent, sizeof(percent), "%u%%", value->brightness);
        label_text_if_changed(row->level, circuit.type == LX_LIGHT_UNSUPPORTED ? tr("Nem támogatott", "Unsupported") :
            value->valid ? percent : tr("Nincs adat", "No data"));
        light_syncing = true;
        if (!editing && circuit.type == LX_LIGHT_SWITCH)
        {
            if (value->brightness) lv_obj_add_state(row->control, LV_STATE_CHECKED);
            else lv_obj_remove_state(row->control, LV_STATE_CHECKED);
        }
        if (!editing && circuit.type == LX_LIGHT_DIMMER && lv_slider_get_value(row->control) != (int)value->brightness)
            lv_slider_set_value(row->control, (int)value->brightness, LV_ANIM_OFF);
        light_ui_enable(row->control, allowed);
        if (row->swatch) bg_color_if_changed(row->swatch, c(light_ui_color(value)), 0);
        if (row->arrow) label_text_if_changed(row->arrow, (int)index == light_expanded ? "v" : ">");
        light_syncing = false;
        if ((int)index == light_expanded && circuit.type == LX_LIGHT_COLOR)
        {
            if (light_picker_row != row) light_ui_picker_create(row, &circuit);
            if (!editing) light_edit = circuit.actual;
            lighting_ui_sync_picker(&circuit);
        }
    }
    light_refreshing = false;
}

static void light_ui_scroll(lv_event_t *event)
{
    (void)event;
    if (!light_touch) lighting_ui_refresh_rows();
}

static void lighting_ui_update(void)
{
    if (!light_overlay) return;
    app_loxone_light_info_t info;
    if (!app_loxone_light_info(light_handle, &info))
    {
        lighting_ui_reset();
        return;
    }
    if (!info.ready)
    {
        label_text_if_changed(light_status, info.failed ? tr("A világítási körök nem tölthetők be.", "Unable to load lighting circuits.") :
            tr("Világítási körök betöltése...", "Loading lighting circuits..."));
        return;
    }
    if (!light_rows && light_rows_failed)
    {
        label_text_if_changed(light_status, tr("Nincs elegendő memória. Zárd be és nyisd újra.", "Not enough memory. Close and reopen."));
        return;
    }
    if (!light_rows)
    {
        light_rows = light_ui_alloc(LIGHT_UI_ROWS, sizeof(*light_rows));
        if (!light_rows)
        {
            light_rows_failed = true;
            label_text_if_changed(light_status, tr("Nincs elegendő memória.", "Not enough memory."));
            return;
        }
        light_count = info.circuit_count;
        lv_obj_set_height(light_content, (int)light_count * LIGHT_ROW_PITCH);
        for (unsigned i = 0; i < LIGHT_UI_ROWS; ++i)
        {
            light_ui_row_t *row = &light_rows[i];
            row->root = shape(light_content, 0, 0, LIGHT_ROW_WIDTH, LIGHT_ROW_HEIGHT, light_ui_background(), 16);
            lv_obj_remove_flag(row->root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
            lv_obj_add_event_cb(row->root, light_ui_expand_event, LV_EVENT_CLICKED, row);
            row->name = light_ui_label(row->root, "", 16, 27, 380);
            row->level = light_ui_label(row->root, "", 708, 27, 104);
            lv_obj_set_style_text_align(row->level, LV_TEXT_ALIGN_RIGHT, 0);
        }
    }
    lighting_ui_refresh_rows();
    label_text_if_changed(light_status, info.failed || light_ui_error ?
        tr("A parancs nem sikerült. Ellenőrizd a kapcsolatot és a jogosultságot.", "Command failed. Check connection and permissions.") :
        !light_count ? tr("Nincs külön vezérelhető világítási kör.", "No separately controllable lighting circuits.") : "");
}

static void light_ui_open(unsigned index)
{
    if (light_overlay || index >= home_config.card_count || s_pair_pad || loxone_display_is_held() ||
        home_request_get() != 2 + (int)home_card_page || !home_connected) return;
    unsigned handle = app_loxone_light_open(home_config.cards[index].id, home_revision);
    if (!handle) return;
    light_handle = handle;
    light_expanded = -1;
    light_overlay = shape(s_screen, 0, 0, 1280, 800, 0x000000, 0);
    bg_opa_if_changed(light_overlay, 150, 0);
    lv_obj_remove_flag(light_overlay, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(light_overlay, light_ui_close_event, LV_EVENT_CLICKED, NULL);
    light_panel = shape(light_overlay, 190, 60, 900, 680, home_night ? 0x14272D : 0xFBFDF9, 24);
    lv_obj_remove_flag(light_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *title = lbl(light_panel, home_config.cards[index].name, 24, 24, APP_FONT_28, light_ui_foreground());
    lv_obj_set_width(title, 748);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *close = pairing_button(light_panel, "X", 820, 12, 56, 56);
    lv_obj_add_event_cb(close, light_ui_close_event, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_flag(close, LV_OBJ_FLAG_GESTURE_BUBBLE);
    light_body = lv_obj_create(light_panel);
    lv_obj_remove_style_all(light_body);
    lv_obj_set_pos(light_body, 24, 86);
    lv_obj_set_size(light_body, 852, 550);
    lv_obj_set_scroll_dir(light_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(light_body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(light_body, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_event_cb(light_body, light_ui_scroll, LV_EVENT_SCROLL, NULL);
    light_content = shape(light_body, 6, 0, LIGHT_ROW_WIDTH, 1, 0, 0);
    bg_opa_if_changed(light_content, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(light_content, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    light_status = lbl(light_panel, tr("Világítási körök betöltése...", "Loading lighting circuits..."),
                       24, 646, APP_FONT_16, light_ui_muted());
    lv_obj_set_width(light_status, 852);
    lv_label_set_long_mode(light_status, LV_LABEL_LONG_MODE_DOTS);
    lighting_ui_update();
}

static void light_card_open_event(lv_event_t *event)
{
    light_ui_open((unsigned)(uintptr_t)lv_event_get_user_data(event));
    lv_event_stop_bubbling(event);
}

static void light_card_command_event(lv_event_t *event)
{
    unsigned encoded = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    unsigned index = encoded / 4, operation = encoded % 4;
    if (light_overlay || s_pair_pad || loxone_display_is_held() || !home_connected || home_command_wait ||
        home_request_get() != 2 + (int)home_card_page || index >= home_config.card_count || operation > 2) return;
    lx_home_command_t action = operation == 0 ? LX_HOME_COMMAND_LIGHT_PREV :
        operation == 1 ? LX_HOME_COMMAND_LIGHT_OFF : LX_HOME_COMMAND_LIGHT_NEXT;
    if (app_loxone_home_command(home_config.cards[index].id, action) == ESP_OK)
    {
        home_command_wait = true;
        snprintf(home_status, sizeof(home_status), "%s", tr("Kérés küldése...", "Sending request..."));
    }
    lv_event_stop_bubbling(event);
}

static void lighting_card_build(lv_obj_t *card, lv_obj_t *title, unsigned slot, unsigned index)
{
    lv_image_set_pivot(home_icons[slot], 0, 0);
    lv_image_set_scale(home_icons[slot], 192);
    lv_obj_set_pos(home_icons[slot], 110, 6);
    lv_obj_add_flag(home_icons[slot], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_icons[slot], light_card_open_event, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    lv_obj_set_y(title, 82);
    home_labels[slot] = lbl(card, "", 10, 112, APP_FONT_16, light_ui_muted());
    home_text(home_labels[slot], 272);
    const char *captions[] = {"-", tr("Ki", "Off"), "+"};
    for (unsigned button = 0; button < 3; ++button)
    {
        lv_obj_t *object = pairing_button(card, captions[button], 16 + (int)button * 92, 146, 76, 44);
        bg_color_if_changed(object, c(0x007D7D), 0);
        lv_obj_set_style_radius(object, 20, 0);
        lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(object, light_card_command_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(index * 4 + button));
        light_card_buttons[slot][button] = object;
    }
}

static void lighting_card_update(unsigned slot, unsigned index, bool valid)
{
    lx_light_summary_t summary;
    bool current = app_loxone_light_summary(home_config.cards[index].id, home_revision, &summary);
    char text[128];
    if (!current || !summary.available || summary.label == LX_LIGHT_UNKNOWN)
        snprintf(text, sizeof(text), "%s", tr("Nincs adat", "No data"));
    else if (summary.label == LX_LIGHT_OFF)
        snprintf(text, sizeof(text), "%s", tr("Kikapcsolva", "Off"));
    else if (summary.label == LX_LIGHT_ONE_MOOD)
        snprintf(text, sizeof(text), "%s", summary.name);
    else if (summary.label == LX_LIGHT_MANY_MOODS)
        snprintf(text, sizeof(text), tr("%u aktív világítási kép", "%u active moods"), summary.active_count);
    else
        snprintf(text, sizeof(text), "%s", tr("Egyedi beállítás", "Custom setting"));
    label_text_if_changed(home_labels[slot], text);
    bool enabled = valid && current && summary.controllable && !home_command_wait;
    for (unsigned button = 0; button < 3; ++button)
        light_ui_enable(light_card_buttons[slot][button], enabled);
    light_ui_enable(home_icons[slot], valid && current && summary.available);
}
