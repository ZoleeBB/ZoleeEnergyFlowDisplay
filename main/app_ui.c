/*
 * LVGL felhasználói felület a 1280x800-as fekvő kijelzőhöz.
 *
 * A modul kezeli az indulási képernyőt, a teljes dashboardot, az energiaáramlás
 * animációját, a nappali/éjszakai témát, a grafikonokat és a kommunikációs
 * hibaoldalakat. A kijelzőt csak az LVGL lock birtokában módosítjuk.
 */
#include "app_ui.h"
#include "app_assets.h"
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
#if LV_USE_OS != LV_OS_NONE || LV_DRAW_SW_DRAW_UNIT_CNT != 1
#error "Use LV_OS_NONE and one software draw unit; the adapter serializes LVGL with its own mutex"
#endif
#include "app_state.h"
#include "app_stats.h"
#include "app_storage.h"
#include "app_templates.h"
#include "app_diagnostics.h"

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
static bool s_cfg_ok,s_started,s_dash_created,s_last_night;
static lv_obj_t *s_screen,*s_dash,*s_sd_warn;
static lv_obj_t *s_title,*s_date,*s_clock,*s_weather_temp,*s_wind_speed,*s_weather_icon,*s_wind_icon;
static lv_obj_t *s_pv,*s_home,*s_bat,*s_grid,*s_soc,*s_bt,*s_status;
static lv_obj_t *s_bat_icon,*s_grid_icon,*s_status_icon;
static lv_obj_t *s_grid_node,*s_battery_node,*s_pv_node,*s_home_node,*s_status_bubble,*s_status_detail;
static lv_obj_t *s_battery_bar,*s_status_card;
static lv_obj_t *s_bat_charge,*s_bat_charge_cmp,*s_bat_discharge,*s_bat_discharge_cmp;
static lv_obj_t *s_metric[8],*s_cmp[8],*s_cmp_note[8];
static lv_obj_t *s_chart1,*s_chart2;
static lv_obj_t *s_chart1_y_top,*s_chart1_y_mid,*s_chart1_y_bottom;
static lv_obj_t *s_chart2_y_top,*s_chart2_y_mid,*s_chart2_y_bottom;

static lv_obj_t *s_error_pulse;
static int s_brightness=50,s_target=50;
static int64_t s_last_chart_update_us;
/* A 20 kB-os snapshot a UI modul tulajdona, nem a task vermét terheli. */
static app_stats_snapshot_t s_stats_snapshot;
static app_stats_period_t s_week_period,s_month_period;
static app_diagnostics_t s_diagnostics;
static bool s_diagnostics_requested;
static lv_obj_t *s_diagnostic_values[6];
static app_energy_sample_t s_animation_sample;
static int64_t s_animation_us;
static void animation_tick(lv_timer_t *timer);
static bool template_is_active(void);
static void template_animate(float dt);
static unsigned template_catalogue_count(void);
static void template_gesture(lv_event_t *event);

/* A runtime UI kölcsönösen kizáró fő nézetei. */
typedef enum {
    VIEW_NONE = 0,
    VIEW_DASHBOARD,
    VIEW_DIAGNOSTICS,
    VIEW_WIFI_ERROR,
    VIEW_MQTT_ERROR,
    VIEW_WAIT_DATA,
    VIEW_DATA_TIMEOUT,
    VIEW_FATAL
} runtime_view_t;

static runtime_view_t s_view = VIEW_NONE;
static app_boot_stage_t s_boot_stage=APP_BOOT_SD;
static void error_screen(runtime_view_t view,const char *title,const char *detail,bool fatal);

/* Az MQTT adatokból származtatott időjárási ikonállapotok. */
typedef enum {
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
typedef struct {
    lv_obj_t *line;
    lv_obj_t *dots[3];
    lv_point_precise_t points[2];
    int x1,y1,x2,y2;
    float phase;
} flow_t;
static flow_t f_pv,f_bat,f_grid,f_home;

/* 24 bites RGB szín átalakítása LVGL színobjektummá. */
static lv_color_t c(uint32_t x){return lv_color_hex(x);}
/* Igaz, ha a felület magyar nyelven működik. */
static bool hu(void){return !s_cfg_ok||strcmp(s_cfg.project.language,"hu")==0;}
/* Két beégetett fordítás közül a konfigurált nyelv kiválasztása. */
static const char *tr(const char *a,const char *b){return hu()?a:b;}

/* Éjszakai módban ugyanazon funkciószínek tompított változatának kiválasztása. */
static uint32_t tone(uint32_t color,bool night){
    if(!night)return color;
    if(color==GREEN)return 0x23784C;
    if(color==ORANGE)return 0xA46E2D;
    if(color==YELLOW)return 0x95852B;
    if(color==RED)return 0x8E3A38;
    if(color==BLUE)return 0x2B5D8B;
    if(color==GRAY)return 0x56615B;
    return color;
}

/* Csak saját, azonos selectorú stílusértéket hagyunk ki: az öröklésből
 * származó egyezés nem helyettesíti a kívánt explicit lokális beállítást. */
static void style_color_if_changed(lv_obj_t *obj, lv_style_prop_t prop,
                                  lv_color_t color, lv_style_selector_t selector)
{
    lv_style_value_t old;
    if (lv_obj_get_local_style_prop(obj,prop,&old,selector)==LV_STYLE_RES_FOUND && lv_color_eq(old.color,color)) return;
    lv_style_value_t value={.color=color};lv_obj_set_local_style_prop(obj,prop,value,selector);
}
static void style_number_if_changed(lv_obj_t *obj, lv_style_prop_t prop,
                                   int32_t number, lv_style_selector_t selector)
{
    lv_style_value_t old;
    if (lv_obj_get_local_style_prop(obj,prop,&old,selector)==LV_STYLE_RES_FOUND && old.num==number) return;
    lv_style_value_t value={.num=number};lv_obj_set_local_style_prop(obj,prop,value,selector);
}
#define text_color_if_changed(o,v,s) style_color_if_changed(o,LV_STYLE_TEXT_COLOR,v,s)
#define bg_color_if_changed(o,v,s) style_color_if_changed(o,LV_STYLE_BG_COLOR,v,s)
#define border_color_if_changed(o,v,s) style_color_if_changed(o,LV_STYLE_BORDER_COLOR,v,s)
#define opa_if_changed(o,v,s) style_number_if_changed(o,LV_STYLE_OPA,v,s)
#define bg_opa_if_changed(o,v,s) style_number_if_changed(o,LV_STYLE_BG_OPA,v,s)
#define border_opa_if_changed(o,v,s) style_number_if_changed(o,LV_STYLE_BORDER_OPA,v,s)
#define text_opa_if_changed(o,v,s) style_number_if_changed(o,LV_STYLE_TEXT_OPA,v,s)

/* Teljes képernyős háttér alapstílusa. */
static void screen_style(lv_obj_t *o,bool night){bg_color_if_changed(o,c(night?BG_DARK:BG_LIGHT),0);bg_opa_if_changed(o,LV_OPA_COVER,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);}
/* Közös kártyastílus: háttér, keret, lekerekítés és nappali árnyék. */
static void card_style(lv_obj_t *o,bool night){
    bg_color_if_changed(o,c(night?CARD_DARK:CARD_LIGHT),0);
    bg_opa_if_changed(o,LV_OPA_COVER,0);
    border_color_if_changed(o,c(night?0x26302A:0xE4E9EB),0);
    lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_radius(o,18,0);
    lv_obj_set_style_pad_all(o,12,0);
    lv_obj_set_style_shadow_width(o,night?0:12,0);
    lv_obj_set_style_shadow_opa(o,night?LV_OPA_TRANSP:LV_OPA_10,0);
    lv_obj_set_style_shadow_ofs_y(o,night?0:4,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
}
/* A változatlan szöveg ne foglaljon memóriát és ne érvénytelenítse a rajzterületet. */
static void label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!text || strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}
/* Egységes LVGL címke létrehozása pozícióval, fonttal és színnel. */
static lv_obj_t *lbl(lv_obj_t *p,const char *txt,int x,int y,const lv_font_t *font,uint32_t color){lv_obj_t *l=lv_label_create(p);label_text_if_changed(l,txt?txt:"");lv_obj_set_pos(l,x,y);if(font)lv_obj_set_style_text_font(l,font,0);text_color_if_changed(l,c(color),0);return l;}
/* Az érték nem lóghat át a szomszédos kártyába; a legnagyobb elférő fontot választjuk. */
static void fit_text(lv_obj_t *label,const char *text,int width,const lv_font_t *preferred)
{
    const lv_font_t *fonts[]={APP_FONT_28,APP_FONT_22,APP_FONT_20,APP_FONT_18,
        APP_FONT_16,APP_FONT_14,APP_FONT_12};
    const lv_font_t *chosen=APP_FONT_12;
    for(size_t i=0;i<sizeof(fonts)/sizeof(fonts[0]);i++){
        if(fonts[i]->line_height>preferred->line_height)continue;
        lv_point_t size;
        lv_text_get_size(&size,text,fonts[i],0,0,32767,LV_TEXT_FLAG_NONE);
        if(size.x<=width){chosen=fonts[i];break;}
    }
    if(lv_obj_get_style_width(label,0)!=width)lv_obj_set_width(label,width);
    if(lv_obj_get_style_text_font(label,0)!=chosen)lv_obj_set_style_text_font(label,chosen,0);
    if(lv_label_get_long_mode(label)!=LV_LABEL_LONG_MODE_DOTS)lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_DOTS);
    label_text_if_changed(label,text);
}

/* Az aktív képernyő tartalmának törlése és a UI objektumállapot alaphelyzetbe állítása. */
static void clear(void){
    lv_obj_clean(s_screen);
    s_dash=NULL;
    s_dash_created=false;
    s_error_pulse=NULL;
    s_view=VIEW_NONE;
}

/* SOC alapján a specifikált zöld/narancs/piros akkumulátorszín kiválasztása. */
static uint32_t battery_color(double soc){
    if(soc>50.0)return GREEN;
    if(soc>=20.0)return ORANGE;
    return RED;
}

/* Egyszerű kitöltött téglalap/kör primitív létrehozása ikonokhoz és jelölőkhöz. */
static lv_obj_t *shape(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius){
    lv_obj_t *obj=lv_obj_create(parent);
    lv_obj_set_pos(obj,x,y);
    lv_obj_set_size(obj,w,h);
    bg_color_if_changed(obj,c(color),0);
    bg_opa_if_changed(obj,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(obj,0,0);
    lv_obj_set_style_radius(obj,radius,0);
    lv_obj_set_style_pad_all(obj,0,0);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

/* Átlátszó dobozba középre helyezett PNG asset létrehozása. */
static lv_obj_t *asset_image_box(
    lv_obj_t *parent,
    int x,
    int y,
    int box_size,
    int image_size,
    const lv_image_dsc_t *asset,
    bool night)
{
    lv_obj_t *box=lv_obj_create(parent);
    lv_obj_set_pos(box,x,y);
    lv_obj_set_size(box,box_size,box_size);
    bg_opa_if_changed(box,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(box,0,0);
    lv_obj_set_style_pad_all(box,0,0);
    lv_obj_remove_flag(box,LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *image=lv_image_create(box);
    lv_image_set_src(image,asset);
    uint32_t source_width=
        asset->header.w>0?asset->header.w:128;

    lv_image_set_scale(
        image,
        (uint32_t)(
            (image_size*256U+source_width/2U)/
            source_width));
    opa_if_changed(
        image,
        night?LV_OPA_80:LV_OPA_COVER,
        0);
    lv_obj_align(image,LV_ALIGN_CENTER,0,0);
    return image;
}

/* Az aktuális időjárási állapothoz tartozó PNG asset kiválasztása. */
static const lv_image_dsc_t *weather_asset(weather_kind_t kind)
{
    switch(kind){
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
static void weather_render(weather_kind_t kind,bool night)
{
    if(!s_weather_icon||kind==s_last_weather){
        return;
    }

    lv_image_set_src(
        s_weather_icon,
        weather_asset(kind));

    opa_if_changed(
        s_weather_icon,
        night?LV_OPA_80:LV_OPA_COVER,
        0);

    s_last_weather=kind;
}

/* Lebegőpontos szám lokalizált tizedeselválasztóval történő formázása. */
static void fmt_dec(char *out,size_t n,double v,int d){snprintf(out,n,"%.*f",d,v);if(hu()){char *p=strchr(out,'.');if(p)*p=',';}}
/* Teljesítmény formázása: 1 kW alatt egész W, felette egy tizedes kW. */
static void fmt_power(char *out,size_t n,double kw){double a=fabs(kw);if(a>=1){char q[20];fmt_dec(q,sizeof(q),a,1);snprintf(out,n,"%s kW",q);}else snprintf(out,n,"%d W",(int)llround(a*1000));}
/* Energia formázása egy tizedes kWh értékre. */
static void fmt_kwh(char *out,size_t n,double v){char q[20];fmt_dec(q,sizeof(q),v,1);snprintf(out,n,"%s kWh",q);}
/* Százalék formázása egy tizedessel. */
static void fmt_pct(char *out,size_t n,double v){char q[20];fmt_dec(q,sizeof(q),v,1);snprintf(out,n,"%s%%",q);}
/* Tegnapi összehasonlítás nyíl + százalék szövegének elkészítése. */
static void fmt_cmp(char *out,size_t n,double t,double y){
    if(fabs(y)<1e-9){
        snprintf(out,n,"—");
        return;
    }
    double p=(t-y)/y*100;
    char q[20];
    fmt_dec(q,sizeof(q),p,1);
    snprintf(out,n,"%s %s%s%%",p>=0?"↑":"↓",p>=0?"+":"",q);
}
/* Összehasonlítási értékhez zöld, narancs vagy szürke szín választása. */
static uint32_t cmp_color(double t,double y){if(fabs(y)<1e-9)return GRAY;return t>=y?GREEN:ORANGE;}

/* A körháttér külön LVGL réteg, az ikon átlátszó PNG. */
static lv_obj_t *icon_bubble(
    lv_obj_t *parent,
    int x,
    int y,
    int size,
    const lv_image_dsc_t *asset,
    bool night,
    uint32_t accent)
{
    lv_obj_t *bubble=lv_obj_create(parent);
    lv_obj_set_pos(bubble,x,y);
    lv_obj_set_size(bubble,size,size);
    lv_obj_set_style_radius(bubble,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_border_width(bubble,0,0);
    bg_color_if_changed(bubble,c(tone(accent,night)),0);
    bg_opa_if_changed(bubble,night?LV_OPA_10:LV_OPA_20,0);
    lv_obj_set_style_pad_all(bubble,0,0);
    lv_obj_remove_flag(bubble,LV_OBJ_FLAG_SCROLLABLE);

    asset_image_box(
        bubble,
        0,
        0,
        size,
        (asset==&app_img_grid_import || asset==&app_img_grid_export)?size:size-12,
        asset,
        night);

    return bubble;
}

/* Energia-node: natív, dinamikus körvonal és külön átlátszó PNG. */
static lv_obj_t *energy_circle(
    lv_obj_t *parent,
    int center_x,
    int center_y,
    int size,
    const lv_image_dsc_t *asset,
    lv_obj_t **image_out,
    bool night,
    uint32_t accent)
{
    lv_obj_t *outer=lv_obj_create(parent);
    lv_obj_set_pos(
        outer,
        center_x-size/2,
        center_y-size/2);
    lv_obj_set_size(outer,size,size);
    lv_obj_set_style_radius(outer,LV_RADIUS_CIRCLE,0);
    bg_color_if_changed(outer,c(night?0x202B30:0xF2FBFE),0);
    bg_opa_if_changed(outer,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(outer,4,0);
    border_color_if_changed(outer,c(tone(accent,night)),0);
    lv_obj_set_style_shadow_width(outer,12,0);
    lv_obj_set_style_shadow_opa(outer,LV_OPA_10,0);
    lv_obj_set_style_pad_all(outer,0,0);
    lv_obj_remove_flag(outer,LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *image=asset_image_box(
        outer,
        0,
        0,
        size-8,
        size-16,
        asset,
        night);

    if(image_out){
        *image_out=image;
    }

    return outer;
}

/* Egy energiaáramlási vonal és három animált pont egyszeri létrehozása. */
static void flow_init(flow_t *f,lv_obj_t *p,int x1,int y1,int x2,int y2,bool night){
    f->x1=x1;
    f->y1=y1;
    f->x2=x2;
    f->y2=y2;
    f->phase=0;
    f->points[0].x=x1;
    f->points[0].y=y1;
    f->points[1].x=x2;
    f->points[1].y=y2;

    f->line=lv_line_create(p);
    lv_line_set_points(f->line,f->points,2);
    lv_obj_set_style_line_width(f->line,3,0);
    lv_obj_set_style_line_color(f->line,c(night?0x333B37:0xDCE3E5),0);

    for(int i=0;i<3;i++){
        f->dots[i]=lv_obj_create(p);
        lv_obj_set_size(f->dots[i],9,9);
        lv_obj_set_style_radius(f->dots[i],LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_border_width(f->dots[i],0,0);
        lv_obj_add_flag(f->dots[i],LV_OBJ_FLAG_HIDDEN);
    }
}
/* A körvonal és a kapcsolat ugyanazt a nappali/éjszakai színt kapja. */
static uint32_t flow_color(double kw,uint32_t active_color,bool night)
{
    return fabs(kw)<0.0005?(night?0x333B37:0xDCE3E5):tone(active_color,night);
}

/* Energiaáramlási vonal színének, irányának és pontsebességének frissítése. */
static void flow_update(flow_t *f,double kw,bool forward,uint32_t color,bool night,float dt){
    /* Közel nulla teljesítménynél csak a halvány statikus kapcsolat marad. */
    double m=fabs(kw);if(m<0.0005){lv_obj_set_style_line_color(f->line,c(flow_color(kw,color,night)),0);for(int i=0;i<3;i++)lv_obj_add_flag(f->dots[i],LV_OBJ_FLAG_HIDDEN);return;}
    /*
     * 0..7 kW között lineárisan nő a pontsebesség, 7 kW felett telítődik.
     * Az irányt a forward paraméter határozza meg.
     */
    uint32_t shown=flow_color(kw,color,night);
    lv_obj_set_style_line_color(f->line,c(shown),0);f->phase=app_flow_advance(f->phase,kw,forward,dt);
    for(int i=0;i<3;i++){float t=f->phase+i/3.0f;while(t>=1)t-=1;int x=(int)lroundf(f->x1+(f->x2-f->x1)*t),y=(int)lroundf(f->y1+(f->y2-f->y1)*t);lv_obj_set_pos(f->dots[i],x-4,y-4);bg_color_if_changed(f->dots[i],c(shown),0);bg_opa_if_changed(f->dots[i],LV_OPA_COVER,0);lv_obj_clear_flag(f->dots[i],LV_OBJ_FLAG_HIDDEN);}
}
/* Bal oldali, nagy napi mutatókártya az eredeti látványterv vizuális hierarchiájával. */
static lv_obj_t *metric_card(
    lv_obj_t *parent,
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
    lv_obj_t *card=lv_obj_create(parent);
    lv_obj_set_pos(card,x,y);
    lv_obj_set_size(card,width,height);
    card_style(card,night);
    lv_obj_set_style_pad_all(card,0,0);
    const int top=(height-(91+APP_FONT_12->line_height))/2;

    icon_bubble(
        card,
        23,
        (height-64)/2,
        64,
        asset,
        night,
        accent);

    lv_obj_t *title_label=
        lbl(card,title,101,top,APP_FONT_14,night?TEXT_DARK:TEXT_LIGHT);
    lv_label_set_long_mode(title_label,LV_LABEu�������k�w��{
        error_screen(
            VIEW_WAIT_DATA,
            http?tr("Várakozás HTTP adatcsomagra","Waiting for HTTP data"):tr("Várakozás MQTT adatcsomagra","Waiting for MQTT data"),
            tr("Az első érvényes adatcsomagra várunk...","Waiting for the first valid packet..."),
            false);
    }else if(!have||timed_out){
        error_screen(
            VIEW_DATA_TIMEOUT,
            http?tr("Nem érkezik HTTP adat","No HTTP data received"):tr("Nem érkezett MQTT adatcsomag.","No MQTT data packet received."),
            tr("Kapcsolat helyreállítására várunk...","Waiting for connection to recover..."),
            false);
    }else{
        if(!s_dash_created||s.night_mode!=s_last_night){
            if(template_is_active())template_build(s.night_mode);else build_dashboard(s.night_mode);
            s_last_night=s.night_mode;
        }
        if(template_is_active())template_update(&s,&s_stats_snapshot);else update(&s,&s_stats_snapshot);
    }

}

static void runtime(void *arg){
    (void)arg;
    (void)esp_task_wdt_add(NULL);

    while(true){
        (void)esp_task_wdt_reset();
        int64_t now=esp_timer_get_time();

        app_runtime_state_t rt;
        app_state_get_runtime(&rt);

        app_energy_sample_t s={0};
        bool have=app_state_get_sample(&s);
        if(have){
            app_stats_get_snapshot(&s_stats_snapshot);
            static int64_t period_update;
            if(!period_update||now-period_update>=5000000){
                app_stats_get_period(false,&s_week_period);app_stats_get_period(true,&s_month_period);period_update=now;
            }
        }

        if(have){
            s_target=s.night_mode?s_cfg.display.night_brightness:s_cfg.display.day_brightness;
            if(s_brightness!=s_target){
                int d=s_target-s_brightness;
                s_brightness+=d>0?1:-1;
                bsp_display_brightness_set(s_brightness);
            }
        }

        app_diagnostics_capture(&s_diagnostics);
        template_prepare();
        if(bsp_display_lock(-1)==ESP_OK){
            render_runtime(rt,s,have,now);

            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
/* A folyamatos UI runtime task egyszeri elindítása. */
esp_err_t app_ui_start_runtime(void)
{
    if(s_started)return ESP_OK;
    if(xTaskCreate(runtime,"ui_runtime",16384,NULL,5,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    s_started=true;
    return ESP_OK;
}


