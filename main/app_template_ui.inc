/* Az app_ui.c része: ugyanazok az adatok, formázók és LVGL-lock szabályok.
 * Minden geometria és elemlista az SD JSON-jából jön, nincs sablonazonosító szerinti kódág. */
#include "src/misc/cache/instance/lv_image_cache.h"
LV_FONT_DECLARE(app_font_numbers_48);
LV_FONT_DECLARE(app_font_numbers_96);
typedef struct {
    const cJSON *spec;lv_obj_t *obj,*detail,*dots[4],*axis[3];
    /* Two cubic segments, 32 samples each; drawing and animation share this path. */
    lv_point_precise_t points[65];float segments[64],length;unsigned point_count;double power;float phase;
} template_node_t;
static app_template_view_t template_view,template_pending;
static template_node_t template_nodes[96];static unsigned template_node_count;
static lv_image_dsc_t template_images[32];static int64_t template_chart_time;
static bool template_enter_transition;
static void template_opacity(void *obj,int32_t value){lv_obj_set_style_opa(obj,value,0);}
static bool template_is_active(void){return template_view.document!=NULL;}
static const char *ts(const cJSON *o,const char *key){const char *v=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,key));return v?v:"";}
static int tn(const cJSON *o,const char *key,int fallback){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsNumber(v)?v->valueint:fallback;}
static uint32_t tc(const cJSON *o,const char *key,uint32_t fallback){const char *s=ts(o,key);return s[0]=='#'?(uint32_t)strtoul(s+1,NULL,16):fallback;}
static void template_image(lv_obj_t *obj,const char *file,int w,int h){
    for(unsigned i=0;i<template_view.asset_count;i++)if(!strcmp(file,template_view.assets[i].name)){
        lv_image_set_src(obj,&template_images[i]);lv_image_set_pivot(obj,0,0);
        lv_image_set_scale_x(obj,w*256/template_view.assets[i].width);lv_image_set_scale_y(obj,h*256/template_view.assets[i].height);return;
    }
}
static void template_path(template_node_t *node,const cJSON *e){
    lv_point_precise_t control[8];unsigned n=0;const cJSON *pt;
    cJSON_ArrayForEach(pt,cJSON_GetObjectItem(e,"points"))control[n++]=(lv_point_precise_t){tn(pt,"x",0),tn(pt,"y",0)};
    if(!strcmp(ts(e,"curve"),"bezier")){
        node->points[node->point_count++]=control[0];
        for(unsigned k=0;k+3<n;k+=3)for(unsigned i=1;i<=32;i++){
            float t=i/32.f,u=1.f-t;
            node->points[node->point_count++]=(lv_point_precise_t){
                u*u*u*control[k].x+3*u*u*t*control[k+1].x+3*u*t*t*control[k+2].x+t*t*t*control[k+3].x,
                u*u*u*control[k].y+3*u*u*t*control[k+1].y+3*u*t*t*control[k+2].y+t*t*t*control[k+3].y};
        }
    }else{memcpy(node->points,control,n*sizeof(*control));node->point_count=n;}
    for(unsigned i=1;i<node->point_count;i++){
        float dx=node->points[i].x-node->points[i-1].x,dy=node->points[i].y-node->points[i-1].y;
        node->segments[i-1]=sqrtf(dx*dx+dy*dy);node->length+=node->segments[i-1];
    }
}
static const lv_font_t *tf(int size){return size==96?&app_font_numbers_96:size==48?&app_font_numbers_48:size==28?APP_FONT_28:size==20?APP_FONT_20:size==16?APP_FONT_16:APP_FONT_12;}
static double template_value(const char *source,const app_energy_sample_t *s,const app_stats_snapshot_t *st){
    app_energy_split_t split=app_energy_split(s->power_pv_kw,s->power_full_kw,s->power_bat_kw);
#define V(key,value) if(!strcmp(source,key))return value
    V("pv",s->power_pv_kw);V("home",s->power_full_kw);V("grid",s->power_grid_kw);V("battery",s->power_bat_kw);
    V("soc",s->soc_bat_pct);V("battery_temp",s->battery_temperature_c);V("temperature",s->temperature_c);V("wind",s->wind_speed_kmh);
    V("production_today",st->totals.production_kwh);V("consumption_today",st->totals.consumption_kwh);
    V("import_today",st->totals.grid_import_kwh);V("export_today",st->totals.grid_export_kwh);
    V("charge_today",st->totals.battery_charge_kwh);V("discharge_today",st->totals.battery_discharge_kwh);
    app_stats_metrics_t metrics=app_stats_metrics(&st->totals,&s_cfg.energy);
    app_stats_metrics_t month=app_stats_metrics(&s_month_period.totals,&s_cfg.energy);
    V("self_sufficiency_pct",metrics.self_sufficiency_pct);V("self_consumption_pct",metrics.self_consumption_pct);
    V("pv_home_today",st->totals.pv_home_kwh);V("pv_export_today",st->totals.pv_export_kwh);V("pv_stored_today",st->totals.pv_stored_kwh);
    V("grid_home_today",st->totals.grid_home_kwh);V("battery_home_today",st->totals.battery_home_kwh);
    V("pv_home_pct",st->totals.production_kwh>0?100*st->totals.pv_home_kwh/st->totals.production_kwh:0);
    V("pv_export_pct",st->totals.production_kwh>0?100*st->totals.pv_export_kwh/st->totals.production_kwh:0);
    V("pv_stored_pct",st->totals.production_kwh>0?100*st->totals.pv_stored_kwh/st->totals.production_kwh:0);
    V("pv_load_pct",st->totals.consumption_kwh>0?100*st->totals.pv_home_kwh/st->totals.consumption_kwh:0);
    V("grid_home_pct",st->totals.consumption_kwh>0?100*st->totals.grid_home_kwh/st->totals.consumption_kwh:0);
    V("battery_home_pct",st->totals.consumption_kwh>0?100*st->totals.battery_home_kwh/st->totals.consumption_kwh:0);
    V("grid_net_today",st->totals.grid_import_kwh-st->totals.grid_export_kwh);
    V("battery_net_today",st->totals.battery_discharge_kwh-st->totals.battery_charge_kwh);
    V("purchase_day",metrics.purchase_cost);V("revenue_day",metrics.export_revenue);V("savings_day",metrics.savings);V("net_day",metrics.net_cost);
    V("purchase_month",month.purchase_cost);V("revenue_month",month.export_revenue);V("savings_month",month.savings);V("net_month",month.net_cost);
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
    if(strstr(source,"_pct"))unit="%";
    else if(strstr(source,"_day")||strstr(source,"_month"))unit=s_cfg.project.currency;
    else if(strstr(source,"_today"))unit="kWh";
    else if(!strcmp(source,"soc"))unit="%";
    else if(!strcmp(source,"temperature")||!strcmp(source,"battery_temp"))unit="°C";
    else if(!strcmp(source,"wind")){fmt_dec(value,sizeof(value),v,0);unit="km/h";}
    else{fmt_power(out,128,v);return;}
    snprintf(out,128,"%s %s",value,unit);
}
static void template_gesture(lv_event_t *event){
    (void)event;lv_indev_t *input=lv_indev_active();if(!input)return;
    lv_dir_t direction=lv_indev_get_gesture_dir(input);
    if(direction==LV_DIR_TOP||direction==LV_DIR_BOTTOM){
        s_diagnostics_requested=direction==LV_DIR_TOP;lv_indev_wait_release(input);return;
    }
    if(s_diagnostics_requested)return;
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
    template_enter_transition=s_dash_created&&s_view==VIEW_DASHBOARD;
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
    /* Az áttűnés alatt is a cél háttér látszik: sötét témában nincs fehér villanás. */
    screen_style(s_screen,night);lv_obj_set_style_bg_color(s_screen,c(tc(scene,"background",night?BG_DARK:BG_LIGHT)),0);
    s_dash=lv_obj_create(s_screen);lv_obj_set_size(s_dash,W,H);screen_style(s_dash,night);
    lv_obj_set_style_radius(s_dash,0,0);
    lv_obj_set_style_bg_color(s_dash,c(tc(scene,"background",BG_LIGHT)),0);
    if(*ts(scene,"gradient")){
        lv_obj_set_style_bg_grad_color(s_dash,c(tc(scene,"gradient",BG_LIGHT)),0);
        lv_obj_set_style_bg_grad_dir(s_dash,LV_GRAD_DIR_HOR,0);
    }
    const cJSON *palette=cJSON_GetObjectItem(scene,"palette");
    lv_obj_remove_flag(s_dash,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_event_cb(s_dash,template_gesture,LV_EVENT_GESTURE,NULL);
    const cJSON *e;cJSON_ArrayForEach(e,cJSON_GetObjectItem(scene,"elements")){
        template_node_t *node=&template_nodes[template_node_count++];memset(node,0,sizeof(*node));node->spec=e;
        const char *type=ts(e,"type");int x=tn(e,"x",0),y=tn(e,"y",0),w=tn(e,"w",1),h=tn(e,"h",1);
        uint32_t text=tc(e,"color",night?TEXT_DARK:TEXT_LIGHT);
        if(!strcmp(type,"label")||!strcmp(type,"value")){
            if(cJSON_IsTrue(cJSON_GetObjectItem(e,"text_shadow"))){
                int offset=tn(e,"shadow_offset",1);
                node->detail=lbl(s_dash,!strcmp(type,"label")?(!hu()&&*ts(e,"text_en")?ts(e,"text_en"):ts(e,"text")):"",
                    x+offset,y+offset,tf(tn(e,"font",20)),0x000000);
                lv_obj_set_size(node->detail,w,h);lv_label_set_long_mode(node->detail,LV_LABEL_LONG_MODE_DOTS);
                lv_obj_set_style_text_opa(node->detail,tn(e,"shadow_opacity",100),0);
                if(!strcmp(ts(e,"align"),"center"))lv_obj_set_style_text_align(node->detail,LV_TEXT_ALIGN_CENTER,0);
            }
            node->obj=lbl(s_dash,!strcmp(type,"label")?(!hu()&&*ts(e,"text_en")?ts(e,"text_en"):ts(e,"text")):"",x,y,tf(tn(e,"font",20)),text);
            lv_obj_set_size(node->obj,w,h);lv_label_set_long_mode(node->obj,LV_LABEL_LONG_MODE_DOTS);
            if(!strcmp(ts(e,"align"),"center"))lv_obj_set_style_text_align(node->obj,LV_TEXT_ALIGN_CENTER,0);
        }else if(!strcmp(type,"image")||!strcmp(type,"weather")){
            node->obj=lv_image_create(s_dash);
            if(!strcmp(type,"image"))template_image(node->obj,ts(e,"file"),w,h);
            lv_obj_set_pos(node->obj,x,y);
        }else if(!strcmp(type,"flow")){
            template_path(node,e);
            if(tn(e,"glow",0)>0){
                node->detail=lv_line_create(s_dash);lv_line_set_points(node->detail,node->points,node->point_count);
                lv_obj_set_pos(node->detail,x,y);lv_obj_set_style_line_color(node->detail,c(text),0);
                lv_obj_set_style_line_width(node->detail,tn(e,"radius",3)+tn(e,"glow",8),0);
                lv_obj_set_style_line_opa(node->detail,24,0);lv_obj_set_style_line_rounded(node->detail,true,0);
            }
            node->obj=lv_line_create(s_dash);
            lv_line_set_points(node->obj,node->points,node->point_count);lv_obj_set_pos(node->obj,x,y);
            lv_obj_set_style_line_color(node->obj,c(text),0);lv_obj_set_style_line_width(node->obj,tn(e,"radius",3),0);
            lv_obj_set_style_line_opa(node->obj,100,0);lv_obj_set_style_line_rounded(node->obj,strcmp(ts(e,"curve"),"bezier")!=0,0);
            for(int i=0;i<4;i++){
                node->dots[i]=shape(s_dash,x,y,7,7,text,LV_RADIUS_CIRCLE);
                if(tn(e,"glow",0)>0){
                    lv_obj_set_style_shadow_color(node->dots[i],c(text),0);
                    lv_obj_set_style_shadow_width(node->dots[i],tn(e,"glow",8),0);
                    lv_obj_set_style_shadow_opa(node->dots[i],LV_OPA_40,0);
                }
            }
        }else if(!strcmp(type,"bar")){
            node->obj=lv_bar_create(s_dash);lv_obj_set_pos(node->obj,x,y);lv_obj_set_size(node->obj,w,h);lv_bar_set_range(node->obj,0,tn(e,"max",100));
            lv_obj_set_style_bg_color(node->obj,c(text),LV_PART_INDICATOR);lv_obj_set_style_bg_color(node->obj,c(tc(e,"background",night?0x24312D:0xE2EAE5)),0);
        }else if(!strcmp(type,"gauge")){
            node->obj=lv_arc_create(s_dash);lv_obj_set_pos(node->obj,x,y);lv_obj_set_size(node->obj,w,h);lv_arc_set_range(node->obj,0,tn(e,"max",15)*10);
            lv_arc_set_bg_angles(node->obj,135,405);lv_obj_set_style_arc_color(node->obj,c(text),LV_PART_INDICATOR);lv_obj_set_style_arc_width(node->obj,14,LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(node->obj,c(tc(e,"background",night?0x25394A:0xDFE8ED)),0);lv_obj_set_style_arc_width(node->obj,12,0);lv_obj_set_style_opa(node->obj,LV_OPA_TRANSP,LV_PART_KNOB);
        }else if(!strcmp(type,"chart")){
            bool consumption=!strcmp(ts(e,"kind"),"consumption");
            const char *kind=ts(e,"kind");
            app_chart_kind_t chart_kind=!strcmp(kind,"grid")?APP_CHART_GRID:!strcmp(kind,"battery")?APP_CHART_BATTERY:
                !strcmp(kind,"soc")?APP_CHART_SOC:!strcmp(kind,"week")?APP_CHART_WEEK:!strcmp(kind,"month")?APP_CHART_MONTH:
                consumption?APP_CHART_CONSUMPTION:APP_CHART_PRODUCTION;
            uint32_t first=tc(palette,consumption?"grid":"pv",tone(consumption?RED:GREEN,night));
            uint32_t second=tc(palette,consumption?"pv":"home",tone(consumption?GREEN:ORANGE,night));
            if(chart_kind==APP_CHART_GRID){first=tc(palette,"grid",RED);second=tc(palette,"home",ORANGE);}
            else if(chart_kind==APP_CHART_BATTERY||chart_kind==APP_CHART_SOC)first=tc(palette,"battery",BLUE);
            node->obj=app_chart_create_kind(s_dash,chart_kind,night,first,second,tc(palette,"battery",tone(BLUE,night)));
            if(cJSON_IsTrue(cJSON_GetObjectItem(e,"reference_style")))app_chart_set_reference_style(node->obj);
            lv_obj_set_pos(node->obj,x+35,y+8);lv_obj_set_size(node->obj,w-45,h-35);
            if(cJSON_IsTrue(cJSON_GetObjectItem(e,"reference_style")))lbl(s_dash,chart_kind==APP_CHART_SOC?"%":"kW",x+w-32,y+1,APP_FONT_12,text);
            for(int i=0;i<3;i++)node->axis[i]=lbl(s_dash,"",x,y+(h-35)*i/2,APP_FONT_12,text);
            if(chart_kind==APP_CHART_WEEK||chart_kind==APP_CHART_MONTH){
                lbl(s_dash,chart_kind==APP_CHART_WEEK?tr("Hétfő – Vasárnap · kWh","Mon – Sun · kWh"):tr("1 – hónap vége · kWh","1 – month end · kWh"),x+35,y+h-19,APP_FONT_12,text);
            }else for(int i=0;i<5;i++){char time[8];snprintf(time,sizeof(time),"%02d:00",i*6);lbl(s_dash,time,x+20+(w-46)*i/4,y+h-19,APP_FONT_12,text);}
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
    if(template_enter_transition){
        lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,s_dash);
        lv_anim_set_exec_cb(&anim,template_opacity);lv_anim_set_values(&anim,0,255);
        lv_anim_set_duration(&anim,160);lv_anim_set_path_cb(&anim,lv_anim_path_ease_out);
        lv_anim_start(&anim);template_enter_transition=false;
    }
}
static void template_update(const app_energy_sample_t *s,const app_stats_snapshot_t *stats){
    if(app_storage_is_ok())lv_obj_add_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);
    int64_t now=esp_timer_get_time();bool redraw=!template_chart_time||now-template_chart_time>=5000000;
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];const cJSON *e=node->spec;const char *type=ts(e,"type"),*source=ts(e,"source");
        double value=template_value(source,s,stats);node->power=value;
        if(!strcmp(type,"value")){char text[128];template_text(text,source,s,stats);lv_label_set_text(node->obj,text);if(node->detail)lv_label_set_text(node->detail,text);}
        else if(!strcmp(type,"bar")){
            lv_bar_set_value(node->obj,(int)fabs(value),LV_ANIM_OFF);
            if(!strcmp(source,"soc"))lv_obj_set_style_bg_color(node->obj,c(tone(battery_color(value),s->night_mode)),LV_PART_INDICATOR);
        }
        else if(!strcmp(type,"gauge"))lv_arc_set_value(node->obj,(int)(fabs(value)*10));
        else if(!strcmp(type,"weather")){
            const cJSON *files=cJSON_GetObjectItem(e,"files");
            if(files){
                static const char *keys[]={"sunny","partly_cloudy","cloudy","rain","snow","night"};
                template_image(node->obj,ts(files,keys[weather_kind(s)]),tn(e,"w",128),tn(e,"h",128));
            }else{
                lv_image_set_src(node->obj,weather_asset(weather_kind(s)));lv_image_set_pivot(node->obj,0,0);
                lv_image_set_scale_x(node->obj,tn(e,"w",128)*256/128);lv_image_set_scale_y(node->obj,tn(e,"h",128)*256/128);
            }
        }else if(!strcmp(type,"chart")&&redraw){
            const char *kind=ts(e,"kind");double maximum=1,minimum=0;
            time_t t=time(NULL);struct tm tmv;localtime_r(&t,&tmv);int minute=tmv.tm_hour*60+tmv.tm_min;
            bool period=!strcmp(kind,"week")||!strcmp(kind,"month");
            if(period){
                const app_stats_period_t *p=!strcmp(kind,"month")?&s_month_period:&s_week_period;
                app_chart_update_period(node->obj,p);
                for(unsigned j=0;j<p->count;j++)if(p->days[j].valid){
                    maximum=fmax(maximum,fmax(p->days[j].totals.production_kwh,fmax(p->days[j].totals.consumption_kwh,p->days[j].totals.grid_import_kwh)));
                }
            }else{
                for(int j=0;j<APP_STATS_SLOT_COUNT;j++)if(stats->slots[j].valid&&!stats->slots[j].chart_gap&&j*5<=minute){
                    const app_stats_slot_t *slot=&stats->slots[j];
                    maximum=fmax(maximum,!strcmp(kind,"grid")?fabs(slot->grid_kw):!strcmp(kind,"battery")?fabs(slot->battery_kw):!strcmp(kind,"consumption")?slot->load_kw:slot->pv_kw);
                }
                maximum=!strcmp(kind,"soc")?100:ceil(maximum);
                if(!strcmp(kind,"grid")||!strcmp(kind,"battery"))minimum=-maximum;
                app_chart_update(node->obj,stats,maximum,minute);
            }
            maximum=ceil(maximum);
            for(int j=0;j<3;j++){char text[24];fmt_dec(text,sizeof(text),maximum-(maximum-minimum)*j/2,j==1?1:0);lv_label_set_text(node->axis[j],text);}
        }
        if(!strcmp(type,"value")&&!cJSON_IsTrue(cJSON_GetObjectItem(e,"shadow"))&&(!strcmp(source,"wind")||!strcmp(source,"battery_temp"))){
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
        if(node->detail){
            int glow=fabs(node->power)<.001?0:20+(int)(fmin(7,fabs(node->power))*6);
            lv_obj_set_style_line_opa(node->detail,glow,0);
            lv_obj_set_style_line_opa(node->obj,fabs(node->power)<.001?40:130+(int)(fmin(7,fabs(node->power))*15),0);
        }
        node->phase=app_flow_advance(node->phase,fabs(node->power),node->power>=0,dt);
        for(int d=0;d<4;d++){
            if(fabs(node->power)<.001||node->length<1){lv_obj_add_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN);continue;}lv_obj_remove_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN);
            float position=fmodf(node->phase+d*.25f,1.f)*node->length;unsigned k=0;
            while(k+2<node->point_count&&position>node->segments[k]){position-=node->segments[k];k++;}
            float ratio=node->segments[k]>0?position/node->segments[k]:0;
            lv_obj_set_pos(node->dots[d],tn(node->spec,"x",0)+(int)(node->points[k].x+(node->points[k+1].x-node->points[k].x)*ratio)-3,
                tn(node->spec,"y",0)+(int)(node->points[k].y+(node->points[k+1].y-node->points[k].y)*ratio)-3);
        }
    }
}

/* Közös oldal: sem az elrendezés, sem az adatok nem függnek a sablontól. */
static void diagnostics_render(app_runtime_state_t rt){
    if(s_view!=VIEW_DIAGNOSTICS){
        clear();screen_style(s_screen,true);
        lbl(s_screen,tr("Eszközdiagnosztika","Device diagnostics"),32,28,APP_FONT_28,TEXT_DARK);
        lbl(s_screen,tr("Lefelé húzás: vissza az előző témához","Swipe down to return to the selected theme"),32,73,APP_FONT_16,MUTED_DARK);
        const char *titles[]={"Wi-Fi / IP",tr("Adatfogadás","Data input"),tr("Utolsó adatcsomag","Last data packet"),
            tr("SD-kártya","SD card"),"Firmware",tr("Üzemidő / memória","Uptime / memory")};
        for(int i=0;i<6;i++){
            int x=32+(i%2)*622,y=131+(i/2)*199;
            lv_obj_t *card=lv_obj_create(s_screen);lv_obj_set_pos(card,x,y);lv_obj_set_size(card,594,177);card_style(card,true);
            lv_obj_remove_flag(card,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(card,LV_OBJ_FLAG_GESTURE_BUBBLE);
            lbl(card,titles[i],18,16,APP_FONT_20,MUTED_DARK);
            s_diagnostic_values[i]=lbl(card,"",18,55,APP_FONT_20,TEXT_DARK);lv_obj_set_size(s_diagnostic_values[i],552,105);
            lv_obj_add_flag(s_diagnostic_values[i],LV_OBJ_FLAG_GESTURE_BUBBLE);
        }
        s_view=VIEW_DIAGNOSTICS;
    }
    char text[160];
    snprintf(text,sizeof(text),"%s\n%s · %d dBm",rt.wifi_connected?tr("Kapcsolódva","Connected"):tr("Nincs kapcsolat","Disconnected"),s_diagnostics.ip,s_diagnostics.rssi);
    lv_label_set_text(s_diagnostic_values[0],text);
    snprintf(text,sizeof(text),"%s\n%s",rt.data_recept_mode==APP_DATA_HTTP?"HTTP":"MQTT",app_state_data_ready(&rt)?tr("Elérhető","Ready"):tr("Nem elérhető","Unavailable"));
    lv_label_set_text(s_diagnostic_values[1],text);
    if(s_diagnostics.packet_age_seconds<0)snprintf(text,sizeof(text),"%s",tr("Még nem érkezett","No packet received"));
    else snprintf(text,sizeof(text),"%lld s\n%s",(long long)s_diagnostics.packet_age_seconds,
        app_state_data_timed_out(&rt,esp_timer_get_time(),app_config_data_timeout(&s_cfg))?tr("Lejárt adat","Stale data"):tr("Érvényes adat","Valid data"));
    lv_label_set_text(s_diagnostic_values[2],text);
    lv_label_set_text(s_diagnostic_values[3],rt.sd_ok?tr("Elérhető · mentés aktív","Available · saving active"):tr("Hiba · kijelzés RAM-ból","Fault · display continues from RAM"));
    lv_label_set_text(s_diagnostic_values[4],s_diagnostics.firmware);
    snprintf(text,sizeof(text),"%lld min\n%u KiB",(long long)(s_diagnostics.uptime_seconds/60),s_diagnostics.free_heap_kib);
    lv_label_set_text(s_diagnostic_values[5],text);
}
