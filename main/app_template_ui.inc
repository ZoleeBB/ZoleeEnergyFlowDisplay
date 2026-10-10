/* Az app_ui.c része: ugyanazok az adatok, formázók és LVGL-lock szabályok.
 * Minden geometria és elemlista az SD JSON-jából jön, nincs sablonazonosító szerinti kódág. */
#include "src/misc/cache/instance/lv_image_cache.h"
LV_FONT_DECLARE(app_font_numbers_48);
LV_FONT_DECLARE(app_font_numbers_96);
typedef struct {
    const cJSON *spec;lv_obj_t *obj,*detail,*dots[4],*axis[3];
    lv_point_precise_t points[8];unsigned point_count;double power;float phase;
} template_node_t;
static app_template_view_t template_view,template_pending;
static template_node_t template_nodes[96];static unsigned template_node_count;
static lv_image_dsc_t template_images[32];static int64_t template_chart_time;
static bool template_is_active(void){return template_view.document!=NULL;}
static const char *ts(const cJSON *o,const char *key){const char *v=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,key));return v?v:"";}
static int tn(const cJSON *o,const char *key,int fallback){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsNumber(v)?v->valueint:fallback;}
static uint32_t tc(const cJSON *o,const char *key,uint32_t fallback){const char *s=ts(o,key);return s[0]=='#'?(uint32_t)strtoul(s+1,NULL,16):fallback;}
static const lv_font_t *tf(int size){return size==96?&app_font_numbers_96:size==48?&app_font_numbers_48:size==28?APP_FONT_28:size==20?APP_FONT_20:size==16?APP_FONT_16:APP_FONT_12;}
static double template_value(const char *source,const app_energy_sample_t *s,const app_stats_snapshot_t *st){
    app_energy_split_t split=app_energy_split(s->power_pv_kw,s->power_full_kw,s->power_bat_kw);
#define V(key,value) if(!strcmp(source,key))return value
    V("pv",s->power_pv_kw);V("home",s->power_full_kw);V("grid",s->power_grid_kw);V("battery",s->power_bat_kw);
    V("soc",s->soc_bat_pct);V("battery_temp",s->battery_temperature_c);V("temperature",s->temperature_c);V("wind",s->wind_speed_kmh);
    V("production_today",st->totals.production_kwh);V("consumption_today",st->totals.consumption_kwh);
    V("import_today",st->totals.grid_import_kwh);V("export_today",st->totals.grid_export_kwh);
    V("charge_today",st->totals.battery_charge_kwh);V("discharge_today",st->totals.battery_discharge_kwh);
    V("pv_home",split.pv_home);V("pv_export",split.pv_export);V("pv_stored",split.pv_stored);
#undef V
    return 0;
}
static void template_text(char out[128],const char *source,const app_energy_sample_t *s,const app_stats_snapshot_t *st){
    time_t now=time(NULL);struct tm tmv;localtime_r(&now,&tmv);
    if(!strcmp(source,"clock")){strftime(out,128,"%H:%M",&tmv);return;}
    if(!strcmp(source,"date")){strftime(out,128,hu()?"%Y.%m.%d.":"%Y-%m-%d",&tmv);return;}
    if(!strcmp(source,"status")){snprintf(out,128,"%s",s->status==1?tr("Normál működés","Normal operation"):s->status==2?tr("Felkészülés áramszünetre","Preparing for outage"):s->status==3?tr("Szigetüzem","Island mode"):tr("Rendszerhiba","System error"));return;}
    if(!strcmp(source,"grid_direction")){snprintf(out,128,"%s",s->power_grid_kw<0?tr("Visszatáplálás","Export"):tr("Vételezés","Import"));return;}
    if(!strcmp(source,"battery_direction")){snprintf(out,128,"%s",s->power_bat_kw<0?tr("Töltés","Charging"):s->power_bat_kw>0?tr("Leadás","Discharging"):tr("Készenlét","Idle"));return;}
    double v=template_value(source,s,st);char value[64];fmt_dec(value,sizeof(value),v,1);
    const char *unit="kW";
    if(strstr(source,"_today"))unit="kWh";
    else if(!strcmp(source,"soc"))unit="%";
    else if(!strcmp(source,"temperature")||!strcmp(source,"battery_temp"))unit="°C";
    else if(!strcmp(source,"wind")){fmt_dec(value,sizeof(value),v,0);unit="km/h";}
    else{fmt_power(out,128,v);return;}
    snprintf(out,128,"%s %s",value,unit);
}
static void template_gesture(lv_event_t *event){
    (void)event;lv_indev_t *input=lv_indev_active();if(!input)return;
    lv_dir_t direction=lv_indev_get_gesture_dir(input);
    if(direction==LV_DIR_LEFT||direction==LV_DIR_RIGHT){app_templates_step(direction==LV_DIR_LEFT?1:-1);lv_indev_wait_release(input);}
}
static void template_release_cache(void){for(unsigned i=0;i<template_view.asset_count;i++)lv_image_cache_drop(&template_images[i]);}
static void template_prepare(void){
    /* Nem a grafikus lock alatt olvasunk SD-t és dekódolunk ellenőrzéshez. */
    if(template_pending.document)return;
    app_templates_load_pending(&template_pending);
}
static void template_apply(void){
    if(template_pending.generation==template_view.generation)return;
    clear();template_release_cache();app_templates_view_free(&template_view);
    template_view=template_pending;memset(&template_pending,0,sizeof(template_pending));template_pending.generation=template_view.generation;
    for(unsigned i=0;i<template_view.asset_count;i++){
        app_template_asset_t *a=&template_view.assets[i];
        template_images[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW_ALPHA,.w=a->width,.h=a->height},.data_size=a->size,.data=a->bytes};
    }
}
static void template_build(bool night){
    clear();template_node_count=0;template_chart_time=0;
    const cJSON *scene=cJSON_GetObjectItem(template_view.document,night?"dark":"light");
    s_dash=lv_obj_create(s_screen);lv_obj_set_size(s_dash,W,H);screen_style(s_dash,night);
    lv_obj_set_style_radius(s_dash,0,0);
    lv_obj_set_style_bg_color(s_dash,c(tc(scene,"background",BG_LIGHT)),0);
    lv_obj_remove_flag(s_dash,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_event_cb(s_dash,template_gesture,LV_EVENT_GESTURE,NULL);
    const cJSON *e;cJSON_ArrayForEach(e,cJSON_GetObjectItem(scene,"elements")){
        template_node_t *node=&template_nodes[template_node_count++];memset(node,0,sizeof(*node));node->spec=e;
        const char *type=ts(e,"type");int x=tn(e,"x",0),y=tn(e,"y",0),w=tn(e,"w",1),h=tn(e,"h",1);
        uint32_t text=tc(e,"color",night?TEXT_DARK:TEXT_LIGHT);
        if(!strcmp(type,"label")||!strcmp(type,"value")){
            node->obj=lbl(s_dash,!strcmp(type,"label")?(!hu()&&*ts(e,"text_en")?ts(e,"text_en"):ts(e,"text")):"",x,y,tf(tn(e,"font",20)),text);
            lv_obj_set_size(node->obj,w,h);lv_label_set_long_mode(node->obj,LV_LABEL_LONG_MODE_DOTS);
            if(!strcmp(ts(e,"align"),"center"))lv_obj_set_style_text_align(node->obj,LV_TEXT_ALIGN_CENTER,0);
        }else if(!strcmp(type,"image")||!strcmp(type,"weather")){
            node->obj=lv_image_create(s_dash);
            if(!strcmp(type,"image"))for(unsigned i=0;i<template_view.asset_count;i++)if(!strcmp(ts(e,"file"),template_view.assets[i].name)){
                lv_image_set_src(node->obj,&template_images[i]);lv_image_set_pivot(node->obj,0,0);
                lv_image_set_scale_x(node->obj,w*256/template_view.assets[i].width);lv_image_set_scale_y(node->obj,h*256/template_view.assets[i].height);break;
            }
            lv_obj_set_pos(node->obj,x,y);
        }else if(!strcmp(type,"flow")){
            node->obj=lv_line_create(s_dash);const cJSON *pt;
            cJSON_ArrayForEach(pt,cJSON_GetObjectItem(e,"points"))node->points[node->point_count++]=(lv_point_precise_t){tn(pt,"x",0),tn(pt,"y",0)};
            lv_line_set_points(node->obj,node->points,node->point_count);lv_obj_set_pos(node->obj,x,y);
            lv_obj_set_style_line_color(node->obj,c(text),0);lv_obj_set_style_line_width(node->obj,tn(e,"radius",3),0);
            lv_obj_set_style_line_opa(node->obj,100,0);lv_obj_set_style_line_rounded(node->obj,true,0);
            for(int i=0;i<4;i++)node->dots[i]=shape(s_dash,x,y,7,7,text,LV_RADIUS_CIRCLE);
        }else if(!strcmp(type,"bar")){
            node->obj=lv_bar_create(s_dash);lv_obj_set_pos(node->obj,x,y);lv_obj_set_size(node->obj,w,h);lv_bar_set_range(node->obj,0,tn(e,"max",100));
            lv_obj_set_style_bg_color(node->obj,c(text),LV_PART_INDICATOR);lv_obj_set_style_bg_color(node->obj,c(tc(e,"background",night?0x24312D:0xE2EAE5)),0);
        }else if(!strcmp(type,"gauge")){
            node->obj=lv_arc_create(s_dash);lv_obj_set_pos(node->obj,x,y);lv_obj_set_size(node->obj,w,h);lv_arc_set_range(node->obj,0,tn(e,"max",15)*10);
            lv_arc_set_bg_angles(node->obj,135,405);lv_obj_set_style_arc_color(node->obj,c(text),LV_PART_INDICATOR);lv_obj_set_style_arc_width(node->obj,14,LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(node->obj,c(tc(e,"background",night?0x25394A:0xDFE8ED)),0);lv_obj_set_style_arc_width(node->obj,12,0);lv_obj_set_style_opa(node->obj,LV_OPA_TRANSP,LV_PART_KNOB);
        }else if(!strcmp(type,"chart")){
            bool consumption=!strcmp(ts(e,"kind"),"consumption");
            node->obj=app_chart_create(s_dash,consumption,night,tone(consumption?RED:GREEN,night),tone(consumption?GREEN:ORANGE,night),tone(BLUE,night));
            lv_obj_set_pos(node->obj,x+35,y+8);lv_obj_set_size(node->obj,w-45,h-35);
            for(int i=0;i<3;i++)node->axis[i]=lbl(s_dash,"",x,y+(h-35)*i/2,APP_FONT_12,text);
            for(int i=0;i<5;i++){char time[8];snprintf(time,sizeof(time),"%02d:00",i*6);lbl(s_dash,time,x+20+(w-46)*i/4,y+h-19,APP_FONT_12,text);}
        }else{
            node->obj=lv_obj_create(s_dash);lv_obj_remove_style_all(node->obj);lv_obj_set_pos(node->obj,x,y);lv_obj_set_size(node->obj,w,h);
            lv_obj_set_style_bg_color(node->obj,c(tc(e,"background",night?CARD_DARK:CARD_LIGHT)),0);lv_obj_set_style_bg_opa(node->obj,tn(e,"opacity",255),0);lv_obj_set_style_radius(node->obj,tn(e,"radius",16),0);
        }
        if(node->obj){lv_obj_remove_flag(node->obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(node->obj,LV_OBJ_FLAG_GESTURE_BUBBLE);}
    }
    unsigned n=template_view.catalogue_count;
    for(unsigned i=0;i<n;i++)shape(s_dash,640-(int)n*10+(int)i*20,781,7,7,i==template_view.catalogue_index?tone(GREEN,night):(night?0x53626A:0xC8CFD4),LV_RADIUS_CIRCLE);
    s_sd_warn=lbl(s_dash,tr("SD-kártya hiba","SD card error"),20,776,APP_FONT_12,ORANGE);
    lv_obj_add_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);
    s_dash_created=true;s_view=VIEW_DASHBOARD;
}
static void template_update(const app_energy_sample_t *s,const app_stats_snapshot_t *stats){
    if(app_storage_is_ok())lv_obj_add_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);
    int64_t now=esp_timer_get_time();bool redraw=!template_chart_time||now-template_chart_time>=5000000;
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];const cJSON *e=node->spec;const char *type=ts(e,"type"),*source=ts(e,"source");
        double value=template_value(source,s,stats);node->power=value;
        if(!strcmp(type,"value")){char text[128];template_text(text,source,s,stats);lv_label_set_text(node->obj,text);}
        else if(!strcmp(type,"bar")){
            lv_bar_set_value(node->obj,(int)fabs(value),LV_ANIM_OFF);
            if(!strcmp(source,"soc"))lv_obj_set_style_bg_color(node->obj,c(tone(battery_color(value),s->night_mode)),LV_PART_INDICATOR);
        }
        else if(!strcmp(type,"gauge"))lv_arc_set_value(node->obj,(int)(fabs(value)*10));
        else if(!strcmp(type,"weather")){
            lv_image_set_src(node->obj,weather_asset(weather_kind(s)));lv_image_set_pivot(node->obj,0,0);
            lv_image_set_scale_x(node->obj,tn(e,"w",128)*256/128);lv_image_set_scale_y(node->obj,tn(e,"h",128)*256/128);
        }else if(!strcmp(type,"chart")&&redraw){
            bool consumption=!strcmp(ts(e,"kind"),"consumption");double maximum=1;time_t t=time(NULL);struct tm tmv;localtime_r(&t,&tmv);int minute=tmv.tm_hour*60+tmv.tm_min;
            for(int j=0;j<APP_STATS_SLOT_COUNT;j++)if(stats->slots[j].valid&&!stats->slots[j].chart_gap&&j*5<=minute)maximum=fmax(maximum,consumption?stats->slots[j].load_kw:stats->slots[j].pv_kw);
            maximum=ceil(maximum);app_chart_update(node->obj,stats,maximum,minute);
            for(int j=0;j<3;j++){char text[24];fmt_dec(text,sizeof(text),maximum*(2-j)/2,j==1?1:0);lv_label_set_text(node->axis[j],text);}
        }
        if(!strcmp(type,"value")&&(!strcmp(source,"wind")||!strcmp(source,"battery_temp"))){
            int warn=!strcmp(source,"wind")?35:40;
            lv_obj_set_style_text_color(node->obj,c(value>50?RED:value>warn?ORANGE:tc(e,"color",s->night_mode?TEXT_DARK:TEXT_LIGHT)),0);
        }
    }
    if(redraw)template_chart_time=now;
}
static void template_animate(float dt){
    if(!template_view.document||!s_dash_created||s_view!=VIEW_DASHBOARD)return;
    for(unsigned n=0;n<template_node_count;n++){
        template_node_t *node=&template_nodes[n];if(strcmp(ts(node->spec,"type"),"flow"))continue;
        node->phase=app_flow_advance(node->phase,fabs(node->power),node->power>=0,dt);
        float length=0,segments[7];for(unsigned i=1;i<node->point_count;i++){float dx=node->points[i].x-node->points[i-1].x,dy=node->points[i].y-node->points[i-1].y;segments[i-1]=sqrtf(dx*dx+dy*dy);length+=segments[i-1];}
        for(int d=0;d<4;d++){
            if(fabs(node->power)<.001||length<1){lv_obj_add_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN);continue;}lv_obj_remove_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN);
            float position=fmodf(node->phase+d*.25f,1.f)*length;unsigned k=0;
            while(k+2<node->point_count&&position>segments[k]){position-=segments[k];k++;}
            float ratio=segments[k]>0?position/segments[k]:0;
            lv_obj_set_pos(node->dots[d],tn(node->spec,"x",0)+(int)(node->points[k].x+(node->points[k+1].x-node->points[k].x)*ratio)-3,
                tn(node->spec,"y",0)+(int)(node->points[k].y+(node->points[k+1].y-node->points[k].y)*ratio)-3);
        }
    }
}
