/* Az app_ui.c része: ugyanazok az adatok, formázók és LVGL-lock szabályok.
 * Minden geometria és elemlista az SD JSON-jából jön, nincs sablonazonosító szerinti kódág. */
#include "src/misc/cache/instance/lv_image_cache.h"
#ifndef APP_TEMPLATE_DESKTOP
#include "esp_attr.h"
#define TEMPLATE_RAM EXT_RAM_BSS_ATTR
#else
#define TEMPLATE_RAM
#endif
LV_FONT_DECLARE(app_font_numbers_48);
LV_FONT_DECLARE(app_font_numbers_96);
typedef struct {
    const cJSON *spec;lv_obj_t *obj,*detail,*dots[4],*axis[3];
    /* Two cubic segments, 96 samples each; drawing and animation share this path. */
    lv_point_precise_t points[193];float segments[192],length;unsigned point_count;double power;float phase,charge_phase[2];int gauge_target;bool gauge_initialized;bool legacy_battery_corner;unsigned battery_branches;
    lv_vector_path_t *flow_path; /* One continuous cubic path shared by glow and body. */
} template_node_t;
static app_template_view_t template_view,template_pending;
static unsigned template_catalogue_count(void){return s_cfg.cardless_slave ? 1 : template_view.catalogue_count;}
static TEMPLATE_RAM template_node_t template_nodes[96];static unsigned template_node_count;
static lv_image_dsc_t template_images[32];static int64_t template_chart_time;
static bool template_enter_transition;
static bool template_loading_requested;
static bool template_waiting_for_load;
static void template_opacity(void *obj,int32_t value){opa_if_changed(obj,value,0);}
static bool template_is_active(void){return !s_cfg.cardless_slave && template_view.document!=NULL;}
static bool template_heavy_animation(void){
    return !strcmp(template_view.id,"02-energy-paths")||
           !strcmp(template_view.id,"06-energy-orbit");
}
static const char *ts(const cJSON *o,const char *key){const char *v=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,key));return v?v:"";}
static int tn(const cJSON *o,const char *key,int fallback){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsNumber(v)?v->valueint:fallback;}
static uint32_t tc(const cJSON *o,const char *key,uint32_t fallback){const char *s=ts(o,key);return s[0]=='#'?(uint32_t)strtoul(s+1,NULL,16):fallback;}
static bool template_clock_synced;
#include "app_solar_timeline_ui.inc"
#if LV_USE_SNAPSHOT
#define TEMPLATE_RASTER_BUDGET (6U*1024U*1024U)
typedef struct { unsigned asset;int w,h;lv_draw_buf_t *buffer;lv_image_dsc_t image; } template_raster_t;
static TEMPLATE_RAM template_raster_t template_rasters[64];
static unsigned template_raster_count;static size_t template_raster_bytes;
static const lv_image_dsc_t *template_raster(unsigned asset,int w,int h){
    for(unsigned i=0;i<template_raster_count;i++)if(template_rasters[i].asset==asset&&template_rasters[i].w==w&&template_rasters[i].h==h)return &template_rasters[i].image;
    if(w<=0||h<=0||template_raster_count>=64||(size_t)w*h*4>TEMPLATE_RASTER_BUDGET-template_raster_bytes)return NULL;
    /* Offscreen root: scale once with LVGL's own rasterizer, preserving alpha.
     * Never visible, never modifies the active scene or its z-order. */
    lv_obj_t *root=lv_obj_create(NULL);if(!root)return NULL;
    lv_obj_remove_style_all(root);
    lv_obj_t *canvas=lv_obj_create(root);
    lv_obj_remove_style_all(canvas);lv_obj_set_size(canvas,w,h);lv_obj_set_pos(canvas,0,0);
    lv_obj_t *scratch=lv_image_create(canvas);if(!scratch){lv_obj_delete(root);return NULL;}
    lv_image_set_src(scratch,&template_images[asset]);lv_image_set_pivot(scratch,0,0);
    lv_obj_set_pos(scratch,0,0);
    lv_image_set_scale_x(scratch,w*256/template_view.assets[asset].width);
    lv_image_set_scale_y(scratch,h*256/template_view.assets[asset].height);
    lv_draw_buf_t *buffer=lv_snapshot_take(canvas,LV_COLOR_FORMAT_ARGB8888);
    lv_obj_delete(root);
    if(!buffer)return NULL;
    if(buffer->header.w!=(unsigned)w||buffer->header.h!=(unsigned)h||buffer->data_size>TEMPLATE_RASTER_BUDGET-template_raster_bytes){lv_draw_buf_destroy(buffer);return NULL;}
    template_raster_t *r=&template_rasters[template_raster_count++];
    *r=(template_raster_t){.asset=asset,.w=w,.h=h,.buffer=buffer};lv_draw_buf_to_image(buffer,&r->image);
    template_raster_bytes+=buffer->data_size;
    lv_image_cache_drop(&template_images[asset]);
    return &r->image;
}
#endif
static void template_image(lv_obj_t *obj,const char *file,int w,int h){
    for(unsigned i=0;i<template_view.asset_count;i++)if(!strcmp(file,template_view.assets[i].name)){
        const lv_image_dsc_t *image=&template_images[i];bool native=false;
#if LV_USE_SNAPSHOT
        if((unsigned)w!=template_view.assets[i].width||(unsigned)h!=template_view.assets[i].height){
            const lv_image_dsc_t *r=template_raster(i,w,h);if(r){image=r;native=true;}
        }
#endif
        /* set_src invalidates even an unchanged image in LVGL 9.5. */
        if(lv_image_get_src(obj)==image)return;
        lv_image_set_src(obj,image);lv_image_set_pivot(obj,0,0);
        lv_image_set_scale_x(obj,native?256:w*256/template_view.assets[i].width);
        lv_image_set_scale_y(obj,native?256:h*256/template_view.assets[i].height);return;
    }
}
static void template_path(template_node_t *node,const cJSON *e){
    lv_point_precise_t control[8];unsigned n=0;const cJSON *pt;
    cJSON_ArrayForEach(pt,cJSON_GetObjectItem(e,"points"))control[n++]=(lv_point_precise_t){tn(pt,"x",0),tn(pt,"y",0)};
    /* A már SD-re másolt Nap pálya v10 is jó geometriát kapjon
     * firmware-frissítés után, sablon ZIP-csere nélkül. */
    if(!strcmp(template_view.id,"09-solar-journey") &&
       tn(template_view.document,"version",0)<11 &&
       !strcmp(ts(e,"type"),"flow") &&
       !strcmp(ts(e,"source"),"battery")){
        node->legacy_battery_corner=true;
        /* A régi flow origója x=1036. Abszolút: 1016 -> 1038 -> 1038. */
        control[0]=(lv_point_precise_t){-20,1};
        control[1]=(lv_point_precise_t){2,1};
        control[2]=(lv_point_precise_t){2,88};
        n=3;
    }
    if(!node->legacy_battery_corner && !strcmp(ts(e,"curve"),"bezier")){
        node->points[node->point_count++]=control[0];
        for(unsigned k=0;k+3<n;k+=3)for(unsigned i=1;i<=96;i++){
            float t=i/96.f,u=1.f-t;
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
/* A kirajzolt út egyetlen, összefüggő köbös Bézier-stroke; nincsenek
 * egymást fedő, áttetsző rövid szakaszok. A görbe geometria a téma
 * betöltésekor egyszer épül fel, és közös a fényudvarnak és a fővonalnak. */
static uint32_t template_soc_color(const cJSON *spec,double soc){
    if(cJSON_IsTrue(cJSON_GetObjectItem(spec,"soc_four_bands")))
        return soc<30?RED:soc<50?ORANGE:soc<=70?YELLOW:GREEN;
    return battery_color(soc);
}
static void template_flow_draw(lv_event_t *event){
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_REFR_EXT_DRAW_SIZE){
        lv_event_set_ext_draw_size(event,48);
        return;
    }
    if(code!=LV_EVENT_DRAW_MAIN)return;
    template_node_t *node=lv_event_get_user_data(event);
    if(!node||!node->flow_path)return;
    lv_obj_t *obj=lv_event_get_target(event);
    if(lv_obj_get_style_line_opa(obj,0)==LV_OPA_TRANSP)return;
    lv_draw_vector_dsc_t *dsc=lv_draw_vector_dsc_create(lv_event_get_layer(event));
    if(!dsc)return;
    lv_area_t area;lv_obj_get_coords(obj,&area);
    lv_draw_vector_dsc_translate(dsc,(float)area.x1,(float)area.y1);
    lv_draw_vector_dsc_set_fill_opa(dsc,LV_OPA_TRANSP);
    lv_draw_vector_dsc_set_stroke_color(dsc,lv_obj_get_style_line_color(obj,0));
    lv_draw_vector_dsc_set_stroke_width(dsc,lv_obj_get_style_line_width(obj,0));
    lv_draw_vector_dsc_set_stroke_opa(dsc,lv_obj_get_style_line_opa(obj,0));
    lv_draw_vector_dsc_set_stroke_cap(dsc,LV_VECTOR_STROKE_CAP_ROUND);
    lv_draw_vector_dsc_set_stroke_join(dsc,LV_VECTOR_STROKE_JOIN_ROUND);
    lv_draw_vector_dsc_add_path(dsc,node->flow_path);
    lv_draw_vector(dsc);
    lv_draw_vector_dsc_delete(dsc);
}
static void template_flow_path_create(template_node_t *node){
    if(node->flow_path)return;
    node->flow_path=lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);
    if(!node->flow_path)return;
    const cJSON *pts=cJSON_GetObjectItem(node->spec,"points");
    /* Ugyanaz a valódi kezdőpont a vektoros rajzban és a mozgó pontoknál,
     * beleértve a régi SD-sablon helyreállított akkumulátorútját is. */
    lv_fpoint_t origin={(float)node->points[0].x,(float)node->points[0].y};
    lv_vector_path_move_to(node->flow_path,&origin);
    if(!node->legacy_battery_corner && !strcmp(ts(node->spec,"curve"),"bezier")){
        unsigned count=cJSON_GetArraySize(pts);
        for(unsigned i=1;i+2<count;i+=3){
            lv_fpoint_t p[3];
            for(int k=0;k<3;k++){
                const cJSON *point=cJSON_GetArrayItem(pts,i+k);
                p[k]=(lv_fpoint_t){(float)tn(point,"x",0),(float)tn(point,"y",0)};
            }
            lv_vector_path_cubic_to(node->flow_path,&p[0],&p[1],&p[2]);
        }
    }else{
        for(unsigned i=1;i<node->point_count;i++){
            lv_fpoint_t p={(float)node->points[i].x,(float)node->points[i].y};
            lv_vector_path_line_to(node->flow_path,&p);
        }
    }
}
static void template_flow_release_paths(void){
    for(unsigned i=0;i<template_node_count;i++){
        if(template_nodes[i].flow_path){
            lv_vector_path_delete(template_nodes[i].flow_path);
            template_nodes[i].flow_path=NULL;
        }
    }
}
static lv_obj_t *template_flow_object(lv_obj_t *parent,template_node_t *node,
                                      int x,int y,int w,int h,uint32_t color,int thickness,int opacity){
    template_flow_path_create(node);
    lv_obj_t *obj=lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj,template_flow_draw,LV_EVENT_ALL,node);
    lv_obj_set_style_line_color(obj,c(color),0);
    lv_obj_set_style_line_width(obj,thickness,0);
    lv_obj_set_style_line_opa(obj,opacity,0);
    return obj;
}
/* A stroke átlátszósága kizárólag az 5 s-os adatfrissítéskor változik;
 * a mozgó pontok külön grafikus objektumok. */
static void template_flow_tone(template_node_t *node,uint32_t color,bool active){
    /* A vector stroke egyszer rajzol, ezért nincs alpha-halmozódás
     * a Bézier szakaszok illesztéseinél. */
    const bool thick=tn(node->spec,"radius",3)>=16;
    /* Inaktív útvonal: a szín felismerhető marad (28–34% opacity),
     * de az aktív, változatlan 70–78%-os stroke lényegesen erősebb. */
    /* Egyes sablonok világosabb inaktív útvonalat igényelnek, a többi
     * megjelenítés változatlan alapértékeket használ. */
    const int main_opacity=active?
        tn(node->spec,"active_opacity",thick?178:200):
        tn(node->spec,"inactive_opacity",thick?72:86);
    const int glow_opacity=active?37:tn(node->spec,"inactive_glow_opacity",16);
    if(node->obj){
        style_color_if_changed(node->obj,LV_STYLE_LINE_COLOR,c(color),0);
        style_number_if_changed(node->obj,LV_STYLE_LINE_OPA,main_opacity,0);
    }
    if(node->detail){
        style_color_if_changed(node->detail,LV_STYLE_LINE_COLOR,c(color),0);
        style_number_if_changed(node->detail,LV_STYLE_LINE_OPA,glow_opacity,0);
    }
}
static const lv_font_t *tf(int size){return size==96?&app_font_numbers_96:size==48?&app_font_numbers_48:size==28?APP_FONT_28:size==20?APP_FONT_20:size==16?APP_FONT_16:APP_FONT_12;}
static double template_value(const char *source,const app_energy_sample_t *s,const app_stats_snapshot_t *st){
    app_energy_split_t split=app_energy_split(s->power_pv_kw,s->power_full_kw,s->power_bat_kw);
#define V(key,value) if(!strcmp(source,key))return value
    V("solar_savings_day",(st->totals.pv_home_kwh+st->totals.pv_stored_kwh)*s_cfg.energy.electricity_purchase_price+st->totals.grid_export_kwh*s_cfg.energy.feed_in_price);
    V("solar_value_today",st->totals.production_kwh*s_cfg.energy.electricity_purchase_price);
    V("battery_summary",s_cfg.energy.battery_capacity_kwh);
    V("battery_capacity",s_cfg.energy.battery_capacity_kwh);
    V("battery_energy",s_cfg.energy.battery_capacity_kwh>0?s_cfg.energy.battery_capacity_kwh*s->soc_bat_pct/100.:0);
    V("sun_azimuth",s->sun_azimuth_deg);V("sun_altitude",s->sun_altitude_deg);
    V("sunrise",s->sunrise_minute);V("sunset",s->sunset_minute);
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
    if(!strcmp(source,"weather_label")){
        static const char *const hu_weather[]={"Derült égbolt","Változóan felhős","Felhős","Eső","Havazás","Éjszaka"};
        static const char *const en_weather[]={"Sunny","Partly cloudy","Cloudy","Rain","Snow","Night"};
        int kind=(int)weather_kind(s);
        if(!weather_available(s)||kind<0||kind>=6)snprintf(out,128,"%s",tr("Nincs időjárásadat","No weather data"));
        else snprintf(out,128,"%s",hu()?hu_weather[kind]:en_weather[kind]);
        return;
    }
    if(!strcmp(source,"battery_summary")){
        double cap=s_cfg.energy.battery_capacity_kwh;
        if(cap<=0){snprintf(out,128,"—");return;}
        char energy[24],capacity[24];
        fmt_dec(energy,sizeof(energy),cap*s->soc_bat_pct/100.,1);
        fmt_dec(capacity,sizeof(capacity),cap,cap==floor(cap)?0:1);
        snprintf(out,128,"%s / %s kWh",energy,capacity);
        return;
    }
    if(strstr(source,"_yesterday_pct")){
        const app_energy_totals_t *today=&st->comparison_today_totals;
        const app_energy_totals_t *yesterday=&st->comparison_yesterday_totals;
        if(!st->comparison_ready){snprintf(out,128,"—");return;}
        /* Két nap kizárólag azonos, ténylegesen megmért ötperces
         * időablakaiból; a hiányos lefedettséget ~ jelöli. */
        double t=0,y=0;
        if(!strcmp(source,"production_yesterday_pct")){t=today->production_kwh;y=yesterday->production_kwh;}
        else if(!strcmp(source,"production_value_yesterday_pct")){
            t=today->production_kwh*s_cfg.energy.electricity_purchase_price;
            y=yesterday->production_kwh*s_cfg.energy.electricity_purchase_price;
        }else if(!strcmp(source,"consumption_yesterday_pct")){t=today->consumption_kwh;y=yesterday->consumption_kwh;}
        else if(!strcmp(source,"export_yesterday_pct")){t=today->grid_export_kwh;y=yesterday->grid_export_kwh;}
        else if(!strcmp(source,"savings_yesterday_pct")){
            t=(today->pv_home_kwh+today->pv_stored_kwh)*s_cfg.energy.electricity_purchase_price+today->grid_export_kwh*s_cfg.energy.feed_in_price;
            y=(yesterday->pv_home_kwh+yesterday->pv_stored_kwh)*s_cfg.energy.electricity_purchase_price+yesterday->grid_export_kwh*s_cfg.energy.feed_in_price;
        }
        fmt_compared(out,128,t,y,st->yesterday_comparison_partial);
        return;
    }
    if(!strcmp(source,"sun_direction")){
        static const char *const hu_dirs[]={"É","ÉK","K","DK","D","DNy","Ny","ÉNy"};
        static const char *const en_dirs[]={"N","NE","E","SE","S","SW","W","NW"};
        int index=(int)floor((s->sun_azimuth_deg+22.5)/45.)%8;
        snprintf(out,128,"%s",s->sun_position_valid?(hu()?hu_dirs[index]:en_dirs[index]):"-");return;
    }
    if(!strcmp(source,"sunrise")||!strcmp(source,"sunset")){
        int m=!strcmp(source,"sunrise")?s->sunrise_minute:s->sunset_minute;
        if(s->sun_position_valid)snprintf(out,128,"%02d:%02d",m/60,m%60);
        else snprintf(out,128,"-");
        return;
    }
    if(!strcmp(source,"clock")){strftime(out,128,"%H:%M",&tmv);return;}
    if(!strcmp(source,"date")){strftime(out,128,hu()?"%Y.%m.%d.":"%Y-%m-%d",&tmv);return;}
    if(!strcmp(source,"status")){snprintf(out,128,"%s",s->status==1?tr("Normál működés","Normal operation"):s->status==2?tr("Felkészülés áramszünetre","Preparing for outage"):s->status==3?tr("Szigetüzem","Island mode"):tr("Rendszerhiba","System error"));return;}
    if(!strcmp(source,"grid_direction")){snprintf(out,128,"%s",fabs(s->power_grid_kw)<.001?tr("Nincs áramlás","Idle"):s->power_grid_kw<0?tr("Visszatáplálás","Export"):tr("Vételezés","Import"));return;}
    if(!strcmp(source,"battery_direction")){snprintf(out,128,"%s",s->power_bat_kw<0?tr("Töltés","Charging"):s->power_bat_kw>0?tr("Leadás","Discharging"):tr("Készenlét","Idle"));return;}
    double v=template_value(source,s,st);
    if(!strcmp(source,"solar_value_today")||!strcmp(source,"solar_savings_day")){
        snprintf(out,128,"%.0f %s",v,s_cfg.project.currency);return;
    }
    char value[64];fmt_dec(value,sizeof(value),v,!strcmp(source,"soc")?0:1);
    const char *unit="kW";
    if(strstr(source,"_pct"))unit="%";
    else if(strstr(source,"_day")||strstr(source,"_month")||!strcmp(source,"solar_value_today"))unit=s_cfg.project.currency;
    else if(strstr(source,"_today"))unit="kWh";
    else if(!strcmp(source,"soc"))unit="%";
    else if(!strcmp(source,"sun_altitude")||!strcmp(source,"sun_azimuth"))unit="°";
    else if(!strcmp(source,"battery_capacity")||!strcmp(source,"battery_energy"))unit="kWh";
    else if(!strcmp(source,"temperature")||!strcmp(source,"battery_temp"))unit="°C";
    else if(!strcmp(source,"wind")){fmt_dec(value,sizeof(value),v,0);unit="km/h";}
    else{fmt_power(out,128,v);return;}
    snprintf(out,128,"%s %s",value,unit);
}
static void template_gesture(lv_event_t *event){
    if (s_pair_pad) return;
    (void)event;lv_indev_t *input=lv_indev_active();if(!input)return;
    lv_dir_t direction=lv_indev_get_gesture_dir(input);
    if (home_is_open()) {
        home_gesture(direction);
        lv_indev_wait_release(input);
        lv_event_stop_bubbling(event);
        return;
    }
    if(s_diagnostics_requested)return;
    if(direction==LV_DIR_TOP){
        s_diagnostics_requested=true;lv_indev_wait_release(input);return;
    }
    /* Lefelé húzás az Otthon-oldal számára fenntartva; diagnosztikát nem zár. */
    if(direction==LV_DIR_BOTTOM){home_cards_requested();lv_indev_wait_release(input);return;}
    if(!s_cfg.cardless_slave && (direction==LV_DIR_LEFT||direction==LV_DIR_RIGHT)){
        app_templates_step_available(direction==LV_DIR_LEFT?1:-1,s_animation_sample.sun_position_valid);
        template_loading_requested=true;
        lv_indev_wait_release(input);
    }
}
static void template_release_cache(void){
    template_flow_release_paths();
#if LV_USE_SNAPSHOT
    for(unsigned i=0;i<template_raster_count;i++){lv_image_cache_drop(&template_rasters[i].image);lv_draw_buf_destroy(template_rasters[i].buffer);}
    memset(template_rasters,0,sizeof(template_rasters));template_raster_count=0;template_raster_bytes=0;
#endif
    for(unsigned i=0;i<template_view.asset_count;i++)lv_image_cache_drop(&template_images[i]);
}
/* A következő képkockára az SD és képfeldolgozás előtt fekete oldal kerül. */
static void template_loading_screen(void){
    clear();
    bg_color_if_changed(s_screen,c(0x000000),0);
    lv_obj_set_style_bg_grad_dir(s_screen,LV_GRAD_DIR_NONE,0);
    /* A betűkészlet nem tartalmaz minden Unicode jelet: három ASCII pont. */
    lv_obj_t *label=lbl(s_screen,tr("Adatok és grafikai elemek betöltése...","Loading data and graphics..."),
                         0,339,APP_FONT_28,0xFFFFFF);
    lv_obj_set_width(label,W);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    /* A forgó kör a felirat alatt külön objektum, nem hiányzó betű. */
    lv_obj_t *spinner=lv_spinner_create(s_screen);
    lv_obj_set_size(spinner,52,52);
    lv_obj_set_pos(spinner,(W-52)/2,421);
    lv_spinner_set_anim_params(spinner,900,90);
    lv_obj_set_style_arc_width(spinner,5,LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner,5,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner,c(0x303A40),LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner,c(0x63E0A4),LV_PART_INDICATOR);
    lv_obj_remove_flag(spinner,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    s_view=VIEW_THEME_LOADING;
}
/* Minden kiválasztási út (web, lapozás, aktív csomag frissítése) ugyanitt
 * kap látható képkockát az SD I/O előtt. A katalóguslekérdezés csak lockol. */
static bool template_loading_frame(void){
    if (s_cfg.cardless_slave) return true;
    if(!template_loading_requested&&!app_templates_has_pending(template_pending.generation))return true;
    if(bsp_display_lock(-1)!=ESP_OK)return false;
    /* Foglalt SD mellett a meglévő spinner folytatódjon, ne épüljön újra. */
    if(s_view!=VIEW_THEME_LOADING)template_loading_screen();
    bsp_display_unlock();
    template_loading_requested=false;
    template_waiting_for_load=app_templates_has_pending(template_pending.generation);
    vTaskDelay(pdMS_TO_TICKS(85));
    return true;
}
static void template_prepare(void){
    if (s_cfg.cardless_slave) return;
    /* Nem a grafikus lock alatt olvasunk SD-t és dekódolunk ellenőrzéshez. */
    if(template_pending.document)return;
    if(app_templates_load_pending(&template_pending))template_waiting_for_load=false;
}
static void template_apply(void){
    if (s_cfg.cardless_slave) return;
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
    clear();template_flow_release_paths();template_node_count=0;template_chart_time=0;
    const cJSON *scene=cJSON_GetObjectItem(template_view.document,night?"dark":"light");
    /*
     * A régebbi SD-kártyán lévő három gyári téma még sávos RGB565
     * színátmenetet tartalmazhat. A javítás OTA-val is működjön,
     * ne követelje meg a sablon ZIP-ek ismételt telepítését.
     * Egyéni telepített témákhoz nem nyúlunk.
     */
    uint32_t background=tc(scene,"background",night?BG_DARK:BG_LIGHT);
    bool no_gradient=false;
    if(!strcmp(template_view.id,"04-daily-balance")){
        background=night?0x1B2E38:0xE5F1ED;no_gradient=true;
    }else if(!strcmp(template_view.id,"05-calm-home")){
        background=night?0x223432:0xE8F0E7;no_gradient=true;
    }else if(!strcmp(template_view.id,"06-energy-orbit")){
        background=night?0x1B263C:0xE8EEF7;no_gradient=true;
    }else if(!strcmp(template_view.id,"08-statistics")){
        background=night?0x1B2B38:0xE8F2EE;no_gradient=true;
    }
    screen_style(s_screen,night);bg_color_if_changed(s_screen,c(background),0);
    s_dash=lv_obj_create(s_screen);lv_obj_set_size(s_dash,W,H);screen_style(s_dash,night);
    lv_obj_set_style_radius(s_dash,0,0);
    bg_color_if_changed(s_dash,c(background),0);
    if(!no_gradient&&*ts(scene,"gradient")){
        lv_obj_set_style_bg_grad_color(s_dash,c(tc(scene,"gradient",BG_LIGHT)),0);
        lv_obj_set_style_bg_grad_dir(s_dash,LV_GRAD_DIR_HOR,0);
    }
    const cJSON *palette=cJSON_GetObjectItem(scene,"palette");
    lv_obj_remove_flag(s_dash,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_event_cb(s_dash,template_gesture,LV_EVENT_GESTURE,NULL);
    const cJSON *e;cJSON_ArrayForEach(e,cJSON_GetObjectItem(scene,"elements")){
        template_node_t *node=&template_nodes[template_node_count++];memset(node,0,sizeof(*node));node->spec=e;
        const char *type=ts(e,"type");int x=tn(e,"x",0),y=tn(e,"y",0),w=tn(e,"w",1),h=tn(e,"h",1);
        /* A gyári Energiakör régi (SD-n lévő) v7-v8-as sablonjait
         * OTA után is javítjuk; a v9 már tartalmazza az új pozíciókat. */
        if(!strcmp(template_view.id,"06-energy-orbit")&&
           tn(template_view.document,"version",9)<9){
            const char *source=ts(e,"source"),*label=ts(e,"text"),*file=ts(e,"file");
            if(!strcmp(type,"image")&&(!strcmp(file,"flow_house.png")||!strcmp(file,"flow_battery.png")))x=1060;
            else if((!strcmp(type,"label")&&(!strcmp(label,"Otthon")||!strcmp(label,"Akkumulátor")||!strcmp(label,"Töltöttség")))||
                    (!strcmp(type,"value")&&(!strcmp(source,"home")||!strcmp(source,"battery")||!strcmp(source,"soc")))){
                x=1020;w=200;
            }
        }
        uint32_t text=tc(e,"color",night?TEXT_DARK:TEXT_LIGHT);
        /* A felhasználó SD-jén még a korábbi Élő otthon v9 (vagy régebbi)
         * csomag lehet: OTA után is piros legyen a Fogyasztás ma értéke.
         * Az árnyék külön (fekete) eleme változatlan marad. */
        if(!strcmp(template_view.id,"01-live-home")&&
           tn(template_view.document,"version",10)<10&&
           !strcmp(type,"value")&&!strcmp(ts(e,"source"),"consumption_today")&&
           !cJSON_IsTrue(cJSON_GetObjectItem(e,"shadow")))
            text=night?0xE66B6B:0xFF1744;
        if(!strcmp(type,"label")||!strcmp(type,"value")){
            if(cJSON_IsTrue(cJSON_GetObjectItem(e,"text_shadow"))){
                int offset=tn(e,"shadow_offset",1);
                node->detail=lbl(s_dash,!strcmp(type,"label")?(!hu()&&*ts(e,"text_en")?ts(e,"text_en"):ts(e,"text")):"",
                    x+offset,y+offset,tf(tn(e,"font",20)),tc(e,"shadow_color",0x000000));
                lv_obj_set_size(node->detail,w,h);lv_label_set_long_mode(node->detail,LV_LABEL_LONG_MODE_DOTS);
                text_opa_if_changed(node->detail,tn(e,"shadow_opacity",100),0);
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
            const int width=tn(e,"radius",3),glow=tn(e,"glow",0);
            if(glow>0)
                node->detail=template_flow_object(s_dash,node,x,y,w,h,text,width+glow,9);
            node->obj=template_flow_object(s_dash,node,x,y,w,h,text,width,38);
            /* Kiemelt fehér mag, színes perem és fényudvar.
             * A 28 px-es Energiautaknál is jól látszanak a pontok. */
            int diameter=tn(e,"dot_size",width>=16?14:11);
            if(diameter<3)diameter=3;
            if(diameter>18)diameter=18;
            for(int i=0;i<4;i++){
                node->dots[i]=shape(s_dash,x,y,diameter,diameter,0xFFFFFF,LV_RADIUS_CIRCLE);
                lv_obj_set_style_border_width(node->dots[i],diameter<=7?1:2,0);
                border_color_if_changed(node->dots[i],c(text),0);
                lv_obj_set_style_shadow_color(node->dots[i],c(text),0);
                lv_obj_set_style_shadow_width(node->dots[i],diameter<=7?3:(glow>0?glow+5:8),0);
                lv_obj_set_style_shadow_opa(node->dots[i],LV_OPA_70,0);
                /* A felül lévő pontok ne nyeljék el a témalapozó gesztust. */
                lv_obj_remove_flag(node->dots[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(node->dots[i],LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_HIDDEN);
            }
        }else if(!strcmp(type,"solar_timeline")){
            solar_timeline_build(node,night);
        }else if(!strcmp(type,"solar_arc")){
            /* A napút pontosan LVGL-vonalakból áll, kis memóriaigénnyel.
             * A felső és alsó ív külön sablonelem, ezért eltérő színt kaphat. */
            int cx=tn(e,"center_x",w/2),cy=tn(e,"center_y",h/2);
            int rx=tn(e,"radius_x",w/2),ry=tn(e,"radius_y",h/2);
            bool lower=cJSON_IsTrue(cJSON_GetObjectItem(e,"night_arc"));
            node->point_count=65;
            for(unsigned k=0;k<65;k++){
                float angle=(float)3.14159265358979323846*(lower?(1.f+k/64.f):(1.f-k/64.f));
                node->points[k].x=cx+rx*cosf(angle);
                node->points[k].y=cy-ry*sinf(angle);
            }
            node->obj=lv_line_create(s_dash);lv_line_set_points(node->obj,node->points,node->point_count);
            lv_obj_set_pos(node->obj,x,y);
            lv_obj_set_style_line_width(node->obj,tn(e,"radius",3),0);
            lv_obj_set_style_line_color(node->obj,c(text),0);
            lv_obj_set_style_line_opa(node->obj,lower?LV_OPA_60:LV_OPA_COVER,0);
        }else if(!strcmp(type,"solar_disc")){
            /* A napkorong külön, kis LVGL objektum: nincs teljes képes újrarajzolás. */
            /* A grafikai csomag különálló napkorongját használjuk, ha van.
             * Régi SD-sablon esetén megtartjuk a vektoros kört. */
            const char *disc=ts(e,"file");
            bool found=false;
            for(unsigned asset=0;disc[0] && asset<template_view.asset_count;asset++)
                if(!strcmp(disc,template_view.assets[asset].name)){found=true;break;}
            /* A fényudvar külön alakzat, ezért a PNG alfa-rétege mögött is
             * látható. A kép soha nem kap négyszögű háttérkitöltést. */
            node->detail=shape(s_dash,x-6,y-6,w+12,h+12,text,LV_RADIUS_CIRCLE);
            bg_opa_if_changed(node->detail,LV_OPA_10,0);
            lv_obj_set_style_shadow_color(node->detail,c(text),0);
            lv_obj_set_style_shadow_width(node->detail,tn(e,"glow",20),0);
            if(found){
                node->obj=lv_image_create(s_dash);
                template_image(node->obj,disc,w,h);
                lv_obj_set_pos(node->obj,x,y);
            }else{
                node->obj=shape(s_dash,x,y,w,h,text,LV_RADIUS_CIRCLE);
            }
            lv_obj_set_style_shadow_color(node->obj,c(text),0);
            lv_obj_set_style_shadow_width(node->obj,0,0);
        }else if(!strcmp(type,"pulse")){
            node->obj=shape(s_dash,x,y,w,h,text,LV_RADIUS_CIRCLE);
            lv_obj_set_style_shadow_color(node->obj,c(text),0);
            lv_obj_set_style_shadow_width(node->obj,tn(e,"glow",20),0);
        }else if(!strcmp(type,"bar")){
            node->obj=lv_bar_create(s_dash);lv_obj_set_pos(node->obj,x,y);lv_obj_set_size(node->obj,w,h);lv_bar_set_range(node->obj,0,tn(e,"max",100));
            bg_color_if_changed(node->obj,c(text),LV_PART_INDICATOR);bg_color_if_changed(node->obj,c(tc(e,"background",night?0x24312D:0xE2EAE5)),0);
            if(*ts(e,"outline_color")){
                for(int part=0;part<2;part++){lv_style_selector_t selector=part?LV_PART_INDICATOR:LV_PART_MAIN;
                    lv_obj_set_style_border_width(node->obj,1,selector);border_color_if_changed(node->obj,c(tc(e,"outline_color",0)),selector);}
            }
        }else if(!strcmp(type,"gauge")){
            if(*ts(e,"outline_color")){
                node->detail=lv_arc_create(s_dash);lv_obj_set_pos(node->detail,x-1,y-1);lv_obj_set_size(node->detail,w+2,h+2);lv_arc_set_range(node->detail,0,tn(e,"max",15)*10);
                lv_arc_set_bg_angles(node->detail,135,405);
                for(int part=0;part<2;part++){lv_style_selector_t selector=part?LV_PART_INDICATOR:LV_PART_MAIN;lv_obj_set_style_arc_color(node->detail,c(tc(e,"outline_color",0)),selector);lv_obj_set_style_arc_width(node->detail,part?16:14,selector);}
                opa_if_changed(node->detail,LV_OPA_TRANSP,LV_PART_KNOB);lv_obj_remove_flag(node->detail,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            }
            node->obj=lv_arc_create(s_dash);lv_obj_set_pos(node->obj,x,y);lv_obj_set_size(node->obj,w,h);lv_arc_set_range(node->obj,0,tn(e,"max",15)*10);
            lv_arc_set_bg_angles(node->obj,135,405);lv_obj_set_style_arc_color(node->obj,c(text),LV_PART_INDICATOR);lv_obj_set_style_arc_width(node->obj,14,LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(node->obj,c(tc(e,"background",night?0x25394A:0xDFE8ED)),0);lv_obj_set_style_arc_width(node->obj,12,0);opa_if_changed(node->obj,LV_OPA_TRANSP,LV_PART_KNOB);
        }else if(!strcmp(type,"chart")){
            bool consumption=!strcmp(ts(e,"kind"),"consumption");
            const char *kind=ts(e,"kind");
            app_chart_kind_t chart_kind=!strcmp(kind,"solar_hourly")?APP_CHART_SOLAR_HOURLY:!strcmp(kind,"grid")?APP_CHART_GRID:!strcmp(kind,"battery")?APP_CHART_BATTERY:
                !strcmp(kind,"soc")?APP_CHART_SOC:!strcmp(kind,"week")?APP_CHART_WEEK:!strcmp(kind,"month")?APP_CHART_MONTH:
                consumption?APP_CHART_CONSUMPTION:APP_CHART_PRODUCTION;
            uint32_t first=tc(palette,consumption?"grid":"pv",tone(consumption?RED:GREEN,night));
            uint32_t second=tc(palette,consumption?"pv":"home",tone(consumption?GREEN:ORANGE,night));
            if(chart_kind==APP_CHART_SOLAR_HOURLY){first=0x23B779;second=0xFFCB35;}
            if(chart_kind==APP_CHART_GRID){first=tc(palette,"grid",RED);second=tc(palette,"home",ORANGE);}
            else if(chart_kind==APP_CHART_BATTERY||chart_kind==APP_CHART_SOC)first=tc(palette,"battery",BLUE);
            if(chart_kind==APP_CHART_BATTERY){first=tc(e,"negative_color",first);second=tc(e,"positive_color",first);}
            node->obj=app_chart_create_kind(s_dash,chart_kind,night,first,second,tc(palette,(chart_kind==APP_CHART_WEEK||chart_kind==APP_CHART_MONTH)?"grid":"battery",tone(BLUE,night)));
            if(cJSON_IsTrue(cJSON_GetObjectItem(e,"reference_style")))app_chart_set_reference_style(node->obj);
            lv_obj_set_pos(node->obj,x+35,y+8);lv_obj_set_size(node->obj,w-45,h-35);
            if(cJSON_IsTrue(cJSON_GetObjectItem(e,"reference_style")))lbl(s_dash,chart_kind==APP_CHART_SOC?"%":"kW",x+w-32,y+1,APP_FONT_12,text);
            for(int i=0;i<3;i++)node->axis[i]=lbl(s_dash,"",x,y+(h-35)*i/2,APP_FONT_12,text);
            if(chart_kind==APP_CHART_WEEK||chart_kind==APP_CHART_MONTH){
                lbl(s_dash,chart_kind==APP_CHART_WEEK?tr("Hétfő – Vasárnap · kWh","Mon – Sun · kWh"):tr("1 – hónap vége · kWh","1 – month end · kWh"),x+35,y+h-19,APP_FONT_12,text);
            }else for(int i=0;i<5;i++){char time[8];snprintf(time,sizeof(time),"%02d:00",i*6);lbl(s_dash,time,x+20+(w-46)*i/4,y+h-19,APP_FONT_12,text);}
        }else{
            node->obj=lv_obj_create(s_dash);lv_obj_remove_style_all(node->obj);lv_obj_set_pos(node->obj,x,y);lv_obj_set_size(node->obj,w,h);
            bg_color_if_changed(node->obj,c(tc(e,"background",night?CARD_DARK:CARD_LIGHT)),0);bg_opa_if_changed(node->obj,tn(e,"opacity",255),0);lv_obj_set_style_radius(node->obj,tn(e,"radius",16),0);
            if(*ts(e,"outline_color")){
                lv_obj_set_style_border_width(node->obj,tn(e,"outline_width",1),0);
                border_color_if_changed(node->obj,c(tc(e,"outline_color",BLUE)),0);
                lv_obj_set_style_border_opa(node->obj,LV_OPA_60,0);
            }
        }
        if(node->obj){lv_obj_remove_flag(node->obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(node->obj,LV_OBJ_FLAG_GESTURE_BUBBLE);}
    }
    /* A pontok mindig a többi grafikus elem fölé kerülnek.
     * Egy később létrehozott energiaút sem fedheti el őket. */
    for(unsigned j=0;j<template_node_count;j++){
        template_node_t *flow=&template_nodes[j];
        if(strcmp(ts(flow->spec,"type"),"flow"))continue;
        for(int d=0;d<4;d++)if(flow->dots[d])lv_obj_move_foreground(flow->dots[d]);
    }
    /* A kijelölt fedőikon vagy pulzáló csomópont a pontok fölé kerülhet. */
    for(unsigned j=0;j<template_node_count;j++){
        template_node_t *node=&template_nodes[j];
        const char *type=ts(node->spec,"type");
        if((!strcmp(type,"image")||!strcmp(type,"pulse"))&&cJSON_IsTrue(cJSON_GetObjectItem(node->spec,"foreground")))lv_obj_move_foreground(node->obj);
    }
    unsigned n=template_view.catalogue_count;
    for(unsigned i=0;i<n;i++)shape(s_dash,640-(int)n*10+(int)i*20,781,7,7,i==template_view.catalogue_index?tone(GREEN,night):(night?0x53626A:0xC8CFD4),LV_RADIUS_CIRCLE);
    s_sd_warn=lbl(s_dash,tr("SD-kártya hiba","SD card error"),20,776,APP_FONT_12,ORANGE);
    lv_obj_add_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);
    home_bind_hotspot(s_dash, scene);
    s_dash_created=true;s_view=VIEW_DASHBOARD;
    /*
     * A sok nagy PNG-vel és négy animált energiaúttal rajzolt gyári
     * témákra ne tegyünk teljes képernyős áttűnést: a 1280x800 képkocka
     * alfa-kompozitálása és újrarajzolása a lapozást érezhetően lassítja.
     */
    bool heavy_transition=!strcmp(template_view.id,"02-energy-paths")||
                          !strcmp(template_view.id,"06-energy-orbit");
    if(template_enter_transition && !heavy_transition){
        lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,s_dash);
        lv_anim_set_exec_cb(&anim,template_opacity);lv_anim_set_values(&anim,0,255);
        lv_anim_set_duration(&anim,160);lv_anim_set_path_cb(&anim,lv_anim_path_ease_out);
        lv_anim_start(&anim);
    }
    template_enter_transition=false;
}
static void template_gauge_value(void *object,int32_t value){
    lv_arc_set_value(object,value);
    for(unsigned i=0;i<template_node_count;i++)if(template_nodes[i].obj==object){
        if(template_nodes[i].detail)lv_arc_set_value(template_nodes[i].detail,value);break;
    }
}
/* A Nap pálya akkuágának aktív vonala ugyanazt az ágat követi, mint a pontok.
 * A vektor és a két LVGL-objektum megmarad; csak útváltáskor épül újra a geometria. */
static void template_solar_battery_route(template_node_t *node,const app_energy_sample_t *sample){
    if(strcmp(template_view.id,"09-solar-journey")||strcmp(ts(node->spec,"type"),"flow")||strcmp(ts(node->spec,"source"),"battery")||node->point_count!=3||!node->flow_path)return;
    template_node_t *pv=NULL;
    for(unsigned i=0;i<template_node_count;i++)
        if(!strcmp(ts(template_nodes[i].spec,"type"),"flow")&&!strcmp(ts(template_nodes[i].spec,"source"),"pv_home"))pv=&template_nodes[i];
    if(!pv||pv->point_count<2)return;
    float house_x=tn(pv->spec,"x",0)+pv->points[pv->point_count-1].x-tn(node->spec,"x",0);
    app_energy_split_t split=app_energy_split(sample->power_pv_kw,sample->power_full_kw,sample->power_bat_kw);
    double charge=fmax(0,-node->power),pv_charge=fmin(charge,split.pv_stored);
    unsigned branches=node->power>=.001?2:((pv_charge>=.001?1:0)|((charge-pv_charge)>=.001?2:0));
    if(branches==node->battery_branches)return;
    node->battery_branches=branches;
    lv_vector_path_clear(node->flow_path);
    lv_fpoint_t corner={node->points[1].x,node->points[1].y},bottom={node->points[2].x,node->points[2].y};
    lv_fpoint_t start={(branches==2)?house_x:node->points[0].x,node->points[0].y};
    lv_vector_path_move_to(node->flow_path,&start);
    lv_vector_path_line_to(node->flow_path,&corner);lv_vector_path_line_to(node->flow_path,&bottom);
    if(branches==3){
        start=(lv_fpoint_t){house_x,corner.y};
        lv_vector_path_move_to(node->flow_path,&start);lv_vector_path_line_to(node->flow_path,&corner);
    }
    lv_obj_invalidate(node->obj);if(node->detail)lv_obj_invalidate(node->detail);
}
/* A frissítés és az animáció ugyanazt a fényudvar-állapotot alkalmazza. */
static void template_solar_halo(template_node_t *node,const app_energy_sample_t *s,float dt){
    double pv=fmax(0,s->power_pv_kw);
    if(!s->sun_position_valid)return;
    float fraction=(float)fmin(pv/8.,1.);
    bool daylight=!s->night_mode&&s->sun_altitude_deg>0;
    /* Fix 1,2 ciklus/s: a termelés csak a végső körméretet adja.
     * 8 kW-tól a korábbi maximális méret; nullánál nincs fényudvar. */
    node->phase=fmodf(node->phase+dt*(daylight?1.2f:1.f/4.2f),1.f);
    float wave=.5f+.5f*sinf(node->phase*6.2831853f);
    /* Az éjszakai halvány kék napkorong NEM pulzál. */
    if(node->detail){
        /* A hold képe nem pulzál: csak a mögötte lévő áttetsző
         * fénygyűrű lélegzik lassan, +/- 2 px kiterjedéssel. */
        /* Az apró méretváltozást finom alpha-/glow-átmenet
         * egészíti ki: nem ugrál 1 px-es lépésekben a fény. */
        const int pad=6+(int)lroundf(2.f*wave);
        const int disc_w=tn(node->spec,"w",40),disc_h=tn(node->spec,"h",40);
        const int width=daylight?1+(int)lroundf((disc_w+55)*fraction*node->phase):disc_w+2*pad;
        const int height=daylight?1+(int)lroundf((disc_h+55)*fraction*node->phase):disc_h+2*pad;
        if(lv_obj_get_style_width(node->detail,0)!=width || lv_obj_get_style_height(node->detail,0)!=height)
            lv_obj_set_size(node->detail,width,height);
        lv_obj_set_pos(node->detail,lv_obj_get_style_x(node->obj,0)+(disc_w-width)/2,lv_obj_get_style_y(node->obj,0)+(disc_h-height)/2);
        /* A nap középpontjából induló kitöltött kör kifelé halad,
         * és a teljes méret elérése előtt nullára halványul. */
        lv_opa_t ring_alpha=daylight&&pv>0?(lv_opa_t)lroundf(190.f*(1.f-node->phase)):0;
        lv_obj_set_style_border_width(node->detail,daylight?2:0,0);
        lv_obj_set_style_border_opa(node->detail,ring_alpha,0);
        lv_obj_set_style_shadow_width(node->detail,daylight?8:10+(int)lroundf(4.f*wave),0);
        lv_obj_set_style_shadow_opa(node->detail,daylight?ring_alpha:(lv_opa_t)(35+20*wave),0);
        bg_opa_if_changed(node->detail,daylight?(pv>0?(lv_opa_t)lroundf(110.f*(1.f-node->phase)):0):(lv_opa_t)(22+16*wave),0);
    }
}
static void template_update(const app_energy_sample_t *s,const app_stats_snapshot_t *stats){
    /* A sablonok is az aktuális csomagot adják át az animációs timernek. */
    s_animation_sample=*s;
    if(app_storage_is_ok())lv_obj_add_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN);
    int64_t now=esp_timer_get_time();bool redraw=!template_chart_time||now-template_chart_time>=5000000;
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];const cJSON *e=node->spec;const char *type=ts(e,"type"),*source=ts(e,"source");
        if(!strcmp(type,"solar_timeline")){
            solar_timeline_update(node,s);
            continue;
        }
        bool visible = (!strcmp(type,"solar_disc") || !strcmp(type,"solar_arc")) ? s->sun_position_valid :
            !strcmp(type,"weather") ? weather_available(s) :
            !strcmp(source,"temperature") ? s->temperature_valid :
            !strcmp(source,"wind") ? s->wind_speed_valid :
            (!strcmp(source,"sun_azimuth")||!strcmp(source,"sun_altitude")||
             !strcmp(source,"sunrise")||!strcmp(source,"sunset")||!strcmp(source,"sun_direction")) ? s->sun_position_valid :
            (!strcmp(source,"weather_label")) ? weather_available(s) :
            (!strcmp(source,"battery_capacity")||!strcmp(source,"battery_energy")||!strcmp(source,"battery_summary")) ? s_cfg.energy.battery_capacity_kwh>0 : true;
        weather_visibility(node->obj,visible);
        if(node->detail)weather_visibility(node->detail,visible);
        if(!visible)continue;
        double value=template_value(source,s,stats);node->power=value;
        if(!strcmp(type,"solar_disc")){
            /* Egyezik a solar_arc elemek paraméteres félkörével.
             * Nappal: K -> D -> Ny; éjjel: Ny -> É -> K. */
            if(!s->sun_position_valid)continue;

            /* Fotóból kalibrált abszolút napív, külön fényes vonal nélkül. */
            int sunrise=s->sunrise_minute,sunset=s->sunset_minute;
            time_t clock_now=time(NULL);struct tm local_now;
            localtime_r(&clock_now,&local_now);
            int minute=local_now.tm_hour*60+local_now.tm_min;
            bool daylight=s->sun_altitude_deg>0;
            float travel;
            if(sunrise>=0&&sunrise<1440&&sunset>sunrise&&sunset<1440){
                if(daylight)travel=(float)(minute-sunrise)/(float)(sunset-sunrise);
                else{
                    int passed=minute>=sunset?minute-sunset:minute+1440-sunset;
                    travel=(float)passed/(float)(sunrise+1440-sunset);
                }
            }else travel=(float)(s->sun_azimuth_deg/360.);
            travel=fmaxf(0.f,fminf(1.f,travel));
            int px,py;
            const cJSON *arc=cJSON_GetObjectItem(e,daylight?"day_path":"night_path");
            if(cJSON_IsArray(arc)&&cJSON_GetArraySize(arc)>=2){
                double length=0.;
                const cJSON *prev=NULL,*pt=NULL;
                cJSON_ArrayForEach(pt,arc){
                    if(prev)length+=hypot((double)(tn(pt,"x",0)-tn(prev,"x",0)),
                                           (double)(tn(pt,"y",0)-tn(prev,"y",0)));
                    prev=pt;
                }
                double target=length*travel;
                int cx=tn(cJSON_GetArrayItem(arc,0),"x",452);
                int cy=tn(cJSON_GetArrayItem(arc,0),"y",383);
                prev=NULL;
                cJSON_ArrayForEach(pt,arc){
                    if(prev){
                        double dx=tn(pt,"x",0)-tn(prev,"x",0);
                        double dy=tn(pt,"y",0)-tn(prev,"y",0);
                        double dist=hypot(dx,dy);
                        if(target<=dist){
                            double ratio=dist>0?fmax(0.,fmin(1.,target/dist)):0.;
                            cx=(int)lround(tn(prev,"x",0)+dx*ratio);
                            cy=(int)lround(tn(prev,"y",0)+dy*ratio);
                            break;
                        }
                        target-=dist;
                        cx=tn(pt,"x",cx);cy=tn(pt,"y",cy);
                    }
                    prev=pt;
                }
                px=cx-tn(e,"w",40)/2;py=cy-tn(e,"h",40)/2;
            }else{
                /* A régi sablonok ellipszis módja megmarad. */
                int rx=tn(e,"radius_x",377),cx=tn(e,"center_x",452),cy=tn(e,"center_y",356);
                int ry=tn(e,daylight?"radius_y":"night_radius_y",244);
                float angle=travel*3.14159265358979323846f;
                px=cx+(int)lroundf((daylight?-1.f:1.f)*cosf(angle)*rx)-tn(e,"w",40)/2;
                py=cy+(int)lroundf((daylight?-1.f:1.f)*sinf(angle)*ry)-tn(e,"h",40)/2;
            }
            lv_obj_set_pos(node->obj,px,py);
            if(node->detail){
                int pad=(lv_obj_get_width(node->detail)-tn(e,"w",40))/2;
                lv_obj_set_pos(node->detail,px-pad,py-pad);
            }
            if(!*ts(e,"file")){
                uint32_t disc_color=daylight?0xFFD23B:0x78BFEF;
                bg_color_if_changed(node->obj,c(disc_color),0);
                lv_obj_set_style_shadow_color(node->obj,c(disc_color),0);
            }
            if(node->detail){
                lv_obj_set_style_shadow_color(node->detail,c(daylight?0xFFD23B:0x78BFEF),0);
                border_color_if_changed(node->detail,c(daylight?0xFFD23B:0x78BFEF),0);
                bg_color_if_changed(node->detail,c(daylight?0xFFD23B:0x78BFEF),0);
            }
            /* Ne várjunk az első animációs tickre a módváltás után. */
            template_solar_halo(node,s,0.f);
            continue;
        }
        if(!strcmp(type,"flow")){
            template_solar_battery_route(node,s);
            uint32_t flow_color=tc(e,value<0?"negative_color":"positive_color",tc(e,"color",BLUE));
            template_flow_tone(node,flow_color,fabs(value)>=.001);
        }
        if(!strcmp(type,"value")){char text[128];template_text(text,source,s,stats);
            if(!strcmp(source,"battery_temp")&&!strcmp(template_view.id,"09-solar-journey")){char number[64];fmt_dec(number,sizeof(number),s->battery_temperature_c,0);snprintf(text,sizeof(text),"%s °C",number);}
            label_text_if_changed(node->obj,text);if(node->detail)label_text_if_changed(node->detail,text);}
        else if(!strcmp(type,"bar")){
            lv_bar_set_value(node->obj,(int)fabs(value),LV_ANIM_OFF);
            if(!strcmp(source,"soc"))bg_color_if_changed(node->obj,c(tone(template_soc_color(e,value),s->night_mode)),LV_PART_INDICATOR);
        }
        else if(!strcmp(type,"gauge")){
            int target=(int)fmin(tn(e,"max",15)*10,fabs(value)*10);
            if(!node->gauge_initialized){template_gauge_value(node->obj,target);node->gauge_initialized=true;node->gauge_target=target;}
            else if(target!=node->gauge_target){
                lv_anim_delete(node->obj,template_gauge_value);node->gauge_target=target;
                if(tn(e,"animate_ms",0)>0){
                    lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,node->obj);lv_anim_set_exec_cb(&anim,template_gauge_value);
                    lv_anim_set_values(&anim,lv_arc_get_value(node->obj),target);lv_anim_set_duration(&anim,tn(e,"animate_ms",280));lv_anim_set_path_cb(&anim,lv_anim_path_ease_out);lv_anim_start(&anim);
                }else template_gauge_value(node->obj,target);
            }
            lv_obj_set_style_arc_color(node->obj,c(tc(e,value<0?"negative_color":"positive_color",tc(e,"color",BLUE))),LV_PART_INDICATOR);
        }
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
            if(!strcmp(kind,"solar_hourly")){
                app_chart_update(node->obj,stats,1,minute);
                for(int hour=0;hour<24;hour++){
                    double sum=0;
                    for(int k=0;k<12;k++)sum+=stats->slots[hour*12+k].energy.pv_home_kwh+
                        stats->slots[hour*12+k].energy.pv_stored_kwh+
                        stats->slots[hour*12+k].energy.pv_export_kwh;
                    maximum=fmax(maximum,sum);
                }
                maximum=ceil(maximum);
                for(int j=0;j<3;j++){char text[24];fmt_dec(text,sizeof(text),maximum*(2-j)/2.,j==1?1:0);label_text_if_changed(node->axis[j],text);}
                continue;
            }
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
            for(int j=0;j<3;j++){char text[24];fmt_dec(text,sizeof(text),maximum-(maximum-minimum)*j/2,j==1?1:0);label_text_if_changed(node->axis[j],text);}
        }
        if(!cJSON_IsTrue(cJSON_GetObjectItem(e,"shadow"))&&(*ts(e,"positive_color")||*ts(e,"negative_color"))){
            double direction=!strcmp(source,"grid_direction")?s->power_grid_kw:!strcmp(source,"battery_direction")?s->power_bat_kw:value;
            uint32_t color=tc(e,direction<0?"negative_color":"positive_color",tc(e,"color",BLUE));
            /* Az alapértelmezett előjelszínek mellé opcionális nyugalmi szín. */
            if(fabs(direction)<.001)color=tc(e,"zero_color",color);
            if(!strcmp(type,"value"))text_color_if_changed(node->obj,c(color),0);
            if(!strcmp(type,"flow")){
                template_flow_tone(node,color,fabs(value)>=.001);
                for(int d=0;d<4;d++){
                    border_color_if_changed(node->dots[d],c(color),0);
                    style_color_if_changed(node->dots[d],LV_STYLE_SHADOW_COLOR,c(color),0);
                }
            }
        }
        if(!strcmp(type,"value")&&!cJSON_IsTrue(cJSON_GetObjectItem(e,"shadow"))&&(!strcmp(source,"wind")||!strcmp(source,"battery_temp"))){
            int warn=!strcmp(source,"wind")?35:40;
            text_color_if_changed(node->obj,c(value>50?RED:value>warn?ORANGE:tc(e,"color",s->night_mode?TEXT_DARK:TEXT_LIGHT)),0);
        }
        /*
         * A régi 07-energy-ledger SD sablonok csak statikus színt adnak meg,
         * ezért a PowerGrid iránytól függő színezést itt is garantáljuk.
         * PowerGrid > 0: vételezés piros; PowerGrid < 0: export citromsárga.
         */
        if(!strcmp(template_view.id,"07-energy-ledger")&&!strcmp(type,"value")&&
          (!strcmp(source,"grid")||!strcmp(source,"grid_direction"))){
            text_color_if_changed(node->obj,c(s->power_grid_kw<0?0xFFEB38:0xE53935),0);
        }
    }
    if(redraw)template_chart_time=now;
}
/* A Nap pálya T-csomópontján a pontok és az aktív vonalak együtt váltanak ágat.
 * A pontok a töltés forrásától függően a PV vagy a ház ágát követik.
 * Két töltőforrás esetén mindkét ág kap saját folyamatos pontsorozatot. */
static bool template_solar_battery_animate(template_node_t *node,float dt){
    if(strcmp(template_view.id,"09-solar-journey")||strcmp(ts(node->spec,"source"),"battery")||node->point_count!=3)return false;
    template_node_t *pv=NULL;
    for(unsigned i=0;i<template_node_count;i++)
        if(!strcmp(ts(template_nodes[i].spec,"type"),"flow")&&!strcmp(ts(template_nodes[i].spec,"source"),"pv_home"))pv=&template_nodes[i];
    if(!pv||pv->point_count<2)return false;
    const float house_x=tn(pv->spec,"x",0)+pv->points[pv->point_count-1].x-tn(node->spec,"x",0);
    if(fabsf(node->points[2].y-node->points[1].y)<1||fabsf(house_x-node->points[1].x)<1||fabsf(node->points[0].x-node->points[1].x)<1)return false;
    app_energy_split_t split=app_energy_split(s_animation_sample.power_pv_kw,s_animation_sample.power_full_kw,s_animation_sample.power_bat_kw);
    double charge=fmax(0,-node->power);
    double pv_charge=fmin(charge,split.pv_stored),grid_charge=fmax(0,charge-pv_charge);
    bool mixed=pv_charge>=.001&&grid_charge>=.001;
    node->phase=app_flow_advance(node->phase,fabs(node->power),node->power<0,dt);
    node->charge_phase[0]=app_flow_advance(node->charge_phase[0],pv_charge,true,dt);
    node->charge_phase[1]=app_flow_advance(node->charge_phase[1],grid_charge,true,dt);
    int count=tn(node->spec,"dot_count",2);if(count<1)count=1;if(count>4)count=4;
    if(mixed&&count<2)count=2;
    for(int d=0;d<4;d++){
        if(d>=count||fabs(node->power)<.001){lv_obj_add_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN);continue;}
        lv_obj_remove_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN);
        bool from_grid=node->power>0||(grid_charge>=.001&&(!mixed||d%2==1));
        lv_point_precise_t start=from_grid?(lv_point_precise_t){house_x,node->points[1].y}:node->points[0];
        float horizontal=fabsf(start.x-node->points[1].x),vertical=fabsf(node->points[2].y-node->points[1].y);
        float phase=node->power>0?node->phase:node->charge_phase[from_grid?1:0];
        unsigned group_count=mixed?(from_grid?count/2:(count+1)/2):count;
        unsigned group_index=mixed?(unsigned)d/2:(unsigned)d;
        float position=fmodf(phase+(float)group_index/group_count,1.f)*(horizontal+vertical);
        float x,y;
        if(position<horizontal){x=start.x+(node->points[1].x-start.x)*position/horizontal;y=start.y;}
        else{x=node->points[1].x;y=node->points[1].y+(node->points[2].y-node->points[1].y)*(position-horizontal)/vertical;}
        lv_obj_set_pos(node->dots[d],tn(node->spec,"x",0)+(int)x-lv_obj_get_width(node->dots[d])/2,
                       tn(node->spec,"y",0)+(int)y-lv_obj_get_height(node->dots[d])/2);
    }
    return true;
}
static void template_animate(float dt){
    if(!template_view.document||!s_dash_created||s_view!=VIEW_DASHBOARD)return;
    for(unsigned n=0;n<template_node_count;n++){
        template_node_t *node=&template_nodes[n];
        if(!strcmp(ts(node->spec,"type"),"solar_disc")){
            template_solar_halo(node,&s_animation_sample,dt);
            continue;
        }
        if(!strcmp(ts(node->spec,"type"),"pulse")){
            node->phase=fmodf(node->phase+dt*.7f,1.f);
            int alpha=145+(int)(110*(.5f+.5f*sinf(node->phase*6.2831853f)));
            bg_opa_if_changed(node->obj,alpha,0);
            lv_obj_set_style_shadow_opa(node->obj,alpha,0);continue;
        }
        if(strcmp(ts(node->spec,"type"),"flow"))continue;
        if(template_solar_battery_animate(node,dt))continue;
        bool forward=node->power>=0;
        if(cJSON_IsTrue(cJSON_GetObjectItem(node->spec,"reverse")))forward=!forward;
        node->phase=app_flow_advance(node->phase,fabs(node->power),forward,dt);
        int dot_count=tn(node->spec,"dot_count",4);
        if(dot_count<1)dot_count=1;
        if(dot_count>4)dot_count=4;
        for(int d=0;d<4;d++){
            if(d>=dot_count||fabs(node->power)<.001||node->length<1){lv_obj_add_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN);continue;}lv_obj_remove_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN);
            float position=fmodf(node->phase+(float)d/dot_count,1.f)*node->length;unsigned k=0;
            while(k+2<node->point_count&&position>node->segments[k]){position-=node->segments[k];k++;}
            float ratio=node->segments[k]>0?position/node->segments[k]:0;
            lv_obj_set_pos(node->dots[d],tn(node->spec,"x",0)+(int)(node->points[k].x+(node->points[k+1].x-node->points[k].x)*ratio)-lv_obj_get_width(node->dots[d])/2,
                tn(node->spec,"y",0)+(int)(node->points[k].y+(node->points[k+1].y-node->points[k].y)*ratio)-lv_obj_get_height(node->dots[d])/2);
        }
    }
}

/* A naplópuffer rövid mutexét az LVGL-lock előtt kérjük el. */
static void diagnostics_capture(void){
    app_diagnostics_capture(&s_diagnostics);
    if(s_diagnostics_requested)s_diagnostic_event_count=app_events_snapshot(s_diagnostic_events,15);
}
/* Közös oldal: tömör adatok balra, a legfrissebb RAM-események jobbra. */
static void diagnostics_close(lv_event_t *event)
{
    (void)event;
    s_diagnostics_requested = false;
    template_loading_requested = true;
    if (event) lv_event_stop_bubbling(event);
}

static void diagnostics_render(app_runtime_state_t rt)
{
    if (s_view != VIEW_DIAGNOSTICS)
    {
        clear();
        screen_style(s_screen, true);
        lbl(s_screen,
            tr("Eszközdiagnosztika", "Device diagnostics"),
            32,
            28,
            APP_FONT_28,
            TEXT_DARK);
        lv_obj_t *hint = lbl(s_screen,
                             tr("Bezárás a jobb felső X gombbal",
                                "Close with the X button in the upper right"),
                             32,
                             73,
                             APP_FONT_16,
                             MUTED_DARK);
        lv_obj_set_width(hint, 560);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_DOTS);
        const char *titles[] = {
            "Wi-Fi / IP",
            tr("Adatfogadás", "Data input"),
            tr("Utolsó adatcsomag", "Last data packet"),
            tr("SD-kártya", "SD card"),
            tr("Firmware / üzemidő", "Firmware / uptime"),
            "CPU",
            tr("Belső RAM", "Internal RAM"),
            "PSRAM",
            tr("Bluetooth szerep", "Bluetooth role"),
            tr("Saját BT cím", "Local BT address"),
            rt.data_recept_mode == APP_DATA_SLAVE ? tr("Master BT cím", "Master BT address")
                                                  : tr("BT név / párosítási PIN", "BT name / pairing PIN"),
            rt.data_recept_mode == APP_DATA_SLAVE ? tr("Master jelerősség", "Master signal")
                                                  : tr("Kapcsolódó slave-ek", "Connected slaves"),
            tr("Bluetooth állapot", "Bluetooth state"),
            tr("Időszinkron", "Time synchronization")};
        if (rt.ble_enabled && rt.ble_master) {
            lv_obj_t *button = pairing_button(s_screen, "", 352, 28, 272, 36);
            s_pair_window_label = lv_obj_get_child(button, 0);
            lv_obj_set_style_text_font(s_pair_window_label, APP_FONT_14, 0);
            lv_obj_set_style_pad_all(button, 0, 0);
            lv_obj_set_width(s_pair_window_label, 232);
            lv_obj_set_style_text_align(s_pair_window_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(s_pair_window_label, LV_ALIGN_CENTER, 0, 0);
            lv_obj_t *icon = lv_label_create(button);
            lv_label_set_text(icon, LV_SYMBOL_BLUETOOTH);
            lv_obj_set_style_text_font(icon, LV_FONT_DEFAULT, 0);
            lv_obj_set_style_text_color(icon, c(0xFFFFFF), 0);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 7, 0);
            lv_obj_add_event_cb(button, pairing_window_clicked, LV_EVENT_CLICKED, NULL);
        }
        for (int i = 0; i < 14; i++)
        {
            lv_obj_t *card = lv_obj_create(s_screen);
            lv_obj_set_pos(card, 32 + (i % 2) * 296, 118 + (i / 2) * 94);
            lv_obj_set_size(card, 280, 88);
            card_style(card, true);
            lv_obj_set_style_pad_all(card, 0, 0);
            lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
            lbl(card, titles[i], 16, 8, APP_FONT_14, MUTED_DARK);
            s_diagnostic_values[i] = lbl(card, "", 16, 29, APP_FONT_14, TEXT_DARK);
            lv_obj_set_size(s_diagnostic_values[i], 248, 46);
            lv_label_set_long_mode(s_diagnostic_values[i], LV_LABEL_LONG_MODE_DOTS);
            if (i == 6)
                lv_obj_set_style_text_font(s_diagnostic_values[i], APP_FONT_12, 0);
            if (i == 3 || i == 6 || i == 7)
            {
                int bar = i == 3 ? 0 : i - 5;
                s_diagnostic_bars[bar] = lv_bar_create(card);
                lv_obj_set_pos(s_diagnostic_bars[bar], 16, 78);
                lv_obj_set_size(s_diagnostic_bars[bar], 248, 4);
                lv_bar_set_range(s_diagnostic_bars[bar], 0, 100);
                lv_obj_set_style_bg_color(s_diagnostic_bars[bar], c(0x32465B), LV_PART_MAIN);
                lv_obj_set_style_bg_color(s_diagnostic_bars[bar], c(0x58B89C), LV_PART_INDICATOR);
                lv_obj_add_flag(s_diagnostic_bars[bar], LV_OBJ_FLAG_GESTURE_BUBBLE);
            }
            lv_obj_add_flag(s_diagnostic_values[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
        }
        lv_obj_t *log = lv_obj_create(s_screen);
        lv_obj_set_pos(log, 640, 24);
        lv_obj_set_size(log, 608, 752);
        card_style(log, true);
        lv_obj_set_style_pad_all(log, 0, 0);
        lv_obj_remove_flag(log, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(log, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lbl(log,
            tr("Rendszernapló · legutóbbi események", "System log · latest events"),
            18,
            16,
            APP_FONT_20,
            TEXT_DARK);
        for (int i = 0; i < 15; i++)
        {
            s_diagnostic_log[i] = lbl(log, "", 18, 60 + i * 44, APP_FONT_16, TEXT_DARK);
            lv_obj_set_size(s_diagnostic_log[i], 564, 40);
            lv_label_set_long_mode(s_diagnostic_log[i], LV_LABEL_LONG_MODE_DOTS);
            lv_obj_add_flag(s_diagnostic_log[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
        }
        lv_obj_t *close = pairing_button(s_screen, "X", 1192, 32, 48, 48);
        lv_obj_set_style_radius(close, 24, 0);
        lv_obj_add_event_cb(close, diagnostics_close, LV_EVENT_CLICKED, NULL);
        s_view = VIEW_DIAGNOSTICS;
    }
    char text[192];
    snprintf(text,
             sizeof(text),
             "%s · %d dBm\n%s",
             rt.wifi_connected ? tr("Kapcsolódva", "Connected")
                               : tr("Nincs kapcsolat", "Disconnected"),
             s_diagnostics.rssi,
             s_diagnostics.ip);
    label_text_if_changed(s_diagnostic_values[0], text);
    snprintf(text,
             sizeof(text),
             "%s · %s",
             rt.data_recept_mode == APP_DATA_SLAVE    ? "Slave (BLE)"
             : rt.data_recept_mode == APP_DATA_LOXONE ? "Loxone"
             : rt.data_recept_mode == APP_DATA_HTTP   ? "HTTP"
                                                      : "MQTT",
             app_state_data_ready(&rt) ? tr("Elérhető", "Ready")
                                       : tr("Nem elérhető", "Unavailable"));
    label_text_if_changed(s_diagnostic_values[1], text);
    if (s_diagnostics.packet_age_seconds < 0)
        snprintf(text, sizeof(text), "%s", tr("Még nem érkezett", "No packet received"));
    else
        snprintf(
            text,
            sizeof(text),
            "%lld s · %s",
            (long long)s_diagnostics.packet_age_seconds,
            app_state_data_timed_out(&rt, esp_timer_get_time(), app_config_data_timeout(&s_cfg))
                ? tr("Lejárt adat", "Stale data")
                : tr("Érvényes adat", "Valid data"));
    label_text_if_changed(s_diagnostic_values[2], text);
    unsigned sd_used = s_diagnostics.sd_space_valid
                         ? (unsigned)((s_diagnostics.sd_total_bytes - s_diagnostics.sd_free_bytes) *
                                      100 / s_diagnostics.sd_total_bytes)
                         : 0;
    if (rt.sd_ok && s_diagnostics.sd_space_valid)
        snprintf(text,
                 sizeof(text),
                 "%u%% · %.1f / %.1f GiB",
                 sd_used,
                 (double)(s_diagnostics.sd_total_bytes - s_diagnostics.sd_free_bytes) / 1073741824.,
                 (double)s_diagnostics.sd_total_bytes / 1073741824.);
    else
        snprintf(text,
                 sizeof(text),
                 "%s",
                 rt.sd_ok ? tr("Kapacitás lekérése...", "Reading capacity...")
                          : tr("Hiba · kijelzés RAM-ból", "Fault · display from RAM"));
    if (rt.cardless_slave)
        snprintf(text, sizeof(text), "%s\n%s", tr("SD nélkül · belső tár", "No SD · internal storage"),
                 rt.remote_stats_valid ? tr("Master stat.: friss", "Master stats: current") : tr("Master stat.: várakozás", "Master stats: waiting"));
    label_text_if_changed(s_diagnostic_values[3], text);
    lv_bar_set_value(s_diagnostic_bars[0], sd_used, LV_ANIM_OFF);
    if (rt.sd_ok && s_diagnostics.sd_space_valid)
        lv_obj_remove_flag(s_diagnostic_bars[0], LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_diagnostic_bars[0], LV_OBJ_FLAG_HIDDEN);
    snprintf(text,
             sizeof(text),
             "%s\n%lld min",
             s_diagnostics.firmware,
             (long long)(s_diagnostics.uptime_seconds / 60));
    label_text_if_changed(s_diagnostic_values[4], text);
    if (s_diagnostics.cpu_valid)
        snprintf(text,
                 sizeof(text),
                 "CPU0 %u%% · CPU1 %u%%",
                 s_diagnostics.cpu_percent[0],
                 s_diagnostics.cpu_percent[1]);
    else
        snprintf(text, sizeof(text), "%s", tr("Mérés indul...", "Starting measurement..."));
    label_text_if_changed(s_diagnostic_values[5], text);
    for (int i = 0; i < 2; i++)
    {
        unsigned total = i ? s_diagnostics.total_psram_kib : s_diagnostics.total_internal_kib;
        unsigned available = i ? s_diagnostics.free_psram_kib : s_diagnostics.free_internal_kib;
        unsigned used = total && available <= total ? (total - available) * 100 / total : 0;
        snprintf(text,
                 sizeof(text),
                 "%u%% · %s %u / %u KiB",
                 used,
                 tr("szabad", "free"),
                 available,
                 total);
        if (!i)
        {
            size_t end = strlen(text);
            snprintf(text + end,
                     sizeof(text) - end,
                     "\nDMA max %u KiB",
                     s_diagnostics.largest_dma_block_bytes / 1024);
        }
        label_text_if_changed(s_diagnostic_values[6 + i], text);
        lv_bar_set_value(s_diagnostic_bars[1 + i], used, LV_ANIM_OFF);
    }
    label_text_if_changed(s_diagnostic_values[8],
                          !rt.ble_enabled ? tr("Kikapcsolva", "Disabled")
                          : rt.ble_master ? "Master (BLE)"
                                          : "Slave (BLE)");
    label_text_if_changed(s_diagnostic_values[9],
                          rt.ble_enabled && rt.ble_address[0] ? rt.ble_address : "-");
    if (rt.ble_enabled && rt.ble_master) {
        snprintf(text, sizeof(text), "ZoleeEnergy\nPIN: %s", s_cfg.data_seed_pin);
        label_text_if_changed(s_diagnostic_values[10], text);
        app_ble_pairing_status_t pairing; app_ble_pairing_status(&pairing);
        snprintf(text, sizeof(text), pairing.seconds ? tr("Párosítás nyitva: %u mp", "Pairing open: %u s")
                                                    : tr("Új párosítás (2 perc)", "New pairing (2 min)"), pairing.seconds);
        if (s_pair_window_label)
        {
            label_text_if_changed(s_pair_window_label, text);
            bg_color_if_changed(lv_obj_get_parent(s_pair_window_label),
                                c(pairing.seconds ? 0x0082FC : 0x284C68), 0);
        }
    } else {
        label_text_if_changed(s_diagnostic_values[10], rt.ble_enabled ?
            (rt.ble_peer[0] ? rt.ble_peer : tr("Keresés...", "Searching...")) : "-");
    }
    if (!rt.ble_enabled)
        snprintf(text, sizeof(text), "-");
    else if (rt.ble_master)
        snprintf(text, sizeof(text), "%u", rt.ble_slaves);
    else if (rt.ble_peer[0])
        snprintf(text, sizeof(text), "%d dBm", rt.ble_rssi);
    else
        snprintf(text, sizeof(text), "-");
    label_text_if_changed(s_diagnostic_values[11], text);
    label_text_if_changed(s_diagnostic_values[12],
                          !rt.ble_enabled      ? tr("Kikapcsolva", "Disabled")
                          : !rt.ble_ready      ? tr("Inicializálás...", "Initializing...")
                          : rt.ble_advertising ? tr("Hirdetés aktív", "Advertising")
                          : rt.ble_scanning    ? tr("Master keresése", "Searching for master")
                          : rt.slave_connected ? tr("Master csatlakoztatva", "Master connected")
                          : rt.ble_master      ? tr("Aktív", "Active")
                                               : tr("Kapcsolódás...", "Connecting..."));
    snprintf(text,
             sizeof(text),
             "%s\n%s",
             rt.data_recept_mode == APP_DATA_SLAVE ? "Bluetooth / master" : "NTP",
             rt.time_synced ? tr("Szinkronizálva", "Synchronized")
                            : tr("Várakozás...", "Waiting..."));
    label_text_if_changed(s_diagnostic_values[13], text);
    for (unsigned i = 0; i < 15; i++)
    {
        if (i >= s_diagnostic_event_count)
        {
            label_text_if_changed(s_diagnostic_log[i],
                                  i == 0 ? tr("Még nincs esemény.", "No events yet.") : "");
            continue;
        }
        const app_event_entry_t *entry = &s_diagnostic_events[s_diagnostic_event_count - 1 - i];
        char stamp[16] = "--:--:--";
        time_t epoch = (time_t)entry->epoch_s;
        struct tm tmv;
        if (epoch >= 1577836800 && localtime_r(&epoch, &tmv))
            strftime(stamp, sizeof(stamp), "%H:%M:%S", &tmv);
        snprintf(text, sizeof(text), "%s  %s", stamp, hu() ? entry->text : entry->text_en);
        label_text_if_changed(s_diagnostic_log[i], text);
        text_color_if_changed(s_diagnostic_log[i],
                              c(entry->level == APP_EVENT_ERROR     ? RED
                                : entry->level == APP_EVENT_WARNING ? ORANGE
                                                                    : TEXT_DARK),
                              0);
    }
}
