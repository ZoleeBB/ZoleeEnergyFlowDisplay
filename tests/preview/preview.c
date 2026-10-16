/* A tényleges firmware UI-jának headless LVGL renderje. Nincs külön mockup-implementáció. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "../../main/app_ui.c"
#include "src/draw/lv_draw_vector_private.h"

static lv_display_t *display;
static uint32_t pixels[1280*800];
static int64_t ticks;
static bool sd_ok=true;
static bool runtime_wifi_connected=true;
static app_stats_snapshot_t fixture;
static time_t epoch;
static lv_point_t touch_point;
static bool touch_pressed;
static int touch_swap_xy,touch_mirror_x,touch_mirror_y;
static void touch_read(lv_indev_t *input,lv_indev_data_t *data){
    (void)input;data->point=touch_point;
    /* Same order as esp_lcd_touch_get_data: native mirrors, then axis swap. */
    if(touch_mirror_x)data->point.x=800-data->point.x;
    if(touch_mirror_y)data->point.y=1280-data->point.y;
    if(touch_swap_xy){int x=data->point.x;data->point.x=data->point.y;data->point.y=x;}
    data->state=touch_pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
static void touch_sample_xy(int x,int y,bool pressed){
    touch_point=(lv_point_t){y,1280-x};touch_pressed=pressed;ticks+=40000;lv_tick_inc(40);lv_timer_handler();
}
static void touch_sample(int x,bool pressed){touch_sample_xy(x,400,pressed);}

time_t host_time(time_t *out){if(out)*out=epoch;return epoch;}
int64_t esp_timer_get_time(void){return ticks;}
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg){assert(cfg->rotation==ESP_LV_ADAPTER_ROTATE_270);assert(cfg->lv_adapter_cfg.task_stack_size==32768);assert(cfg->lv_adapter_cfg.stack_in_psram);touch_swap_xy=cfg->touch_flags.swap_xy;touch_mirror_x=cfg->touch_flags.mirror_x;touch_mirror_y=cfg->touch_flags.mirror_y;assert(touch_swap_xy==1&&touch_mirror_x==0&&touch_mirror_y==1);return display;}
esp_err_t bsp_display_lock(int timeout){(void)timeout;return ESP_OK;}
void bsp_display_unlock(void){}
void bsp_display_backlight_on(void){}
void bsp_display_brightness_set(int value){(void)value;}
int xTaskCreate(void (*f)(void *),const char *n,unsigned st,void *a,unsigned p,void *h)
{(void)f;(void)n;(void)st;(void)a;(void)p;(void)h;return 1;}
void vTaskDelay(unsigned ms){(void)ms;}
void app_state_get_runtime(app_runtime_state_t *out){*out=(app_runtime_state_t){.wifi_connected=runtime_wifi_connected,.mqtt_connected=true};}
bool app_state_get_sample(app_energy_sample_t *out){(void)out;return false;}
void app_stats_get_snapshot(app_stats_snapshot_t *out){*out=fixture;}
void app_stats_get_period(bool month,app_stats_period_t *out){
    memset(out,0,sizeof(*out));out->count=month?31:7;
    for(unsigned i=0;i<out->count;i++){
        snprintf(out->days[i].date,11,"2026-10-%02u",i+1);
        out->days[i].valid=i!=2;out->days[i].totals=fixture.totals;
        out->days[i].totals.production_kwh=18+i%7*2;out->days[i].totals.consumption_kwh=24+i%5;
        out->days[i].totals.grid_import_kwh=5+i%4;
        if(out->days[i].valid){out->totals.grid_import_kwh+=out->days[i].totals.grid_import_kwh;out->totals.grid_export_kwh+=3;}
    }
}

bool app_storage_is_ok(void){return sd_ok;}
static void flush(lv_display_t *disp,const lv_area_t *area,uint8_t *map)
{(void)area;(void)map;lv_display_flush_ready(disp);}
static void count_invalidations(lv_event_t *event){unsigned *count=lv_event_get_user_data(event);++*count;}

static void save_frame(const char *name)
{
    lv_obj_update_layout(lv_screen_active());
    for(int i=0;i<16;i++){ticks+=16000;lv_tick_inc(16);lv_timer_handler();}
    lv_refr_now(display);
    char path[160];snprintf(path,sizeof(path),"%s.ppm",name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1280 800\n255\n");
    for(int i=0;i<1280*800;i++){
        unsigned char rgb[3]={(pixels[i]>>16)&255,(pixels[i]>>8)&255,pixels[i]&255};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
    printf("Rendered %s\n",name);
#if LV_USE_SNAPSHOT
    if(template_is_active()){
        assert(template_raster_bytes<=TEMPLATE_RASTER_BUDGET);
        printf("RASTER %s: %u scaled icon variants, %zu bytes\n",
               name,template_raster_count,template_raster_bytes);
    }
#endif
}

/* A vonal áttetsző, adatfüggően halványul; a mozgó pontok fehérek
 * és a vonalakat a rendszer nem alkotja újra animáció közben. */
static void test_flow_rendering(void){
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];
        if(strcmp(ts(node->spec,"type"),"flow"))continue;
        assert(lv_obj_check_type(node->obj,&lv_obj_class));
        assert(node->flow_path!=NULL);
        assert(node->detail && lv_obj_check_type(node->detail,&lv_obj_class));
        const bool thick=tn(node->spec,"radius",3)>=16;
        assert(lv_obj_get_style_line_opa(node->obj,0)==tn(node->spec,"active_opacity",thick?178:200));
        assert(lv_obj_get_style_line_opa(node->detail,0)==37);
        const lv_obj_t *line=node->obj;
        const lv_vector_path_t *path=node->flow_path;
        const int opacity=lv_obj_get_style_line_opa(node->obj,0);
        for(unsigned frame=0;frame<30;frame++)template_animate(.016f);
        assert(node->obj==line && node->flow_path==path);
        assert(lv_obj_get_style_line_opa(node->obj,0)==opacity);
        for(int d=0;d<4;d++){
            lv_obj_t *dot=node->dots[d];
            assert(lv_obj_get_style_bg_color(dot,0).red==255);
            int diameter=tn(node->spec,"dot_size",tn(node->spec,"radius",3)>=16?14:11);
            if(diameter<3)diameter=3;
            if(diameter>18)diameter=18;
            assert(lv_obj_get_style_border_width(dot,0)==(diameter<=7?1:2));
            assert(lv_obj_get_index(dot)>lv_obj_get_index(node->obj));
        }
        double power=node->power;
        node->power=0;template_flow_tone(node,tc(node->spec,"color",BLUE),false);
        const int inactive=tn(node->spec,"inactive_opacity",thick?72:86);
        assert(lv_obj_get_style_line_opa(node->obj,0)==inactive);
        assert(lv_obj_get_style_line_opa(node->detail,0)==tn(node->spec,"inactive_glow_opacity",16));
        assert(inactive<opacity); /* The active path is stronger; inactive stays visible. */
        assert(opacity-inactive>=50);
        template_animate(.016f);
        for(int d=0;d<4;d++)assert(lv_obj_has_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN));
        node->power=power;
        template_flow_tone(node,tc(node->spec,"color",BLUE),true);
    }
}
/* Nap pálya: napelem -> derékszög -> akku, és helyes irányok. */
static void test_solar_battery_route(void){
    if(strcmp(template_view.id,"09-solar-journey"))return;
    template_node_t *battery=NULL;
    for(unsigned i=0;i<template_node_count;i++)
        if(!strcmp(ts(template_nodes[i].spec,"type"),"flow") &&
           !strcmp(ts(template_nodes[i].spec,"source"),"battery"))battery=&template_nodes[i];
    assert(battery && battery->point_count==3 && battery->flow_path);
    assert(tn(battery->spec,"x",0)+battery->points[0].x==1016);
    assert(tn(battery->spec,"y",0)+battery->points[0].y==473);
    assert(tn(battery->spec,"x",0)+battery->points[1].x==1038);
    assert(battery->points[0].y==battery->points[1].y);
    assert(battery->points[1].x==battery->points[2].x);
    assert(tn(battery->spec,"y",0)+battery->points[2].y==560);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(battery->spec,"reverse")));
    double original=battery->power;app_energy_sample_t original_sample=s_animation_sample;
    const lv_vector_path_t *path=battery->flow_path;
    const double cases[][3]={{0,3,2},{0,-3,2},{8,-3,2},{3,-3,2},{0,0,2}};
    const unsigned branches[]={2,2,1,3,0};
    for(unsigned i=0;i<5;i++){
        app_energy_sample_t state=original_sample;
        state.power_pv_kw=cases[i][0];state.power_bat_kw=cases[i][1];state.power_full_kw=cases[i][2];
        state.power_grid_kw=state.power_full_kw-state.power_pv_kw-state.power_bat_kw;
        template_update(&state,&fixture);
        assert(battery->battery_branches==branches[i]&&battery->flow_path==path);
        assert(lv_array_size(&path->points)==(branches[i]==3?5:3));
        const lv_fpoint_t *start=lv_array_at(&path->points,0);
        assert(start->x==(branches[i]==2?61:0));
        if(branches[i]==3){const lv_fpoint_t *house=lv_array_at(&path->points,3);assert(house->x==61);}
    }
    template_update(&original_sample,&fixture);
    /* Kisütés: az alsó ágon felfelé, majd a házhoz jobbra. */
    battery->power=3;battery->phase=.1f;template_animate(.01f);lv_obj_update_layout(s_screen);
    assert(battery->phase<.1f);
    int x=lv_obj_get_x(battery->dots[0])+3,y=lv_obj_get_y(battery->dots[0])+3;
    assert(x>1038&&x<1077&&y==473);
    battery->phase=.8f;template_animate(.01f);lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(battery->dots[0])+3==1038);
    assert(lv_obj_get_y(battery->dots[0])+3>473);
    /* Hálózati töltés: a háztól balra, majd lefelé az akkuhoz. */
    s_animation_sample.power_pv_kw=0;s_animation_sample.power_full_kw=2;
    s_animation_sample.power_bat_kw=-3;s_animation_sample.power_grid_kw=5;
    battery->power=-3;battery->charge_phase[1]=.1f;template_animate(.01f);lv_obj_update_layout(s_screen);
    assert(battery->charge_phase[1]>.1f);
    assert(lv_obj_get_x(battery->dots[0])+3>1038&&lv_obj_get_y(battery->dots[0])+3==473);
    battery->charge_phase[1]=.9f;template_animate(.01f);lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(battery->dots[0])+3==1038&&lv_obj_get_y(battery->dots[0])+3>540);
    /* PV-töltés a bal oldali ágról; vegyes töltés mindkét ágról. */
    s_animation_sample.power_pv_kw=8;
    battery->charge_phase[0]=.1f;template_animate(.01f);lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(battery->dots[0])+3<1038&&lv_obj_get_y(battery->dots[0])+3==473);
    s_animation_sample.power_pv_kw=3;
    battery->charge_phase[0]=battery->charge_phase[1]=.1f;template_animate(.01f);lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(battery->dots[0])+3<1038);
    assert(lv_obj_get_x(battery->dots[1])+3>1038);
    battery->power=0;template_animate(.01f);lv_obj_update_layout(s_screen);
    for(int d=0;d<4;d++)assert(lv_obj_has_flag(battery->dots[d],LV_OBJ_FLAG_HIDDEN));
    battery->power=original;s_animation_sample=original_sample;
    puts("PASS: battery dots turn to house on discharge, descend from house on grid charge, split mixed charging");
}

/* A valódi LVGL objektumokon ellenőrzött Nap pálya finomítások. */
static void test_theme_readability(void){
    const char *id=template_view.id;
    if(!strcmp(id,"02-energy-paths")){
        template_node_t *hub=NULL;
        for(unsigned i=0;i<template_node_count;i++){
            template_node_t *node=&template_nodes[i];
            if(!strcmp(ts(node->spec,"file"),"flow_hub.png"))hub=node;
            const char *type=ts(node->spec,"type");
            if((!strcmp(type,"label")||!strcmp(type,"value"))&&tn(node->spec,"y",999)<110){
                assert(node->detail);
                assert(lv_color_eq(lv_obj_get_style_text_color(node->detail,0),c(0xFFFFFF)));
            }
            assert(strcmp(type,"box")||tn(node->spec,"w",0)!=1280||tn(node->spec,"h",0)!=800);
        }
        assert(hub);
        for(unsigned i=0;i<template_node_count;i++){
            template_node_t *flow=&template_nodes[i];
            if(strcmp(ts(flow->spec,"type"),"flow"))continue;
            for(int d=0;d<4;d++)assert(lv_obj_get_index(hub->obj)>lv_obj_get_index(flow->dots[d]));
        }
    }
    if(!strcmp(id,"03-instruments")){
        for(unsigned i=0;i<template_node_count;i++){
            template_node_t *node=&template_nodes[i];const char *type=ts(node->spec,"type");
            if((!strcmp(type,"label")||!strcmp(type,"value"))&&tn(node->spec,"y",999)<110)
                assert(lv_color_eq(lv_obj_get_style_text_color(node->obj,0),c(0xFFFFFF)));
            else if(!strcmp(type,"label")||!strcmp(ts(node->spec,"source"),"status")||strstr(ts(node->spec,"source"),"direction"))
                assert(lv_color_eq(lv_obj_get_style_text_color(node->obj,0),c(0x000000)));
        }
    }
}

static void test_solar_dashboard(const app_energy_sample_t *original){
    if(strcmp(template_view.id,"09-solar-journey"))return;
    template_node_t *disc=NULL,*bar=NULL,*grid=NULL,*production=NULL,*comparison=NULL;
    app_energy_sample_t saved_animation=s_animation_sample;
    double saved_capacity=s_cfg.energy.battery_capacity_kwh;
    s_cfg.energy.battery_capacity_kwh=15;
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *n=&template_nodes[i];
        const char *type=ts(n->spec,"type"),*source=ts(n->spec,"source");
        if(!strcmp(type,"solar_disc"))disc=n;
        if(!strcmp(type,"bar")&&!strcmp(source,"soc"))bar=n;
        if(!strcmp(type,"value")&&!strcmp(source,"grid"))grid=n;
        if(tn(n->spec,"y",0)>700&&!strcmp(source,"production_today"))production=n;
        if(tn(n->spec,"y",0)>700&&!strcmp(source,"production_yesterday_pct"))comparison=n;
        if(!strcmp(type,"label")&&(!strcmp(ts(n->spec,"text"),"Napelem")||
                                  !strcmp(ts(n->spec,"text"),"Háztartás"))){
            const char *power_source=!strcmp(ts(n->spec,"text"),"Napelem")?"pv":"home";
            for(unsigned j=0;j<template_node_count;j++){
                template_node_t *p=&template_nodes[j];
                if(strcmp(ts(p->spec,"type"),"value")||strcmp(ts(p->spec,"source"),power_source))continue;
                assert(tn(n->spec,"x",0)*2+tn(n->spec,"w",0)==tn(p->spec,"x",0)*2+tn(p->spec,"w",0));
                assert(lv_obj_get_style_text_align(n->obj,0)==LV_TEXT_ALIGN_CENTER);
            }
        }
        if(!strcmp(type,"label")&&tn(n->spec,"y",0)>=700){
            const char *text=lv_label_get_text(n->obj);
            lv_point_t size;
            lv_text_get_size(&size,text,lv_obj_get_style_text_font(n->obj,0),0,0,1280,LV_TEXT_FLAG_NONE);
            assert(size.x<=tn(n->spec,"w",0));
        }
    }
    assert(disc&&bar&&grid&&production&&comparison);
    app_energy_sample_t s=*original;
    s.sun_position_valid=true;s.sun_altitude_deg=25;s.night_mode=false;
    s_animation_sample=s;
    const double levels[]={2,4,8,16};
    for(unsigned i=0;i<4;i++){
        s_animation_sample.power_pv_kw=levels[i];disc->phase=0;
        template_animate(.1f);
        float expected=i<2?.03f*(i+1):.12f;
        assert(fabsf(disc->phase-expected)<.0001f);
    }
    int last_width=0,last_alpha=256,last_fill=256;
    assert(lv_obj_get_index(disc->detail)<lv_obj_get_index(disc->obj));
    for(int step=0;step<=4;step++){
        disc->phase=step==4?.999f:step*.25f;template_animate(0);
        lv_obj_update_layout(s_screen);
        int width=lv_obj_get_width(disc->detail);
        int alpha=lv_obj_get_style_border_opa(disc->detail,0);
        assert(width>last_width&&alpha<last_alpha);
        int fill=lv_obj_get_style_bg_opa(disc->detail,0);
        assert(fill<last_fill);
        assert(abs(lv_obj_get_x(disc->detail)*2+width-(lv_obj_get_x(disc->obj)*2+tn(disc->spec,"w",40)))<=1);
        if(step==0)assert(width==1&&fill==110);
        last_width=width;last_alpha=alpha;last_fill=fill;
    }
    assert(last_alpha==0&&lv_obj_get_style_shadow_opa(disc->detail,0)==0);
    s_animation_sample.power_pv_kw=0;disc->phase=.5f;template_animate(.1f);
    assert(disc->phase==.5f&&lv_obj_get_style_border_opa(disc->detail,0)==0);
    s_animation_sample.night_mode=true;s_animation_sample.sun_altitude_deg=-10;
    int disc_x=lv_obj_get_x(disc->obj),disc_y=lv_obj_get_y(disc->obj);
    template_animate(.1f);
    assert(lv_obj_get_style_border_width(disc->detail,0)==0);
    assert(lv_obj_get_x(disc->obj)==disc_x&&lv_obj_get_y(disc->obj)==disc_y);
    const int socs[]={0,29,30,47,49,50,70,71,100};
    const uint32_t soc_colors[]={RED,RED,ORANGE,ORANGE,ORANGE,YELLOW,YELLOW,GREEN,GREEN};
    for(unsigned i=0;i<sizeof(socs)/sizeof(socs[0]);i++){
        s.soc_bat_pct=socs[i];template_update(&s,&fixture);
        assert(lv_bar_get_value(bar->obj)==socs[i]);
        assert(lv_color_eq(lv_obj_get_style_bg_color(bar->obj,LV_PART_INDICATOR),c(soc_colors[i])));
        assert(tn(bar->spec,"x",0)==1012&&tn(bar->spec,"y",0)==627);
    }
    const double powers[]={0,.0004,-.0004,1,-1};
    for(unsigned i=0;i<5;i++){
        s.power_grid_kw=powers[i];template_update(&s,&fixture);
        uint32_t expected=tc(grid->spec,i<3?"zero_color":i==3?"positive_color":"negative_color",0);
        assert(lv_color_eq(lv_obj_get_style_text_color(grid->obj,0),c(expected)));
    }
    app_stats_snapshot_t *stats=malloc(sizeof(*stats));assert(stats);*stats=fixture;
    stats->totals.production_kwh=19;stats->totals.grid_export_kwh=2;
    stats->comparison_ready=true;stats->yesterday_comparison_partial=false;
    stats->comparison_today_totals.production_kwh=15;stats->comparison_yesterday_totals.production_kwh=10;
    template_update(&s,stats);
    char expected[128];template_text(expected,"production_today",&s,stats);
    assert(!strcmp(lv_label_get_text(production->obj),expected));
    assert(strstr(lv_label_get_text(comparison->obj),"+50")!=NULL);
    s.night_mode=original->night_mode;s.sun_altitude_deg=s.night_mode?-25:25;
    s.sunrise_minute=420;s.sunset_minute=1080;s.sun_azimuth_deg=220;
    s.power_grid_kw=0;s.soc_bat_pct=25;
    template_update(&s,stats);s_animation_sample=s;
    disc->phase=.35f;template_animate(0);
    save_frame(s.night_mode?"solar-details-dark":"solar-details-light");
    free(stats);
    s_cfg.energy.battery_capacity_kwh=saved_capacity;s_animation_sample=saved_animation;
    template_update(original,&fixture);
    puts("PASS: solar ripple growth/fade, 8kW speed cap, zero/night, SOC colors, idle grid and daily production");
}

/* Régi, SD-n maradt v10-es sablon firmware-szintű helyreállítása. */
static void test_legacy_solar_battery_route(void){
    if(strcmp(template_view.id,"09-solar-journey"))return;
    cJSON *old_doc=cJSON_Parse("{\"version\":10}");
    cJSON *old_spec=cJSON_Parse("{\"type\":\"flow\",\"source\":\"battery\",\"curve\":\"bezier\",\"points\":[{\"x\":40,\"y\":1},{\"x\":27,\"y\":1},{\"x\":2,\"y\":29},{\"x\":2,\"y\":86}]}");
    assert(old_doc && old_spec);
    cJSON *saved=template_view.document;
    template_view.document=old_doc;
    template_node_t old_node={0};
    old_node.spec=old_spec;
    template_path(&old_node,old_spec);
    assert(old_node.legacy_battery_corner && old_node.point_count==3);
    assert(old_node.points[0].x==-20 && old_node.points[0].y==1);
    assert(old_node.points[1].x==2 && old_node.points[1].y==1);
    assert(old_node.points[2].x==2 && old_node.points[2].y==88);
    template_flow_path_create(&old_node);
    assert(old_node.flow_path);
    lv_vector_path_delete(old_node.flow_path);
    template_view.document=saved;
    cJSON_Delete(old_doc);
    cJSON_Delete(old_spec);
}

/* Exercise actual line objects and dot positions, including reverse/zero/cap. */
static void test_template_motion(void){
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];
        if(strcmp(ts(node->spec,"type"),"flow"))continue;
        if(!strcmp(ts(node->spec,"curve"),"bezier")){
            unsigned controls=cJSON_GetArraySize(cJSON_GetObjectItem(node->spec,"points"));
            assert(node->point_count==(controls==4?97:193));
            const cJSON *last=cJSON_GetArrayItem(cJSON_GetObjectItem(node->spec,"points"),controls-1);
            assert(fabs(node->points[node->point_count-1].x-tn(last,"x",0))<.01);
            assert(fabs(node->points[node->point_count-1].y-tn(last,"y",0))<.01);
        }
        double original=node->power;
        bool reversed=cJSON_IsTrue(cJSON_GetObjectItem(node->spec,"reverse"));
        node->phase=.5f;node->power=7;template_animate(.02f);float capped=node->phase;
        assert(reversed?capped<.5f:capped>.5f);
        node->phase=.5f;node->power=15;template_animate(.02f);assert(fabs(node->phase-capped)<.00001);
        node->phase=.5f;node->power=-7;template_animate(.02f);
        assert(reversed?node->phase>.5f:node->phase<.5f);
        node->phase=.5f;node->power=0;template_animate(.02f);
        for(int d=0;d<4;d++)assert(lv_obj_has_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN));
        node->power=original;
    }
    template_animate(.02f);lv_obj_update_layout(s_screen);
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];if(strcmp(ts(node->spec,"type"),"flow")||fabs(node->power)<.001)continue;
        int dot_count=tn(node->spec,"dot_count",4);
        if(dot_count<1)dot_count=1;
        if(dot_count>4)dot_count=4;
        for(int d=0;d<4;d++){
            if(d>=dot_count){
                assert(lv_obj_has_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN));
                continue;
            }
            float x=lv_obj_get_x(node->dots[d])+lv_obj_get_width(node->dots[d])/2-tn(node->spec,"x",0),
                  y=lv_obj_get_y(node->dots[d])+lv_obj_get_height(node->dots[d])/2-tn(node->spec,"y",0),distance=10000;
            for(unsigned k=1;k<node->point_count;k++){
                float ax=node->points[k-1].x,ay=node->points[k-1].y,dx=node->points[k].x-ax,dy=node->points[k].y-ay;
                float sq=dx*dx+dy*dy,t=sq?((x-ax)*dx+(y-ay)*dy)/sq:0;t=fmaxf(0,fminf(1,t));
                distance=fminf(distance,hypotf(x-ax-t*dx,y-ay-t*dy));
            }
            /* A Nap pálya akku pontjai a már kirajzolt PV–ház vonalon is járnak. */
            if(!strcmp(template_view.id,"09-solar-journey")&&!strcmp(ts(node->spec,"source"),"battery")){
                for(unsigned j=0;j<template_node_count;j++){
                    template_node_t *pv=&template_nodes[j];
                    if(strcmp(ts(pv->spec,"type"),"flow")||strcmp(ts(pv->spec,"source"),"pv_home"))continue;
                    float left=tn(pv->spec,"x",0)+pv->points[0].x-tn(node->spec,"x",0);
                    float right=tn(pv->spec,"x",0)+pv->points[pv->point_count-1].x-tn(node->spec,"x",0);
                    float line_y=tn(pv->spec,"y",0)+pv->points[0].y-tn(node->spec,"y",0);
                    distance=fminf(distance,hypotf(x-fmaxf(left,fminf(right,x)),y-line_y));
                }
            }
            assert(distance<2); /* integer display rounding only */
        }
    }
}

static void test_template_indicators(const app_energy_sample_t *sample){
    app_energy_sample_t state=*sample;
    for(int direction=-1;direction<=1;direction+=2){
        state.power_grid_kw=direction*2;state.power_bat_kw=direction*2;
        template_update(&state,&fixture);
        for(unsigned i=0;i<template_node_count;i++){
            template_node_t *node=&template_nodes[i];
            const char *type=ts(node->spec,"type"),*source=ts(node->spec,"source");
            if(!cJSON_IsTrue(cJSON_GetObjectItem(node->spec,"shadow"))&&(*ts(node->spec,"positive_color")||*ts(node->spec,"negative_color"))&&(!strcmp(source,"grid")||!strcmp(source,"battery")||strstr(source,"direction"))){
                uint32_t expected=tc(node->spec,direction<0?"negative_color":"positive_color",tc(node->spec,"color",BLUE));
                if(!strcmp(type,"value"))assert(lv_color_eq(lv_obj_get_style_text_color(node->obj,0),c(expected)));
                if(!strcmp(type,"flow")){
                    assert(lv_color_eq(lv_obj_get_style_line_color(node->obj,0),c(expected)));
                    for(int d=0;d<4;d++)assert(lv_color_eq(lv_obj_get_style_border_color(node->dots[d],0),c(expected)));
                }
            }
            if(*ts(node->spec,"outline_color")){
                uint32_t outline=tc(node->spec,"outline_color",0);
                if(!strcmp(type,"bar"))assert(lv_obj_get_style_border_width(node->obj,LV_PART_INDICATOR)==1);
                if(!strcmp(type,"gauge")){assert(node->detail);assert(lv_color_eq(lv_obj_get_style_arc_color(node->detail,LV_PART_INDICATOR),c(outline)));assert(lv_arc_get_value(node->detail)==lv_arc_get_value(node->obj));}
            }
            if(strcmp(type,"gauge"))continue;
            uint32_t expected=tc(node->spec,direction<0?"negative_color":"positive_color",tc(node->spec,"color",BLUE));
            assert(lv_color_eq(lv_obj_get_style_arc_color(node->obj,LV_PART_INDICATOR),c(expected)));
        }
    }
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];
        if(strcmp(ts(node->spec,"type"),"gauge")||strcmp(ts(node->spec,"source"),"grid")||!tn(node->spec,"animate_ms",0))continue;
        state.power_grid_kw=2;template_update(&state,&fixture);
        for(int k=0;k<24;k++){ticks+=16000;lv_tick_inc(16);lv_timer_handler();}
        assert(lv_arc_get_value(node->obj)==20);
        state.power_grid_kw=5;template_update(&state,&fixture);assert(lv_arc_get_value(node->obj)==20);
        for(int k=0;k<6;k++){ticks+=16000;lv_tick_inc(16);lv_timer_handler();assert(lv_arc_get_value(node->obj)==lv_arc_get_value(node->detail));}
        int middle=lv_arc_get_value(node->obj);assert(middle>20&&middle<50);
        state.power_grid_kw=1;template_update(&state,&fixture);assert(lv_arc_get_value(node->obj)==middle);
        for(int k=0;k<24;k++){ticks+=16000;lv_tick_inc(16);lv_timer_handler();}
        assert(lv_arc_get_value(node->obj)==10&&lv_arc_get_value(node->detail)==10);
    }
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];
        if(strcmp(ts(node->spec,"type"),"pulse"))continue;
        template_animate(.1f);int first=lv_obj_get_style_bg_opa(node->obj,0);
        template_animate(.3f);assert(first!=lv_obj_get_style_bg_opa(node->obj,0));
    }
    template_chart_time=0;template_update(sample,&fixture);
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];const char *kind=ts(node->spec,"kind");
        if(!strcmp(ts(node->spec,"type"),"chart")&&(!strcmp(kind,"grid")||!strcmp(kind,"battery")))
            assert(lv_label_get_text(node->axis[2])[0]=='-');
    }
}

static void test_weather_visibility(const app_energy_sample_t *sample){
    for(unsigned mask=0;mask<16;mask++){
        app_energy_sample_t state=*sample;
        state.temperature_valid=!!(mask&1);state.wind_speed_valid=!!(mask&2);
        state.sun_radiation_valid=!!(mask&4);state.precipitation_valid=!!(mask&8);
        template_update(&state,&fixture);
        for(unsigned i=0;i<template_node_count;i++){
            template_node_t *n=&template_nodes[i];const char *source=ts(n->spec,"source");
            const char *type=ts(n->spec,"type");
            bool sun_source=!strcmp(source,"sun_azimuth")||!strcmp(source,"sun_altitude")||
                !strcmp(source,"sunrise")||!strcmp(source,"sunset")||!strcmp(source,"sun_direction");
            bool capacity_source=!strcmp(source,"battery_capacity")||!strcmp(source,"battery_energy")||
                !strcmp(source,"battery_summary");
            bool visible=(!strcmp(type,"solar_disc")||!strcmp(type,"solar_arc")||sun_source)?
                state.sun_position_valid:
                (!strcmp(type,"weather")||!strcmp(source,"weather_label"))?weather_available(&state):
                !strcmp(source,"temperature")?state.temperature_valid:
                !strcmp(source,"wind")?state.wind_speed_valid:
                capacity_source?s_cfg.energy.battery_capacity_kwh>0:true;
            assert(lv_obj_has_flag(n->obj,LV_OBJ_FLAG_HIDDEN)==!visible);
            if(n->detail)assert(lv_obj_has_flag(n->detail,LV_OBJ_FLAG_HIDDEN)==!visible);
        }
    }
    template_update(sample,&fixture);
}
static void test_template_weather(const app_energy_sample_t *sample){
    static const char *keys[]={"sunny","partly_cloudy","cloudy","rain","snow","night"};
    static const struct {double pv,temp;bool sun,precip,night;weather_kind_t expected;} cases[]={
        {0,20,true,false,false,WEATHER_SUNNY},
        {2,20,false,false,false,WEATHER_PARTLY_CLOUDY},
        {0,20,false,false,false,WEATHER_CLOUDY},
        {0,20,false,true,false,WEATHER_RAIN},
        {0,-6,false,true,false,WEATHER_SNOW},
        {0,20,false,false,true,WEATHER_NIGHT},
        {1,20,false,false,false,WEATHER_CLOUDY},
        {1.001,20,false,false,false,WEATHER_PARTLY_CLOUDY},
        {5,20,false,false,false,WEATHER_PARTLY_CLOUDY},
        {5.001,20,false,false,false,WEATHER_SUNNY},
        {6,20,false,true,false,WEATHER_RAIN},
        {6,-6,false,true,false,WEATHER_SNOW},
        {6,20,false,false,true,WEATHER_NIGHT}
    };
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];const cJSON *files=cJSON_GetObjectItem(node->spec,"files");
        if(strcmp(ts(node->spec,"type"),"weather")||!files)continue;
        for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);k++){
            app_energy_sample_t state=*sample;state.night_mode=cases[k].night;state.has_precipitation=cases[k].precip;
            state.has_sun_radiation=cases[k].sun;state.power_pv_kw=cases[k].pv;state.temperature_c=cases[k].temp;
            template_update(&state,&fixture);assert(weather_kind(&state)==cases[k].expected);
            bool matched=false;
            for(unsigned a=0;a<template_view.asset_count;a++)if(!strcmp(ts(files,keys[cases[k].expected]),template_view.assets[a].name)){
                const void *actual=lv_image_get_src(node->obj);bool right=actual==&template_images[a];
#if LV_USE_SNAPSHOT
                for(unsigned r=0;r<template_raster_count;r++)if(template_rasters[r].asset==a&&actual==&template_rasters[r].image){
                    assert(template_rasters[r].image.header.w==(unsigned)tn(node->spec,"w",128));
                    assert(template_rasters[r].image.header.h==(unsigned)tn(node->spec,"h",128));right=true;
                }
                unsigned before=template_raster_count;size_t bytes=template_raster_bytes;
                template_update(&state,&fixture);assert(before==template_raster_count&&bytes==template_raster_bytes);
#endif
                assert(right);matched=true;
            }
            assert(matched);
        }
    }
    template_update(sample,&fixture);
}

/* Ismert, egyenként 2 kW magas rétegek, a plot tényleges koordinátáival.
 * Nem kötjük a tesztet a demógörbe tetszőleges képpontjához. */
static void test_stacked_charts(void)
{
    static app_stats_snapshot_t test_data;
    for(int i=0;i<176;i++){
        test_data.slots[i]=(app_stats_slot_t){.valid=true,.sample_count=1,
            .split=app_energy_split(6,2,-2),.chart_gap=i>=60 && i<=84};
    }
    app_chart_update(s_chart1,&test_data,8,877);
    for(int i=0;i<176;i++)test_data.slots[i].split=app_energy_split(2,6,2);
    app_chart_update(s_chart2,&test_data,8,877);
    save_frame("stacked-chart-regression");
    lv_obj_t *charts_to_test[]={s_chart1,s_chart2};
    /* Alsó/középső/felső réteg domináns RGB csatornája. */
    const int channels[2][3]={{8,16,0},{16,8,0}};
    for(int chart=0;chart<2;chart++){
        lv_area_t area;lv_obj_get_content_coords(charts_to_test[chart],&area);
        int x=area.x1+(area.x2-area.x1)*120/288;
        for(int layer=0;layer<3;layer++){
            int y=area.y2-(area.y2-area.y1)*(2*layer+1)/8;
            uint32_t pixel=pixels[y*1280+x];
            int dominant=(pixel>>channels[chart][layer])&255;
            int other=(pixel>>(channels[chart][layer]==16?8:16))&255;
            assert(dominant>other+5);
        }
        /* Nincs implicit kontúrkitöltés a rétegek fölött, a hiányzó
         * időablakban vagy a jövőben. Egyik pont sem rácsvonalra esik. */
        int y=area.y2-(area.y2-area.y1)*7/8;
        assert((pixels[y*1280+x]&0xffffff)==0xffffff);
        y=area.y2-(area.y2-area.y1)*3/8;
        int gap_x=area.x1+(area.x2-area.x1)*70/288;
        int future_x=area.x1+(area.x2-area.x1)*230/288;
        assert((pixels[y*1280+gap_x]&0xffffff)==0xffffff);
        assert((pixels[y*1280+future_x]&0xffffff)==0xffffff);
    }
    charts(&fixture);
}

static void test_signed_and_soc_charts(void){
    static app_stats_snapshot_t samples;
    for(int i=0;i<220;i++)samples.slots[i]=(app_stats_slot_t){.valid=true,.soc_valid=true,
        .grid_kw=i<96?2:-2,.battery_kw=i<96?2:-2,.soc_pct=60,.chart_gap=i>=140&&i<=165};
    lv_obj_t *parent=lv_obj_create(s_screen);lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent,1280,800);lv_obj_set_style_bg_color(parent,c(0xFFFFFF),0);lv_obj_set_style_bg_opa(parent,255,0);
    for(int kind=APP_CHART_GRID;kind<=APP_CHART_SOC;kind++){
        lv_obj_t *chart=app_chart_create_kind(parent,kind,false,0xFF0066,0xFF9900,0x0099FF);
        lv_obj_set_pos(chart,100,100);lv_obj_set_size(chart,600,200);
        app_chart_update(chart,&samples,4,1100);save_frame("signed-soc-regression");
        lv_area_t area;lv_obj_get_content_coords(chart,&area);
        int positive_x=area.x1+(area.x2-area.x1)*40/288;
        int negative_x=area.x1+(area.x2-area.x1)*115/288;
        int gap_x=area.x1+(area.x2-area.x1)*152/288;
        int y=kind==APP_CHART_SOC?area.y2-40:area.y1+70;
        assert((pixels[y*1280+positive_x]&0xffffff)!=0xffffff);
        if(kind==APP_CHART_BATTERY){
            uint32_t positive=pixels[y*1280+positive_x],negative=pixels[(area.y1+130)*1280+negative_x];
            assert(((positive>>8)&255) > ((negative>>8)&255));
        }
        if(kind!=APP_CHART_SOC){
            assert((pixels[(area.y1+130)*1280+negative_x]&0xffffff)!=0xffffff);
            assert((pixels[y*1280+negative_x]&0xffffff)==0xffffff);
        }
        assert((pixels[y*1280+gap_x]&0xffffff)==0xffffff);
        lv_obj_delete(chart);
    }
    lv_obj_delete(parent);
    puts("PASS: positive/negative fills, zero axis and SOC gaps in actual rendered pixels");
}

/* A kis órás diagram mind a 23 belső rácsvonala egyetlen pixel széles. */
static void test_hourly_chart_grid(void){
    lv_obj_t *parent=lv_obj_create(s_screen);lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent,1280,800);lv_obj_set_style_bg_color(parent,c(0xFFFFFF),0);lv_obj_set_style_bg_opa(parent,255,0);
    lv_obj_t *chart=app_chart_create_kind(parent,APP_CHART_SOLAR_HOURLY,false,0x23B779,0xFFCB35,0);
    lv_obj_set_pos(chart,100,100);lv_obj_set_size(chart,258,65);
    save_frame("solar-grid-regression");
    lv_area_t area;lv_obj_get_content_coords(chart,&area);
    int y=area.y1+10;
    uint32_t reference=0;
    for(int hour=1;hour<24;hour++){
        int x=(int)floorf(area.x1+(area.x2-area.x1)*hour/24.f);
        uint32_t pixel=pixels[y*1280+x]&0xffffff;
        if(hour==1)reference=pixel;
        assert(pixel==reference&&pixel!=0xffffff);
        assert((pixels[y*1280+x-1]&0xffffff)==0xffffff);
        assert((pixels[y*1280+x+1]&0xffffff)==0xffffff);
    }
    /* A középső skála vezetővonala vastagabb az óravonalaknál. */
    int middle=(int)floorf((area.y1+area.y2)/2.f),x=area.x1+5;
    assert((pixels[middle*1280+x]&0xffffff)!=0xffffff);
    assert((pixels[(middle-1)*1280+x]&0xffffff)!=0xffffff ||
           (pixels[(middle+1)*1280+x]&0xffffff)!=0xffffff);
    assert((pixels[(middle+2)*1280+x]&0xffffff)==0xffffff);
    lv_obj_delete(parent);
    puts("PASS: identical one-pixel hourly grid lines and stronger horizontal scale lines in rendered pixels");
}

int main(void)
{
    /* Tegnap 0 értéknél sem szabad téves hiányjelet kiírni. */
    char cmp_test[32];
    fmt_cmp(cmp_test,sizeof(cmp_test),0,0);assert(!strcmp(cmp_test,"0%"));
    fmt_cmp(cmp_test,sizeof(cmp_test),3,0);assert(strstr(cmp_test,"új")!=NULL);
    fmt_cmp(cmp_test,sizeof(cmp_test),6,3);assert(strstr(cmp_test,"+100")!=NULL);
    fmt_compared(cmp_test,sizeof(cmp_test),6,3,true);
    assert(cmp_test[0]=='~' && strstr(cmp_test,"+100")!=NULL);
    fmt_compared(cmp_test,sizeof(cmp_test),6,3,false);
    assert(cmp_test[0]!='~');
    printf("Template geometry storage: %zu bytes\n",sizeof(template_nodes));
    setenv("TZ","UTC0",1);tzset();
    struct tm t={.tm_year=126,.tm_mon=9,.tm_mday=5,.tm_hour=14,.tm_min=37};epoch=mktime(&t);
    lv_init();display=lv_display_create(1280,800);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display,flush);
    assert(app_ui_init()==ESP_OK);
    runtime_wifi_connected=false;
    app_ui_show_boot(APP_BOOT_WIFI,NULL);
    animation_tick(NULL);
    assert(s_error_pulse==NULL); /* Initial connection is progress, not a fault. */
    app_ui_show_boot(APP_BOOT_NTP,NULL);
    animation_tick(NULL);
    assert(s_view==VIEW_WIFI_ERROR && s_error_pulse!=NULL);
    assert(lv_obj_get_style_opa(s_error_pulse,0)==LV_OPA_COVER);
    assert(lv_obj_get_style_outline_width(s_error_pulse,0)==0);
    assert(lv_obj_get_style_shadow_width(s_error_pulse,0)==0);
    assert(lv_obj_get_style_border_opa(s_error_pulse,0)>=175);
    runtime_wifi_connected=true;
    app_ui_show_boot(APP_BOOT_NTP,NULL);
    lv_obj_t *text_probe=lbl(s_screen,"Probe",10,10,APP_FONT_14,0xffffff);
    opa_if_changed(text_probe,LV_OPA_COVER,0);
    lv_obj_update_layout(s_screen);lv_refr_now(display);
    unsigned invalidations=0;
    lv_display_add_event_cb(display,count_invalidations,LV_EVENT_INVALIDATE_AREA,&invalidations);
    label_text_if_changed(text_probe,"Probe");lv_obj_update_layout(s_screen);lv_refr_now(display);
    assert(invalidations==0);
    text_color_if_changed(text_probe,c(0xffffff),0);opa_if_changed(text_probe,LV_OPA_COVER,0);
    lv_obj_update_layout(s_screen);lv_refr_now(display);assert(invalidations==0);
    label_text_if_changed(text_probe,"Changed");lv_obj_update_layout(s_screen);lv_refr_now(display);
    assert(invalidations>0);
    lv_display_remove_event_cb_with_user_data(display,count_invalidations,&invalidations);lv_obj_delete(text_probe);
    puts("PASS: unchanged labels, colours and opacity do not invalidate the screen; changed text still refreshes");
    puts("PASS: initial Wi-Fi wait stays on boot page; later loss uses border-only error pulse without outline/shadow");
    app_config_t config={0};
    snprintf(config.project.title,sizeof(config.project.title),"Otthoni energia");
    snprintf(config.project.language,sizeof(config.project.language),"hu");
    snprintf(config.project.currency,sizeof(config.project.currency),"Ft");
    config.energy.electricity_purchase_price=65;config.energy.feed_in_price=5;
    config.energy.battery_capacity_kwh=15;
    config.energy.co2_factor_kg_per_kwh=.381;config.energy.tree_absorption_kg_per_year=22;
    app_ui_set_config(&config);s_started=true;
    fixture.totals=(app_energy_totals_t){.production_kwh=17.3,.consumption_kwh=11.8,.grid_import_kwh=2.1,.grid_export_kwh=8.4,.battery_charge_kwh=3.2,.battery_discharge_kwh=1.4,.pv_home_kwh=8.3,.pv_export_kwh=5.8,.pv_stored_kwh=3.2,.grid_home_kwh=2.1,.battery_home_kwh=1.4};
    fixture.completed_interval_totals=fixture.totals;
    fixture.yesterday_completed_interval_totals=(app_energy_totals_t){.production_kwh=15.4,.consumption_kwh=12.8,.grid_import_kwh=3.1,.grid_export_kwh=6.4,.battery_charge_kwh=3.5,.battery_discharge_kwh=1.1};
    fixture.has_yesterday=true;
    fixture.comparison_ready=true;
    fixture.comparison_today_totals=fixture.totals;
    fixture.comparison_yesterday_totals=fixture.yesterday_completed_interval_totals;
    for(int i=0;i<176;i++){
        float daylight=fmaxf(0,sinf((i-72)*3.14159265f/144));
        fixture.slots[i]=(app_stats_slot_t){.valid=true,.sample_count=60,.soc_pct=20+daylight*80,.soc_valid=true,.pv_kw=daylight*7,
            .load_kw=1.5f+.7f*sinf(i*.13f),.grid_kw=1.4f*sinf(i*.05f),.battery_kw=2.f*cosf(i*.04f)};
        fixture.slots[i].split=app_energy_split(fixture.slots[i].pv_kw,fixture.slots[i].load_kw,fixture.slots[i].battery_kw);
        const app_energy_split_t *split=&fixture.slots[i].split;
        fixture.slots[i].energy=(app_energy_totals_t){.production_kwh=fixture.slots[i].pv_kw/12.,
            .pv_home_kwh=split->pv_home/12.,.pv_export_kwh=split->pv_export/12.,.pv_stored_kwh=split->pv_stored/12.};
    }
    app_energy_sample_t sample={.power_pv_kw=7,.power_full_kw=2.1,.power_grid_kw=-4.5,.power_bat_kw=.4,
        .temperature_valid=true,.wind_speed_valid=true,.sun_radiation_valid=true,.precipitation_valid=true,
        .soc_bat_pct=87.4,.temperature_c=18.2,.wind_speed_kmh=12,.battery_temperature_c=35,.has_sun_radiation=true,.status=1};
    build_dashboard(false);s_last_night=false;update(&sample,&fixture);save_frame("hu-day");
    test_stacked_charts();
    assert(f_pv.phase>0.f);
    sd_ok=false;sample.status=3;sample.soc_bat_pct=19.9;update(&sample,&fixture);save_frame("hu-island-sd-error");
    sd_ok=true;sample.status=2;sample.power_bat_kw=-3.5;sample.soc_bat_pct=100;update(&sample,&fixture);save_frame("hu-preparing");
    for(unsigned mask=0;mask<16;mask++){
        app_energy_sample_t optional=sample;
        optional.temperature_valid=!!(mask&1);optional.wind_speed_valid=!!(mask&2);
        optional.sun_radiation_valid=!!(mask&4);optional.precipitation_valid=!!(mask&8);
        update(&optional,&fixture);
        assert(lv_obj_has_flag(lv_obj_get_parent(s_weather_icon),LV_OBJ_FLAG_HIDDEN)==!weather_available(&optional));
        assert(lv_obj_has_flag(s_weather_temp,LV_OBJ_FLAG_HIDDEN)==!optional.temperature_valid);
        assert(lv_obj_has_flag(s_wind_speed,LV_OBJ_FLAG_HIDDEN)==!optional.wind_speed_valid);
        assert(lv_obj_has_flag(lv_obj_get_parent(s_wind_icon),LV_OBJ_FLAG_HIDDEN)==!optional.wind_speed_valid);
    }
    update(&sample,&fixture);
    sample.status=4;sample.battery_temperature_c=51;update(&sample,&fixture);save_frame("hu-system-error");
    sample.status=1;sample.night_mode=true;sample.power_pv_kw=0;sample.has_sun_radiation=false;
    snprintf(config.project.language,sizeof(config.project.language),"en");app_ui_set_config(&config);
    build_dashboard(true);s_last_night=true;update(&sample,&fixture);save_frame("en-night");
    error_screen(VIEW_WIFI_ERROR,"No Wi-Fi connection","Waiting for connection to recover...",false);save_frame("en-wifi-error");
    snprintf(config.project.language,sizeof(config.project.language),"hu");
    sample.night_mode=false;
    config.data_recept_mode=APP_DATA_HTTP;config.http.data_timeout_seconds=30;
    app_ui_set_config(&config);
    app_ui_show_boot(APP_BOOT_MQTT,NULL);save_frame("http-boot-hu");
    app_runtime_state_t rt={.data_recept_mode=APP_DATA_HTTP,.wifi_connected=true,.http_ready=true,.http_ready_monotonic_us=1000000};
    render_runtime(rt,sample,false,30000000);assert(s_view==VIEW_WAIT_DATA);save_frame("http-wait-hu");
    render_runtime(rt,sample,false,31000000);assert(s_view==VIEW_DATA_TIMEOUT);save_frame("http-timeout-hu");
    rt.wifi_connected=false;
    render_runtime(rt,sample,false,31000000);assert(s_view==VIEW_WIFI_ERROR);
    rt.wifi_connected=true;rt.has_valid_sample=true;rt.last_valid_packet_monotonic_us=31000000;
    s_stats_snapshot=fixture;app_stats_get_period(false,&s_week_period);app_stats_get_period(true,&s_month_period);
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);save_frame("http-recovered-hu");
    snprintf(config.project.language,sizeof(config.project.language),"en");app_ui_set_config(&config);
    render_runtime(rt,sample,true,61000000);assert(s_view==VIEW_DATA_TIMEOUT);save_frame("http-timeout-en");
    test_signed_and_soc_charts();
    test_hourly_chart_grid();
    app_diagnostics_capture(&s_diagnostics);s_diagnostics_requested=true;
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DIAGNOSTICS);save_frame("diagnostics-common");
    s_diagnostics_requested=false;render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);
    if(getenv("ZOLEE_TEMPLATE_ROOT")){
        assert(app_templates_init()==ESP_OK);
        app_template_info_t list[APP_TEMPLATE_MAX];char active[33];
        unsigned count=app_templates_list(list,APP_TEMPLATE_MAX,active);assert(count==10);
        sample.power_pv_kw=7;sample.power_bat_kw=-2.5;sample.power_grid_kw=-2.4;sample.power_full_kw=2.1;
        sample.soc_bat_pct=74;sample.status=1;sample.temperature_c=-6;sample.has_sun_radiation=true;
        snprintf(config.project.language,sizeof(config.project.language),"hu");app_ui_set_config(&config);
        for(unsigned i=1;i<count;i++){
            assert(app_templates_select(list[i].id)==ESP_OK);
            assert(app_templates_has_pending(template_pending.generation));
            assert(template_loading_frame());assert(s_view==VIEW_THEME_LOADING);
            save_frame("theme-loading");
            template_prepare();
            assert(!app_templates_has_pending(template_pending.generation));
            for(int night=0;night<2;night++){
                sample.night_mode=night;rt.wifi_connected=true;
                render_runtime(rt,sample,true,31000000);assert(template_is_active());assert(s_view==VIEW_DASHBOARD);
                for(unsigned asset=0;asset<template_view.asset_count;asset++)
                    assert(strncmp(template_view.assets[asset].name,"preview-",8));
                const cJSON *scene=cJSON_GetObjectItem(template_view.document,night?"dark":"light");
                if(!strcmp(template_view.id,"03-instruments")||!strcmp(template_view.id,"07-energy-ledger")){
                    const cJSON *layer;
                    cJSON_ArrayForEach(layer,cJSON_GetObjectItem(scene,"elements"))
                        assert(strcmp(ts(layer,"type"),"box")||tn(layer,"w",0)!=1280||tn(layer,"h",0)!=800);
                }
                uint32_t expected_bg=tc(scene,"background",0);
                if(!strcmp(template_view.id,"04-daily-balance"))expected_bg=night?0x1B2E38:0xE5F1ED;
                if(!strcmp(template_view.id,"05-calm-home"))expected_bg=night?0x223432:0xE8F0E7;
                if(!strcmp(template_view.id,"06-energy-orbit"))expected_bg=night?0x1B263C:0xE8EEF7;
                if(!strcmp(template_view.id,"08-statistics"))expected_bg=night?0x1B2B38:0xE8F2EE;
                assert(lv_color_eq(lv_obj_get_style_bg_color(s_screen,0),c(expected_bg)));
                test_weather_visibility(&sample);test_template_weather(&sample);test_template_motion();test_template_indicators(&sample);test_flow_rendering();test_solar_battery_route();test_legacy_solar_battery_route();test_theme_readability();test_solar_dashboard(&sample);
                if(!strcmp(list[i].id,"01-live-home")){
                    bool consumption_found=false;
                    for(unsigned node_id=0;node_id<template_node_count;node_id++){
                        template_node_t *n=&template_nodes[node_id];
                        if(strcmp(ts(n->spec,"type"),"value")||
                           strcmp(ts(n->spec,"source"),"consumption_today")||
                           cJSON_IsTrue(cJSON_GetObjectItem(n->spec,"shadow")))continue;
                        consumption_found=true;
                        uint32_t expected=night?0xE66B6B:0xFF1744;
                        assert(lv_color_eq(lv_obj_get_style_text_color(n->obj,0),c(expected)));
                    }
                    assert(consumption_found);
                }
                if(!night&&!strcmp(list[i].id,"01-live-home")){
                    /* Warning recolouring must never turn temperature shadows red. */
                    app_energy_sample_t hot=sample;hot.battery_temperature_c=51;
                    template_update(&hot,&fixture);
                    bool temperature_shadow=false;
                    for(unsigned n=0;n<template_node_count;n++){
                        template_node_t *node=&template_nodes[n];
                        if(cJSON_IsTrue(cJSON_GetObjectItem(node->spec,"shadow"))){
                            lv_color_t ink=lv_obj_get_style_text_color(node->obj,0);
                            assert(ink.red==0&&ink.green==0&&ink.blue==0);
                            if(!strcmp(ts(node->spec,"source"),"battery_temp"))temperature_shadow=true;
                        }
                    }
                    assert(temperature_shadow);template_update(&sample,&fixture);
                }
                if(!strcmp(list[i].id,"09-solar-journey")){
                    sample.sun_position_valid=true;sample.sun_altitude_deg=night?-25:25;
                    sample.sunrise_minute=420;sample.sunset_minute=1080;sample.sun_azimuth_deg=220;
                    sample.has_sun_radiation=false;sample.power_pv_kw=night?0:3.5;
                    sample.power_bat_kw=night?1.5:-.5;
                    sample.power_grid_kw=sample.power_full_kw-sample.power_pv_kw-sample.power_bat_kw;
                    sample.battery_temperature_c=32;sample.temperature_c=18.2;
                    s_animation_sample=sample;template_update(&sample,&fixture);
                    for(unsigned n=0;n<template_node_count;n++)
                        if(!strcmp(ts(template_nodes[n].spec,"type"),"solar_disc"))template_nodes[n].phase=.6f;
                    template_animate(0);
                }
                char filename[100];snprintf(filename,sizeof(filename),"template-%s-%s",list[i].id,night?"dark":"light");save_frame(filename);
                if(!night&&!strcmp(list[i].id,"01-live-home"))for(int frame=0;frame<12;frame++){
                    template_animate(.12f);snprintf(filename,sizeof(filename),"live-home-flow-%02d",frame);save_frame(filename);
                }
                rt.wifi_connected=false;render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_WIFI_ERROR);
                rt.wifi_connected=true;render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);
                assert(!strcmp(template_view.id,list[i].id));assert(s_last_night==(bool)night);
            }
        }
        app_templates_step(1);template_prepare();render_runtime(rt,sample,true,31000000);assert(!template_is_active());
        save_frame("builtin-pagination");
        /* A témalapozás visszatért a beépített Rendszer témára.
         * A régi fix pixelellenőrzés a korábbi elrendezéstől függött,
         * ezért egy új téma hozzáadásakor téves hibát eredményezett. */
        assert(!strcmp(template_view.id,"builtin"));
        assert(s_view==VIEW_DASHBOARD);
        assert(!template_is_active());
        lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,touch_read);
        unsigned expected_index=0;
        for(unsigned i=1;i<=count;i++){
            lv_obj_update_layout(s_screen);
            touch_sample(1050,true);touch_sample(850,true);touch_sample(650,true);touch_sample(650,false);
            template_prepare();render_runtime(rt,sample,true,31000000);
            /* A T1 nélküli mintában a Nap pálya nem lapozható: a várható
             * sorrend ugyanazt az opcionális szűrést kövesse, mint a UI. */
            do { expected_index=(expected_index+1)%count; }
            while(!sample.sun_position_valid &&
                  !strcmp(list[expected_index].id,"09-solar-journey"));
            if(strcmp(template_view.id,list[expected_index].id))
                fprintf(stderr,"Swipe %u: got %s expected %s\n",i,template_view.id,list[expected_index].id);
            assert(!strcmp(template_view.id,list[expected_index].id));
        }
        lv_obj_update_layout(s_screen);
        touch_sample_xy(640,650,true);touch_sample_xy(640,450,true);touch_sample_xy(640,250,true);touch_sample_xy(640,250,false);
        assert(s_diagnostics_requested);
        render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DIAGNOSTICS);
        lv_obj_update_layout(s_screen);
        touch_sample_xy(640,200,true);touch_sample_xy(640,400,true);touch_sample_xy(640,600,true);touch_sample_xy(640,600,false);
        assert(!s_diagnostics_requested);
        puts("PASS: raw portrait touch mapping separates horizontal theme swipes and vertical diagnostics gestures");
        lv_indev_delete(input);
        puts("PASS: 18 template renders, common error/recovery, NightMode and actual LVGL swipe across all 10 layouts; Bezier drawing/dot agreement, zero/reverse/7kW speed cap; six package weather states");
    }
    clear();template_release_cache();app_templates_view_free(&template_view);
    lv_display_delete(display);lv_deinit();return 0;
}
