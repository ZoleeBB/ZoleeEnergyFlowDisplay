#include "app_ui.h"
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

LV_FONT_DECLARE(font_noto_sans_basic_14_1);
LV_FONT_DECLARE(font_noto_sans_basic_16_4);
LV_FONT_DECLARE(font_noto_sans_basic_20_4);
LV_FONT_DECLARE(font_noto_sans_basic_30_4);

static lv_font_t s_font_14;
static lv_font_t s_font_16;
static lv_font_t s_font_20;
static lv_font_t s_font_30;

#define APP_FONT_12 (&s_font_14)
#define APP_FONT_14 (&s_font_14)
#define APP_FONT_16 (&s_font_16)
#define APP_FONT_18 (&s_font_16)
#define APP_FONT_20 (&s_font_20)
#define APP_FONT_22 (&s_font_20)
#define APP_FONT_28 (&s_font_30)

#define W 1280
#define H 800
#define BG_LIGHT 0xF5F7F8
#define BG_DARK 0x000000
#define CARD_LIGHT 0xFFFFFF
#define CARD_DARK 0x111513
#define TEXT_LIGHT 0x1B2428
#define TEXT_DARK 0xE7ECE9
#define MUTED_LIGHT 0x6E7C83
#define MUTED_DARK 0x839087
#define GREEN 0x2E9D62
#define ORANGE 0xE99B38
#define RED 0xD9534F
#define BLUE 0x367DC4
#define GRAY 0x9AA5A9

static app_config_t s_cfg;
static bool s_cfg_ok,s_started,s_dash_created,s_last_night;
static lv_obj_t *s_screen,*s_dash,*s_sd_warn;
static lv_obj_t *s_title,*s_date,*s_clock,*s_weather_temp,*s_wind_speed,*s_weather_icon;
static lv_obj_t *s_pv,*s_home,*s_bat,*s_grid,*s_soc,*s_bt,*s_status;
static lv_obj_t *s_bat_icon,*s_status_icon,*s_status_accent_a,*s_status_accent_b;
static lv_obj_t *s_battery_bar,*s_status_card;
static lv_obj_t *s_bat_soc_detail,*s_bat_power_detail,*s_bat_temp_detail;
static lv_obj_t *s_bat_charge,*s_bat_charge_cmp,*s_bat_discharge,*s_bat_discharge_cmp;
static lv_obj_t *s_metric[8],*s_cmp[8];
static lv_obj_t *s_chart1,*s_chart2;
static lv_chart_series_t *s_pv_ser,*s_load_ser,*s_grid_ser,*s_bat_ser;
static lv_obj_t *s_error_pulse;
static int s_brightness=50,s_target=50;
static int64_t s_last_chart_update_us;

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

typedef enum {
    WEATHER_SUNNY = 0,
    WEATHER_PARTLY_CLOUDY,
    WEATHER_CLOUDY,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_NIGHT
} weather_kind_t;

static weather_kind_t s_last_weather = (weather_kind_t)-1;

typedef struct {
    lv_obj_t *line;
    lv_obj_t *dots[3];
    lv_point_precise_t points[2];
    int x1,y1,x2,y2;
    float phase;
} flow_t;
static flow_t f_pv,f_bat,f_grid;

static lv_color_t c(uint32_t x){return lv_color_hex(x);}
static bool hu(void){return !s_cfg_ok||strcmp(s_cfg.project.language,"hu")==0;}
static const char *tr(const char *a,const char *b){return hu()?a:b;}

static void fonts_init(void)
{
    s_font_14=font_noto_sans_basic_14_1;
    s_font_16=font_noto_sans_basic_16_4;
    s_font_20=font_noto_sans_basic_20_4;
    s_font_30=font_noto_sans_basic_30_4;

    /*
     * Keep LVGL's built-in Montserrat fonts as fallbacks because they include
     * the FontAwesome LV_SYMBOL glyphs used by the dashboard icons.
     */
    s_font_14.fallback=&lv_font_montserrat_14;
    s_font_16.fallback=&lv_font_montserrat_16;
    s_font_20.fallback=&lv_font_montserrat_20;
    s_font_30.fallback=&lv_font_montserrat_28;
}

static uint32_t tone(uint32_t color,bool night){
    if(!night)return color;
    if(color==GREEN)return 0x23784C;
    if(color==ORANGE)return 0xA46E2D;
    if(color==RED)return 0x8E3A38;
    if(color==BLUE)return 0x2B5D8B;
    return color;
}

static void screen_style(lv_obj_t *o,bool night){lv_obj_set_style_bg_color(o,c(night?BG_DARK:BG_LIGHT),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);}
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
static lv_obj_t *lbl(lv_obj_t *p,const char *txt,int x,int y,const lv_font_t *font,uint32_t color){lv_obj_t *l=lv_label_create(p);lv_label_set_text(l,txt?txt:"");lv_obj_set_pos(l,x,y);if(font)lv_obj_set_style_text_font(l,font,0);lv_obj_set_style_text_color(l,c(color),0);return l;}
static void clear(void){
    lv_obj_clean(s_screen);
    s_dash=NULL;
    s_dash_created=false;
    s_error_pulse=NULL;
    s_view=VIEW_NONE;
}

static uint32_t battery_color(double soc){
    if(soc>50.0)return GREEN;
    if(soc>=20.0)return ORANGE;
    return RED;
}

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

static void weather_draw_sun(lv_obj_t *parent,int x,int y){
    shape(parent,x+11,y+8,16,16,0xF4B942,LV_RADIUS_CIRCLE);
    shape(parent,x+18,y+1,2,5,0xF4B942,1);
    shape(parent,x+18,y+26,2,5,0xF4B942,1);
    shape(parent,x+4,y+15,5,2,0xF4B942,1);
    shape(parent,x+29,y+15,5,2,0xF4B942,1);
}

static void weather_draw_cloud(lv_obj_t *parent,int x,int y,bool night){
    uint32_t cloud=night?0x8C9892:0x9DA9AE;
    shape(parent,x+5,y+17,31,12,cloud,6);
    shape(parent,x+9,y+11,17,17,cloud,LV_RADIUS_CIRCLE);
    shape(parent,x+20,y+8,19,19,cloud,LV_RADIUS_CIRCLE);
}

static void weather_render(weather_kind_t kind,bool night){
    if(!s_weather_icon||kind==s_last_weather)return;
    lv_obj_clean(s_weather_icon);

    switch(kind){
        case WEATHER_SUNNY:
            weather_draw_sun(s_weather_icon,0,0);
            break;
        case WEATHER_PARTLY_CLOUDY:
            weather_draw_sun(s_weather_icon,-2,-2);
            weather_draw_cloud(s_weather_icon,7,6,night);
            break;
        case WEATHER_CLOUDY:
            weather_draw_cloud(s_weather_icon,2,4,night);
            break;
        case WEATHER_RAIN:
            weather_draw_cloud(s_weather_icon,2,0,night);
            shape(s_weather_icon,12,30,2,8,BLUE,1);
            shape(s_weather_icon,22,30,2,8,BLUE,1);
            shape(s_weather_icon,32,30,2,8,BLUE,1);
            break;
        case WEATHER_SNOW:
            weather_draw_cloud(s_weather_icon,2,0,night);
            shape(s_weather_icon,11,31,4,4,0xE6F3FA,LV_RADIUS_CIRCLE);
            shape(s_weather_icon,21,34,4,4,0xE6F3FA,LV_RADIUS_CIRCLE);
            shape(s_weather_icon,31,31,4,4,0xE6F3FA,LV_RADIUS_CIRCLE);
            break;
        case WEATHER_NIGHT:
            shape(s_weather_icon,9,5,24,24,0xB8C7CE,LV_RADIUS_CIRCLE);
            shape(s_weather_icon,17,1,24,24,BG_DARK,LV_RADIUS_CIRCLE);
            break;
    }

    s_last_weather=kind;
}

static void wind_icon_create(lv_obj_t *parent,int x,int y,bool night){
    uint32_t col=night?MUTED_DARK:MUTED_LIGHT;
    lv_obj_t *w=lv_obj_create(parent);
    lv_obj_set_pos(w,x,y);
    lv_obj_set_size(w,30,24);
    lv_obj_set_style_bg_opa(w,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(w,0,0);
    lv_obj_set_style_pad_all(w,0,0);
    lv_obj_remove_flag(w,LV_OBJ_FLAG_SCROLLABLE);
    shape(w,1,5,18,2,col,1);
    shape(w,6,11,22,2,col,1);
    shape(w,1,17,15,2,col,1);
}

static void fmt_dec(char *out,size_t n,double v,int d){snprintf(out,n,"%.*f",d,v);if(hu()){char *p=strchr(out,'.');if(p)*p=',';}}
static void fmt_power(char *out,size_t n,double kw){double a=fabs(kw);if(a>=1){char q[20];fmt_dec(q,sizeof(q),a,1);snprintf(out,n,"%s kW",q);}else snprintf(out,n,"%d W",(int)llround(a*1000));}
static void fmt_kwh(char *out,size_t n,double v){char q[20];fmt_dec(q,sizeof(q),v,1);snprintf(out,n,"%s kWh",q);}
static void fmt_pct(char *out,size_t n,double v){char q[20];fmt_dec(q,sizeof(q),v,1);snprintf(out,n,"%s%%",q);}
static void fmt_cmp(char *out,size_t n,double t,double y){
    if(fabs(y)<1e-9){
        snprintf(out,n,"%s",LV_SYMBOL_MINUS);
        return;
    }
    double p=(t-y)/y*100;
    char q[20];
    fmt_dec(q,sizeof(q),p,1);
    snprintf(out,n,"%s %s%%",p>=0?LV_SYMBOL_UP:LV_SYMBOL_DOWN,q);
}
static uint32_t cmp_color(double t,double y){if(fabs(y)<1e-9)return GRAY;return t>=y?GREEN:ORANGE;}

#if LV_DRAW_SW_COMPLEX
static void chart_positive_gradient_cb(lv_event_t *e)
{
    lv_draw_task_t *task=lv_event_get_draw_task(e);
    if(!task||lv_draw_task_get_type(task)!=LV_DRAW_TASK_TYPE_LINE)return;

    lv_draw_dsc_base_t *base=(lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
    if(!base||base->part!=LV_PART_ITEMS)return;

    lv_draw_line_dsc_t *line=lv_draw_task_get_line_dsc(task);
    if(!line||line->point_cnt<2)return;

    lv_obj_t *chart=lv_event_get_target_obj(e);
    lv_area_t coords;
    lv_obj_get_coords(chart,&coords);

    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);
    tri.grad.dir=LV_GRAD_DIR_VER;

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_grad.dir=LV_GRAD_DIR_VER;

    for(int32_t i=0;i<line->point_cnt-1;i++){
        lv_point_precise_t p1=line->points[i];
        lv_point_precise_t p2=line->points[i+1];

        if(p1.x==LV_DRAW_LINE_POINT_NONE||p1.y==LV_DRAW_LINE_POINT_NONE||
           p2.x==LV_DRAW_LINE_POINT_NONE||p2.y==LV_DRAW_LINE_POINT_NONE){
            continue;
        }

        int32_t upper=LV_MIN(p1.y,p2.y);
        int32_t lower=LV_MAX(p1.y,p2.y);
        int32_t full_h=lv_obj_get_height(chart);
        if(full_h<=0)continue;

        int32_t upper_frac=LV_CLAMP(0,(upper-coords.y1)*255/full_h,255);
        int32_t lower_frac=LV_CLAMP(0,(lower-coords.y1)*255/full_h,255);
        lv_opa_t upper_opa=(lv_opa_t)(70-(upper_frac*25/255));
        lv_opa_t lower_opa=(lv_opa_t)(55-(lower_frac*25/255));

        tri.p[0].x=p1.x;tri.p[0].y=p1.y;
        tri.p[1].x=p2.x;tri.p[1].y=p2.y;
        tri.p[2].x=p1.y<p2.y?p1.x:p2.x;
        tri.p[2].y=lower;
        tri.grad.stops[0].color=line->color;
        tri.grad.stops[0].opa=upper_opa;
        tri.grad.stops[0].frac=0;
        tri.grad.stops[1].color=line->color;
        tri.grad.stops[1].opa=lower_opa;
        tri.grad.stops[1].frac=255;
        lv_draw_triangle(base->layer,&tri);

        lv_area_t area={
            .x1=(int32_t)p1.x,
            .x2=(int32_t)p2.x-1,
            .y1=lower,
            .y2=coords.y2
        };
        if(area.x2<area.x1)continue;

        rect.bg_grad.stops[0].color=line->color;
        rect.bg_grad.stops[0].opa=lower_opa;
        rect.bg_grad.stops[0].frac=0;
        rect.bg_grad.stops[1].color=line->color;
        rect.bg_grad.stops[1].opa=0;
        rect.bg_grad.stops[1].frac=255;
        lv_draw_rect(base->layer,&rect,&area);
    }
}
#endif

static lv_obj_t *node(lv_obj_t *p,int x,int y,int w,int h,const char *name,const char *icon,lv_obj_t **icon_out,lv_obj_t **value,bool night,uint32_t accent){
    lv_obj_t *o=lv_obj_create(p);
    lv_obj_set_pos(o,x,y);
    lv_obj_set_size(o,w,h);
    card_style(o,night);
    uint32_t shown_accent=tone(accent,night);
    lv_obj_set_style_border_color(o,c(shown_accent),0);
    lbl(o,name,8,5,APP_FONT_14,night?MUTED_DARK:MUTED_LIGHT);
    lv_obj_t *ic=lbl(o,icon,8,33,APP_FONT_20,shown_accent);
    if(icon_out)*icon_out=ic;
    *value=lbl(o,"-",38,32,APP_FONT_22,night?TEXT_DARK:TEXT_LIGHT);
    return o;
}
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
static void flow_update(flow_t *f,double kw,bool forward,uint32_t color,bool night,float dt){
    double m=fabs(kw);if(m<0.0005){lv_obj_set_style_line_color(f->line,c(night?0x333B37:0xDCE3E5),0);for(int i=0;i<3;i++)lv_obj_add_flag(f->dots[i],LV_OBJ_FLAG_HIDDEN);return;}
    uint32_t shown=tone(color,night);
    lv_obj_set_style_line_color(f->line,c(shown),0);float speed=.75f*(float)fmin(m/7.0,1.0);f->phase+=dt*speed;while(f->phase>=1)f->phase-=1;
    for(int i=0;i<3;i++){float t=f->phase+i/3.0f;while(t>=1)t-=1;if(!forward)t=1-t;int x=(int)lroundf(f->x1+(f->x2-f->x1)*t),y=(int)lroundf(f->y1+(f->y2-f->y1)*t);lv_obj_set_pos(f->dots[i],x-4,y-4);lv_obj_set_style_bg_color(f->dots[i],c(shown),0);lv_obj_set_style_bg_opa(f->dots[i],LV_OPA_COVER,0);lv_obj_clear_flag(f->dots[i],LV_OBJ_FLAG_HIDDEN);}
}
static lv_obj_t *metric_card(lv_obj_t *p,int x,int y,const char *title,int idx,bool night){
    lv_obj_t *o=lv_obj_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,145,104);card_style(o,night);lbl(o,title,0,0,APP_FONT_14,night?MUTED_DARK:MUTED_LIGHT);s_metric[idx]=lbl(o,"-",0,29,APP_FONT_20,night?TEXT_DARK:TEXT_LIGHT);s_cmp[idx]=lbl(o,"",0,65,APP_FONT_12,GREEN);return o;
}
static void build_dashboard(bool night){
    clear();screen_style(s_screen,night);s_dash=lv_obj_create(s_screen);lv_obj_set_size(s_dash,W,H);screen_style(s_dash,night);
    s_title=lbl(s_dash,s_cfg.project.title,28,16,APP_FONT_28,night?TEXT_DARK:TEXT_LIGHT);
    s_date=lbl(s_dash,"",760,17,APP_FONT_18,night?MUTED_DARK:MUTED_LIGHT);
    s_clock=lbl(s_dash,"",1110,10,APP_FONT_28,night?TEXT_DARK:TEXT_LIGHT);

    s_weather_icon=lv_obj_create(s_dash);
    lv_obj_set_pos(s_weather_icon,900,8);
    lv_obj_set_size(s_weather_icon,48,42);
    lv_obj_set_style_bg_opa(s_weather_icon,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(s_weather_icon,0,0);
    lv_obj_set_style_pad_all(s_weather_icon,0,0);
    lv_obj_remove_flag(s_weather_icon,LV_OBJ_FLAG_SCROLLABLE);
    s_last_weather=(weather_kind_t)-1;

    s_weather_temp=lbl(s_dash,"",950,17,APP_FONT_18,night?TEXT_DARK:TEXT_LIGHT);
    wind_icon_create(s_dash,1015,16,night);
    s_wind_speed=lbl(s_dash,"",1046,17,APP_FONT_18,night?TEXT_DARK:TEXT_LIGHT);

    lv_obj_t *flow=lv_obj_create(s_dash);lv_obj_set_pos(flow,20,78);lv_obj_set_size(flow,620,342);card_style(flow,night);lbl(flow,tr("Energiaáramlás","Energy flow"),6,0,APP_FONT_20,night?TEXT_DARK:TEXT_LIGHT);
    flow_init(&f_pv,flow,190,102,300,172,night);flow_init(&f_bat,flow,190,250,300,190,night);flow_init(&f_grid,flow,410,181,525,181,night);
    node(flow,28,55,165,90,tr("Napelem","Solar"),LV_SYMBOL_CHARGE,NULL,&s_pv,night,GREEN);
    lv_obj_t *bat_node=node(flow,28,215,165,90,tr("Akkumulátor","Battery"),LV_SYMBOL_BATTERY_3,&s_bat_icon,&s_bat,night,BLUE);
    node(flow,250,130,165,105,tr("Ház","Home"),LV_SYMBOL_HOME,NULL,&s_home,night,ORANGE);
    node(flow,438,130,150,105,tr("Hálózat","Grid"),LV_SYMBOL_POWER,NULL,&s_grid,night,RED);

    s_battery_bar=lv_bar_create(bat_node);
    lv_obj_set_pos(s_battery_bar,8,65);
    lv_obj_set_size(s_battery_bar,135,9);
    lv_bar_set_range(s_battery_bar,0,100);
    lv_obj_set_style_bg_color(s_battery_bar,c(night?0x29302C:0xE4E9E6),LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_battery_bar,LV_OPA_COVER,LV_PART_MAIN);
    lv_obj_set_style_radius(s_battery_bar,5,LV_PART_MAIN);
    lv_obj_set_style_radius(s_battery_bar,5,LV_PART_INDICATOR);

    s_soc=lbl(flow,"",40,286,APP_FONT_14,night?MUTED_DARK:MUTED_LIGHT);
    s_bt=lbl(flow,"",115,286,APP_FONT_14,night?MUTED_DARK:MUTED_LIGHT);

    const char *t[8]={tr("Mai termelés","Production"),tr("Mai fogyasztás","Consumption"),tr("Hálózati vételezés","Grid import"),tr("Visszatáplálás","Grid export"),tr("Megtakarított pénz","Money saved"),tr("Megtakarított CO2","CO2 saved"),tr("Fa-egyenérték","Tree equivalent"),tr("Önellátás","Self-sufficiency")};
    for(int i=0;i<8;i++)metric_card(s_dash,655+(i%4)*155,78+(i/4)*114,t[i],i,night);
    s_status_card=lv_obj_create(s_dash);
    lv_obj_set_pos(s_status_card,965,306);
    lv_obj_set_size(s_status_card,295,114);
    card_style(s_status_card,night);
    lbl(s_status_card,tr("Rendszerállapot","System status"),0,0,APP_FONT_14,night?MUTED_DARK:MUTED_LIGHT);
    s_status_icon=lbl(s_status_card,LV_SYMBOL_OK,0,38,APP_FONT_20,GREEN);
    s_status=lbl(s_status_card,"-",34,35,APP_FONT_20,GREEN);

    /*
     * U+0171 (ű) is the only Hungarian glyph not present in the component's
     * compact basic charset. Two tiny two-segment marks complete the plain
     * 'u' visually without allocating any per-dashboard font data.
     */
    s_status_accent_a=lv_obj_create(s_status_card);
    lv_obj_set_pos(s_status_accent_a,112,30);
    lv_obj_set_size(s_status_accent_a,5,7);
    lv_obj_set_style_bg_opa(s_status_accent_a,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(s_status_accent_a,0,0);
    lv_obj_set_style_pad_all(s_status_accent_a,0,0);
    lv_obj_remove_flag(s_status_accent_a,LV_OBJ_FLAG_SCROLLABLE);
    shape(s_status_accent_a,0,3,2,4,GREEN,1);
    shape(s_status_accent_a,2,0,2,4,GREEN,1);

    s_status_accent_b=lv_obj_create(s_status_card);
    lv_obj_set_pos(s_status_accent_b,119,30);
    lv_obj_set_size(s_status_accent_b,5,7);
    lv_obj_set_style_bg_opa(s_status_accent_b,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(s_status_accent_b,0,0);
    lv_obj_set_style_pad_all(s_status_accent_b,0,0);
    lv_obj_remove_flag(s_status_accent_b,LV_OBJ_FLAG_SCROLLABLE);
    shape(s_status_accent_b,0,3,2,4,GREEN,1);
    shape(s_status_accent_b,2,0,2,4,GREEN,1);

    lv_obj_add_flag(s_status_accent_a,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_status_accent_b,LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *batday=lv_obj_create(s_dash);
    lv_obj_set_pos(batday,655,306);
    lv_obj_set_size(batday,300,114);
    card_style(batday,night);
    lbl(batday,tr("Akkumulátor","Battery"),0,0,APP_FONT_14,night?MUTED_DARK:MUTED_LIGHT);
    s_bat_soc_detail=lbl(batday,"",0,22,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);
    s_bat_power_detail=lbl(batday,"",145,22,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);
    s_bat_charge=lbl(batday,"",0,42,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);
    s_bat_charge_cmp=lbl(batday,"",190,42,APP_FONT_12,GREEN);
    s_bat_discharge=lbl(batday,"",0,61,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);
    s_bat_discharge_cmp=lbl(batday,"",190,61,APP_FONT_12,GREEN);
    s_bat_temp_detail=lbl(batday,"",0,80,APP_FONT_12,night?TEXT_DARK:TEXT_LIGHT);

    lv_obj_t *cc1=lv_obj_create(s_dash);lv_obj_set_pos(cc1,20,435);lv_obj_set_size(cc1,610,345);card_style(cc1,night);lbl(cc1,tr("Termelés és fogyasztás","Production and consumption"),0,0,APP_FONT_18,night?TEXT_DARK:TEXT_LIGHT);
    s_chart1=lv_chart_create(cc1);
    lv_obj_set_pos(s_chart1,20,42);
    lv_obj_set_size(s_chart1,565,245);
    lv_chart_set_type(s_chart1,LV_CHART_TYPE_CURVE);
    lv_chart_set_point_count(s_chart1,APP_STATS_SLOT_COUNT);
    lv_chart_set_div_line_count(s_chart1,5,6);
    lv_obj_set_style_bg_opa(s_chart1,LV_OPA_TRANSP,0);
    lv_obj_set_style_line_color(s_chart1,c(night?0x29322E:0xE5EAEC),LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart1,1,LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart1,2,LV_PART_ITEMS);
#if LV_DRAW_SW_COMPLEX
    lv_obj_add_event_cb(s_chart1,chart_positive_gradient_cb,LV_EVENT_DRAW_TASK_ADDED,NULL);
    lv_obj_set_send_draw_task_events(s_chart1,true);
#endif
    s_pv_ser=lv_chart_add_series(s_chart1,c(tone(GREEN,night)),LV_CHART_AXIS_PRIMARY_Y);
    s_load_ser=lv_chart_add_series(s_chart1,c(tone(ORANGE,night)),LV_CHART_AXIS_PRIMARY_Y);
    shape(cc1,343,8,8,8,tone(GREEN,night),LV_RADIUS_CIRCLE);
    lbl(cc1,tr("Termelés","Production"),356,3,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    shape(cc1,458,8,8,8,tone(ORANGE,night),LV_RADIUS_CIRCLE);
    lbl(cc1,tr("Fogyasztás","Consumption"),471,3,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    lbl(cc1,"kW",8,43,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    lbl(cc1,"00:00  04:00  08:00  12:00  16:00  20:00  24:00",42,300,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);

    lv_obj_t *cc2=lv_obj_create(s_dash);
    lv_obj_set_pos(cc2,645,435);
    lv_obj_set_size(cc2,615,345);
    card_style(cc2,night);
    lbl(cc2,tr("Hálózat és akkumulátor","Grid and battery"),0,0,APP_FONT_18,night?TEXT_DARK:TEXT_LIGHT);
    s_chart2=lv_chart_create(cc2);
    lv_obj_set_pos(s_chart2,20,42);
    lv_obj_set_size(s_chart2,570,245);
    lv_chart_set_type(s_chart2,LV_CHART_TYPE_CURVE);
    lv_chart_set_point_count(s_chart2,APP_STATS_SLOT_COUNT);
    lv_chart_set_div_line_count(s_chart2,5,6);
    lv_obj_set_style_bg_opa(s_chart2,LV_OPA_TRANSP,0);
    lv_obj_set_style_line_color(s_chart2,c(night?0x29322E:0xE5EAEC),LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart2,1,LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart2,2,LV_PART_ITEMS);
    s_grid_ser=lv_chart_add_series(s_chart2,c(tone(RED,night)),LV_CHART_AXIS_PRIMARY_Y);
    s_bat_ser=lv_chart_add_series(s_chart2,c(tone(BLUE,night)),LV_CHART_AXIS_PRIMARY_Y);
    shape(cc2,370,8,8,8,tone(RED,night),LV_RADIUS_CIRCLE);
    lbl(cc2,tr("Hálózat","Grid"),383,3,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    shape(cc2,465,8,8,8,tone(BLUE,night),LV_RADIUS_CIRCLE);
    lbl(cc2,tr("Akkumulátor","Battery"),478,3,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    shape(cc2,20,163,570,2,night?0x56615B:0xA7B1B5,0);
    lbl(cc2,"kW",8,43,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);
    lbl(cc2,"00:00  04:00  08:00  12:00  16:00  20:00  24:00",42,300,APP_FONT_12,night?MUTED_DARK:MUTED_LIGHT);

    s_sd_warn=lbl(s_dash,tr("SD-kártya hiba","SD card error"),1090,52,APP_FONT_14,RED);
    lv_obj_add_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);
    s_dash_created=true;
    s_view=VIEW_DASHBOARD;
    s_last_chart_update_us=0;
}
static double self(const app_energy_totals_t *t){if(t->consumption_kwh<=1e-9)return 0;double x=(t->consumption_kwh-t->grid_import_kwh)/t->consumption_kwh*100;if(x<0)x=0;if(x>100)x=100;return x;}
static double money(const app_energy_totals_t *t){double local=t->production_kwh-t->grid_export_kwh;if(local<0)local=0;return local*s_cfg.energy.electricity_purchase_price+t->grid_export_kwh*s_cfg.energy.feed_in_price;}
static double co2(const app_energy_totals_t *t){return t->production_kwh*s_cfg.energy.co2_factor_kg_per_kwh;}
static double tree(const app_energy_totals_t *t){return co2(t)/s_cfg.energy.tree_absorption_kg_per_year;}
static void metric(int i,const char *v,double today,double yesterday,bool have){lv_label_set_text(s_metric[i],v);if(!have){lv_label_set_text(s_cmp[i],"");return;}char x[32];fmt_cmp(x,sizeof(x),today,yesterday);lv_label_set_text(s_cmp[i],x);lv_obj_set_style_text_color(s_cmp[i],c(cmp_color(today,yesterday)),0);}
static void charts(const app_stats_snapshot_t *st){
    double m1=1,m2=1;for(int i=0;i<APP_STATS_SLOT_COUNT;i++)if(st->slots[i].valid){m1=fmax(m1,fmax(st->slots[i].pv_kw,st->slots[i].load_kw));m2=fmax(m2,fmax(fabs(st->slots[i].grid_kw),fabs(st->slots[i].battery_kw)));}m1=ceil(m1);m2=ceil(m2);
    lv_chart_set_axis_range(s_chart1,LV_CHART_AXIS_PRIMARY_Y,0,(int32_t)lround(m1*1000));lv_chart_set_axis_range(s_chart2,LV_CHART_AXIS_PRIMARY_Y,(int32_t)lround(-m2*1000),(int32_t)lround(m2*1000));
    for(int i=0;i<APP_STATS_SLOT_COUNT;i++){int32_t none=LV_CHART_POINT_NONE;if(st->slots[i].valid){lv_chart_set_series_value_by_id(s_chart1,s_pv_ser,i,(int32_t)lround(st->slots[i].pv_kw*1000));lv_chart_set_series_value_by_id(s_chart1,s_load_ser,i,(int32_t)lround(st->slots[i].load_kw*1000));lv_chart_set_series_value_by_id(s_chart2,s_grid_ser,i,(int32_t)lround(st->slots[i].grid_kw*1000));lv_chart_set_series_value_by_id(s_chart2,s_bat_ser,i,(int32_t)lround(st->slots[i].battery_kw*1000));}else{lv_chart_set_series_value_by_id(s_chart1,s_pv_ser,i,none);lv_chart_set_series_value_by_id(s_chart1,s_load_ser,i,none);lv_chart_set_series_value_by_id(s_chart2,s_grid_ser,i,none);lv_chart_set_series_value_by_id(s_chart2,s_bat_ser,i,none);}}lv_chart_refresh(s_chart1);lv_chart_refresh(s_chart2);
}
static weather_kind_t weather_kind(const app_energy_sample_t *s){
    if(s->has_precipitation)return s->temperature_c<-4?WEATHER_SNOW:WEATHER_RAIN;
    if(s->night_mode)return WEATHER_NIGHT;
    if(s->has_sun_radiation)return WEATHER_SUNNY;
    if(s->power_pv_kw>1)return WEATHER_PARTLY_CLOUDY;
    return WEATHER_CLOUDY;
}
static void update(const app_energy_sample_t *s,const app_stats_snapshot_t *st,float dt){
    time_t now=time(NULL);struct tm tmv;localtime_r(&now,&tmv);char b[96],q[32];
    strftime(q,sizeof(q),"%H:%M",&tmv);lv_label_set_text(s_clock,q);
    if(hu())snprintf(q,sizeof(q),"%04d.%02d.%02d.",tmv.tm_year+1900,tmv.tm_mon+1,tmv.tm_mday);
    else snprintf(q,sizeof(q),"%04d-%02d-%02d",tmv.tm_year+1900,tmv.tm_mon+1,tmv.tm_mday);
    lv_label_set_text(s_date,q);

    weather_render(weather_kind(s),s->night_mode);
    char temp[16];
    fmt_dec(temp,sizeof(temp),s->temperature_c,1);
    snprintf(b,sizeof(b),"%s °C",temp);
    lv_label_set_text(s_weather_temp,b);
    snprintf(b,sizeof(b),"%d km/h",(int)lround(s->wind_speed_kmh));
    lv_label_set_text(s_wind_speed,b);
    fmt_power(q,sizeof(q),s->power_pv_kw);lv_label_set_text(s_pv,q);
    fmt_power(q,sizeof(q),s->power_full_kw);lv_label_set_text(s_home,q);
    fmt_power(q,sizeof(q),s->power_bat_kw);lv_label_set_text(s_bat,q);
    fmt_power(q,sizeof(q),s->power_grid_kw);lv_label_set_text(s_grid,q);
    fmt_pct(q,sizeof(q),s->soc_bat_pct);lv_label_set_text(s_soc,q);
    snprintf(q,sizeof(q),"%d °C",(int)lround(s->battery_temperature_c));lv_label_set_text(s_bt,q);

    uint32_t bt_color=s->battery_temperature_c>50?RED:(s->battery_temperature_c>40?ORANGE:(s->night_mode?MUTED_DARK:MUTED_LIGHT));
    lv_obj_set_style_text_color(s_bt,c(bt_color),0);

    lv_bar_set_value(s_battery_bar,(int32_t)lround(s->soc_bat_pct),LV_ANIM_OFF);
    uint32_t soc_color=battery_color(s->soc_bat_pct);
    lv_obj_set_style_bg_color(s_battery_bar,c(tone(soc_color,s->night_mode)),LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s_bat_icon,c(tone(soc_color,s->night_mode)),0);
    lv_label_set_text(s_bat_icon,
        s->soc_bat_pct>87.5?LV_SYMBOL_BATTERY_FULL:
        s->soc_bat_pct>62.5?LV_SYMBOL_BATTERY_3:
        s->soc_bat_pct>37.5?LV_SYMBOL_BATTERY_2:
        s->soc_bat_pct>12.5?LV_SYMBOL_BATTERY_1:
        LV_SYMBOL_BATTERY_EMPTY);

    fmt_pct(q,sizeof(q),s->soc_bat_pct);
    snprintf(b,sizeof(b),"SOC: %s",q);
    lv_label_set_text(s_bat_soc_detail,b);

    fmt_power(q,sizeof(q),s->power_bat_kw);
    snprintf(b,sizeof(b),"%s: %s",tr("Telj.","Power"),q);
    lv_label_set_text(s_bat_power_detail,b);

    snprintf(b,sizeof(b),"%s: %d °C",tr("Hőm.","Temp."),(int)lround(s->battery_temperature_c));
    lv_label_set_text(s_bat_temp_detail,b);
    lv_obj_set_style_text_color(s_bat_temp_detail,c(bt_color),0);
    flow_update(&f_pv,s->power_pv_kw,true,GREEN,s->night_mode,dt);flow_update(&f_bat,s->power_bat_kw,s->power_bat_kw>0,s->power_bat_kw<0?GREEN:ORANGE,s->night_mode,dt);if(s->status==3){for(int i=0;i<3;i++)lv_obj_add_flag(f_grid.dots[i],LV_OBJ_FLAG_HIDDEN);lv_obj_set_style_line_color(f_grid.line,c(GRAY),0);}else flow_update(&f_grid,s->power_grid_kw,s->power_grid_kw<0,s->power_grid_kw<0?GREEN:ORANGE,s->night_mode,dt);
    fmt_kwh(b,sizeof(b),st->totals.production_kwh);metric(0,b,st->completed_interval_totals.production_kwh,st->yesterday_completed_interval_totals.production_kwh,st->has_yesterday);fmt_kwh(b,sizeof(b),st->totals.consumption_kwh);metric(1,b,st->completed_interval_totals.consumption_kwh,st->yesterday_completed_interval_totals.consumption_kwh,st->has_yesterday);fmt_kwh(b,sizeof(b),st->totals.grid_import_kwh);metric(2,b,st->completed_interval_totals.grid_import_kwh,st->yesterday_completed_interval_totals.grid_import_kwh,st->has_yesterday);fmt_kwh(b,sizeof(b),st->totals.grid_export_kwh);metric(3,b,st->completed_interval_totals.grid_export_kwh,st->yesterday_completed_interval_totals.grid_export_kwh,st->has_yesterday);
    snprintf(b,sizeof(b),"%.0f %s",money(&st->totals),s_cfg.project.currency);metric(4,b,money(&st->completed_interval_totals),money(&st->yesterday_completed_interval_totals),st->has_yesterday);char n[20];fmt_dec(n,sizeof(n),co2(&st->totals),1);snprintf(b,sizeof(b),"%s kg",n);metric(5,b,co2(&st->completed_interval_totals),co2(&st->yesterday_completed_interval_totals),st->has_yesterday);fmt_dec(b,sizeof(b),tree(&st->totals),2);metric(6,b,tree(&st->completed_interval_totals),tree(&st->yesterday_completed_interval_totals),st->has_yesterday);fmt_pct(b,sizeof(b),self(&st->totals));metric(7,b,self(&st->completed_interval_totals),self(&st->yesterday_completed_interval_totals),st->has_yesterday);
    fmt_kwh(q,sizeof(q),st->totals.battery_charge_kwh);
    snprintf(b,sizeof(b),"%s: %s",tr("Töltés","Charge"),q);
    lv_label_set_text(s_bat_charge,b);
    fmt_kwh(q,sizeof(q),st->totals.battery_discharge_kwh);
    snprintf(b,sizeof(b),"%s: %s",tr("Kisütés","Discharge"),q);
    lv_label_set_text(s_bat_discharge,b);

    if(st->has_yesterday){
        fmt_cmp(q,sizeof(q),st->completed_interval_totals.battery_charge_kwh,st->yesterday_completed_interval_totals.battery_charge_kwh);
        lv_label_set_text(s_bat_charge_cmp,q);
        lv_obj_set_style_text_color(s_bat_charge_cmp,c(cmp_color(st->completed_interval_totals.battery_charge_kwh,st->yesterday_completed_interval_totals.battery_charge_kwh)),0);
        fmt_cmp(q,sizeof(q),st->completed_interval_totals.battery_discharge_kwh,st->yesterday_completed_interval_totals.battery_discharge_kwh);
        lv_label_set_text(s_bat_discharge_cmp,q);
        lv_obj_set_style_text_color(s_bat_discharge_cmp,c(cmp_color(st->completed_interval_totals.battery_discharge_kwh,st->yesterday_completed_interval_totals.battery_discharge_kwh)),0);
    }else{
        lv_label_set_text(s_bat_charge_cmp,"");
        lv_label_set_text(s_bat_discharge_cmp,"");
    }

    const char *stxt;
    bool draw_double_acute=false;
    if(s->status==1){
        if(hu()){
            stxt="Normál muködés";
            draw_double_acute=true;
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

    uint32_t status_color=s->status==1?GREEN:s->status==2?ORANGE:s->status==3?BLUE:RED;
    lv_label_set_text(s_status,stxt);

    if(draw_double_acute){
        lv_obj_clear_flag(s_status_accent_a,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_status_accent_b,LV_OBJ_FLAG_HIDDEN);
        uint32_t accent_color=tone(status_color,s->night_mode);
        for(uint32_t i=0;i<lv_obj_get_child_count(s_status_accent_a);i++){
            lv_obj_set_style_bg_color(lv_obj_get_child(s_status_accent_a,i),c(accent_color),0);
        }
        for(uint32_t i=0;i<lv_obj_get_child_count(s_status_accent_b);i++){
            lv_obj_set_style_bg_color(lv_obj_get_child(s_status_accent_b,i),c(accent_color),0);
        }
    }else{
        lv_obj_add_flag(s_status_accent_a,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status_accent_b,LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(s_status,c(tone(status_color,s->night_mode)),0);
    lv_label_set_text(s_status_icon,
        s->status==1?LV_SYMBOL_OK:
        s->status==2?LV_SYMBOL_CHARGE:
        s->status==3?LV_SYMBOL_POWER:
        LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(s_status_icon,c(tone(status_color,s->night_mode)),0);
    lv_obj_set_style_border_color(s_status_card,c(tone(status_color,s->night_mode)),0);
    lv_obj_set_style_border_width(s_status_card,s->status==4?3:1,0);

    if(app_storage_is_ok())lv_obj_add_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);else lv_obj_clear_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);

    int64_t chart_now=esp_timer_get_time();
    if(s_last_chart_update_us==0||chart_now-s_last_chart_update_us>=5000000LL){
        charts(st);
        s_last_chart_update_us=chart_now;
    }
}
static void error_screen(runtime_view_t view,const char *title,const char *detail,bool fatal){
    if(s_view!=view){
        clear();
        screen_style(s_screen,true);
        s_error_pulse=lv_obj_create(s_screen);
        lv_obj_set_size(s_error_pulse,112,112);
        lv_obj_set_pos(s_error_pulse,584,175);
        lv_obj_set_style_radius(s_error_pulse,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_border_width(s_error_pulse,0,0);
        lv_obj_set_style_bg_color(s_error_pulse,c(fatal?RED:ORANGE),0);
        lv_obj_set_style_bg_opa(s_error_pulse,LV_OPA_COVER,0);
        lv_obj_t *bang=lbl(s_error_pulse,"!",38,20,&lv_font_montserrat_48,0xFFFFFF);
        (void)bang;
        lv_obj_t *t=lbl(s_screen,title,0,330,APP_FONT_28,fatal?RED:TEXT_DARK);
        lv_obj_set_width(t,W);
        lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_t *d=lbl(s_screen,detail?detail:"",0,390,APP_FONT_18,MUTED_DARK);
        lv_obj_set_width(d,W);
        lv_obj_set_style_text_align(d,LV_TEXT_ALIGN_CENTER,0);
        s_view=view;
    }

    if(s_error_pulse){
        int phase=(int)((esp_timer_get_time()/10000LL)%200);
        if(phase>100)phase=200-phase;
        lv_obj_set_style_opa(s_error_pulse,(lv_opa_t)(150+phase),0);
    }
}

esp_err_t app_ui_init(void){
    fonts_init();
    bsp_display_cfg_t cfg={.lv_adapter_cfg=ESP_LV_ADAPTER_DEFAULT_CONFIG(),.rotation=ESP_LV_ADAPTER_ROTATE_90,.tear_avoid_mode=ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,.touch_flags={.swap_xy=0,.mirror_x=0,.mirror_y=0}};
    lv_display_t *disp=bsp_display_start_with_config(&cfg);if(!disp)return ESP_FAIL;bsp_display_backlight_on();bsp_display_brightness_set(50);
    if (bsp_display_lock(-1) != ESP_OK) {
        return ESP_FAIL;
    }
    s_screen = lv_screen_active();
    screen_style(s_screen, false);
    bsp_display_unlock();
    app_ui_show_boot(APP_BOOT_SD, NULL);
    return ESP_OK;
}
void app_ui_set_config(const app_config_t *cfg){if(!cfg)return;s_cfg=*cfg;s_cfg_ok=true;s_target=s_cfg.display.day_brightness;}
void app_ui_show_boot(app_boot_stage_t stage,const char *detail){if(bsp_display_lock(-1)!=ESP_OK)return;clear();screen_style(s_screen,false);const char *h[]={"SD-kártya","Konfiguráció","Wi-Fi kapcsolat","Időszinkron","MQTT kapcsolat","Várakozás az első adatcsomagra"},*e[]={"SD card","Configuration","Wi-Fi connection","Time synchronization","MQTT connection","Waiting for first data packet"};lbl(s_screen,s_cfg_ok?s_cfg.project.title:"Zolee Energy Flow Display",34,30,APP_FONT_28,TEXT_LIGHT);lbl(s_screen,tr("Rendszerindítás","Starting system"),34,120,APP_FONT_28,TEXT_LIGHT);lbl(s_screen,hu()?h[stage]:e[stage],34,180,APP_FONT_22,GREEN);if(detail)lbl(s_screen,detail,34,225,APP_FONT_14,MUTED_LIGHT);for(int i=0;i<6;i++){char x[150];snprintf(x,sizeof(x),"%s  %s",i<stage?LV_SYMBOL_OK:i==stage?LV_SYMBOL_REFRESH:"-",hu()?h[i]:e[i]);lbl(s_screen,x,70,310+i*55,APP_FONT_18,i<stage?GREEN:i==stage?ORANGE:GRAY);}bsp_display_unlock();}
void app_ui_show_fatal(const char *title,const char *detail){if(bsp_display_lock(-1)!=ESP_OK)return;error_screen(VIEW_FATAL,title,detail,true);bsp_display_unlock();}
static void runtime(void *arg){
    (void)arg;
    (void)esp_task_wdt_add(NULL);
    int64_t prev=esp_timer_get_time();

    while(true){
        (void)esp_task_wdt_reset();
        int64_t now=esp_timer_get_time();
        float dt=(float)(now-prev)/1000000.0f;
        if(dt>1)dt=.2f;
        prev=now;

        app_runtime_state_t rt;
        app_state_get_runtime(&rt);

        app_energy_sample_t s;
        bool have=app_state_get_sample(&s);

        if(have){
            s_target=s.night_mode?s_cfg.display.night_brightness:s_cfg.display.day_brightness;
            if(s_brightness!=s_target){
                int d=s_target-s_brightness;
                s_brightness+=d>0?1:-1;
                bsp_display_brightness_set(s_brightness);
            }
        }

        if(bsp_display_lock(-1)==ESP_OK){
            int64_t timeout_us=(int64_t)s_cfg.mqtt.data_timeout_seconds*1000000LL;
            bool fresh=have&&(now-rt.last_valid_packet_monotonic_us)<=timeout_us;
            bool first_packet_timeout=
                !have &&
                rt.mqtt_connected &&
                rt.mqtt_connected_monotonic_us>0 &&
                (now-rt.mqtt_connected_monotonic_us)>timeout_us;

            if(!rt.wifi_connected){
                error_screen(
                    VIEW_WIFI_ERROR,
                    tr("Nincs Wi-Fi kapcsolat","No Wi-Fi connection"),
                    tr("Kapcsolat helyreállítására várunk...","Waiting for connection to recover..."),
                    false);
            }else if(!rt.mqtt_connected){
                error_screen(
                    VIEW_MQTT_ERROR,
                    tr("Nincs MQTT kapcsolat","No MQTT connection"),
                    tr("Kapcsolat helyreállítására várunk...","Waiting for connection to recover..."),
                    false);
            }else if(!have&&!first_packet_timeout){
                error_screen(
                    VIEW_WAIT_DATA,
                    tr("Várakozás MQTT adatcsomagra","Waiting for MQTT data"),
                    tr("Az első érvényes adatcsomagra várunk...","Waiting for the first valid packet..."),
                    false);
            }else if(!have||!fresh){
                error_screen(
                    VIEW_DATA_TIMEOUT,
                    tr("Nem érkezett MQTT adatcsomag.","No MQTT data packet received."),
                    tr("Kapcsolat helyreállítására várunk...","Waiting for connection to recover..."),
                    false);
            }else{
                if(!s_dash_created||s.night_mode!=s_last_night){
                    build_dashboard(s.night_mode);
                    s_last_night=s.night_mode;
                }
                app_stats_snapshot_t st;
                app_stats_get_snapshot(&st);
                update(&s,&st,dt);
            }

            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
void app_ui_start_runtime(void){if(s_started)return;s_started=true;xTaskCreate(runtime,"ui_runtime",16384,NULL,5,NULL);}
