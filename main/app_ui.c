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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "app_state.h"
#include "app_stats.h"
#include "app_storage.h"

LV_FONT_DECLARE(app_font_12);
#define APP_FONT_12 (&app_font_12)
LV_FONT_DECLARE(app_font_14);
#define APP_FONT_14 (&app_font_14)
LV_FONT_DECLARE(app_font_16);
#define APP_FONT_16 (&app_font_16)
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
#define RED 0xE53935
#define BLUE 0x0060C0
#define GRAY 0x96A1AA

/* A boot után RAM-ban tartott konfiguráció és a UI életciklusának állapota. */
static app_config_t s_cfg;
static bool s_cfg_ok,s_started,s_dash_created,s_last_night;
static lv_obj_t *s_screen,*s_dash,*s_sd_warn;
static lv_obj_t *s_title,*s_date,*s_clock,*s_weather_temp,*s_wind_speed,*s_weather_icon;
static lv_obj_t *s_pv,*s_home,*s_bat,*s_grid,*s_soc,*s_bt,*s_status;
static lv_obj_t *s_bat_icon,*s_grid_icon,*s_status_icon;
static lv_obj_t *s_grid_node,*s_battery_node,*s_pv_node,*s_home_node,*s_status_bubble,*s_status_detail;
static lv_obj_t *s_battery_bar,*s_status_card;
static lv_obj_t *s_bat_soc_detail,*s_bat_power_detail,*s_bat_temp_detail;
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
static app_energy_sample_t s_animation_sample;
static int64_t s_animation_us;
static void animation_tick(lv_timer_t *timer);

/* A runtime UI kölcsönösen kizáró fő nézetei. */
typedef enum {
    VIEW_NONE = 0,
    VIEW_DASHBOARD,
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
    if(color==RED)return 0x8E3A38;
    if(color==BLUE)return 0x2B5D8B;
    if(color==GRAY)return 0x56615B;
    return color;
}

/* Teljes képernyős háttér alapstílusa. */
static void screen_style(lv_obj_t *o,bool night){lv_obj_set_style_bg_color(o,c(night?BG_DARK:BG_LIGHT),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);}
/* Közös kártyastílus: háttér, keret, lekerekítés és nappali árnyék. */
static void card_style(lv_obj_t *o,bool night){
    lv_obj_set_style_bg_color(o,c(night?CARD_DARK:CARD_LIGHT),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(o,c(night?0x26302A:0xE4E9EB),0);
    lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_radius(o,18,0);
    lv_obj_set_style_pad_all(o,12,0);
    lv_obj_set_style_shadow_width(o,night?0:12,0);
    lv_obj_set_style_shadow_opa(o,night?LV_OPA_TRANSP:LV_OPA_10,0);
    lv_obj_set_style_shadow_ofs_y(o,night?0:4,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
}
/* Egységes LVGL címke létrehozása pozícióval, fonttal és színnel. */
static lv_obj_t *lbl(lv_obj_t *p,const char *txt,int x,int y,const lv_font_t *font,uint32_t color){lv_obj_t *l=lv_label_create(p);lv_label_set_text(l,txt?txt:"");lv_obj_set_pos(l,x,y);if(font)lv_obj_set_style_text_font(l,font,0);lv_obj_set_style_text_color(l,c(color),0);return l;}
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
    lv_obj_set_width(label,width);
    lv_obj_set_style_text_font(label,chosen,0);
    lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(label,text);
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
    lv_obj_set_style_bg_color(obj,c(color),0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
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
    lv_obj_set_style_bg_opa(box,LV_OPA_TRANSP,0);
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
    lv_obj_set_style_opa(
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

    lv_obj_set_style_opa(
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
    lv_obj_set_style_bg_color(bubble,c(tone(accent,night)),0);
    lv_obj_set_style_bg_opa(bubble,night?LV_OPA_10:LV_OPA_20,0);
    lv_obj_set_style_pad_all(bubble,0,0);
    lv_obj_remove_flag(bubble,LV_OBJ_FLAG_SCROLLABLE);

    asset_image_box(
        bubble,
        0,
        0,
        size,
        size-12,
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
    lv_obj_set_style_bg_color(outer,c(night?0x202B30:0xF2FBFE),0);
    lv_obj_set_style_bg_opa(outer,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(outer,4,0);
    lv_obj_set_style_border_color(outer,c(tone(accent,night)),0);
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
    for(int i=0;i<3;i++){float t=f->phase+i/3.0f;while(t>=1)t-=1;int x=(int)lroundf(f->x1+(f->x2-f->x1)*t),y=(int)lroundf(f->y1+(f->y2-f->y1)*t);lv_obj_set_pos(f->dots[i],x-4,y-4);lv_obj_set_style_bg_color(f->dots[i],c(shown),0);lv_obj_set_style_bg_opa(f->dots[i],LV_OPA_COVER,0);lv_obj_clear_flag(f->dots[i],LV_OBJ_FLAG_HIDDEN);}
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

    icon_bubble(
        card,
        10,
        25,
        64,
        asset,
        night,
        accent);

    lv_obj_t *title_label=
        lbl(card,title,88,8,APP_FONT_14,night?TEXT_DARK:TEXT_LIGHT);
    lv_label_set_long_mode(title_label,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title_label,width-110);

    lbl(
        card,
        tr("Ma összesen","Today total"),
        88,
        27,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    s_metric[index]=
        lbl(card,"-",88,45,APP_FONT_28,night?TEXT_DARK:TEXT_LIGHT);

    s_cmp[index]=
        lbl(card,"",88,79,APP_FONT_14,GREEN);

    s_cmp_note[index]=lbl(
        card,
        tr("tegnaphoz képest","vs yesterday"),
        88,
        99,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    return card;
}

/* Alsó összefoglaló szalag egy szegmense. */
static lv_obj_t *summary_metric(
    lv_obj_t *parent,
    int x,
    int width,
    const char *title,
    const lv_image_dsc_t *asset,
    int index,
    bool night,
    uint32_t accent)
{
    lv_obj_t *segment=lv_obj_create(parent);
    lv_obj_set_pos(segment,x,0);
    lv_obj_set_size(segment,width,92);
    lv_obj_set_style_bg_opa(segment,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(segment,0,0);
    lv_obj_set_style_pad_all(segment,0,0);
    lv_obj_remove_flag(segment,LV_OBJ_FLAG_SCROLLABLE);

    icon_bubble(
        segment,
        4,
        18,
        54,
        asset,
        night,
        accent);

    lv_obj_t *title_label=
        lbl(segment,title,68,6,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    lv_label_set_long_mode(title_label,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title_label,width-74);

    s_metric[index]=
        lbl(segment,"-",68,29,APP_FONT_20,night?TEXT_DARK:TEXT_LIGHT);

    s_cmp[index]=
        lbl(segment,"",68,58,APP_FONT_12,GREEN);

    s_cmp_note[index]=NULL;
    return segment;
}

/* A teljes 1280x800 dashboard objektumfájának felépítése nappali vagy éjszakai témában. */
static void build_dashboard(bool night)
{
    clear();
    screen_style(s_screen,night);

    s_dash=lv_obj_create(s_screen);
    lv_obj_set_size(s_dash,W,H);
    screen_style(s_dash,night);
    lv_obj_remove_flag(s_dash,LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Fejléc: az eredeti látványterv fehér, lekerekített fejlécét tartjuk meg,
     * de a későbbi döntés szerint nincs navigációs tabsor és dátumválasztó.
     */
    lv_obj_t *header=lv_obj_create(s_dash);
    lv_obj_set_pos(header,20,12);
    lv_obj_set_size(header,1240,58);
    card_style(header,night);
    lv_obj_set_style_radius(header,22,0);
    lv_obj_set_style_pad_all(header,6,0);

    icon_bubble(
        header,
        4,
        2,
        42,
        &app_img_header_home_leaf,
        night,
        GREEN);

    s_title=lbl(
        header,
        s_cfg.project.title,
        54,
        -1,
        APP_FONT_22,
        night?TEXT_DARK:TEXT_LIGHT);
    lv_label_set_long_mode(s_title,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_title,560);

    lbl(
        header,
        tr("Valós idejű energiaáramlás","Real-time energy flow"),
        54,
        29,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    s_date=lbl(
        header,
        "",
        675,
        17,
        APP_FONT_16,
        night?MUTED_DARK:MUTED_LIGHT);

    s_weather_icon=asset_image_box(
        header,
        850,
        2,
        46,
        42,
        &app_img_weather_sun,
        night);
    s_last_weather=(weather_kind_t)-1;

    s_weather_temp=lbl(
        header,
        "",
        900,
        17,
        APP_FONT_16,
        night?TEXT_DARK:TEXT_LIGHT);

    asset_image_box(
        header,
        985,
        8,
        36,
        30,
        &app_img_wind,
        night);

    s_wind_speed=lbl(
        header,
        "",
        1020,
        17,
        APP_FONT_16,
        night?TEXT_DARK:TEXT_LIGHT);

    s_clock=lbl(
        header,
        "",
        1125,
        9,
        APP_FONT_28,
        night?TEXT_DARK:TEXT_LIGHT);

    /*
     * Bal oldali napi kártyák: az eredeti mockup nagy, ikonközpontú
     * összefoglaló kártyáinak szerkezete.
     */
    metric_card(
        s_dash,20,82,300,132,
        tr("Mai termelés","Production"),
        &app_img_leaf,
        0,night,GREEN);

    metric_card(
        s_dash,20,224,300,132,
        tr("Mai fogyasztás","Consumption"),
        &app_img_house,
        1,night,ORANGE);

    metric_card(
        s_dash,20,366,300,132,
        tr("Hálózati vételezés","Grid import"),
        &app_img_grid_metric,
        2,night,BLUE);

    metric_card(
        s_dash,20,508,300,142,
        tr("Visszatáplálás","Grid export"),
        &app_img_grid_metric,
        3,night,BLUE);

    /*
     * Központi energiaáramlás: az eredeti látványterv szerint ez a képernyő
     * fő vizuális eleme. A négy energiaelem egy szürke központi hub körül áll.
     */
    lv_obj_t *flow=lv_obj_create(s_dash);
    lv_obj_set_pos(flow,332,82);
    lv_obj_set_size(flow,490,568);
    card_style(flow,night);
    lv_obj_set_style_pad_all(flow,0,0);

    lbl(
        flow,
        tr("Energiaáramlás","Energy flow"),
        16,
        12,
        APP_FONT_18,
        night?TEXT_DARK:TEXT_LIGHT);

    /* A kapcsolatokat előbb hozzuk létre, hogy a kör alakú node-ok föléjük kerüljenek. */
    flow_init(&f_grid,flow,245,156,245,210,night);
    flow_init(&f_bat,flow,146,245,210,245,night);
    flow_init(&f_pv,flow,344,245,280,245,night);
    flow_init(&f_home,flow,245,280,245,329,night);

    /* Központi energiaelosztó: a látványterv szürke, villámos hub grafikája. */
    asset_image_box(
        flow,
        210,
        210,
        70,
        70,
        &app_img_hub,
        night);

    /* Hálózat felül. */
    s_grid_node=energy_circle(
        flow,
        245,
        110,
        92,
        &app_img_grid,
        &s_grid_icon,
        night,
        GREEN);

    s_grid=lbl(
        flow,
        "-",
        170,
        23,
        APP_FONT_22,
        night?TEXT_DARK:TEXT_LIGHT);
    lv_obj_set_width(s_grid,150);
    lv_obj_set_style_text_align(s_grid,LV_TEXT_ALIGN_CENTER,0);

    lv_obj_t *grid_name=lbl(
        flow,
        tr("Hálózat","Grid"),
        170,
        46,
        APP_FONT_14,
        night?MUTED_DARK:MUTED_LIGHT);
    lv_obj_set_width(grid_name,150);
    lv_obj_set_style_text_align(grid_name,LV_TEXT_ALIGN_CENTER,0);

    /* Akkumulátor bal oldalon. */
    s_battery_node=energy_circle(
        flow,
        100,
        245,
        92,
        &app_img_battery,
        &s_bat_icon,
        night,
        ORANGE);

    /*
     * A kör alakú akkumulátor-grafikán belül külön SOC sáv is látható.
     * A függőleges sáv az ikon bal oldalába illesztve alulról felfelé jelzi a SOC-ot.
     * Színe a töltöttséget követi. Az ikon állandó PNG;
     * a körvonal színe kizárólag az energiaáramlás színét követi.
     */
    s_battery_bar=lv_bar_create(s_battery_node);
    lv_obj_set_pos(s_battery_bar,24,23);
    lv_obj_set_size(s_battery_bar,4,46);
    lv_bar_set_orientation(s_battery_bar,LV_BAR_ORIENTATION_VERTICAL);
    lv_bar_set_range(s_battery_bar,0,100);
    lv_obj_set_style_bg_color(
        s_battery_bar,
        c(night?0x29302C:0xE4E9E6),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(
        s_battery_bar,
        LV_OPA_COVER,
        LV_PART_MAIN);
    lv_obj_set_style_radius(
        s_battery_bar,
        4,
        LV_PART_MAIN);
    lv_obj_set_style_radius(
        s_battery_bar,
        4,
        LV_PART_INDICATOR);

    lbl(
        flow,
        tr("Akkumulátor","Battery"),
        6,
        145,
        APP_FONT_14,
        night?MUTED_DARK:MUTED_LIGHT);

    s_bat=lbl(
        flow,
        "-",
        6,
        166,
        APP_FONT_22,
        night?TEXT_DARK:TEXT_LIGHT);

    s_soc=lbl(
        flow,
        "",
        6,
        193,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    s_bt=lbl(
        flow,
        "",
        6,
        211,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    /* Napelem jobb oldalon. */
    s_pv_node=energy_circle(
        flow,
        390,
        245,
        92,
        &app_img_solar,
        NULL,
        night,
        GREEN);

    lbl(
        flow,
        tr("Napelem","Solar"),
        326,
        145,
        APP_FONT_14,
        night?MUTED_DARK:MUTED_LIGHT);

    s_pv=lbl(
        flow,
        "-",
        326,
        166,
        APP_FONT_22,
        night?TEXT_DARK:TEXT_LIGHT);

    /* Ház alul. */
    s_home_node=energy_circle(
        flow,
        245,
        375,
        92,
        &app_img_home_scene,
        NULL,
        night,
        ORANGE);

    lv_obj_t *home_name=lbl(
        flow,
        tr("Ház","Home"),
        170,
        424,
        APP_FONT_14,
        night?MUTED_DARK:MUTED_LIGHT);
    lv_obj_set_width(home_name,150);
    lv_obj_set_style_text_align(home_name,LV_TEXT_ALIGN_CENTER,0);

    s_home=lbl(
        flow,
        "-",
        170,
        444,
        APP_FONT_22,
        night?TEXT_DARK:TEXT_LIGHT);
    lv_obj_set_width(s_home,150);
    lv_obj_set_style_text_align(s_home,LV_TEXT_ALIGN_CENTER,0);

    /*
     * Részletes akkumulátorblokk a központi kártya alján.
     * Csak a napi töltés és kisütés kap tegnapi összehasonlítást.
     */
    lv_obj_t *battery_detail=lv_obj_create(flow);
    lv_obj_set_pos(battery_detail,8,474);
    lv_obj_set_size(battery_detail,450,78);
    card_style(battery_detail,night);
    lv_obj_set_style_radius(battery_detail,14,0);
    lv_obj_set_style_pad_all(battery_detail,8,0);

    s_bat_soc_detail=
        lbl(battery_detail,"",0,0,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);
    s_bat_power_detail=
        lbl(battery_detail,"",135,0,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);
    s_bat_temp_detail=
        lbl(battery_detail,"",285,0,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);

    s_bat_charge=
        lbl(battery_detail,"",0,28,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);
    s_bat_charge_cmp=
        lbl(battery_detail,"",0,49,APP_FONT_12,GREEN);

    s_bat_discharge=
        lbl(battery_detail,"",225,28,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);
    s_bat_discharge_cmp=
        lbl(battery_detail,"",225,49,APP_FONT_12,GREEN);

    /*
     * Jobb oldali két grafikon egymás alatt, az eredeti mockup szerint.
     * A későbbi specifikáció miatt mindkettő két sorozatot tartalmaz.
     */
    lv_obj_t *chart_card_1=lv_obj_create(s_dash);
    lv_obj_set_pos(chart_card_1,834,82);
    lv_obj_set_size(chart_card_1,426,276);
    card_style(chart_card_1,night);

    lbl(
        chart_card_1,
        tr("Termelés és fogyasztás","Production and consumption"),
        0,
        0,
        APP_FONT_16,
        night?TEXT_DARK:TEXT_LIGHT);

    shape(
        chart_card_1,
        230,7,8,8,
        tone(GREEN,night),
        LV_RADIUS_CIRCLE);
    lbl(
        chart_card_1,
        tr("Termelés","PV"),
        243,
        2,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    shape(
        chart_card_1,
        315,7,8,8,
        tone(ORANGE,night),
        LV_RADIUS_CIRCLE);
    lbl(
        chart_card_1,
        tr("Fogy.","Load"),
        328,
        2,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    lbl(
        chart_card_1,
        "kW",
        0,
        23,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    s_chart1_y_top=
        lbl(chart_card_1,"",0,41,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    s_chart1_y_mid=
        lbl(chart_card_1,"",0,130,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    s_chart1_y_bottom=
        lbl(chart_card_1,"0",0,217,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);

    s_chart1=app_chart_create(chart_card_1,false,night,
        tone(GREEN,night),tone(ORANGE,night));
    if(s_chart1){
        lv_obj_set_pos(s_chart1,38,48);
        lv_obj_set_size(s_chart1,350,178);
    }
    /* A felirat középpontja a negyednapos rácsvonalra esik. */
    for(int tick=0;tick<5;tick++){
        char text[8];
        snprintf(text,sizeof(text),"%02d:00",tick*6);
        lv_obj_t *label=lbl(chart_card_1,text,38+(int)lround(349.*tick/4.)-23,234,
            APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
        lv_obj_set_width(label,46);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    }

    lv_obj_t *chart_card_2=lv_obj_create(s_dash);
    lv_obj_set_pos(chart_card_2,834,374);
    lv_obj_set_size(chart_card_2,426,276);
    card_style(chart_card_2,night);

    lbl(
        chart_card_2,
        tr("Hálózat és akkumulátor","Grid and battery"),
        0,
        0,
        APP_FONT_16,
        night?TEXT_DARK:TEXT_LIGHT);

    shape(
        chart_card_2,
        246,7,8,8,
        tone(RED,night),
        LV_RADIUS_CIRCLE);
    lbl(
        chart_card_2,
        tr("Hálózat","Grid"),
        259,
        2,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    shape(
        chart_card_2,
        326,7,8,8,
        tone(BLUE,night),
        LV_RADIUS_CIRCLE);
    lbl(
        chart_card_2,
        tr("Akku","Battery"),
        339,
        2,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    lbl(
        chart_card_2,
        "kW",
        0,
        23,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    s_chart2_y_top=
        lbl(chart_card_2,"",0,41,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    s_chart2_y_mid=
        lbl(chart_card_2,"0",0,130,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    s_chart2_y_bottom=
        lbl(chart_card_2,"",0,217,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);

    s_chart2=app_chart_create(chart_card_2,true,night,
        tone(RED,night),tone(BLUE,night));
    if(s_chart2){
        lv_obj_set_pos(s_chart2,38,48);
        lv_obj_set_size(s_chart2,350,178);
    }
    /* A felirat középpontja a negyednapos rácsvonalra esik. */
    for(int tick=0;tick<5;tick++){
        char text[8];
        snprintf(text,sizeof(text),"%02d:00",tick*6);
        lv_obj_t *label=lbl(chart_card_2,text,38+(int)lround(349.*tick/4.)-23,234,
            APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
        lv_obj_set_width(label,46);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    }

    /*
     * Alsó összefoglaló sáv: az eredeti terv szerinti horizontális összegzés.
     * Az első négy szegmens napi mutató, az utolsó a rendszerállapot.
     */
    lv_obj_t *summary=lv_obj_create(s_dash);
    lv_obj_set_pos(summary,20,664);
    lv_obj_set_size(summary,1240,116);
    card_style(summary,night);
    lv_obj_set_style_pad_all(summary,8,0);

    summary_metric(
        summary,0,236,
        tr("Megtakarított pénz","Money saved"),
        &app_img_coins,
        4,night,ORANGE);

    summary_metric(
        summary,236,236,
        tr("Megtakarított CO2","CO2 saved"),
        &app_img_co2,
        5,night,GRAY);

    summary_metric(
        summary,472,236,
        tr("Fa-egyenérték","Tree equivalent"),
        &app_img_tree,
        6,night,GREEN);

    summary_metric(
        summary,708,236,
        tr("Önellátás","Self-sufficiency"),
        &app_img_bulb,
        7,night,ORANGE);

    /* Finom elválasztók az alsó sáv szegmensei között. */
    for(int i=1;i<=4;i++){
        int x=i<4?i*236:944;
        shape(
            summary,
            x,
            10,
            1,
            76,
            night?0x2D3531:0xE0E5E7,
            0);
    }

    s_status_card=lv_obj_create(summary);
    lv_obj_set_pos(s_status_card,944,0);
    lv_obj_set_size(s_status_card,280,92);
    lv_obj_set_style_bg_opa(s_status_card,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_color(s_status_card,c(tone(GREEN,night)),0);
    lv_obj_set_style_border_width(s_status_card,1,0);
    lv_obj_set_style_radius(s_status_card,14,0);
    lv_obj_set_style_pad_all(s_status_card,8,0);
    lv_obj_remove_flag(s_status_card,LV_OBJ_FLAG_SCROLLABLE);

    lbl(
        s_status_card,
        tr("Állapot","Status"),
        58,
        5,
        APP_FONT_12,
        night?MUTED_DARK:MUTED_LIGHT);

    s_status_bubble=lv_obj_create(s_status_card);
    lv_obj_set_pos(s_status_bubble,2,20);
    lv_obj_set_size(s_status_bubble,50,50);
    lv_obj_set_style_radius(s_status_bubble,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_border_width(s_status_bubble,0,0);
    lv_obj_set_style_bg_opa(s_status_bubble,LV_OPA_TRANSP,0);
    lv_obj_set_style_pad_all(s_status_bubble,0,0);
    lv_obj_remove_flag(s_status_bubble,LV_OBJ_FLAG_SCROLLABLE);

    s_status_icon=asset_image_box(
        s_status_bubble,
        0,
        0,
        50,
        50,
        &app_img_status_normal,
        night);

    s_status=
        lbl(s_status_card,"-",60,27,APP_FONT_16,GREEN);
    lv_label_set_long_mode(s_status,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_status,205);

    s_status_detail=
        lbl(
            s_status_card,
            "",
            60,
            51,
            APP_FONT_12,
            night?MUTED_DARK:MUTED_LIGHT);
    lv_label_set_long_mode(s_status_detail,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_status_detail,205);

    /* Futás közbeni SD-hiba kis, nem blokkoló piros figyelmeztetése. */
    s_sd_warn=lbl(
        s_dash,
        tr("SD-kártya hiba","SD card error"),
        430,
        37,
        APP_FONT_12,
        RED);
    lv_obj_set_width(s_sd_warn,230);
    lv_obj_set_style_text_align(s_sd_warn,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_add_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);

    s_dash_created=true;
    s_view=VIEW_DASHBOARD;
    s_last_chart_update_us=0;
}
/* Napi önellátás kiszámítása 0..100% közé korlátozva. */
static double self(const app_energy_totals_t *t){if(t->consumption_kwh<=1e-9)return 0;double x=(t->consumption_kwh-t->grid_import_kwh)/t->consumption_kwh*100;if(x<0)x=0;if(x>100)x=100;return x;}
/* PV helyi felhasználásból és visszatáplálásból származó napi megtakarítás számítása. */
static double money(const app_energy_totals_t *t){double local=t->production_kwh-t->grid_export_kwh;if(local<0)local=0;return local*s_cfg.energy.electricity_purchase_price+t->grid_export_kwh*s_cfg.energy.feed_in_price;}
/* Napi PV-termeléshez tartozó megtakarított CO2 tömeg számítása. */
static double co2(const app_energy_totals_t *t){return t->production_kwh*s_cfg.energy.co2_factor_kg_per_kwh;}
/* Megtakarított CO2 fa-egyenértékre történő átszámítása. */
static double tree(const app_energy_totals_t *t){return co2(t)/s_cfg.energy.tree_absorption_kg_per_year;}
/* Egy napi kártya értékének és opcionális tegnapi összehasonlításának frissítése. */
static void metric(
    int index,
    const char *value,
    double today,
    double yesterday,
    bool have_yesterday)
{
    fit_text(s_metric[index],value,index<4?190:162,index<4?APP_FONT_28:APP_FONT_20);

    if(!have_yesterday){
        lv_label_set_text(s_cmp[index],"");
        if(s_cmp_note[index]){
            lv_obj_add_flag(
                s_cmp_note[index],
                LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if(s_cmp_note[index]){
        lv_obj_clear_flag(
            s_cmp_note[index],
            LV_OBJ_FLAG_HIDDEN);
    }

    char comparison[32];
    fmt_cmp(
        comparison,
        sizeof(comparison),
        today,
        yesterday);

    fit_text(s_cmp[index],comparison,index<4?190:162,APP_FONT_14);

    lv_obj_set_style_text_color(
        s_cmp[index],
        c(tone(cmp_color(today,yesterday),s_last_night)),
        0);
}
/* A 288 ötperces grafikonpont és az automatikus Y-skálák frissítése. */
static void charts(const app_stats_snapshot_t *st)
{
    double max_positive=1.0;
    double max_signed=1.0;

    /* A két grafikon külön, automatikus Y-skálát kap. */
    for(int i=0;i<APP_STATS_SLOT_COUNT;i++){
        if(!st->slots[i].valid){
            continue;
        }

        max_positive=fmax(
            max_positive,
            fmax(
                st->slots[i].pv_kw,
                st->slots[i].load_kw));

        max_signed=fmax(
            max_signed,
            fmax(
                fabs(st->slots[i].grid_kw),
                fabs(st->slots[i].battery_kw)));
    }

    max_positive=ceil(max_positive);
    max_signed=ceil(max_signed);

    char text[20];
    fmt_dec(text,sizeof(text),max_positive,0);
    lv_label_set_text(s_chart1_y_top,text);
    fmt_dec(text,sizeof(text),max_positive/2.,1);
    lv_label_set_text(s_chart1_y_mid,text);
    fmt_dec(text,sizeof(text),max_signed,0);
    lv_label_set_text(s_chart2_y_top,text);
    fmt_dec(text,sizeof(text),-max_signed,0);
    lv_label_set_text(s_chart2_y_bottom,text);
    time_t now=time(NULL);
    struct tm local_time;
    localtime_r(&now,&local_time);
    int minutes=local_time.tm_hour*60+local_time.tm_min;
    app_chart_update(s_chart1,st,max_positive,minutes);
    app_chart_update(s_chart2,st,max_signed,minutes);
}
/* MQTT meteorológiai mezőkből az ikonlogika szerinti időjárási állapot kiválasztása. */
static weather_kind_t weather_kind(const app_energy_sample_t *s){
    /* A csapadék minden más időjárási állapotnál magasabb prioritású. */
    if(s->has_precipitation)return s->temperature_c<-4?WEATHER_SNOW:WEATHER_RAIN;
    if(s->night_mode)return WEATHER_NIGHT;
    if(s->has_sun_radiation)return WEATHER_SUNNY;
    if(s->power_pv_kw>1)return WEATHER_PARTLY_CLOUDY;
    return WEATHER_CLOUDY;
}
/* A vonalak, körvonalak és pontok egyetlen szemantikus színforrást használnak. */
static void update_flows(const app_energy_sample_t *s,float dt)
{
    /*
     * Napelem: termeléskor mindig a központi hub felé mozog,
     * és zöld energiaáramlást használ.
     */
    flow_update(
        &f_pv,
        s->power_pv_kw,
        true,
        GREEN,
        s->night_mode,
        dt);

    /*
     * Akkumulátor: pozitív PowerBat = kisütés, ezért akku -> hub és narancs.
     * Negatív PowerBat = töltés, ezért hub -> akku és zöld.
     */
    flow_update(
        &f_bat,
        s->power_bat_kw,
        s->power_bat_kw>0,
        s->power_bat_kw<0?GREEN:ORANGE,
        s->night_mode,
        dt);

    /*
     * A ház fogyasztása külön, narancs ágon mindig a hub -> ház irányba halad.
     */
    flow_update(
        &f_home,
        s->power_full_kw,
        true,
        ORANGE,
        s->night_mode,
        dt);

    lv_obj_set_style_border_color(s_pv_node,
        c(flow_color(s->power_pv_kw,GREEN,s->night_mode)),0);
    lv_obj_set_style_border_color(s_battery_node,
        c(flow_color(s->power_bat_kw,s->power_bat_kw<0?GREEN:ORANGE,s->night_mode)),0);
    lv_obj_set_style_border_color(s_home_node,
        c(flow_color(s->power_full_kw,ORANGE,s->night_mode)),0);

    /*
     * Szigetüzemben a hálózat fizikailag leválasztottként jelenik meg:
     * nincs áramlási pont, a kapcsolat és a hálózati kör szürke,
     * az ikon pedig megszakított állapotot jelez.
     */
    if(s->status==3){
        for(int i=0;i<3;i++){
            lv_obj_add_flag(f_grid.dots[i],LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_set_style_line_color(
            f_grid.line,
            c(flow_color(0,GREEN,s->night_mode)),
            0);

        lv_image_set_src(
            s_grid_icon,
            &app_img_grid_off);

        lv_obj_set_style_opa(
            s_grid_icon,
            s->night_mode?LV_OPA_70:LV_OPA_COVER,
            0);

        lv_obj_set_style_border_color(
            s_grid_node,
            c(flow_color(0,GREEN,s->night_mode)),
            0);
    }else{
        /*
         * PowerGrid pozitív = import: hálózat -> hub, narancs.
         * PowerGrid negatív = export: hub -> hálózat, zöld.
         */
        uint32_t grid_color=
            s->power_grid_kw<0?GREEN:ORANGE;

        flow_update(
            &f_grid,
            s->power_grid_kw,
            s->power_grid_kw>0,
            grid_color,
            s->night_mode,
            dt);

        lv_image_set_src(
            s_grid_icon,
            &app_img_grid);

        lv_obj_set_style_opa(
            s_grid_icon,
            s->night_mode?LV_OPA_80:LV_OPA_COVER,
            0);

        lv_obj_set_style_border_color(
            s_grid_node,
            c(flow_color(s->power_grid_kw,grid_color,s->night_mode)),
            0);
    }
}

/* A render loophoz közeli, 16 ms-os animáció. Nem végez SD- vagy statisztikai műveletet. */
static void animation_tick(lv_timer_t *timer)
{
    (void)timer;
    /* Az indulás hosszú NTP/MQTT várakozásai alatt is azonnal látszik a kapcsolatvesztés. */
    if(!s_started && s_view!=VIEW_FATAL && s_boot_stage>=APP_BOOT_WIFI){
        app_runtime_state_t state;
        app_state_get_runtime(&state);
        if(!state.wifi_connected)
            error_screen(VIEW_WIFI_ERROR,tr("Nincs Wi-Fi kapcsolat","No Wi-Fi connection"),
                tr("Kapcsolat helyreállítására várunk...","Waiting for connection to recover..."),false);
        else if(s_boot_stage>=APP_BOOT_MQTT && s_cfg.data_recept_mode==APP_DATA_MQTT && !state.mqtt_connected)
            error_screen(VIEW_MQTT_ERROR,tr("Nincs MQTT kapcsolat","No MQTT connection"),
                tr("Kapcsolat helyreállítására várunk...","Waiting for connection to recover..."),false);
    }
    int64_t now=esp_timer_get_time();
    float dt=s_animation_us?(now-s_animation_us)/1000000.f:0.f;
    s_animation_us=now;
    if(dt>.25f)dt=.016f;
    if(s_view==VIEW_DASHBOARD && s_dash_created)update_flows(&s_animation_sample,dt);
    if(s_error_pulse){
        float wave=(sinf(now/1000000.f*3.14159265f)+1.f)*.5f;
        lv_obj_set_style_opa(s_error_pulse,(lv_opa_t)(175+65*wave),0);
    }
}

/* Egy érvényes minta és statisztikai snapshot alapján minden dashboard-elem frissítése. */
static void update(const app_energy_sample_t *s,const app_stats_snapshot_t *st){
    time_t now=time(NULL);struct tm tmv;localtime_r(&now,&tmv);char b[96],q[32];
    strftime(q,sizeof(q),"%H:%M",&tmv);lv_label_set_text(s_clock,q);
    if(hu())snprintf(q,sizeof(q),"%04d.%02d.%02d.",tmv.tm_year+1900,tmv.tm_mon+1,tmv.tm_mday);
    else snprintf(q,sizeof(q),"%04d-%02d-%02d",tmv.tm_year+1900,tmv.tm_mon+1,tmv.tm_mday);
    lv_label_set_text(s_date,q);

    weather_render(weather_kind(s),s->night_mode);
    char temp[16];
    fmt_dec(temp,sizeof(temp),s->temperature_c,1);
    snprintf(b,sizeof(b),"%s °C",temp);
    fit_text(s_weather_temp,b,78,APP_FONT_16);
    snprintf(b,sizeof(b),"%d km/h",(int)lround(s->wind_speed_kmh));
    fit_text(s_wind_speed,b,98,APP_FONT_16);
    fmt_power(q,sizeof(q),s->power_pv_kw);lv_label_set_text(s_pv,q);
    fmt_power(q,sizeof(q),s->power_full_kw);lv_label_set_text(s_home,q);
    fmt_power(q,sizeof(q),s->power_bat_kw);lv_label_set_text(s_bat,q);
    fmt_power(q,sizeof(q),s->power_grid_kw);lv_label_set_text(s_grid,q);
    fmt_pct(q,sizeof(q),s->soc_bat_pct);lv_label_set_text(s_soc,q);
    snprintf(q,sizeof(q),"%d °C",(int)lround(s->battery_temperature_c));lv_label_set_text(s_bt,q);

    uint32_t bt_color=s->battery_temperature_c>50?RED:(s->battery_temperature_c>40?ORANGE:(s->night_mode?MUTED_DARK:MUTED_LIGHT));
    lv_obj_set_style_text_color(s_bt,c(tone(bt_color,s->night_mode)),0);

    lv_bar_set_value(s_battery_bar,(int32_t)lround(s->soc_bat_pct),LV_ANIM_OFF);
    uint32_t soc_color=battery_color(s->soc_bat_pct);
    lv_obj_set_style_bg_color(
        s_battery_bar,
        c(tone(soc_color,s->night_mode)),
        LV_PART_INDICATOR);

    lv_obj_set_style_opa(
        s_bat_icon,
        s->night_mode?LV_OPA_80:LV_OPA_COVER,
        0);

    fmt_pct(q,sizeof(q),s->soc_bat_pct);
    snprintf(b,sizeof(b),"SOC: %s",q);
    lv_label_set_text(s_bat_soc_detail,b);

    fmt_power(q,sizeof(q),s->power_bat_kw);
    snprintf(b,sizeof(b),"%s: %s",tr("Telj.","Power"),q);
    lv_label_set_text(s_bat_power_detail,b);

    snprintf(b,sizeof(b),"%s: %d °C",tr("Hőm.","Temp."),(int)lround(s->battery_temperature_c));
    lv_label_set_text(s_bat_temp_detail,b);
    lv_obj_set_style_text_color(s_bat_temp_detail,c(tone(bt_color,s->night_mode)),0);
    s_animation_sample=*s;
    update_flows(s,0.f);
    fmt_kwh(b,sizeof(b),st->totals.production_kwh);metric(0,b,st->completed_interval_totals.production_kwh,st->yesterday_completed_interval_totals.production_kwh,st->has_yesterday);fmt_kwh(b,sizeof(b),st->totals.consumption_kwh);metric(1,b,st->completed_interval_totals.consumption_kwh,st->yesterday_completed_interval_totals.consumption_kwh,st->has_yesterday);fmt_kwh(b,sizeof(b),st->totals.grid_import_kwh);metric(2,b,st->completed_interval_totals.grid_import_kwh,st->yesterday_completed_interval_totals.grid_import_kwh,st->has_yesterday);fmt_kwh(b,sizeof(b),st->totals.grid_export_kwh);metric(3,b,st->completed_interval_totals.grid_export_kwh,st->yesterday_completed_interval_totals.grid_export_kwh,st->has_yesterday);
    snprintf(b,sizeof(b),"%.0f %s",money(&st->totals),s_cfg.project.currency);metric(4,b,money(&st->completed_interval_totals),money(&st->yesterday_completed_interval_totals),st->has_yesterday);char n[20];fmt_dec(n,sizeof(n),co2(&st->totals),1);snprintf(b,sizeof(b),"%s kg",n);metric(5,b,co2(&st->completed_interval_totals),co2(&st->yesterday_completed_interval_totals),st->has_yesterday);fmt_dec(b,sizeof(b),tree(&st->totals),2);{
        char tree_value[32];
        snprintf(
            tree_value,
            sizeof(tree_value),
            "%.16s %.7s",
            b,
            tr("fa","trees"));
        metric(
            6,
            tree_value,
            tree(&st->completed_interval_totals),
            tree(&st->yesterday_completed_interval_totals),
            st->has_yesterday);
    }fmt_pct(b,sizeof(b),self(&st->totals));metric(7,b,self(&st->completed_interval_totals),self(&st->yesterday_completed_interval_totals),st->has_yesterday);
    fmt_kwh(q,sizeof(q),st->totals.battery_charge_kwh);
    snprintf(b,sizeof(b),"%s: %s",tr("Töltés","Charge"),q);
    fit_text(s_bat_charge,b,215,APP_FONT_12);
    fmt_kwh(q,sizeof(q),st->totals.battery_discharge_kwh);
    snprintf(b,sizeof(b),"%s: %s",tr("Kisütés","Discharge"),q);
    fit_text(s_bat_discharge,b,205,APP_FONT_12);

    /* A tegnapi összehasonlítást csak akkor mutatjuk, ha tényleges tegnapi adat létezik. */
    if(st->has_yesterday){
        fmt_cmp(q,sizeof(q),st->completed_interval_totals.battery_charge_kwh,st->yesterday_completed_interval_totals.battery_charge_kwh);
        fit_text(s_bat_charge_cmp,q,215,APP_FONT_12);
        lv_obj_set_style_text_color(s_bat_charge_cmp,c(tone(cmp_color(st->completed_interval_totals.battery_charge_kwh,st->yesterday_completed_interval_totals.battery_charge_kwh),s->night_mode)),0);
        fmt_cmp(q,sizeof(q),st->completed_interval_totals.battery_discharge_kwh,st->yesterday_completed_interval_totals.battery_discharge_kwh);
        fit_text(s_bat_discharge_cmp,q,205,APP_FONT_12);
        lv_obj_set_style_text_color(s_bat_discharge_cmp,c(tone(cmp_color(st->completed_interval_totals.battery_discharge_kwh,st->yesterday_completed_interval_totals.battery_discharge_kwh),s->night_mode)),0);
    }else{
        lv_label_set_text(s_bat_charge_cmp,"");
        lv_label_set_text(s_bat_discharge_cmp,"");
    }

    const char *stxt;
    if(s->status==1){
        if(hu()){
            stxt="Normál működés";
        }else{
            stxt="Normal operation";
        }
    }else if(s->status==2){
        stxt=tr("Felkészülés áramszünetre","Preparing for outage");
    }else if(s->status==3){
        stxt=tr("Szigetüzem","Island mode");
    }else{
        stxt=tr("Hiba","Error");
    }

    uint32_t status_color=
        s->status==1?GREEN:
        s->status==2?ORANGE:
        s->status==3?BLUE:
        RED;

    const char *status_detail=
        s->status==1
            ? tr("Minden rendben","Everything is OK")
            : s->status==2
                ? tr("Feltöltés és szinten tartás","Charge and hold at 100%")
                : s->status==3
                    ? tr("Hálózatról leválasztva","Disconnected from grid")
                    : tr("Rendszerhiba","System error");

    fit_text(s_status,stxt,205,APP_FONT_16);
    fit_text(s_status_detail,status_detail,205,APP_FONT_12);

    /* Az állapot PNG saját, a mockuphoz illeszkedő színezést tartalmaz. */

    lv_obj_set_style_text_color(
        s_status,
        c(tone(status_color,s->night_mode)),
        0);

    const lv_image_dsc_t *status_asset=
        s->status==1?&app_img_status_normal:
        s->status==2?&app_img_status_prepare:
        s->status==3?&app_img_status_island:
        &app_img_status_error;

    lv_image_set_src(
        s_status_icon,
        status_asset);

    lv_obj_set_style_opa(
        s_status_icon,
        s->night_mode?LV_OPA_80:LV_OPA_COVER,
        0);

    lv_obj_set_style_border_color(
        s_status_card,
        c(tone(status_color,s->night_mode)),
        0);
    lv_obj_set_style_border_width(s_status_card,s->status==4?3:1,0);

    if(app_storage_is_ok())lv_obj_add_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);else lv_obj_clear_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);

    int64_t chart_now=esp_timer_get_time();
    if(s_last_chart_update_us==0||chart_now-s_last_chart_update_us>=5000000LL){
        charts(st);
        s_last_chart_update_us=chart_now;
    }
}
/* Kommunikációs vagy végzetes teljes képernyős hibaoldal PNG illusztrációval. */
static void error_screen(
    runtime_view_t view,
    const char *title,
    const char *detail,
    bool fatal)
{
    if(s_view!=view){
        clear();
        screen_style(s_screen,true);

        /*
         * A központi grafika a dashboard assetstílusát követi.
         * A külső kör pulzál, maga az ikon azonban éles, részletes PNG marad.
         */
        s_error_pulse=lv_obj_create(s_screen);
        lv_obj_set_size(s_error_pulse,132,132);
        lv_obj_set_pos(s_error_pulse,574,154);
        lv_obj_set_style_radius(s_error_pulse,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_border_width(s_error_pulse,1,0);
        lv_obj_set_style_border_color(
            s_error_pulse,
            c(fatal?RED:ORANGE),
            0);
        lv_obj_set_style_bg_color(
            s_error_pulse,
            c(fatal?0x321716:0x332619),
            0);
        lv_obj_set_style_bg_opa(
            s_error_pulse,
            LV_OPA_60,
            0);
        lv_obj_set_style_pad_all(s_error_pulse,0,0);
        lv_obj_remove_flag(s_error_pulse,LV_OBJ_FLAG_SCROLLABLE);

        const lv_image_dsc_t *error_asset=
            fatal?&app_img_status_error:
            view==VIEW_WIFI_ERROR?&app_img_wifi_off:
            view==VIEW_MQTT_ERROR?&app_img_mqtt_off:
            view==VIEW_WAIT_DATA?&app_img_waiting_data:
            &app_img_timeout;

        asset_image_box(
            s_error_pulse,
            14,
            14,
            104,
            92,
            error_asset,
            false);

        lv_obj_t *title_label=lbl(
            s_screen,
            title,
            0,
            326,
            APP_FONT_28,
            fatal?RED:TEXT_DARK);
        lv_obj_set_width(title_label,W);
        lv_obj_set_style_text_align(
            title_label,
            LV_TEXT_ALIGN_CENTER,
            0);

        lv_obj_t *detail_label=lbl(
            s_screen,
            detail?detail:"",
            0,
            386,
            APP_FONT_18,
            MUTED_DARK);
        lv_obj_set_width(detail_label,W);
        lv_obj_set_style_text_align(
            detail_label,
            LV_TEXT_ALIGN_CENTER,
            0);

        s_view=view;
    }

}

/* Waveshare BSP kijelző és LVGL inicializálása fix 1280x800 fekvő tájolásban. */
esp_err_t app_ui_init(void){
    bsp_display_cfg_t cfg={.lv_adapter_cfg=ESP_LV_ADAPTER_DEFAULT_CONFIG(),.rotation=ESP_LV_ADAPTER_ROTATE_90,.tear_avoid_mode=ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,.touch_flags={.swap_xy=0,.mirror_x=0,.mirror_y=0}};
    lv_display_t *disp=bsp_display_start_with_config(&cfg);if(!disp)return ESP_FAIL;bsp_display_backlight_on();bsp_display_brightness_set(50);
    if (bsp_display_lock(-1) != ESP_OK) {
        return ESP_FAIL;
    }
    s_screen = lv_screen_active();
    screen_style(s_screen, false);
    if(!lv_timer_create(animation_tick,16,NULL)){bsp_display_unlock();return ESP_ERR_NO_MEM;}
    bsp_display_unlock();
    app_ui_show_boot(APP_BOOT_SD, NULL);
    return ESP_OK;
}
/* A bootkor beolvasott konfiguráció átadása a UI modulnak. */
void app_ui_set_config(const app_config_t *cfg)
{
    if(!cfg){
        return;
    }

    s_cfg=*cfg;
    s_cfg_ok=true;
    s_target=s_cfg.display.day_brightness;
    s_brightness=s_target;

    /*
     * Az első MQTT csomag előtt még nincs NightMode, ezért a konfigurált
     * nappali fényerő az indulási képernyőkre is azonnal érvényes.
     */
    bsp_display_brightness_set(s_brightness);
}
/* Indulási folyamat aktuális lépésének megjelenítése vektoros állapotikonokkal. */
void app_ui_show_boot(
    app_boot_stage_t stage,
    const char *detail)
{
    if(bsp_display_lock(-1)!=ESP_OK){
        return;
    }

    s_boot_stage=stage;
    clear();
    screen_style(s_screen,false);

    const char *hu_steps[]={
        "SD-kártya",
        "Konfiguráció",
        "Wi-Fi kapcsolat",
        "Időszinkron",
        s_cfg.data_recept_mode==APP_DATA_HTTP?"HTTP adatfogadó":"MQTT kapcsolat",
        "Várakozás az első adatcsomagra"
    };

    const char *en_steps[]={
        "SD card",
        "Configuration",
        "Wi-Fi connection",
        "Time synchronization",
        s_cfg.data_recept_mode==APP_DATA_HTTP?"HTTP receiver":"MQTT connection",
        "Waiting for first data packet"
    };

    lbl(
        s_screen,
        s_cfg_ok?s_cfg.project.title:"Zolee Energy Flow Display",
        34,
        30,
        APP_FONT_28,
        TEXT_LIGHT);

    lbl(
        s_screen,
        tr("Rendszerindítás","Starting system"),
        34,
        120,
        APP_FONT_28,
        TEXT_LIGHT);

    lbl(
        s_screen,
        hu()?hu_steps[stage]:en_steps[stage],
        34,
        180,
        APP_FONT_22,
        GREEN);

    if(detail){
        lv_obj_t *detail_label=lbl(
            s_screen,
            detail,
            34,
            225,
            APP_FONT_14,
            MUTED_LIGHT);
        lv_obj_set_width(detail_label,1180);
    }

    /*
     * A bootlista felismerhető állapotgrafikákat használ:
     * kész = zöld ellenőrzött otthon, aktív = narancs adatmozgás,
     * jövőbeli = visszafogott szürke kör.
     */
    for(int i=0;i<6;i++){
        int y=300+i*57;

        if(i<(int)stage){
            asset_image_box(
                s_screen,
                52,
                y,
                34,
                30,
                &app_img_status_normal,
                false);
        }else if(i==(int)stage){
            asset_image_box(
                s_screen,
                52,
                y,
                34,
                30,
                &app_img_waiting_data,
                false);
        }else{
            lv_obj_t *dot=shape(
                s_screen,
                63,
                y+10,
                10,
                10,
                0xC8D0D4,
                LV_RADIUS_CIRCLE);
            lv_obj_set_style_bg_opa(dot,LV_OPA_COVER,0);
        }

        lbl(
            s_screen,
            hu()?hu_steps[i]:en_steps[i],
            98,
            y+5,
            APP_FONT_18,
            i<(int)stage?GREEN:i==(int)stage?ORANGE:GRAY);
    }

    bsp_display_unlock();
}

/* Végzetes indulási hiba biztonságos megjelenítése LVGL lock mellett. */
void app_ui_show_fatal(const char *title,const char *detail){if(bsp_display_lock(-1)!=ESP_OK)return;error_screen(VIEW_FATAL,title,detail,true);bsp_display_unlock();}
/* Folyamatos UI runtime task: kapcsolatállapot, timeout, fényerő és dashboard frissítése. */
/* Az állapotválasztás közös a készülék és a tényleges LVGL render-teszt számára. */
static void render_runtime(app_runtime_state_t rt, app_energy_sample_t s, bool have, int64_t now)
{
    bool timed_out=app_state_data_timed_out(&rt,now,app_config_data_timeout(&s_cfg));
    bool http=s_cfg.data_recept_mode==APP_DATA_HTTP;

    if(!rt.wifi_connected){
        error_screen(
            VIEW_WIFI_ERROR,
            tr("Nincs Wi-Fi kapcsolat","No Wi-Fi connection"),
            tr("Kapcsolat helyreállítására várunk...","Waiting for connection to recover..."),
            false);
    }else if(!http&&!rt.mqtt_connected){
        error_screen(
            VIEW_MQTT_ERROR,
            tr("Nincs MQTT kapcsolat","No MQTT connection"),
            tr("Kapcsolat helyreállítására várunk...","Waiting for connection to recover..."),
            false);
    }else if(!have&&!timed_out){
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
            build_dashboard(s.night_mode);
            s_last_night=s.night_mode;
        }
        update(&s,&s_stats_snapshot);
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
        if(have)app_stats_get_snapshot(&s_stats_snapshot);

        if(have){
            s_target=s.night_mode?s_cfg.display.night_brightness:s_cfg.display.day_brightness;
            if(s_brightness!=s_target){
                int d=s_target-s_brightness;
                s_brightness+=d>0?1:-1;
                bsp_display_brightness_set(s_brightness);
            }
        }

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


