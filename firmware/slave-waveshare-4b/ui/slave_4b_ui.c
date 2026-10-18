/* A háttérbe égetett illusztrációk előtt minden mérési adat natív LVGL elem.
 * Érvénytelen/lejárt minta esetén nincs mozgás és nincs régi teljesítményérték. */
#include "slave_4b_ui.h"
#include "app_flow_model.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

LV_FONT_DECLARE(app_font_16);
LV_FONT_DECLARE(app_font_20);
LV_FONT_DECLARE(app_font_28);

typedef struct
{
    lv_obj_t *ring, *path, *dots[3], *name, *value;
    float phase;
    int x, y, radius;
} node_t;

static lv_obj_t *root, *background, *hub, *soc, *battery_detail;
static lv_obj_t *temperature, *wind, *clock_label, *weather, *wind_icon;
static lv_obj_t *message, *diagnostics, *pin_pad, *pin_mask, *pin_countdown;
static node_t nodes[4]; /* Hálózat, akku, napelem, ház. */
static uint32_t pin_token;
static char digits[7];
static bool last_night, layout_ready;

static void visible(lv_obj_t *object, bool show)
{
    if (show)
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return object;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       int width, const lv_font_t *font)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_obj_set_style_text_font(object, font, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
    lv_label_set_text(object, text);
    return object;
}

static lv_obj_t *image(const lv_image_dsc_t *source, int x, int y, int size)
{
    lv_obj_t *object = lv_image_create(root);
    lv_image_set_src(object, source);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, size, size);
    lv_image_set_inner_align(object, LV_IMAGE_ALIGN_STRETCH);
    return object;
}

static void diagnostics_toggle(lv_event_t *event)
{
    (void)event;
    visible(diagnostics, lv_obj_has_flag(diagnostics, LV_OBJ_FLAG_HIDDEN));
}

static void layout(bool night)
{
    /* A két elfogadott illusztráció csomópontjai kissé eltérő helyen vannak. */
    static const int day[4][3] = {{360, 161, 61}, {86, 360, 58},
                                  {633, 360, 58}, {360, 576, 63}};
    static const int dark[4][3] = {{360, 140, 57}, {90, 353, 55},
                                   {629, 353, 57}, {360, 568, 60}};
    const int (*positions)[3] = night ? dark : day;
    lv_image_set_src(background, night ? &slave_4b_night : &slave_4b_day);
    for (int i = 0; i < 4; i++)
    {
        node_t *node = &nodes[i];
        node->x = positions[i][0];
        node->y = positions[i][1];
        node->radius = positions[i][2];
        lv_obj_set_pos(node->ring, node->x - node->radius, node->y - node->radius);
        lv_obj_set_size(node->ring, node->radius * 2, node->radius * 2);
        if (i == 0 || i == 3)
        {
            lv_obj_set_pos(node->name, node->x + node->radius + 12, node->y - 28);
            lv_obj_set_pos(node->value, node->x + node->radius + 12, node->y + 1);
        }
        else
        {
            lv_obj_set_pos(node->name, node->x - 82, node->y + node->radius + 6);
            lv_obj_set_pos(node->value, node->x - 82, node->y + node->radius + 33);
        }
        lv_obj_set_style_text_color(node->name, lv_color_hex(night ? 0xF5FAFF : 0x00183D), 0);
    }
    /* A rendszertéma vékony, alulról felfelé töltődő SOC sávja. */
    lv_obj_set_pos(soc, night ? 68 : 64, night ? 327 : 332);
    lv_obj_set_pos(battery_detail, 8, night ? 469 : 478);
    lv_color_t text = lv_color_hex(night ? 0xF5FAFF : 0x00183D);
    lv_obj_set_style_text_color(root, text, 0);
    last_night = night;
    layout_ready = true;
}

void slave_4b_ui_create(lv_obj_t *screen)
{
    root = screen;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x051A35), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    background = image(&slave_4b_day, 0, 0, 720);
    for (int i = 0; i < 4; i++)
    {
        node_t *node = &nodes[i];
        node->path = box(root, 0, 0, 1, 1);
        lv_obj_set_style_bg_opa(node->path, LV_OPA_COVER, 0);
        node->ring = box(root, 0, 0, 120, 120);
        lv_obj_set_style_radius(node->ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(node->ring, 3, 0);
        for (int j = 0; j < 3; j++)
        {
            node->dots[j] = box(root, 0, 0, 9, 9);
            lv_obj_set_style_radius(node->dots[j], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(node->dots[j], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(node->dots[j], lv_color_hex(0xFFFCC1), 0);
            lv_obj_set_style_border_width(node->dots[j], 2, 0);
        }
    }
    hub = image(&slave_4b_hub, 339, 339, 42);
    static const char *names[] = {"Hálózat", "Akkumulátor", "Napelem", "Ház"};
    for (int i = 0; i < 4; i++)
    {
        nodes[i].name = label(root, names[i], 0, 0, 164, &app_font_20);
        nodes[i].value = label(root, "—", 0, 0, 164, &app_font_28);
        if (i == 1 || i == 2)
        {
            lv_obj_set_style_text_align(nodes[i].name, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_align(nodes[i].value, LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    label(root, "Zolee Energy", 26, 27, 248, &app_font_28);
    weather = image(&slave_4b_weather_partly, 280, 20, 46);
    temperature = label(root, "— °C", 332, 32, 99, &app_font_20);
    wind_icon = image(&slave_4b_wind, 442, 25, 36);
    wind = label(root, "— km/h", 484, 32, 108, &app_font_20);
    clock_label = label(root, "--:--", 604, 25, 112, &app_font_28);
    battery_detail = label(root, "— % · — °C", 8, 478, 160, &app_font_16);
    lv_obj_set_style_text_align(battery_detail, LV_TEXT_ALIGN_CENTER, 0);
    soc = lv_bar_create(root);
    lv_obj_remove_style_all(soc);
    lv_obj_set_size(soc, 5, 62);
    lv_bar_set_orientation(soc, LV_BAR_ORIENTATION_VERTICAL);
    lv_bar_set_range(soc, 0, 100);
    lv_obj_set_style_bg_opa(soc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(soc, lv_color_hex(0x29302C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(soc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(soc, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(soc, 3, LV_PART_INDICATOR);
    message = label(root, "Master keresése…", 168, 674, 384, &app_font_16);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    diagnostics = label(root, "Slave", 180, 110, 360, &app_font_20);
    lv_obj_set_style_bg_color(diagnostics, lv_color_hex(0x10243E), 0);
    lv_obj_set_style_bg_opa(diagnostics, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(diagnostics, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(diagnostics, 20, 0);
    visible(diagnostics, false);
    lv_obj_t *diagnostics_button = box(root, 590, 0, 130, 78);
    lv_obj_add_flag(diagnostics_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(diagnostics_button, diagnostics_toggle, LV_EVENT_CLICKED, NULL);
    layout(false);
}

static void flow(node_t *node, double kw, bool inward, uint32_t tone, float dt)
{
    bool active = fabs(kw) >= .0005;
    lv_color_t color = lv_color_hex(active ? tone : last_night ? 0x87A9D4 : 0x506C84);
    lv_obj_set_style_border_color(node->ring, color, 0);
    lv_obj_set_style_bg_color(node->path, color, 0);
    lv_obj_set_style_text_color(node->value, color, 0);
    bool vertical = node->x == 360;
    int end_x = vertical ? 360 : node->x + (node->x < 360 ? node->radius : -node->radius);
    int end_y = vertical ? node->y + (node->y < 360 ? node->radius : -node->radius) : 360;
    int distance = vertical ? abs(end_y - 360) : abs(end_x - 360);
    lv_obj_set_pos(node->path, vertical ? 358 : (end_x < 360 ? end_x : 360),
                    vertical ? (end_y < 360 ? end_y : 360) : 358);
    lv_obj_set_size(node->path, vertical ? 4 : distance, vertical ? distance : 4);
    /* A fázis a külső végponttól a hub felé nő; az előjel csak az irányt váltja. */
    node->phase = app_flow_advance(node->phase, kw, inward, dt);
    for (int j = 0; j < 3; j++)
    {
        float fraction = fmodf(node->phase + j / 3.f, 1.f);
        lv_obj_set_pos(node->dots[j], (int)(end_x + (360 - end_x) * fraction) - 4,
                        (int)(end_y + (360 - end_y) * fraction) - 4);
        lv_obj_set_style_border_color(node->dots[j], color, 0);
        visible(node->dots[j], active);
    }
}

void slave_4b_ui_message(const char *text)
{
    lv_label_set_text(message, text ? text : "");
}

void slave_4b_ui_update(const app_energy_sample_t *sample, bool available,
                    const app_runtime_state_t *runtime, const char *clock, float seconds)
{
    app_energy_sample_t empty = {0};
    if (!sample)
        sample = &empty;
    if (!layout_ready || last_night != sample->night_mode)
        layout(sample->night_mode);
    double powers[4] = {sample->status == 3 ? 0 : sample->power_grid_kw,
                        sample->power_bat_kw, sample->power_pv_kw, sample->power_full_kw};
    bool inward[4] = {powers[0] > 0, powers[1] > 0, true, false};
    uint32_t green = last_night ? 0x58E93C : 0x087E12;
    uint32_t orange = last_night ? 0xFFC44D : 0xC94800;
    uint32_t tones[4] = {powers[0] < 0 ? green : orange,
                         powers[1] < 0 ? green : orange, green, orange};
    char text[256];
    for (int i = 0; i < 4; i++)
    {
        flow(&nodes[i], available ? powers[i] : 0, inward[i], tones[i], seconds);
        if (!available)
            snprintf(text, sizeof(text), "—");
        else if (fabs(powers[i]) < 1)
            snprintf(text, sizeof(text), "%.0f W", fabs(powers[i]) * 1000);
        else
            snprintf(text, sizeof(text), "%.1f kW", fabs(powers[i]));
        lv_label_set_text(nodes[i].value, text);
    }
    if (available)
        snprintf(text, sizeof(text), "%.0f%% · %.0f °C", sample->soc_bat_pct, sample->battery_temperature_c);
    else
        snprintf(text, sizeof(text), "— %% · — °C");
    lv_label_set_text(battery_detail, text);
    lv_bar_set_value(soc, available ? (int)lround(fmax(0, fmin(100, sample->soc_bat_pct))) : 0, LV_ANIM_OFF);
    uint32_t soc_color = sample->soc_bat_pct < 20 ? 0xEF443D : sample->soc_bat_pct < 40 ? 0xFFAF32 : 0x5BF237;
    lv_obj_set_style_bg_color(soc, lv_color_hex(soc_color), LV_PART_INDICATOR);
    if (available && sample->temperature_valid)
        snprintf(text, sizeof(text), "%.1f °C", sample->temperature_c);
    else
        snprintf(text, sizeof(text), "— °C");
    lv_label_set_text(temperature, text);
    if (available && sample->wind_speed_valid)
        snprintf(text, sizeof(text), "%.0f km/h", sample->wind_speed_kmh);
    else
        snprintf(text, sizeof(text), "— km/h");
    lv_label_set_text(wind, text);
    const lv_image_dsc_t *icon = sample->night_mode ? &slave_4b_weather_moon : &slave_4b_weather_cloud;
    if (sample->precipitation_valid && sample->has_precipitation)
        icon = sample->temperature_valid && sample->temperature_c < -4 ? &slave_4b_weather_snow : &slave_4b_weather_rain;
    else if (!sample->night_mode && sample->sun_radiation_valid && sample->has_sun_radiation)
        icon = &slave_4b_weather_sun;
    else if (!sample->night_mode && sample->power_pv_kw > 1)
        icon = &slave_4b_weather_partly;
    lv_image_set_src(weather, icon);
    visible(weather, available && sample->precipitation_valid);
    visible(temperature, available && sample->temperature_valid);
    visible(wind, available && sample->wind_speed_valid);
    visible(wind_icon, available && sample->wind_speed_valid);
    lv_label_set_text(clock_label, clock ? clock : "--:--");
    if (runtime)
    {
        snprintf(text, sizeof(text), "Waveshare 4B · Slave\n\nMaster: %s\nBLE: %s\nRSSI: %d dBm\nWi-Fi: %s\nIdőszinkron: %s",
                 runtime->ble_peer[0] ? runtime->ble_peer : "keresés…",
                 runtime->slave_connected ? "kapcsolódva" : "keresés…", runtime->ble_rssi,
                 runtime->wifi_connected ? "OK" : "nincs", runtime->time_synced ? "OK" : "várakozás");
        lv_label_set_text(diagnostics, text);
        slave_4b_ui_message(available ? "" : runtime->slave_connected ? "Érvényes master adatra várunk…" : "Master keresése…");
    }
}

static void pin_key(lv_event_t *event)
{
    unsigned key = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    size_t length = strlen(digits);
    if (key < 10 && length < 6)
    {
        digits[length] = (char)('0' + key);
        digits[length + 1] = 0;
    }
    else if (key == 10 && length)
        digits[length - 1] = 0;
    else if (key == 11 && length == 6)
    {
        app_ble_pairing_submit(pin_token, digits);
        memset(digits, 0, sizeof(digits));
    }
    else if (key == 12)
    {
        app_ble_pairing_submit(pin_token, NULL);
        memset(digits, 0, sizeof(digits));
    }
    char mask[7] = "______";
    memset(mask, '*', strlen(digits));
    lv_label_set_text(pin_mask, mask);
}

void slave_4b_ui_pairing(const app_ble_pairing_status_t *status)
{
    if (!status->pin_required)
    {
        if (pin_pad)
            lv_obj_delete(pin_pad);
        pin_pad = NULL;
        memset(digits, 0, sizeof(digits));
        return;
    }
    if (!pin_pad || pin_token != status->token)
    {
        if (pin_pad)
            lv_obj_delete(pin_pad);
        pin_token = status->token;
        memset(digits, 0, sizeof(digits));
        pin_pad = box(root, 0, 0, 720, 720);
        lv_obj_add_flag(pin_pad, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(pin_pad, lv_color_hex(0x10243E), 0);
        lv_obj_set_style_bg_opa(pin_pad, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(pin_pad, lv_color_hex(0xFFFFFF), 0);
        label(pin_pad, "Master párosítása", 140, 30, 460, &app_font_28);
        label(pin_pad, "A master diagnosztikáján látható PIN:", 140, 88, 460, &app_font_20);
        pin_mask = label(pin_pad, "______", 275, 142, 220, &app_font_28);
        for (unsigned i = 0; i < 13; i++)
        {
            unsigned key = i < 9 ? i + 1 : i == 9 ? 10 : i == 10 ? 0 : i == 11 ? 11 : 12;
            char digit[2] = {(char)('0' + key), 0};
            const char *text = key == 10 ? "Törlés" : key == 11 ? "OK" : key == 12 ? "Mégsem" : digit;
            lv_obj_t *button = lv_button_create(pin_pad);
            lv_obj_set_pos(button, i == 12 ? 190 : 140 + (i % 3) * 150, i == 12 ? 612 : 205 + (i / 3) * 90);
            lv_obj_set_size(button, i == 12 ? 340 : 140, 76);
            lv_obj_t *caption = label(button, text, 0, 0, 120, &app_font_20);
            lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(caption);
            lv_obj_add_event_cb(button, pin_key, LV_EVENT_CLICKED, (void *)(uintptr_t)key);
        }
        pin_countdown = label(pin_pad, "", 190, 570, 400, &app_font_16);
    }
    char countdown[64];
    snprintf(countdown, sizeof(countdown), "Hátralévő idő: %u mp", status->seconds);
    lv_label_set_text(pin_countdown, countdown);
}
