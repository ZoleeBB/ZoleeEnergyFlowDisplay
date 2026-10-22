/*
 * LVGL felhasználói felület a 1280x800-as fekvő kijelzőhöz.
 *
 * A modul kezeli az indulási képernyőt, a teljes dashboardot, az energiaáramlás
 * animációját, a nappali/éjszakai témát, a grafikonokat és a kommunikációs
 * hibaoldalakat. A kijelzőt csak az LVGL lock birtokában módosítjuk.
 */
#include "app_ui.h"
#include "app_ble_seed.h"
#include "app_slave_display_input.h"
#include "app_slave_presence_service.h"
#include "app_display_orientation.h"
#include "app_assets.h"
#include "app_events.h"
#include "app_chart.h"
#include "app_flow_model.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/draw/lv_image_decoder_private.h"
#if LV_USE_OS != LV_OS_NONE || LV_DRAW_SW_DRAW_UNIT_CNT != 1
#error "Use LV_OS_NONE and one software draw unit; the adapter serializes LVGL with its own mutex"
#endif
#include "app_state.h"
#include "app_stats.h"
#include "app_storage.h"
#include "app_templates.h"
#include "app_diagnostics.h"
#include "app_loxone.h"
#include "app_loxone_home_layout.h"
#ifndef _MSC_VER
#include <stdatomic.h>
#endif

LV_FONT_DECLARE(app_font_12);
#define APP_FONT_12 (&app_font_12)
LV_FONT_DECLARE(app_font_14);
#define APP_FONT_14 (&app_font_14)
LV_FONT_DECLARE(app_font_16);
#define APP_FONT_16 (&app_font_16)
LV_FONT_DECLARE(app_font_wind_bold_16);
#define APP_FONT_WIND_BOLD_16 (&app_font_wind_bold_16)
LV_FONT_DECLARE(app_font_brand_20);
LV_FONT_DECLARE(app_font_brand_16);
LV_FONT_DECLARE(app_font_18);
#define APP_FONT_18 (&app_font_18)
LV_FONT_DECLARE(app_font_20);
#define APP_FONT_20 (&app_font_20)
LV_FONT_DECLARE(app_font_22);
#define APP_FONT_22 (&app_font_22)
LV_FONT_DECLARE(app_font_28);
#define APP_FONT_28 (&app_font_28)

/* A célpanel logikai felbontása fekvő tájolásban. */
#define W 1280
#define H 800
/* Nappali és éjszakai téma alap színei. */
#define BG_LIGHT 0xF5F7F8
#define BG_DARK 0x000000
#define CARD_LIGHT 0xFFFFFF
#define CARD_DARK 0x111513
#define TEXT_LIGHT 0x101827
#define TEXT_DARK 0xE7ECE9
#define MUTED_LIGHT 0x7A8698
#define MUTED_DARK 0x839087
#define GREEN 0x38B830
#define ORANGE 0xF86810
#define YELLOW 0xF2D600
#define RED 0xE53935
#define BLUE 0x0060C0
#define GRAY 0x96A1AA

/* A boot után RAM-ban tartott konfiguráció és a UI életciklusának állapota. */
static app_config_t s_cfg;
static app_energy_config_t s_display_energy;
static bool s_stats_missing_for_ui;
static bool s_cfg_ok, s_started, s_dash_created, s_last_night;
static lv_obj_t *s_screen, *s_dash, *s_sd_warn;
static lv_obj_t *s_title, *s_date, *s_clock, *s_weather_temp, *s_wind_speed, *s_weather_icon,
    *s_wind_icon;
static lv_obj_t *s_pv, *s_home, *s_bat, *s_grid, *s_soc, *s_bt, *s_status;
static lv_obj_t *s_bat_icon, *s_grid_icon, *s_status_icon;
static lv_obj_t *s_grid_node, *s_battery_node, *s_pv_node, *s_home_node, *s_status_bubble,
    *s_status_detail;
static lv_obj_t *s_battery_bar, *s_status_card;
static lv_obj_t *s_bat_charge, *s_bat_charge_cmp, *s_bat_discharge, *s_bat_discharge_cmp;
static lv_obj_t *s_metric[8], *s_cmp[8], *s_cmp_note[8];
static lv_obj_t *s_chart1, *s_chart2;
static lv_obj_t *s_chart1_y_top, *s_chart1_y_mid, *s_chart1_y_bottom;
static lv_obj_t *s_chart2_y_top, *s_chart2_y_mid, *s_chart2_y_bottom;

static lv_obj_t *s_error_pulse;
static int s_brightness = 50, s_target = 50;
static app_slave_display_t s_slave_display;
static app_slave_display_input_t s_slave_input;
static bool s_slave_display_ready;
static int64_t s_last_chart_update_us;
/* A 20 kB-os snapshot a UI modul tulajdona, nem a task vermét terheli. */
static app_stats_snapshot_t s_stats_snapshot;
static app_stats_period_t s_week_period, s_month_period;
static app_diagnostics_t s_diagnostics;
static lv_obj_t *s_diagnostic_bars[3];
static bool s_diagnostics_requested;
static lv_obj_t *s_diagnostic_values[14], *s_diagnostic_log[15];
static lv_obj_t *s_pair_pad, *s_pair_mask, *s_pair_countdown, *s_pair_window_label;
static char s_pair_digits[7];
static uint32_t s_pair_ui_token;
/* A meglévő RAM-napló másolata; nem a runtime task vermén tároljuk. */
static app_event_entry_t s_diagnostic_events[15];
static size_t s_diagnostic_event_count;
static app_energy_sample_t s_animation_sample;
static int64_t s_animation_us;
static void animation_tick(lv_timer_t *timer);
static bool template_is_active(void);
static void template_animate(float dt);
static bool template_heavy_animation(void);
static unsigned template_catalogue_count(void);
static void navigation_bind_inputs(void);

/* A runtime UI kölcsönösen kizáró fő nézetei. */
typedef enum
{
    VIEW_NONE = 0,
    VIEW_DASHBOARD,
    VIEW_THEME_LOADING,
    VIEW_DIAGNOSTICS,
    VIEW_HOME,
    VIEW_WIFI_ERROR,
    VIEW_MQTT_ERROR,
    VIEW_LOXONE_SETUP,
    VIEW_LOXONE_WAIT,
    VIEW_LOXONE_ERROR,
    VIEW_WAIT_DATA,
    VIEW_DATA_TIMEOUT,
    VIEW_FATAL
} runtime_view_t;

static runtime_view_t s_view = VIEW_NONE;
/* Csak LVGL-lock alatt írjuk/olvassuk, a runtime várakozás közben elengedi. */
static bool s_loading_frame_presented;
static app_boot_stage_t s_boot_stage = APP_BOOT_SD;
static bool home_is_open(void);
static void assistant_reset_widgets(void);
static void home_cards_requested(void);
static void home_gesture(lv_dir_t direction);
static void home_open_clicked(lv_event_t *event);
static void home_bind_hotspot(lv_obj_t *parent, const cJSON *scene);
static void error_screen(runtime_view_t view, const char *title, const char *detail, bool fatal);

/* Az MQTT adatokból származtatott időjárási ikonállapotok. */
typedef enum
{
    WEATHER_SUNNY = 0,
    WEATHER_PARTLY_CLOUDY,
    WEATHER_CLOUDY,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_NIGHT
} weather_kind_t;

static weather_kind_t s_last_weather = (weather_kind_t)-1;

/*
 * Egy energiaáramlási kapcsolat LVGL objektumai.
 * A pontok a teljesítmény irányában és nagyságával arányos sebességgel mozognak.
 */
typedef struct
{
    lv_obj_t *line;
    lv_obj_t *dots[3];
    lv_point_precise_t points[2];
    int x1, y1, x2, y2;
    float phase;
} flow_t;
static flow_t f_pv, f_bat, f_grid, f_home;

/* 24 bites RGB szín átalakítása LVGL színobjektummá. */
static lv_color_t c(uint32_t x)
{
    return lv_color_hex(x);
}
/* Igaz, ha a felület magyar nyelven működik. */
static bool hu(void)
{
    return !s_cfg_ok || strcmp(s_cfg.project.language, "hu") == 0;
}
/* Két beégetett fordítás közül a konfigurált nyelv kiválasztása. */
static const char *tr(const char *a, const char *b)
{
    return hu() ? a : b;
}

/* Éjszakai módban ugyanazon funkciószínek tompított változatának kiválasztása. */
static uint32_t tone(uint32_t color, bool night)
{
    if (!night)
        return color;
    if (color == GREEN)
        return 0x23784C;
    if (color == ORANGE)
        return 0xA46E2D;
    if (color == YELLOW)
        return 0x95852B;
    if (color == RED)
        return 0x8E3A38;
    if (color == BLUE)
        return 0x2B5D8B;
    if (color == GRAY)
        return 0x56615B;
    return color;
}

/* Csak saját, azonos selectorú stílusértéket hagyunk ki: az öröklésből
 * származó egyezés nem helyettesíti a kívánt explicit lokális beállítást. */
static void style_color_if_changed(lv_obj_t *obj,
                                   lv_style_prop_t prop,
                                   lv_color_t color,
                                   lv_style_selector_t selector)
{
    lv_style_value_t old;
    if (lv_obj_get_local_style_prop(obj, prop, &old, selector) == LV_STYLE_RES_FOUND &&
        lv_color_eq(old.color, color))
        return;
    lv_style_value_t value = {.color = color};
    lv_obj_set_local_style_prop(obj, prop, value, selector);
}
static void style_number_if_changed(lv_obj_t *obj,
                                    lv_style_prop_t prop,
                                    int32_t number,
                                    lv_style_selector_t selector)
{
    lv_style_value_t old;
    if (lv_obj_get_local_style_prop(obj, prop, &old, selector) == LV_STYLE_RES_FOUND &&
        old.num == number)
        return;
    lv_style_value_t value = {.num = number};
    lv_obj_set_local_style_prop(obj, prop, value, selector);
}
#define text_color_if_changed(o, v, s) style_color_if_changed(o, LV_STYLE_TEXT_COLOR, v, s)
#define bg_color_if_changed(o, v, s) style_color_if_changed(o, LV_STYLE_BG_COLOR, v, s)
#define border_color_if_changed(o, v, s) style_color_if_changed(o, LV_STYLE_BORDER_COLOR, v, s)
#define opa_if_changed(o, v, s) style_number_if_changed(o, LV_STYLE_OPA, v, s)
#define bg_opa_if_changed(o, v, s) style_number_if_changed(o, LV_STYLE_BG_OPA, v, s)
#define border_opa_if_changed(o, v, s) style_number_if_changed(o, LV_STYLE_BORDER_OPA, v, s)
#define text_opa_if_changed(o, v, s) style_number_if_changed(o, LV_STYLE_TEXT_OPA, v, s)

/* Teljes képernyős háttér alapstílusa. */
static void screen_style(lv_obj_t *o, bool night)
{
    bg_color_if_changed(o, c(night ? BG_DARK : BG_LIGHT), 0);
    bg_opa_if_changed(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    /* Fix méretű képernyőn nincs tartalomgörgetés: a scroll-felismerő
     * elsőbbsége különben még az input szintű gesztust is letiltja. */
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
/* Közös kártyastílus: háttér, keret, lekerekítés és nappali árnyék. */
static void card_style(lv_obj_t *o, bool night)
{
    bg_color_if_changed(o, c(night ? CARD_DARK : CARD_LIGHT), 0);
    bg_opa_if_changed(o, LV_OPA_COVER, 0);
    border_color_if_changed(o, c(night ? 0x26302A : 0xE4E9EB), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 18, 0);
    lv_obj_set_style_pad_all(o, 12, 0);
    lv_obj_set_style_shadow_width(o, night ? 0 : 12, 0);
    lv_obj_set_style_shadow_opa(o, night ? LV_OPA_TRANSP : LV_OPA_10, 0);
    lv_obj_set_style_shadow_ofs_y(o, night ? 0 : 4, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
/* A változatlan szöveg ne foglaljon memóriát és ne érvénytelenítse a rajzterületet. */
static void label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!text || strcmp(lv_label_get_text(label), text) != 0)
    {
        lv_label_set_text(label, text);
    }
}
/* Egységes LVGL címke létrehozása pozícióval, fonttal és színnel. */
static lv_obj_t *
lbl(lv_obj_t *p, const char *txt, int x, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(p);
    label_text_if_changed(l, txt ? txt : "");
    lv_obj_set_pos(l, x, y);
    if (font)
        lv_obj_set_style_text_font(l, font, 0);
    text_color_if_changed(l, c(color), 0);
    return l;
}
/* Az érték nem lóghat át a szomszédos kártyába; a legnagyobb elférő fontot választjuk. */
static void fit_text(lv_obj_t *label, const char *text, int width, const lv_font_t *preferred)
{
    const lv_font_t *fonts[] = {
        APP_FONT_28, APP_FONT_22, APP_FONT_20, APP_FONT_18, APP_FONT_16, APP_FONT_14, APP_FONT_12};
    const lv_font_t *chosen = APP_FONT_12;
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++)
    {
        if (fonts[i]->line_height > preferred->line_height)
            continue;
        lv_point_t size;
        lv_text_get_size(&size, text, fonts[i], 0, 0, 32767, LV_TEXT_FLAG_NONE);
        if (size.x <= width)
        {
            chosen = fonts[i];
            break;
        }
    }
    if (lv_obj_get_style_width(label, 0) != width)
        lv_obj_set_width(label, width);
    if (lv_obj_get_style_text_font(label, 0) != chosen)
        lv_obj_set_style_text_font(label, chosen, 0);
    if (lv_label_get_long_mode(label) != LV_LABEL_LONG_MODE_DOTS)
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    label_text_if_changed(label, text);
}

/* Az aktív képernyő tartalmának törlése és a UI objektumállapot alaphelyzetbe állítása. */
static void clear(void)
{
    assistant_reset_widgets();
    lv_obj_clean(s_screen);
    s_pair_pad = s_pair_mask = s_pair_countdown = s_pair_window_label = NULL;
    memset(s_pair_digits, 0, sizeof(s_pair_digits));
    s_dash = NULL;
    s_dash_created = false;
    s_error_pulse = NULL;
    s_view = VIEW_NONE;
}

/* SOC alapján a specifikált zöld/narancs/piros akkumulátorszín kiválasztása. */
static uint32_t battery_color(double soc)
{
    if (soc > 50.0)
        return GREEN;
    if (soc >= 20.0)
        return ORANGE;
    return RED;
}

/* Egyszerű kitöltött téglalap/kör primitív létrehozása ikonokhoz és jelölőkhöz. */
static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    bg_color_if_changed(obj, c(color), 0);
    bg_opa_if_changed(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

/* Átlátszó dobozba középre helyezett PNG asset létrehozása. */
static lv_obj_t *asset_image_box(lv_obj_t *parent,
                                 int x,
                                 int y,
                                 int box_size,
                                 int image_size,
                                 const lv_image_dsc_t *asset,
                                 bool night)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, box_size, box_size);
    bg_opa_if_changed(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *image = lv_image_create(box);
    lv_image_set_src(image, asset);
    uint32_t source_width = asset->header.w > 0 ? asset->header.w : 128;

    lv_image_set_scale(image, (uint32_t)((image_size * 256U + source_width / 2U) / source_width));
    opa_if_changed(image, night ? LV_OPA_80 : LV_OPA_COVER, 0);
    lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);
    return image;
}

/* Az aktuális időjárási állapothoz tartozó PNG asset kiválasztása. */
static const lv_image_dsc_t *weather_asset(weather_kind_t kind)
{
    switch (kind)
    {
        case WEATHER_SUNNY:
            return &app_img_weather_sun;
        case WEATHER_PARTLY_CLOUDY:
            return &app_img_weather_partly;
        case WEATHER_CLOUDY:
            return &app_img_weather_cloud;
        case WEATHER_RAIN:
            return &app_img_weather_rain;
        case WEATHER_SNOW:
            return &app_img_weather_snow;
        case WEATHER_NIGHT:
        default:
            return &app_img_weather_moon;
    }
}

/* Az időjárás ikon csak állapotváltozáskor cserél assetet. */
static void weather_render(weather_kind_t kind, bool night)
{
    if (!s_weather_icon || kind == s_last_weather)
    {
        return;
    }

    lv_image_set_src(s_weather_icon, weather_asset(kind));

    opa_if_changed(s_weather_icon, night ? LV_OPA_80 : LV_OPA_COVER, 0);

    s_last_weather = kind;
}

/* Lebegőpontos szám lokalizált tizedeselválasztóval történő formázása. */
static void fmt_dec(char *out, size_t n, double v, int d)
{
    snprintf(out, n, "%.*f", d, v);
    if (hu())
    {
        char *p = strchr(out, '.');
        if (p)
            *p = ',';
    }
}
/* Teljesítmény formázása: 1 kW alatt egész W, felette egy tizedes kW. */
static void fmt_power(char *out, size_t n, double kw)
{
    double a = fabs(kw);
    if (a >= 1)
    {
        char q[20];
        fmt_dec(q, sizeof(q), a, 1);
        snprintf(out, n, "%s kW", q);
    }
    else
        snprintf(out, n, "%d W", (int)llround(a * 1000));
}
/* Energia formázása egy tizedes kWh értékre. */
static void fmt_kwh(char *out, size_t n, double v)
{
    char q[20];
    fmt_dec(q, sizeof(q), v, 1);
    snprintf(out, n, "%s kWh", q);
}
/* Százalék formázása egy tizedessel. */
static void fmt_pct(char *out, size_t n, double v)
{
    char q[20];
    fmt_dec(q, sizeof(q), v, 1);
    snprintf(out, n, "%s%%", q);
}
/* Tegnapi összehasonlítás nyíl + százalék szövegének elkészítése. */
static void fmt_cmp(char *out, size_t n, double t, double y)
{
    if (!isfinite(t) || !isfinite(y))
    {
        snprintf(out, n, "—");
        return;
    }
    if (fabs(y) < 1e-9)
    {
        /* Van tegnapi statisztika, de az adott mutató értéke 0.
         * Ez nem azonos a hiányzó adattal! */
        if (fabs(t) < 1e-9)
            snprintf(out, n, "0%%");
        else
            snprintf(out, n, "%s", t > 0 ? "↑ új" : "↓ új");
        return;
    }
    double p = (t - y) / y * 100;
    char q[20];
    fmt_dec(q, sizeof(q), p, 1);
    snprintf(out, n, "%s %s%s%%", p >= 0 ? "↑" : "↓", p >= 0 ? "+" : "", q);
}
/* A részleges adatokat ASCII tilde (~) jelöli, amely minden szövegfontban elérhető.
 * A számláló és a nevező kizárólag ugyanazokból a mért 5 perces
 * időablakokból származik, soha nem a hiányzó órák nulláiból. */
static void fmt_compared(char *out, size_t n, double t, double y, bool partial)
{
    char raw[32];
    fmt_cmp(raw, sizeof(raw), t, y);
    if (partial && strcmp(raw, "—"))
        snprintf(out, n, "~%s", raw);
    else
        snprintf(out, n, "%s", raw);
}
/* Összehasonlítási értékhez zöld, narancs vagy szürke szín választása. */
static uint32_t cmp_color(double t, double y)
{
    if (fabs(y) < 1e-9)
        return GRAY;
    return t >= y ? GREEN : ORANGE;
}

/* A körháttér külön LVGL réteg, az ikon átlátszó PNG. */
static lv_obj_t *icon_bubble(lv_obj_t *parent,
                             int x,
                             int y,
                             int size,
                             const lv_image_dsc_t *asset,
                             bool night,
                             uint32_t accent)
{
    lv_obj_t *bubble = lv_obj_create(parent);
    lv_obj_set_pos(bubble, x, y);
    lv_obj_set_size(bubble, size, size);
    lv_obj_set_style_radius(bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    bg_color_if_changed(bubble, c(tone(accent, night)), 0);
    bg_opa_if_changed(bubble, night ? LV_OPA_10 : LV_OPA_20, 0);
    lv_obj_set_style_pad_all(bubble, 0, 0);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    asset_image_box(bubble,
                    0,
                    0,
                    size,
                    (asset == &app_img_grid_import || asset == &app_img_grid_export) ? size
                                                                                     : size - 12,
                    asset,
                    night);

    return bubble;
}

/* Energia-node: natív, dinamikus körvonal és külön átlátszó PNG. */
static lv_obj_t *energy_circle(lv_obj_t *parent,
                               int center_x,
                               int center_y,
                               int size,
                               const lv_image_dsc_t *asset,
                               lv_obj_t **image_out,
                               bool night,
                               uint32_t accent)
{
    lv_obj_t *outer = lv_obj_create(parent);
    lv_obj_set_pos(outer, center_x - size / 2, center_y - size / 2);
    lv_obj_set_size(outer, size, size);
    lv_obj_set_style_radius(outer, LV_RADIUS_CIRCLE, 0);
    bg_color_if_changed(outer, c(night ? 0x202B30 : 0xF2FBFE), 0);
    bg_opa_if_changed(outer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(outer, 4, 0);
    border_color_if_changed(outer, c(tone(accent, night)), 0);
    lv_obj_set_style_shadow_width(outer, 12, 0);
    lv_obj_set_style_shadow_opa(outer, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(outer, 0, 0);
    lv_obj_remove_flag(outer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *image = asset_image_box(outer, 0, 0, size - 8, size - 16, asset, night);
    /* Az átlátszó képtartó ne nyelje el a szülő energia-node érintését. */
    lv_obj_remove_flag(lv_obj_get_parent(image), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);

    if (image_out)
    {
        *image_out = image;
    }

    return outer;
}

/* Egy energiaáramlási vonal és három animált pont egyszeri létrehozása. */
static void flow_init(flow_t *f, lv_obj_t *p, int x1, int y1, int x2, int y2, bool night)
{
    f->x1 = x1;
    f->y1 = y1;
    f->x2 = x2;
    f->y2 = y2;
    f->phase = 0;
    f->points[0].x = x1;
    f->points[0].y = y1;
    f->points[1].x = x2;
    f->points[1].y = y2;

    f->line = lv_line_create(p);
    lv_line_set_points(f->line, f->points, 2);
    lv_obj_set_style_line_width(f->line, 3, 0);
    lv_obj_set_style_line_color(f->line, c(night ? 0x333B37 : 0xDCE3E5), 0);

    for (int i = 0; i < 3; i++)
    {
        f->dots[i] = lv_obj_create(p);
        lv_obj_set_size(f->dots[i], 9, 9);
        lv_obj_set_style_radius(f->dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(f->dots[i], 0, 0);
        lv_obj_add_flag(f->dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}
/* A körvonal és a kapcsolat ugyanazt a nappali/éjszakai színt kapja. */
static uint32_t flow_color(double kw, uint32_t active_color, bool night)
{
    return fabs(kw) < 0.0005 ? (night ? 0x333B37 : 0xDCE3E5) : tone(active_color, night);
}

/* Energiaáramlási vonal színének, irányának és pontsebességének frissítése. */
static void flow_update(flow_t *f, double kw, bool forward, uint32_t color, bool night, float dt)
{
    /* Közel nulla teljesítménynél csak a halvány statikus kapcsolat marad. */
    double m = fabs(kw);
    if (m < 0.0005)
    {
        lv_obj_set_style_line_color(f->line, c(flow_color(kw, color, night)), 0);
        for (int i = 0; i < 3; i++)
            lv_obj_add_flag(f->dots[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    /*
     * 0..7 kW között lineárisan nő a pontsebesség, 7 kW felett telítődik.
     * Az irányt a forward paraméter határozza meg.
     */
    uint32_t shown = flow_color(kw, color, night);
    lv_obj_set_style_line_color(f->line, c(shown), 0);
    f->phase = app_flow_advance(f->phase, kw, forward, dt);
    for (int i = 0; i < 3; i++)
    {
        float t = f->phase + i / 3.0f;
        while (t >= 1)
            t -= 1;
        int x = (int)lroundf(f->x1 + (f->x2 - f->x1) * t),
            y = (int)lroundf(f->y1 + (f->y2 - f->y1) * t);
        lv_obj_set_pos(f->dots[i], x - 4, y - 4);
        bg_color_if_changed(f->dots[i], c(shown), 0);
        bg_opa_if_changed(f->dots[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(f->dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}
/* Bal oldali, nagy napi mutatókártya az eredeti látványterv vizuális hierarchiájával. */
static lv_obj_t *metric_card(lv_obj_t *parent,
                             int x,
                             int y,
                             int width,
                             int height,
                             const char *title,
                             const lv_image_dsc_t *asset,
                             int index,
                             bool night,
                             uint32_t accent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    card_style(card, night);
    lv_obj_set_style_pad_all(card, 0, 0);
    const int top = (height - (91 + APP_FONT_12->line_height)) / 2;

    icon_bubble(card, 23, (height - 64) / 2, 64, asset, night, accent);

    lv_obj_t *title_label = lbl(card, title, 101, top, APP_FONT_14, night ? TEXT_DARK : TEXT_LIGHT);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title_label, width - 110);

    lbl(card,
        tr("Ma összesen", "Today total"),
        101,
        top + 19,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);

    s_metric[index] = lbl(card, "-", 101, top + 37, APP_FONT_28, night ? TEXT_DARK : TEXT_LIGHT);

    s_cmp[index] = lbl(card, "", 101, top + 71, APP_FONT_14, GREEN);

    s_cmp_note[index] = lbl(card,
                            tr("tegnaphoz képest", "vs yesterday"),
                            101,
                            top + 91,
                            APP_FONT_12,
                            night ? MUTED_DARK : MUTED_LIGHT);

    return card;
}

/* Alsó összefoglaló szalag egy szegmense. */
static lv_obj_t *summary_metric(lv_obj_t *parent,
                                int x,
                                int width,
                                const char *title,
                                const lv_image_dsc_t *asset,
                                int index,
                                bool night,
                                uint32_t accent)
{
    lv_obj_t *segment = lv_obj_create(parent);
    lv_obj_set_pos(segment, x, 0);
    lv_obj_set_size(segment, width, 98);
    const int top = (98 - (52 + APP_FONT_14->line_height)) / 2;
    bg_opa_if_changed(segment, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(segment, 0, 0);
    lv_obj_set_style_pad_all(segment, 0, 0);
    lv_obj_remove_flag(segment, LV_OBJ_FLAG_SCROLLABLE);

    icon_bubble(segment, 4, (98 - 54) / 2, 54, asset, night, accent);

    lv_obj_t *title_label =
        lbl(segment, title, 68, top, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title_label, width - 74);

    s_metric[index] = lbl(segment, "-", 68, top + 23, APP_FONT_20, night ? TEXT_DARK : TEXT_LIGHT);

    s_cmp[index] = lbl(segment, "", 68, top + 52, APP_FONT_12, GREEN);

    s_cmp_note[index] = NULL;
    return segment;
}

/* A teljes 1280x800 dashboard objektumfájának felépítése nappali vagy éjszakai témában. */
static void build_dashboard(bool night)
{
    clear();
    screen_style(s_screen, night);

    s_dash = lv_obj_create(s_screen);
    lv_obj_set_size(s_dash, W, H);
    screen_style(s_dash, night);
    lv_obj_remove_flag(s_dash, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    /*
     * Fejléc: az eredeti látványterv fehér, lekerekített fejlécét tartjuk meg,
     * de a későbbi döntés szerint nincs navigációs tabsor és dátumválasztó.
     */
    lv_obj_t *header = lv_obj_create(s_dash);
    lv_obj_set_pos(header, 20, 12);
    lv_obj_set_size(header, 1240, 58);
    card_style(header, night);
    lv_obj_set_style_radius(header, 22, 0);
    lv_obj_set_style_pad_all(header, 6, 0);

    icon_bubble(header, 4, 2, 42, &app_img_header_home_leaf, night, GREEN);

    s_title = lbl(header, s_cfg.project.title, 54, -1, APP_FONT_22, night ? TEXT_DARK : TEXT_LIGHT);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_title, 350);

    /* Beégetett programnév, a konfigurálható projektcímtől függetlenül. */
    lv_obj_t *brand = lv_obj_create(header);
    lv_obj_remove_style_all(brand);
    lv_obj_set_size(brand, 224, 44);
    lv_obj_remove_flag(brand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 430, 0);
    lv_obj_t *brand_name =
        lbl(brand, "Zolee", 0, 0, &app_font_brand_20, night ? 0xA9C5DC : 0x245878);
    lv_obj_set_width(brand_name, 224);
    lv_obj_set_style_text_align(brand_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *brand_detail =
        lbl(brand, "Energy Folow Monitor", 0, 25, &app_font_brand_16, night ? 0xA9C5DC : 0x245878);
    lv_obj_set_width(brand_detail, 224);
    lv_obj_set_style_text_align(brand_detail, LV_TEXT_ALIGN_CENTER, 0);

    lbl(header,
        tr("Valós idejű energiaáramlás", "Real-time energy flow"),
        54,
        29,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);

    s_date = lbl(header, "", 675, 17, APP_FONT_16, night ? MUTED_DARK : MUTED_LIGHT);

    s_weather_icon = asset_image_box(header, 850, 2, 46, 42, &app_img_weather_sun, night);
    s_last_weather = (weather_kind_t)-1;

    s_weather_temp = lbl(header, "", 900, 17, APP_FONT_16, night ? TEXT_DARK : TEXT_LIGHT);

    s_wind_icon = asset_image_box(header, 985, 8, 36, 30, &app_img_wind, night);

    s_wind_speed = lbl(header, "", 1020, 17, APP_FONT_16, night ? TEXT_DARK : TEXT_LIGHT);

    s_clock = lbl(header, "", 1125, 9, APP_FONT_28, night ? TEXT_DARK : TEXT_LIGHT);

    /* A szövegek tényleges magassága alapján középre igazítunk.
     * A kép saját dobozát igazítjuk, így a PNG középpontja is ugyanott van. */
    lv_obj_align(s_date, LV_ALIGN_LEFT_MID, 675, 0);
    lv_obj_align(s_weather_temp, LV_ALIGN_LEFT_MID, 900, 0);
    lv_obj_align(s_wind_speed, LV_ALIGN_LEFT_MID, 1020, 0);
    lv_obj_align(s_clock, LV_ALIGN_LEFT_MID, 1125, 0);
    lv_obj_align(lv_obj_get_parent(s_weather_icon), LV_ALIGN_LEFT_MID, 850, 0);
    lv_obj_align(lv_obj_get_parent(s_wind_icon), LV_ALIGN_LEFT_MID, 985, 0);

    /*
     * Bal oldali napi kártyák: az eredeti mockup nagy, ikonközpontú
     * összefoglaló kártyáinak szerkezete.
     */
    metric_card(
        s_dash, 20, 82, 300, 132, tr("Mai termelés", "Production"), &app_img_leaf, 0, night, GREEN);

    metric_card(s_dash,
                20,
                224,
                300,
                132,
                tr("Mai fogyasztás", "Consumption"),
                &app_img_house,
                1,
                night,
                YELLOW);

    metric_card(s_dash,
                20,
                366,
                300,
                132,
                tr("Hálózati vételezés", "Grid import"),
                &app_img_grid_import,
                2,
                night,
                ORANGE);

    metric_card(s_dash,
                20,
                508,
                300,
                142,
                tr("Visszatáplálás", "Grid export"),
                &app_img_grid_export,
                3,
                night,
                GREEN);

    /*
     * Központi energiaáramlás: az eredeti látványterv szerint ez a képernyő
     * fő vizuális eleme. A négy energiaelem egy szürke központi hub körül áll.
     */
    lv_obj_t *flow = lv_obj_create(s_dash);
    lv_obj_set_pos(flow, 332, 82);
    lv_obj_set_size(flow, 490, 492);
    card_style(flow, night);
    lv_obj_set_style_pad_all(flow, 0, 0);

    lbl(flow,
        tr("Energiaáramlás", "Energy flow"),
        16,
        12,
        APP_FONT_18,
        night ? TEXT_DARK : TEXT_LIGHT);

    /* A kapcsolatokat előbb hozzuk létre, hogy a kör alakú node-ok föléjük kerüljenek. */
    flow_init(&f_grid, flow, 245, 146, 245, 210, night);
    flow_init(&f_bat, flow, 146, 245, 210, 245, night);
    flow_init(&f_pv, flow, 344, 245, 280, 245, night);
    flow_init(&f_home, flow, 245, 280, 245, 344, night);

    /* Központi energiaelosztó: a látványterv szürke, villámos hub grafikája. */
    asset_image_box(flow, 210, 210, 70, 70, &app_img_hub, night);

    /* Hálózat felül. */
    s_grid_node = energy_circle(flow, 245, 100, 92, &app_img_grid, &s_grid_icon, night, GREEN);

    s_grid = lbl(flow, "-", 170, 12, APP_FONT_22, night ? TEXT_DARK : TEXT_LIGHT);
    lv_obj_set_width(s_grid, 150);
    lv_obj_set_style_text_align(s_grid, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *grid_name =
        lbl(flow, tr("Hálózat", "Grid"), 170, 35, APP_FONT_14, night ? MUTED_DARK : MUTED_LIGHT);
    lv_obj_set_width(grid_name, 150);
    lv_obj_set_style_text_align(grid_name, LV_TEXT_ALIGN_CENTER, 0);

    /* Akkumulátor bal oldalon. */
    s_battery_node =
        energy_circle(flow, 100, 245, 92, &app_img_battery, &s_bat_icon, night, ORANGE);

    /*
     * A kör alakú akkumulátor-grafikán belül külön SOC sáv is látható.
     * A függőleges sáv az ikon bal oldalába illesztve alulról felfelé jelzi a SOC-ot.
     * Színe a töltöttséget követi. Az ikon állandó PNG;
     * a körvonal színe kizárólag az energiaáramlás színét követi.
     */
    s_battery_bar = lv_bar_create(s_battery_node);
    lv_obj_set_pos(s_battery_bar, 24, 23);
    lv_obj_set_size(s_battery_bar, 4, 46);
    lv_bar_set_orientation(s_battery_bar, LV_BAR_ORIENTATION_VERTICAL);
    lv_bar_set_range(s_battery_bar, 0, 100);
    bg_color_if_changed(s_battery_bar, c(night ? 0x29302C : 0xE4E9E6), LV_PART_MAIN);
    bg_opa_if_changed(s_battery_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_battery_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_battery_bar, 4, LV_PART_INDICATOR);

    lbl(flow, tr("Akkumulátor", "Battery"), 6, 145, APP_FONT_14, night ? MUTED_DARK : MUTED_LIGHT);

    s_bat = lbl(flow, "-", 6, 166, APP_FONT_22, night ? TEXT_DARK : TEXT_LIGHT);

    s_soc = lbl(flow, "", 6, 193, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);

    s_bt = lbl(flow, "", 6, 211, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);

    /* Napelem jobb oldalon. */
    s_pv_node = energy_circle(flow, 390, 245, 92, &app_img_solar, NULL, night, GREEN);

    lbl(flow, tr("Napelem", "Solar"), 326, 145, APP_FONT_14, night ? MUTED_DARK : MUTED_LIGHT);

    s_pv = lbl(flow, "-", 326, 166, APP_FONT_22, night ? TEXT_DARK : TEXT_LIGHT);

    /* Ház alul. */
    s_home_node = energy_circle(flow, 245, 390, 92, &app_img_home_scene, NULL, night, ORANGE);
    lv_obj_add_flag(s_home_node, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_home_node, home_open_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *home_name =
        lbl(flow, tr("Ház", "Home"), 170, 439, APP_FONT_14, night ? MUTED_DARK : MUTED_LIGHT);
    lv_obj_set_width(home_name, 150);
    lv_obj_set_style_text_align(home_name, LV_TEXT_ALIGN_CENTER, 0);

    s_home = lbl(flow, "-", 170, 459, APP_FONT_22, night ? TEXT_DARK : TEXT_LIGHT);
    lv_obj_set_width(s_home, 150);
    lv_obj_set_style_text_align(s_home, LV_TEXT_ALIGN_CENTER, 0);

    /* Önálló, az energiaáramlással azonos szélességű napi akkukártya. */
    lv_obj_t *battery_detail = lv_obj_create(s_dash);
    lv_obj_set_pos(battery_detail, 332, 586);
    lv_obj_set_size(battery_detail, 490, 64);
    card_style(battery_detail, night);
    lv_obj_set_style_pad_all(battery_detail, 0, 0);

    /* Akku infó ikon: a meglévő részletes PNG és külön információjel. */
    asset_image_box(battery_detail, 12, 7, 50, 46, &app_img_battery, night);
    lv_obj_t *info_badge =
        shape(battery_detail, 48, 40, 16, 16, tone(BLUE, night), LV_RADIUS_CIRCLE);
    lv_obj_t *info_letter = lbl(info_badge, "i", 0, 0, APP_FONT_12, 0xFFFFFF);
    lv_obj_center(info_letter);
    shape(battery_detail, 274, 10, 1, 44, night ? 0x34433D : 0xE4E9EB, 0);
    lbl(battery_detail,
        tr("Össz. töltés ma", "Charged today"),
        80,
        7,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);
    lbl(battery_detail,
        tr("Össz. leadás ma", "Discharged today"),
        288,
        7,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);
    s_bat_charge = lbl(battery_detail, "", 80, 23, APP_FONT_20, night ? TEXT_DARK : TEXT_LIGHT);
    s_bat_discharge = lbl(battery_detail, "", 288, 23, APP_FONT_20, night ? TEXT_DARK : TEXT_LIGHT);
    s_bat_charge_cmp = lbl(battery_detail, "—", 174, 18, APP_FONT_12, GREEN);
    s_bat_discharge_cmp = lbl(battery_detail, "—", 386, 18, APP_FONT_12, GREEN);
    lbl(battery_detail,
        tr("tegnaphoz", "vs yesterday"),
        174,
        35,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);
    lbl(battery_detail,
        tr("tegnaphoz", "vs yesterday"),
        386,
        35,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);

    /*
     * Jobb oldali két grafikon egymás alatt, az eredeti mockup szerint.
     * A későbbi specifikáció miatt mindkettő két sorozatot tartalmaz.
     */
    lv_obj_t *chart_card_1 = lv_obj_create(s_dash);
    lv_obj_set_pos(chart_card_1, 834, 82);
    lv_obj_set_size(chart_card_1, 426, 276);
    card_style(chart_card_1, night);

    lbl(chart_card_1,
        tr("Termelés", "Production"),
        0,
        0,
        APP_FONT_16,
        night ? TEXT_DARK : TEXT_LIGHT);

    shape(chart_card_1, 38, 30, 7, 7, tone(GREEN, night), LV_RADIUS_CIRCLE);
    lbl(chart_card_1,
        tr("Saját fogyasztás", "Self-consumption"),
        50,
        24,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);
    shape(chart_card_1, 186, 30, 7, 7, tone(ORANGE, night), LV_RADIUS_CIRCLE);
    lbl(chart_card_1,
        tr("Visszatáplálás", "Export"),
        198,
        24,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);
    shape(chart_card_1, 323, 30, 7, 7, tone(BLUE, night), LV_RADIUS_CIRCLE);
    lbl(chart_card_1,
        tr("Tárolt", "Stored"),
        335,
        24,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);

    lbl(chart_card_1, "kW", 0, 23, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);

    s_chart1_y_top = lbl(chart_card_1, "", 0, 41, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);
    s_chart1_y_mid = lbl(chart_card_1, "", 0, 130, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);
    s_chart1_y_bottom =
        lbl(chart_card_1, "0", 0, 217, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);

    s_chart1 = app_chart_create(
        chart_card_1, false, night, tone(GREEN, night), tone(ORANGE, night), tone(BLUE, night));
    if (s_chart1)
    {
        lv_obj_set_pos(s_chart1, 38, 48);
        lv_obj_set_size(s_chart1, 350, 178);
    }
    /* A felirat középpontja a negyednapos rácsvonalra esik. */
    for (int tick = 0; tick < 5; tick++)
    {
        char text[8];
        snprintf(text, sizeof(text), "%02d:00", tick * 6);
        lv_obj_t *label = lbl(chart_card_1,
                              text,
                              38 + (int)lround(349. * tick / 4.) - 23,
                              234,
                              APP_FONT_12,
                              night ? MUTED_DARK : MUTED_LIGHT);
        lv_obj_set_width(label, 46);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    lv_obj_t *chart_card_2 = lv_obj_create(s_dash);
    lv_obj_set_pos(chart_card_2, 834, 374);
    lv_obj_set_size(chart_card_2, 426, 276);
    card_style(chart_card_2, night);

    lbl(chart_card_2,
        tr("Fogyasztás", "Consumption"),
        0,
        0,
        APP_FONT_16,
        night ? TEXT_DARK : TEXT_LIGHT);

    shape(chart_card_2, 38, 30, 7, 7, tone(RED, night), LV_RADIUS_CIRCLE);
    lbl(chart_card_2, tr("Hálózat", "Grid"), 50, 24, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);
    shape(chart_card_2, 186, 30, 7, 7, tone(GREEN, night), LV_RADIUS_CIRCLE);
    lbl(chart_card_2,
        tr("Termelés", "Solar"),
        198,
        24,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);
    shape(chart_card_2, 323, 30, 7, 7, tone(BLUE, night), LV_RADIUS_CIRCLE);
    lbl(chart_card_2,
        tr("Tároló", "Battery"),
        335,
        24,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);

    lbl(chart_card_2, "kW", 0, 23, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);

    s_chart2_y_top = lbl(chart_card_2, "", 0, 41, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);
    s_chart2_y_mid = lbl(chart_card_2, "0", 0, 130, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);
    s_chart2_y_bottom =
        lbl(chart_card_2, "", 0, 217, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);

    s_chart2 = app_chart_create(
        chart_card_2, true, night, tone(RED, night), tone(GREEN, night), tone(BLUE, night));
    if (s_chart2)
    {
        lv_obj_set_pos(s_chart2, 38, 48);
        lv_obj_set_size(s_chart2, 350, 178);
    }
    /* A felirat középpontja a negyednapos rácsvonalra esik. */
    for (int tick = 0; tick < 5; tick++)
    {
        char text[8];
        snprintf(text, sizeof(text), "%02d:00", tick * 6);
        lv_obj_t *label = lbl(chart_card_2,
                              text,
                              38 + (int)lround(349. * tick / 4.) - 23,
                              234,
                              APP_FONT_12,
                              night ? MUTED_DARK : MUTED_LIGHT);
        lv_obj_set_width(label, 46);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    /*
     * Alsó összefoglaló sáv: az eredeti terv szerinti horizontális összegzés.
     * Az első négy szegmens napi mutató, az utolsó a rendszerállapot.
     */
    lv_obj_t *summary = lv_obj_create(s_dash);
    lv_obj_set_pos(summary, 20, 664);
    lv_obj_set_size(summary, 1240, 110);
    card_style(summary, night);
    lv_obj_set_style_pad_all(summary, 8, 0);
    lv_obj_set_style_pad_bottom(summary, 2, 0);

    summary_metric(
        summary, 0, 236, tr("Megtakarított pénz", "Money saved"), &app_img_coins, 4, night, ORANGE);

    summary_metric(
        summary, 236, 236, tr("Megtakarított CO2", "CO2 saved"), &app_img_co2, 5, night, GRAY);

    summary_metric(
        summary, 472, 236, tr("Fa-egyenérték", "Tree equivalent"), &app_img_tree, 6, night, GREEN);

    summary_metric(
        summary, 708, 236, tr("Önellátás", "Self-sufficiency"), &app_img_bulb, 7, night, ORANGE);

    s_status_card = lv_obj_create(summary);
    lv_obj_set_pos(s_status_card, 944, 0);
    lv_obj_set_size(s_status_card, 280, 98);
    const int status_top = (98 - (52 + APP_FONT_12->line_height)) / 2;
    bg_opa_if_changed(s_status_card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_status_card, 0, 0);
    lv_obj_set_style_radius(s_status_card, 14, 0);
    lv_obj_set_style_pad_all(s_status_card, 0, 0);
    lv_obj_remove_flag(s_status_card, LV_OBJ_FLAG_SCROLLABLE);

    lbl(s_status_card,
        tr("Állapot", "Status"),
        68,
        status_top,
        APP_FONT_12,
        night ? MUTED_DARK : MUTED_LIGHT);

    s_status_bubble = lv_obj_create(s_status_card);
    lv_obj_set_pos(s_status_bubble, 4, (98 - 54) / 2);
    lv_obj_set_size(s_status_bubble, 54, 54);
    lv_obj_set_style_radius(s_status_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_bubble, 0, 0);
    bg_opa_if_changed(s_status_bubble, night ? LV_OPA_10 : LV_OPA_20, 0);
    lv_obj_set_style_pad_all(s_status_bubble, 0, 0);
    lv_obj_remove_flag(s_status_bubble, LV_OBJ_FLAG_SCROLLABLE);

    s_status_icon = asset_image_box(s_status_bubble, 0, 0, 54, 42, &app_img_status_normal, night);

    s_status = lbl(s_status_card, "-", 68, status_top + 23, APP_FONT_16, GREEN);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_status, 205);

    s_status_detail =
        lbl(s_status_card, "", 68, status_top + 52, APP_FONT_12, night ? MUTED_DARK : MUTED_LIGHT);
    lv_label_set_long_mode(s_status_detail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_status_detail, 205);

    unsigned theme_count = template_catalogue_count();
    for (unsigned i = 0; i < theme_count; i++)
        shape(s_dash,
              640 - (int)theme_count * 10 + (int)i * 20,
              781,
              7,
              7,
              i == 0 ? tone(GREEN, night) : (night ? 0x53626A : 0xC8CFD4),
              LV_RADIUS_CIRCLE);

    /* Futás közbeni SD-hiba kis, nem blokkoló piros figyelmeztetése. */
    s_sd_warn = lbl(s_dash, tr("SD-kártya hiba", "SD card error"), 430, 37, APP_FONT_12, RED);
    lv_obj_set_width(s_sd_warn, 230);
    lv_obj_set_style_text_align(s_sd_warn, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(s_sd_warn, LV_OBJ_FLAG_HIDDEN);

    s_dash_created = true;
    s_view = VIEW_DASHBOARD;
    s_last_chart_update_us = 0;
}
/* Napi önellátás kiszámítása 0..100% közé korlátozva. */
static double self(const app_energy_totals_t *t)
{
    if (t->consumption_kwh <= 1e-9)
        return 0;
    double x = (t->consumption_kwh - t->grid_import_kwh) / t->consumption_kwh * 100;
    if (x < 0)
        x = 0;
    if (x > 100)
        x = 100;
    return x;
}
/* PV helyi felhasználásból és visszatáplálásból származó napi megtakarítás számítása. */
static double money(const app_energy_totals_t *t)
{
    double local = t->production_kwh - t->grid_export_kwh;
    if (local < 0)
        local = 0;
    return local * s_display_energy.electricity_purchase_price +
           t->grid_export_kwh * s_display_energy.feed_in_price;
}
/* Napi PV-termeléshez tartozó megtakarított CO2 tömeg számítása. */
static double co2(const app_energy_totals_t *t)
{
    return t->production_kwh * s_display_energy.co2_factor_kg_per_kwh;
}
/* Megtakarított CO2 fa-egyenértékre történő átszámítása. */
static double tree(const app_energy_totals_t *t)
{
    return co2(t) / s_display_energy.tree_absorption_kg_per_year;
}
/* Egy napi kártya értékének és opcionális tegnapi összehasonlításának frissítése. */
static bool s_yesterday_partial_for_ui;

static void
metric(int index, const char *value, double today, double yesterday, bool have_yesterday)
{
    if (s_stats_missing_for_ui) {
        label_text_if_changed(s_metric[index], "--");
        label_text_if_changed(s_cmp[index], "");
        if (s_cmp_note[index]) lv_obj_add_flag(s_cmp_note[index], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    fit_text(s_metric[index], value, index < 4 ? 190 : 162, index < 4 ? APP_FONT_28 : APP_FONT_20);

    if (!have_yesterday)
    {
        label_text_if_changed(s_cmp[index], "");
        if (s_cmp_note[index])
        {
            lv_obj_add_flag(s_cmp_note[index], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (s_cmp_note[index])
    {
        lv_obj_clear_flag(s_cmp_note[index], LV_OBJ_FLAG_HIDDEN);
    }

    char comparison[48];
    fmt_compared(comparison, sizeof(comparison), today, yesterday, s_yesterday_partial_for_ui);

    fit_text(s_cmp[index], comparison, index < 4 ? 190 : 162, APP_FONT_14);

    text_color_if_changed(s_cmp[index], c(tone(cmp_color(today, yesterday), s_last_night)), 0);
}
/* A 288 ötperces grafikonpont és az automatikus Y-skálák frissítése. */
static void charts(const app_stats_snapshot_t *st)
{
    time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);
    int minutes = local_time.tm_hour * 60 + local_time.tm_min;
    double max_production = 1.0, max_consumption = 1.0;
    for (int i = 0; i < APP_STATS_SLOT_COUNT; i++)
    {
        if (!st->slots[i].valid || st->slots[i].chart_gap || i * 5 > minutes)
            continue;
        const app_energy_split_t *v = &st->slots[i].split;
        max_production = fmax(max_production, v->pv_home + v->pv_export + v->pv_stored);
        max_consumption = fmax(max_consumption, v->grid_home + v->pv_home + v->battery_home);
    }
    max_production = ceil(max_production);
    max_consumption = ceil(max_consumption);
    char text[20];
    fmt_dec(text, sizeof(text), max_production, 0);
    label_text_if_changed(s_chart1_y_top, text);
    fmt_dec(text, sizeof(text), max_production / 2., 1);
    label_text_if_changed(s_chart1_y_mid, text);
    label_text_if_changed(s_chart1_y_bottom, "0");
    fmt_dec(text, sizeof(text), max_consumption, 0);
    label_text_if_changed(s_chart2_y_top, text);
    fmt_dec(text, sizeof(text), max_consumption / 2., 1);
    label_text_if_changed(s_chart2_y_mid, text);
    label_text_if_changed(s_chart2_y_bottom, "0");
    app_chart_update(s_chart1, st, max_production, minutes);
    app_chart_update(s_chart2, st, max_consumption, minutes);
}
/* MQTT meteorológiai mezőkből az ikonlogika szerinti időjárási állapot kiválasztása. */
/* Az ikonhoz minden osztályozó adat kell; hiányból nem következtetünk időjárásra. */
static bool weather_available(const app_energy_sample_t *s)
{
    return s->temperature_valid && s->sun_radiation_valid && s->precipitation_valid;
}
static void weather_visibility(lv_obj_t *obj, bool visible)
{
    if (visible)
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static weather_kind_t weather_kind(const app_energy_sample_t *s)
{
    /* A csapadék minden más időjárási állapotnál magasabb prioritású. */
    if (s->has_precipitation)
        return s->temperature_c < -4 ? WEATHER_SNOW : WEATHER_RAIN;
    if (s->night_mode)
        return WEATHER_NIGHT;
    if (s->has_sun_radiation || s->power_pv_kw > 5)
        return WEATHER_SUNNY;
    if (s->power_pv_kw > 1)
        return WEATHER_PARTLY_CLOUDY;
    return WEATHER_CLOUDY;
}
/* A vonalak, körvonalak és pontok egyetlen szemantikus színforrást használnak. */
static void update_flows(const app_energy_sample_t *s, float dt)
{
    /*
     * Napelem: termeléskor mindig a központi hub felé mozog,
     * és zöld energiaáramlást használ.
     */
    flow_update(&f_pv, s->power_pv_kw, true, GREEN, s->night_mode, dt);

    /*
     * Akkumulátor: pozitív PowerBat = kisütés, ezért akku -> hub és narancs.
     * Negatív PowerBat = töltés, ezért hub -> akku és zöld.
     */
    flow_update(&f_bat,
                s->power_bat_kw,
                s->power_bat_kw > 0,
                s->power_bat_kw < 0 ? GREEN : ORANGE,
                s->night_mode,
                dt);

    /*
     * A ház fogyasztása külön, narancs ágon mindig a hub -> ház irányba halad.
     */
    flow_update(&f_home, s->power_full_kw, true, ORANGE, s->night_mode, dt);

    border_color_if_changed(s_pv_node, c(flow_color(s->power_pv_kw, GREEN, s->night_mode)), 0);
    border_color_if_changed(
        s_battery_node,
        c(flow_color(s->power_bat_kw, s->power_bat_kw < 0 ? GREEN : ORANGE, s->night_mode)),
        0);
    border_color_if_changed(s_home_node, c(flow_color(s->power_full_kw, ORANGE, s->night_mode)), 0);

    /*
     * Szigetüzemben a hálózat fizikailag leválasztottként jelenik meg:
     * nincs áramlási pont, a kapcsolat és a hálózati kör szürke,
     * az ikon pedig megszakított állapotot jelez.
     */
    if (s->status == 3)
    {
        for (int i = 0; i < 3; i++)
        {
            lv_obj_add_flag(f_grid.dots[i], LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_set_style_line_color(f_grid.line, c(flow_color(0, GREEN, s->night_mode)), 0);

        lv_image_set_src(s_grid_icon, &app_img_grid_off);

        opa_if_changed(s_grid_icon, s->night_mode ? LV_OPA_70 : LV_OPA_COVER, 0);

        border_color_if_changed(s_grid_node, c(flow_color(0, GREEN, s->night_mode)), 0);
    }
    else
    {
        /*
         * PowerGrid pozitív = import: hálózat -> hub, narancs.
         * PowerGrid negatív = export: hub -> hálózat, zöld.
         */
        uint32_t grid_color = s->power_grid_kw < 0 ? GREEN : ORANGE;

        flow_update(&f_grid, s->power_grid_kw, s->power_grid_kw > 0, grid_color, s->night_mode, dt);

        lv_image_set_src(s_grid_icon, &app_img_grid);

        opa_if_changed(s_grid_icon, s->night_mode ? LV_OPA_80 : LV_OPA_COVER, 0);

        border_color_if_changed(
            s_grid_node, c(flow_color(s->power_grid_kw, grid_color, s->night_mode)), 0);
    }
}

#include "app_ble_pin_ui.inc"

/* A render loophoz közeli, 16 ms-os animáció. Nem végez SD- vagy statisztikai műveletet. */
static void animation_tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_view != VIEW_FATAL && pairing_ui_tick()) return;
    /* Az indulás hosszú NTP/MQTT várakozásai alatt is azonnal látszik a kapcsolatvesztés. */
    if (!s_started && s_view != VIEW_FATAL && s_boot_stage > APP_BOOT_WIFI)
    {
        app_runtime_state_t state;
        app_state_get_runtime(&state);
        if (s_cfg.data_recept_mode != APP_DATA_SLAVE && !state.wifi_connected)
            error_screen(
                VIEW_WIFI_ERROR,
                tr("Nincs Wi-Fi kapcsolat", "No Wi-Fi connection"),
                tr("Kapcsolat helyreállítására várunk...", "Waiting for connection to recover..."),
                false);
        else if (s_boot_stage > APP_BOOT_MQTT && s_cfg.data_recept_mode == APP_DATA_MQTT &&
                 !state.mqtt_connected)
            error_screen(
                VIEW_MQTT_ERROR,
                tr("Nincs MQTT kapcsolat", "No MQTT connection"),
                tr("Kapcsolat helyreállítására várunk...", "Waiting for connection to recover..."),
                false);
    }
    int64_t now = esp_timer_get_time();
    float dt = s_animation_us ? (now - s_animation_us) / 1000000.f : 0.f;
    s_animation_us = now;
    /* Lassú kirajzoláskor se lassítsuk mesterségesen a pontok időalapját. */
    if (dt > 2.f)
        dt = 2.f; /* kimaradt képkockáknál is kövessük a valódi időt */
    if (dt < 0.f)
        dt = 0.f;
    /* Az összetett görbéken átlagosan 20 FPS; a teljes eltelt idővel
     * léptetünk, nem lassul az energiaáramlás és nem vesznek el tört tickek. */
    static float heavy_flow_interval, heavy_flow_motion;
    if (template_is_active())
    {
        if (template_heavy_animation())
        {
            heavy_flow_interval += dt;
            heavy_flow_motion += dt;
            if (heavy_flow_interval >= .050f)
            {
                template_animate(heavy_flow_motion);
                heavy_flow_motion = 0;
                heavy_flow_interval = fmodf(heavy_flow_interval, .050f);
            }
        }
        else
        {
            heavy_flow_interval = 0;
            heavy_flow_motion = 0;
            template_animate(dt);
        }
    }
    else
    {
        heavy_flow_interval = 0;
        heavy_flow_motion = 0;
        if (s_view == VIEW_DASHBOARD && s_dash_created)
            update_flows(&s_animation_sample, dt);
    }
    if (s_error_pulse)
    {
        float wave = (sinf(now / 1000000.f * 3.14159265f) + 1.f) * .5f;
        /* Pulse only the coloured border; group opacity creates an offscreen
         * layer around the PNG and can leave a flashing rectangular edge. */
        border_opa_if_changed(s_error_pulse, (lv_opa_t)(175 + 65 * wave), 0);
    }
}

/* Egy érvényes minta és statisztikai snapshot alapján minden dashboard-elem frissítése. */
static void update(const app_energy_sample_t *s, const app_stats_snapshot_t *st)
{
    s_display_energy = st->reference_valid ? st->reference_energy : s_cfg.energy;
    s_stats_missing_for_ui = st->remote && !st->remote_available;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char b[96], q[32];
    strftime(q, sizeof(q), "%H:%M", &tmv);
    label_text_if_changed(s_clock, q);
    if (hu())
        snprintf(q, sizeof(q), "%04d.%02d.%02d.", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    else
        snprintf(q, sizeof(q), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    label_text_if_changed(s_date, q);

    weather_visibility(lv_obj_get_parent(s_weather_icon), weather_available(s));
    weather_visibility(lv_obj_get_parent(s_wind_icon), s->wind_speed_valid);
    weather_visibility(s_weather_temp, s->temperature_valid);
    weather_visibility(s_wind_speed, s->wind_speed_valid);
    if (weather_available(s))
        weather_render(weather_kind(s), s->night_mode);
    char temp[16];
    fmt_dec(temp, sizeof(temp), s->temperature_c, 1);
    snprintf(b, sizeof(b), "%s °C", temp);
    fit_text(s_weather_temp, b, 78, APP_FONT_16);
    snprintf(b, sizeof(b), "%d km/h", (int)lround(s->wind_speed_kmh));
    fit_text(s_wind_speed, b, 98, APP_FONT_16);
    if (s->wind_speed_kmh > 35.0)
        lv_obj_set_style_text_font(s_wind_speed, APP_FONT_WIND_BOLD_16, 0);
    const uint32_t wind_color = s->wind_speed_kmh > 50.0 ? RED
                              : s->wind_speed_kmh > 35.0 ? ORANGE
                                                         : (s->night_mode ? TEXT_DARK : TEXT_LIGHT);
    text_color_if_changed(s_wind_speed, c(tone(wind_color, s->night_mode)), 0);
    fmt_power(q, sizeof(q), s->power_pv_kw);
    label_text_if_changed(s_pv, q);
    fmt_power(q, sizeof(q), s->power_full_kw);
    label_text_if_changed(s_home, q);
    fmt_power(q, sizeof(q), s->power_bat_kw);
    label_text_if_changed(s_bat, q);
    fmt_power(q, sizeof(q), s->power_grid_kw);
    label_text_if_changed(s_grid, q);
    snprintf(q, sizeof(q), "%d%%", (int)lround(s->soc_bat_pct));
    label_text_if_changed(s_soc, q);
    snprintf(q, sizeof(q), "%d °C", (int)lround(s->battery_temperature_c));
    label_text_if_changed(s_bt, q);

    uint32_t bt_color =
        s->battery_temperature_c > 50
            ? RED
            : (s->battery_temperature_c > 40 ? ORANGE : (s->night_mode ? MUTED_DARK : MUTED_LIGHT));
    text_color_if_changed(s_bt, c(tone(bt_color, s->night_mode)), 0);

    lv_bar_set_value(s_battery_bar, (int32_t)lround(s->soc_bat_pct), LV_ANIM_OFF);
    uint32_t soc_color = battery_color(s->soc_bat_pct);
    bg_color_if_changed(s_battery_bar, c(tone(soc_color, s->night_mode)), LV_PART_INDICATOR);

    opa_if_changed(s_bat_icon, s->night_mode ? LV_OPA_80 : LV_OPA_COVER, 0);

    s_animation_sample = *s;
    update_flows(s, 0.f);
    /* Minden összevetést azonos időpontban mért, lezárt slotokból számolunk.
     * Hiányos adatsor esetén a kapott százalék közelítő jelölést kap. */
    s_yesterday_partial_for_ui = st->yesterday_comparison_partial;
    fmt_kwh(b, sizeof(b), st->totals.production_kwh);
    metric(0,
           b,
           st->comparison_today_totals.production_kwh,
           st->comparison_yesterday_totals.production_kwh,
           st->comparison_ready);
    fmt_kwh(b, sizeof(b), st->totals.consumption_kwh);
    metric(1,
           b,
           st->comparison_today_totals.consumption_kwh,
           st->comparison_yesterday_totals.consumption_kwh,
           st->comparison_ready);
    fmt_kwh(b, sizeof(b), st->totals.grid_import_kwh);
    metric(2,
           b,
           st->comparison_today_totals.grid_import_kwh,
           st->comparison_yesterday_totals.grid_import_kwh,
           st->comparison_ready);
    fmt_kwh(b, sizeof(b), st->totals.grid_export_kwh);
    metric(3,
           b,
           st->comparison_today_totals.grid_export_kwh,
           st->comparison_yesterday_totals.grid_export_kwh,
           st->comparison_ready);
    snprintf(b, sizeof(b), "%.0f %s", money(&st->totals), st->reference_valid ? st->reference_currency : s_cfg.project.currency);
    metric(4,
           b,
           money(&st->comparison_today_totals),
           money(&st->comparison_yesterday_totals),
           st->comparison_ready);
    char n[20];
    fmt_dec(n, sizeof(n), co2(&st->totals), 1);
    snprintf(b, sizeof(b), "%s kg", n);
    metric(5,
           b,
           co2(&st->comparison_today_totals),
           co2(&st->comparison_yesterday_totals),
           st->comparison_ready);
    fmt_dec(b, sizeof(b), tree(&st->totals), 2);
    {
        char tree_value[32];
        snprintf(tree_value, sizeof(tree_value), "%.16s %.7s", b, tr("fa", "trees"));
        metric(6,
               tree_value,
               tree(&st->comparison_today_totals),
               tree(&st->comparison_yesterday_totals),
               st->comparison_ready);
    }
    fmt_pct(b, sizeof(b), self(&st->totals));
    metric(7,
           b,
           self(&st->comparison_today_totals),
           self(&st->comparison_yesterday_totals),
           st->comparison_ready);
    fmt_kwh(q, sizeof(q), st->totals.battery_charge_kwh);
    if (s_stats_missing_for_ui) snprintf(q, sizeof(q), "--");
    fit_text(s_bat_charge, q, 88, APP_FONT_20);
    fmt_kwh(q, sizeof(q), st->totals.battery_discharge_kwh);
    if (s_stats_missing_for_ui) snprintf(q, sizeof(q), "--");
    fit_text(s_bat_discharge, q, 92, APP_FONT_20);

    /* A tegnapi összehasonlítást csak akkor mutatjuk, ha tényleges tegnapi adat létezik. */
    if (st->comparison_ready)
    {
        fmt_compared(q,
                     sizeof(q),
                     st->comparison_today_totals.battery_charge_kwh,
                     st->comparison_yesterday_totals.battery_charge_kwh,
                     st->yesterday_comparison_partial);
        fit_text(s_bat_charge_cmp, q, 94, APP_FONT_12);
        text_color_if_changed(s_bat_charge_cmp,
                              c(tone(cmp_color(st->comparison_today_totals.battery_charge_kwh,
                                               st->comparison_yesterday_totals.battery_charge_kwh),
                                     s->night_mode)),
                              0);
        fmt_compared(q,
                     sizeof(q),
                     st->comparison_today_totals.battery_discharge_kwh,
                     st->comparison_yesterday_totals.battery_discharge_kwh,
                     st->yesterday_comparison_partial);
        fit_text(s_bat_discharge_cmp, q, 94, APP_FONT_12);
        text_color_if_changed(
            s_bat_discharge_cmp,
            c(tone(cmp_color(st->comparison_today_totals.battery_discharge_kwh,
                             st->comparison_yesterday_totals.battery_discharge_kwh),
                   s->night_mode)),
            0);
    }
    else
    {
        label_text_if_changed(s_bat_charge_cmp, "—");
        text_color_if_changed(s_bat_charge_cmp, c(tone(GRAY, s->night_mode)), 0);
        label_text_if_changed(s_bat_discharge_cmp, "—");
        text_color_if_changed(s_bat_discharge_cmp, c(tone(GRAY, s->night_mode)), 0);
    }

    const char *stxt;
    if (s->status == 1)
    {
        if (hu())
        {
            stxt = "Normál működés";
        }
        else
        {
            stxt = "Normal operation";
        }
    }
    else if (s->status == 2)
    {
        stxt = tr("Felkészülés áramszünetre", "Preparing for outage");
    }
    else if (s->status == 3)
    {
        stxt = tr("Szigetüzem", "Island mode");
    }
    else if (s->status == 5)
    {
        stxt = tr("Hálózat újra elérhető", "Grid available again");
    }
    else
    {
        stxt = tr("Hiba", "Error");
    }

    uint32_t status_color = (s->status == 1 || s->status == 5) ? GREEN
                          : s->status == 2 ? ORANGE
                          : s->status == 3 ? BLUE
                                           : RED;

    const char *status_detail =
        s->status == 1   ? tr("Minden rendben", "Everything is OK")
        : s->status == 2 ? tr("Feltöltés és szinten tartás", "Charge and hold at 100%")
        : s->status == 3 ? tr("Hálózatról leválasztva", "Disconnected from grid")
        : s->status == 5 ? tr("A hálózati feszültség visszatért", "Grid voltage has returned")
                         : tr("Rendszerhiba", "System error");

    fit_text(s_status, stxt, 205, APP_FONT_16);
    fit_text(s_status_detail, status_detail, 205, APP_FONT_12);

    /* Az állapot PNG saját, a mockuphoz illeszkedő színezést tartalmaz. */

    text_color_if_changed(s_status, c(tone(status_color, s->night_mode)), 0);

    const lv_image_dsc_t *status_asset = s->status == 1 ? &app_img_status_normal
                                       : s->status == 2 ? &app_img_status_prepare
                                       : s->status == 3 ? &app_img_status_island
                                       : s->status == 5 ? &app_img_status_grid_restored
                                                        : &app_img_status_error;

    lv_image_set_src(s_status_icon, status_asset);

    opa_if_changed(s_status_icon, s->night_mode ? LV_OPA_80 : LV_OPA_COVER, 0);

    bg_color_if_changed(s_status_bubble, c(tone(status_color, s->night_mode)), 0);

    if (s_cfg.cardless_slave) {
        lv_obj_set_pos(s_sd_warn, 700, 49);
        lv_obj_set_width(s_sd_warn, 180);
        lv_obj_set_style_text_align(s_sd_warn, LV_TEXT_ALIGN_LEFT, 0);
        label_text_if_changed(s_sd_warn, s_stats_missing_for_ui ? tr("Statisztika szinkron...", "Statistics syncing...")
                                                               : tr("Elavult statisztika", "Statistics outdated"));
        if (s_stats_missing_for_ui || st->remote_stale) lv_obj_remove_flag(s_sd_warn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_sd_warn, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_set_pos(s_sd_warn, 430, 37);
        lv_obj_set_width(s_sd_warn, 230);
        lv_obj_set_style_text_align(s_sd_warn, LV_TEXT_ALIGN_RIGHT, 0);
        label_text_if_changed(s_sd_warn, tr("SD-kártya hiba", "SD card error"));
        if (app_storage_is_ok()) lv_obj_add_flag(s_sd_warn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(s_sd_warn, LV_OBJ_FLAG_HIDDEN);
    }

    int64_t chart_now = esp_timer_get_time();
    if (s_last_chart_update_us == 0 || chart_now - s_last_chart_update_us >= 5000000LL)
    {
        charts(st);
        s_last_chart_update_us = chart_now;
    }
}
/* Kommunikációs vagy végzetes teljes képernyős hibaoldal PNG illusztrációval. */
static void error_screen(runtime_view_t view, const char *title, const char *detail, bool fatal)
{
    if (s_view != view)
    {
        clear();
        screen_style(s_screen, true);

        /*
         * A központi grafika a dashboard assetstílusát követi.
         * A külső kör pulzál, maga az ikon azonban éles, részletes PNG marad.
         */
        s_error_pulse = lv_obj_create(s_screen);
        lv_obj_remove_style_all(s_error_pulse);
        lv_obj_set_size(s_error_pulse, 132, 132);
        lv_obj_set_pos(s_error_pulse, 574, 154);
        lv_obj_set_style_radius(s_error_pulse, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_error_pulse, 1, 0);
        border_color_if_changed(s_error_pulse, c(fatal ? RED : ORANGE), 0);
        bg_color_if_changed(s_error_pulse, c(fatal ? 0x321716 : 0x332619), 0);
        bg_opa_if_changed(s_error_pulse, LV_OPA_60, 0);
        lv_obj_set_style_pad_all(s_error_pulse, 0, 0);
        lv_obj_remove_flag(s_error_pulse, LV_OBJ_FLAG_SCROLLABLE);

        const lv_image_dsc_t *error_asset = fatal                   ? &app_img_status_error
                                          : view == VIEW_WIFI_ERROR ? &app_img_wifi_off
                                          : view == VIEW_MQTT_ERROR ? &app_img_mqtt_off
                                          : view == VIEW_WAIT_DATA  ? &app_img_waiting_data
                                                                    : &app_img_timeout;

        asset_image_box(s_error_pulse, 14, 14, 104, 92, error_asset, false);

        lv_obj_t *title_label = lbl(s_screen, title, 0, 326, APP_FONT_28, fatal ? RED : TEXT_DARK);
        lv_obj_set_width(title_label, W);
        lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *detail_label =
            lbl(s_screen, detail ? detail : "", 0, 386, APP_FONT_18, MUTED_DARK);
        lv_obj_set_width(detail_label, W);
        lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);

        s_view = view;
    }
}

static void initial_refresh_report(lv_event_t *event)
{
    static bool reported;
    (void)event;
    if (s_view == VIEW_THEME_LOADING) s_loading_frame_presented = true;
    if (!reported)
    {
        reported = true;
        ESP_LOGI("app_ui", "First LVGL refresh completed");
    }
}

/*
 * A kijelzőt a BSP kezdeti konfigurációjában fordítjuk. Futás közben
 * lv_display_set_rotation() futó MIPI DSI mellett nem használható:
 * 90 foknál Store access faultot okozhat a háttérben futó frissítés.
 */
esp_err_t app_ui_init(uint16_t rotation_degrees)
{
    const app_display_orientation_t o = app_display_orientation_for_rotation(rotation_degrees);
    /* Csak a módosult részletek rajzolódnak; a buildbeli CPU-pipeline
     * a panel három pufferéből mindig kész, koherens képet ad át. */
    bsp_display_cfg_t cfg = {.lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
                             .rotation = o.rotation_degrees == 90 ? ESP_LV_ADAPTER_ROTATE_90
                                                                  : ESP_LV_ADAPTER_ROTATE_270,
                             .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
                             .touch_flags = {.swap_xy = o.touch_swap_xy,
                                             .mirror_x = o.touch_mirror_x,
                                             .mirror_y = o.touch_mirror_y}};
    /* Synchronous PNG/vector drawing runs on this task, rather than 32 KB
     * drawing threads. Keep its stack in PSRAM, preserving internal DMA RAM. */
    cfg.lv_adapter_cfg.task_stack_size = 32768;
    cfg.lv_adapter_cfg.stack_in_psram = true;
    /* A grafika és touch külön magon fusson a Loxone/hálózat mellett. */
    cfg.lv_adapter_cfg.task_core_id = 1;
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp)
        return ESP_FAIL;
    bsp_display_backlight_on();
    bsp_display_brightness_set(50);
    ESP_LOGI("app_ui", "Display initialized at %u degrees", (unsigned)o.rotation_degrees);
    ESP_LOGI("app_ui", "Partial display and touch worker on CPU1; waiting for LVGL lock");
    esp_err_t lock_err = bsp_display_lock(5000);
    if (lock_err != ESP_OK)
    {
        ESP_LOGE("app_ui",
                 "Initial LVGL lock failed: %s; first refresh may be blocked",
                 esp_err_to_name(lock_err));
        return lock_err;
    }
    s_screen = lv_screen_active();
    lv_display_add_event_cb(disp, initial_refresh_report, LV_EVENT_REFR_READY, NULL);
    /* A korábban elfogadott 15/16 ms-os ütemezés: a rajzolás idején
     * felül nem iktatunk be újabb teljes periódusnyi várakozást. */
    lv_timer_set_period(lv_display_get_refr_timer(disp), 15);
    navigation_bind_inputs();
    screen_style(s_screen, false);
    if (!lv_timer_create(animation_tick, 16, NULL))
    {
        bsp_display_unlock();
        return ESP_ERR_NO_MEM;
    }
    bsp_display_unlock();
    ESP_LOGI("app_ui", "UI initialized; creating SD boot screen");
    app_ui_show_boot(APP_BOOT_SD, NULL);
    ESP_LOGI("app_ui", "SD boot screen created");
    return ESP_OK;
}
/* A bootkor beolvasott konfiguráció átadása a UI modulnak. */
void app_ui_set_config(const app_config_t *cfg)
{
    if (!cfg)
    {
        return;
    }

    s_cfg = *cfg;
    s_display_energy = cfg->energy;
    s_cfg.display.rotation_degrees = app_display_normalize_rotation(s_cfg.display.rotation_degrees);
    s_cfg_ok = true;

    s_target = s_cfg.display.day_brightness;
    s_brightness = s_target;

    /*
     * Az első MQTT csomag előtt még nincs NightMode, ezért a konfigurált
     * nappali fényerő az indulási képernyőkre is azonnal érvényes.
     */
    bsp_display_brightness_set(s_brightness);
    if (cfg->data_recept_mode == APP_DATA_SLAVE && bsp_display_lock(-1) == ESP_OK)
    {
        /* A védett jelenlétprofil megérkezéséig nincs sötétítés.
         * Az energia-BLE csomag fényerőmezőjét itt sem használjuk. */
        app_slave_display_profile_t profile = {
            .day_brightness = cfg->display.day_brightness,
            .night_brightness = cfg->display.night_brightness,
            .blank_at_night = false, .presence_timeout_seconds = 30,
        };
        int64_t now = esp_timer_get_time();
        s_slave_display_ready = s_slave_display_ready
            ? app_slave_display_profile(&s_slave_display, &profile, now)
            : app_slave_display_init(&s_slave_display, &profile, now);
        if (s_slave_display_ready)
            app_slave_display_keep_visible(&s_slave_display, true, now);
        bsp_display_unlock();
    }
}
/* Indulási folyamat aktuális lépésének megjelenítése vektoros állapotikonokkal. */
void app_ui_show_boot(app_boot_stage_t stage, const char *detail)
{
    if (bsp_display_lock(-1) != ESP_OK)
    {
        return;
    }

    s_boot_stage = stage;
    clear();
    screen_style(s_screen, false);

    const char *hu_steps[] = {s_cfg.cardless_slave ? "Belső tár (SD nélkül)" : "SD-kártya",
                              "Konfiguráció",
                              s_cfg.data_recept_mode == APP_DATA_SLAVE ? "Wi-Fi (opcionális)" : "Wi-Fi kapcsolat",
                              s_cfg.data_recept_mode == APP_DATA_SLAVE ? "Bluetooth időszinkron" : "Időszinkron",
                              s_cfg.data_recept_mode == APP_DATA_SLAVE ? "Slave / master keresése" :
                              s_cfg.data_recept_mode == APP_DATA_LOXONE ? "Loxone kapcsolat"
                              : s_cfg.data_recept_mode == APP_DATA_HTTP ? "HTTP adatfogadó"
                                                                        : "MQTT kapcsolat",
                              "Várakozás az első adatcsomagra"};

    const char *en_steps[] = {s_cfg.cardless_slave ? "Internal storage (no SD)" : "SD card",
                              "Configuration",
                              s_cfg.data_recept_mode == APP_DATA_SLAVE ? "Wi-Fi (optional)" : "Wi-Fi connection",
                              s_cfg.data_recept_mode == APP_DATA_SLAVE ? "Bluetooth time sync" : "Time synchronization",
                              s_cfg.data_recept_mode == APP_DATA_SLAVE ? "Slave / searching for master" :
                              s_cfg.data_recept_mode == APP_DATA_LOXONE ? "Loxone connection"
                              : s_cfg.data_recept_mode == APP_DATA_HTTP ? "HTTP receiver"
                                                                        : "MQTT connection",
                              "Waiting for first data packet"};

    lbl(s_screen,
        s_cfg_ok ? s_cfg.project.title : "Zolee Energy Flow Display",
        34,
        30,
        APP_FONT_28,
        TEXT_LIGHT);

    lbl(s_screen, tr("Rendszerindítás", "Starting system"), 34, 120, APP_FONT_28, TEXT_LIGHT);

    lbl(s_screen, hu() ? hu_steps[stage] : en_steps[stage], 34, 180, APP_FONT_22, GREEN);

    if (detail)
    {
        lv_obj_t *detail_label = lbl(s_screen, detail, 34, 225, APP_FONT_14, MUTED_LIGHT);
        lv_obj_set_width(detail_label, 1180);
    }

    /*
     * A bootlista felismerhető állapotgrafikákat használ:
     * kész = zöld ellenőrzött otthon, aktív = narancs adatmozgás,
     * jövőbeli = visszafogott szürke kör.
     */
    for (int i = 0; i < 6; i++)
    {
        int y = 300 + i * 57;

        if (i < (int)stage)
        {
            asset_image_box(s_screen, 52, y, 34, 30, &app_img_status_normal, false);
        }
        else if (i == (int)stage)
        {
            asset_image_box(s_screen, 52, y, 34, 30, &app_img_waiting_data, false);
        }
        else
        {
            lv_obj_t *dot = shape(s_screen, 63, y + 10, 10, 10, 0xC8D0D4, LV_RADIUS_CIRCLE);
            bg_opa_if_changed(dot, LV_OPA_COVER, 0);
        }

        lbl(s_screen,
            hu() ? hu_steps[i] : en_steps[i],
            98,
            y + 5,
            APP_FONT_18,
            i < (int)stage    ? GREEN
            : i == (int)stage ? ORANGE
                              : GRAY);
    }

    bsp_display_unlock();
}

/* Végzetes indulási hiba biztonságos megjelenítése LVGL lock mellett. */
void app_ui_show_fatal(const char *title, const char *detail)
{
    if (bsp_display_lock(-1) != ESP_OK)
        return;
    error_screen(VIEW_FATAL, title, detail, true);
    bsp_display_unlock();
}
/* Folyamatos UI runtime task: kapcsolatállapot, timeout, fényerő és dashboard frissítése. */
/* Az állapotválasztás közös a készülék és a tényleges LVGL render-teszt számára. */
#include "app_template_ui.inc"
#include "app_loxone_home_ui.inc"
#include "app_energy_assistant_ui.inc"
#include "app_loxone_logo.h"
/* Saját Loxone képernyők: a témákhoz nem foglalunk új képcache-t. */
static void loxone_screen(runtime_view_t view)
{
    if (s_view == view)
        return;
    clear();
    screen_style(s_screen, true);
    lv_obj_set_style_bg_color(s_screen, c(0x000000), 0);
    lv_obj_t *logo = lv_image_create(s_screen);
    lv_image_set_src(logo, &loxone_logo);
    lv_obj_set_pos(logo, (W - 167) / 2, 140);
    const char *title = view == VIEW_LOXONE_SETUP
                          ? tr("Loxone beállítás szükséges", "Loxone setup required")
                      : view == VIEW_LOXONE_WAIT
                          ? tr("Kapcsolódás a Miniserverhez", "Connecting to Miniserver")
                          : tr("Megszakadt a Loxone adatkapcsolat", "Loxone connection lost");
    lv_obj_t *label = lbl(s_screen, title, 40, 350, APP_FONT_28, 0xFFFFFF);
    lv_obj_set_width(label, W - 80);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    label = lbl(s_screen,
                view == VIEW_LOXONE_SETUP
                    ? tr("Végezd el a paraméterezést a webfelületen.",
                         "Complete setup in the web interface.")
                    : tr("Újracsatlakozás a háttérben. A részletek a webfelületen láthatók.",
                         "Reconnecting in the background. See the web interface for details."),
                40,
                410,
                APP_FONT_18,
                0xB9C6BE);
    lv_obj_set_width(label, W - 80);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    /* A diagnosztika már szálbiztosan tartalmazza az aktuális IP-címet. */
    app_diagnostics_t diagnostic;
    app_diagnostics_capture(&diagnostic);
    const char *s_loxone_ip = diagnostic.ip;
    char address[128];
    snprintf(address,
             sizeof(address),
             "http://%s/loxone",
             s_loxone_ip[0] && strcmp(s_loxone_ip, "-") != 0 ? s_loxone_ip
                                                             : "zolee-energy-flow-display.local");
    label = lbl(s_screen, address, 40, 466, APP_FONT_18, 0x80C64A);
    lv_obj_set_width(label, W - 80);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    s_view = view;
}
static void render_runtime(app_runtime_state_t rt, app_energy_sample_t s, bool have, int64_t now)
{
    template_clock_synced = rt.time_synced;
    app_ble_pairing_status_t pairing;
    app_ble_pairing_status(&pairing);
    if (pairing.pin_required) return;
    if (home_render(&s, have && rt.loxone_connected &&
                    !app_state_data_timed_out(&rt, now, app_config_data_timeout(&s_cfg)))) return;
    static bool had_loxone_connection;
    if (s_cfg.data_recept_mode == APP_DATA_LOXONE && rt.loxone_connected && have)
        had_loxone_connection = true;
    if (s_cfg.data_recept_mode == APP_DATA_LOXONE &&
        (!rt.loxone_configured || !rt.loxone_connected || !have ||
         app_state_data_timed_out(&rt, now, app_config_data_timeout(&s_cfg))))
    {
        loxone_screen(!rt.loxone_configured   ? VIEW_LOXONE_SETUP
                      : had_loxone_connection ? VIEW_LOXONE_ERROR
                                              : VIEW_LOXONE_WAIT);
        return;
    }
    template_apply();
    if (s_view == VIEW_THEME_LOADING && template_waiting_for_load)
        return;
    if (s_diagnostics_requested)
    {
        diagnostics_render(rt);
        return;
    }
    if (s_view == VIEW_DIAGNOSTICS)
        clear();
    bool timed_out = app_state_data_timed_out(&rt, now, app_config_data_timeout(&s_cfg));
    bool http = s_cfg.data_recept_mode == APP_DATA_HTTP;

    if (s_cfg.data_recept_mode == APP_DATA_SLAVE &&
        (!rt.slave_connected || !have || timed_out))
    {
        error_screen(VIEW_DATA_TIMEOUT,
            !rt.slave_connected ? tr("Master keresése", "Searching for master")
                               : tr("Nincs friss master adat", "No fresh master data"),
            tr("Bluetooth kapcsolatra és érvényes energiaadatokra várunk...",
               "Waiting for Bluetooth connection and valid energy data..."), false);
    }
    else if (s_cfg.data_recept_mode != APP_DATA_SLAVE && !rt.wifi_connected)
    {
        error_screen(
            VIEW_WIFI_ERROR,
            tr("Nincs Wi-Fi kapcsolat", "No Wi-Fi connection"),
            tr("Kapcsolat helyreállítására várunk...", "Waiting for connection to recover..."),
            false);
    }
    else if (s_cfg.data_recept_mode == APP_DATA_MQTT && !rt.mqtt_connected)
    {
        error_screen(
            VIEW_MQTT_ERROR,
            tr("Nincs MQTT kapcsolat", "No MQTT connection"),
            tr("Kapcsolat helyreállítására várunk...", "Waiting for connection to recover..."),
            false);
    }
    else if (!have && !timed_out)
    {
        error_screen(
            VIEW_WAIT_DATA,
            http ? tr("Várakozás HTTP adatcsomagra", "Waiting for HTTP data")
                 : tr("Várakozás MQTT adatcsomagra", "Waiting for MQTT data"),
            tr("Az első érvényes adatcsomagra várunk...", "Waiting for the first valid packet..."),
            false);
    }
    else if (!have || timed_out)
    {
        error_screen(
            VIEW_DATA_TIMEOUT,
            http ? tr("Nem érkezik HTTP adat", "No HTTP data received")
                 : tr("Nem érkezett MQTT adatcsomag.", "No MQTT data packet received."),
            tr("Kapcsolat helyreállítására várunk...", "Waiting for connection to recover..."),
            false);
    }
    else
    {
        if (!s_dash_created || s.night_mode != s_last_night)
        {
            if (template_is_active())
                template_build(s.night_mode);
            else
                build_dashboard(s.night_mode);
            s_last_night = s.night_mode;
        }
        if (template_is_active())
            template_update(&s, &s_stats_snapshot);
        else
            update(&s, &s_stats_snapshot);
    }
}

/* Kizárólag a kijelző zárja alatt: az inputkapu ugyanazt az állapotot frissíti. */
static void slave_display_update(const app_runtime_state_t *state,
                                  const app_energy_sample_t *sample, bool have, int64_t now)
{
    if (s_cfg.data_recept_mode != APP_DATA_SLAVE || !s_slave_display_ready)
        return;
    if (!s_slave_input.input)
    {
        for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input))
        {
            if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER &&
                lv_indev_get_display(input) == lv_display_get_default())
            {
                (void)app_slave_display_input_bind(&s_slave_input, input,
                                                  &s_slave_display, esp_timer_get_time);
                break;
            }
        }
    }
    if (have && state->slave_connected &&
        !app_state_data_timed_out(state, now, app_config_data_timeout(&s_cfg)))
        app_slave_display_night(&s_slave_display, sample->night_mode, now);
    app_ble_pairing_status_t pairing;
    app_ble_pairing_status(&pairing);
    /* Használható érintő nélkül ne lehessen véletlenül felébreszthetetlenül sötét. */
    app_slave_display_keep_visible(&s_slave_display,
                                   pairing.pin_required || !s_slave_input.input, now);
    app_slave_presence_service_apply(&s_slave_display, now);
    s_target = (int)app_slave_display_brightness(&s_slave_display, now);
    if (s_brightness != s_target)
    {
        s_brightness = s_target;
        bsp_display_brightness_set(s_brightness);
    }
}

#include "app_intercom_ui.inc"

static void runtime(void *arg)
{
    (void)arg;
    (void)esp_task_wdt_add(NULL);

    while (true)
    {
        (void)esp_task_wdt_reset();
        int64_t now = esp_timer_get_time();

        app_runtime_state_t rt;
        app_state_get_runtime(&rt);

        app_energy_sample_t s = {0};
        bool have = app_state_get_sample(&s);
        if (have)
        {
            app_stats_get_snapshot(&s_stats_snapshot);
            static int64_t period_update;
            if (!period_update || now - period_update >= 5000000)
            {
                app_stats_get_period(false, &s_week_period);
                app_stats_get_period(true, &s_month_period);
                period_update = now;
            }
        }

        if (have && s_cfg.data_recept_mode != APP_DATA_SLAVE)
        {
            s_target = s.night_mode ? s_cfg.display.night_brightness : s_cfg.display.day_brightness;
            if (s_brightness != s_target)
            {
                int d = s_target - s_brightness;
                s_brightness += d > 0 ? 1 : -1;
                bsp_display_brightness_set(s_brightness);
            }
        }

        /* A kaputelefon a témaerőforrások betöltésétől és az energiaadatoktól
         * függetlenül, a csengetés teljes ideje alatt elsőbbséget kap. */
        if (intercom_prepare())
        {
            if (bsp_display_lock(-1) == ESP_OK)
            {
                intercom_render();
                bsp_display_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (bsp_display_lock(-1) == ESP_OK)
        {
            intercom_render();
            bsp_display_unlock();
        }
        diagnostics_capture();
        assistant_prepare();
        /* Egy frame betöltőképernyő a sablon nagy PNG-inek feldolgozása előtt. */
        if (!home_is_open() && !template_loading_frame())
        {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!home_prepare(have ? s.night_mode : home_night))
        {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!home_is_open()) template_prepare();
        if (bsp_display_lock(-1) == ESP_OK)
        {
            slave_display_update(&rt, &s, have, now);
            app_assistant_update(&assistant_state, esp_timer_get_time(), &s,
                have && s_cfg.data_recept_mode == APP_DATA_LOXONE && rt.loxone_connected,
                &assistant_consumers, &assistant_night);
            render_runtime(rt, s, have, now);
            assistant_render();

            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
/* A folyamatos UI runtime task egyszeri elindítása. */
esp_err_t app_ui_start_runtime(void)
{
    if (s_started)
        return ESP_OK;
    if (xTaskCreate(runtime, "ui_runtime", 16384, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    s_started = true;
    return ESP_OK;
}
