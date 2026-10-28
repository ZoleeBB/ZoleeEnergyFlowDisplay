/* A tényleges firmware UI-jának headless LVGL renderje. Nincs külön mockup-implementáció. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../../main/app_ui.c"
#include "../../simulator/home_fixture.h"
#include "../../simulator/battery_fixture.h"
#include "src/draw/lv_draw_vector_private.h"
void app_test_slave_display_input(lv_display_t *display);
static app_ble_pairing_status_t pairing_fixture;
static char submitted_pin[7];
static unsigned pairing_open_requests;
void app_ble_pairing_status(app_ble_pairing_status_t *out){*out=pairing_fixture;}
bool app_ble_pairing_submit(uint32_t token,const char *pin){assert(token==pairing_fixture.token);snprintf(submitted_pin,sizeof(submitted_pin),"%s",pin?pin:"");pairing_fixture.pin_required=false;return true;}
void app_ble_pairing_open(void){pairing_open_requests++;}
/* A desktop demó nem indít BLE/NVS szolgáltatást. */
void app_slave_presence_service_apply(app_slave_display_t *display, int64_t now)
{
    (void)display;
    (void)now;
}


static lv_display_t *display;
static uint32_t pixels[1280*800];
/* A firmware-rel azonos 800×50 pixeles részpuffer: a célképet a flush állítja össze. */
static uint8_t partial_pixels[800 * 50 * 4];
static bool full_refresh;
static uint64_t flushed_pixels;
static bool rgb565;
static unsigned frame_flushes;
static unsigned slow_flush_ms, pointer_reads;
static bool require_loading_flush;
static unsigned loading_flush_baseline;
/* A panel 16 bites és a desktop 32 bites képpontjait azonos RGB-re bontjuk. */
static uint32_t pixel_rgb(unsigned index)
{
    if (!rgb565)
        return pixels[index] & 0xffffff;
    uint16_t value = ((const uint16_t *)pixels)[index];
    unsigned red = (value >> 11) & 31, green = (value >> 5) & 63, blue = value & 31;
    return (((red << 3) | (red >> 2)) << 16) | (((green << 2) | (green >> 4)) << 8) | ((blue << 3) | (blue >> 2));
}
static int64_t ticks;
static bool sd_ok=true;
static unsigned home_load_blocked;
static unsigned home_resource_reads;
static int home_corrupt_asset = -1;
bool __real_app_templates_home_load_for(const char *const *icons, unsigned count, bool night,
                                       bool consumers, app_template_view_t *out);
bool __wrap_app_templates_home_load_for(const char *const *icons, unsigned count, bool night,
                                       bool consumers, app_template_view_t *out)
{
    home_resource_reads++;
    if (require_loading_flush)
    {
        assert(s_view == VIEW_THEME_LOADING);
        assert(frame_flushes > loading_flush_baseline);
    }
    if (home_load_blocked)
    {
        home_load_blocked--;
        return false;
    }
    bool ok = __real_app_templates_home_load_for(icons, count, night, consumers, out);
    if (ok && home_corrupt_asset >= 0)
    {
        assert((unsigned)home_corrupt_asset < out->asset_count);
        app_template_asset_t *asset = &out->assets[home_corrupt_asset];
        asset->bytes[asset->size / 2] ^= 0x40;
        home_corrupt_asset = -1;
    }
    return ok;
}
/* Kártyalapozás: csak az ikonok olvasása, a háttér és a logó megmarad. */
static unsigned home_icon_reads;
static unsigned home_icon_blocked;
static bool home_icon_corrupt;
bool __real_app_templates_home_load_icons(const char *const *icons, unsigned count,
                                          app_template_view_t *out);
bool __wrap_app_templates_home_load_icons(const char *const *icons, unsigned count,
                                          app_template_view_t *out)
{
    home_icon_reads++;
    assert(s_view == VIEW_THEME_LOADING);
    if (home_icon_blocked)
    {
        home_icon_blocked--;
        return false;
    }
    bool ok = __real_app_templates_home_load_icons(icons, count, out);
    if (ok)
        assert(out->asset_count >= 2 && !out->assets[0].bytes && !out->assets[1].bytes);
    if (ok && home_icon_corrupt && out->asset_count > 2)
    {
        app_template_asset_t *asset = &out->assets[2];
        asset->bytes[asset->size / 2] ^= 0x40;
        home_icon_corrupt = false;
    }
    return ok;
}
static bool runtime_wifi_connected=true;
static app_stats_snapshot_t fixture;
static time_t epoch;
static lv_point_t touch_point = {.x = 400, .y = 640};
static bool touch_pressed;
static int last_pwm_brightness;
static int touch_swap_xy,touch_mirror_x,touch_mirror_y;
static void touch_read(lv_indev_t *input,lv_indev_data_t *data){
    pointer_reads++;
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
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg){assert(cfg->rotation==ESP_LV_ADAPTER_ROTATE_270);assert(cfg->tear_avoid_mode==ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL);assert(cfg->lv_adapter_cfg.task_core_id==1);assert(cfg->lv_adapter_cfg.task_stack_size==32768);assert(cfg->lv_adapter_cfg.stack_in_psram);touch_swap_xy=cfg->touch_flags.swap_xy;touch_mirror_x=cfg->touch_flags.mirror_x;touch_mirror_y=cfg->touch_flags.mirror_y;assert(touch_swap_xy==1&&touch_mirror_x==0&&touch_mirror_y==1);return display;}
static unsigned video_lock_attempts;
static bool reject_video_lock;
esp_err_t bsp_display_lock(int timeout)
{
    if (timeout == 100) {
        video_lock_attempts++;
        if (reject_video_lock) return ESP_FAIL;
    }
    return ESP_OK;
}
void bsp_display_unlock(void){}
void bsp_display_backlight_on(void){}
void bsp_display_brightness_set(int value){last_pwm_brightness=value;}
int xTaskCreate(void (*f)(void *),const char *n,unsigned st,void *a,unsigned p,void *h)
{(void)f;(void)n;(void)st;(void)a;(void)p;(void)h;return 1;}
void vTaskDelay(unsigned ms){(void)ms;}
void app_state_get_runtime(app_runtime_state_t *out){*out=(app_runtime_state_t){.wifi_connected=runtime_wifi_connected,.mqtt_connected=true};}
bool app_state_get_sample(app_energy_sample_t *out){(void)out;return false;}
void app_stats_get_snapshot(app_stats_snapshot_t *out){*out=fixture;}
void app_stats_get_period(bool month,app_stats_period_t *out){
    memset(out,0,sizeof(*out));out->count=month?31:7;
    for(unsigned i=0;i<out->count&&i<31;i++){
        snprintf(out->days[i].date,11,"2026-10-%02u",i+1);
        out->days[i].valid=i!=2;out->days[i].totals=fixture.totals;
        out->days[i].totals.production_kwh=18+i%7*2;out->days[i].totals.consumption_kwh=24+i%5;
        out->days[i].totals.grid_import_kwh=5+i%4;
        if(out->days[i].valid){out->totals.grid_import_kwh+=out->days[i].totals.grid_import_kwh;out->totals.grid_export_kwh+=3;}
    }
}

bool app_storage_is_ok(void){return sd_ok;}
static void flush(lv_display_t *disp,const lv_area_t *area,uint8_t *map)
{
    frame_flushes++;
    unsigned width = area->x2 - area->x1 + 1;
    unsigned height = area->y2 - area->y1 + 1;
    flushed_pixels += (uint64_t)width * height;
    if (!full_refresh)
    {
        unsigned bpp = rgb565 ? 2 : 4;
        unsigned stride = lv_display_get_buf_active(disp)->header.stride;
        for (unsigned y = 0; y < height; y++)
            memcpy((uint8_t *)pixels + ((area->y1 + y) * 1280 + area->x1) * bpp,
                   map + y * stride, width * bpp);
    }
    if (slow_flush_ms)
    {
        ticks += slow_flush_ms * 1000;
        lv_tick_inc(slow_flush_ms);
        slow_flush_ms = 0;
    }
    lv_display_flush_ready(disp);
}
static void count_invalidations(lv_event_t *event){unsigned *count=lv_event_get_user_data(event);++*count;}

static void save_frame(const char *name)
{
    lv_obj_update_layout(lv_screen_active());
    for(int i=0;i<16;i++){ticks+=16000;lv_tick_inc(16);lv_timer_handler();}
    lv_refr_now(display);
    char path[160];snprintf(path,sizeof(path),"%s.ppm",name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1280 800\n255\n");
    for(int i=0;i<1280*800;i++){
        unsigned char rgb[3]={(pixel_rgb(i)>>16)&255,(pixel_rgb(i)>>8)&255,pixel_rgb(i)&255};
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
            if((!strcmp(type,"label")||!strcmp(ts(node->spec,"source"),"status"))&&tn(node->spec,"y",0)>=110)
                assert(lv_color_eq(lv_obj_get_style_text_color(node->obj,0),c(s_last_night?0xFFFFFF:0x000000)));
            assert(strcmp(type,"box")||tn(node->spec,"w",0)!=1280||tn(node->spec,"h",0)!=800);
        }
        assert(hub);
        for(unsigned i=0;i<template_node_count;i++){
            template_node_t *flow=&template_nodes[i];
            if(strcmp(ts(flow->spec,"type"),"flow"))continue;
            for(int d=0;d<4;d++)assert(lv_obj_get_index(hub->obj)>lv_obj_get_index(flow->dots[d]));
        }
    }
    if(!strcmp(id,"06-energy-orbit") && tn(template_view.document,"version",0)<13){
        app_energy_sample_t saved=s_animation_sample,active=saved;
        active.power_pv_kw=4;active.power_grid_kw=1;active.power_bat_kw=1;active.power_full_kw=6;
        template_update(&active,&fixture);template_animate(.02f);
        unsigned flows=0,icons=0;
        template_node_t *pulse=NULL;
        lv_point_t ends[4]={{0}};
        for(unsigned i=0;i<template_node_count;i++){
            template_node_t *node=&template_nodes[i];
            if(!strcmp(ts(node->spec,"type"),"pulse"))pulse=node;
            if(!strcmp(ts(node->spec,"type"),"flow")){
                flows++;
                assert(node->point_count==2&&node->length<240);
                assert(tn(node->spec,"dot_count",4)==2&&tn(node->spec,"glow",0)<=2);
                assert(!lv_obj_has_flag(node->dots[0],LV_OBJ_FLAG_HIDDEN));
                assert(!lv_obj_has_flag(node->dots[1],LV_OBJ_FLAG_HIDDEN));
                assert(lv_obj_has_flag(node->dots[2],LV_OBJ_FLAG_HIDDEN));
                assert(lv_obj_has_flag(node->dots[3],LV_OBJ_FLAG_HIDDEN));
                const char *source=ts(node->spec,"source");
                int id=!strcmp(source,"pv")?0:!strcmp(source,"battery")?1:!strcmp(source,"grid")?2:3;
                unsigned outer=id==3?1:0,inner=1-outer;
                int x=tn(node->spec,"x",0),y=tn(node->spec,"y",0);
                assert(x+node->points[inner].x==630&&y+node->points[inner].y==345);
                ends[id]=(lv_point_t){x+node->points[outer].x-630,y+node->points[outer].y-345};
            }
            if(!strcmp(ts(node->spec,"type"),"image")){
                icons++;assert(tn(node->spec,"w",0)==160&&tn(node->spec,"h",0)==160);
            }
        }
        assert(flows==4&&icons==4);
        assert(ends[0].x==-ends[1].x&&ends[0].y==-ends[1].y);
        assert(ends[2].x==-ends[3].x&&ends[2].y==-ends[3].y);
        assert(pulse&&lv_obj_get_x(pulse->obj)+16==630&&lv_obj_get_y(pulse->obj)+16==345);
        for(unsigned i=0;i<template_node_count;i++){
            template_node_t *node=&template_nodes[i];
            assert(lv_obj_get_index(pulse->obj)>=lv_obj_get_index(node->obj));
            if(!strcmp(ts(node->spec,"type"),"flow"))
                for(int d=0;d<4;d++)assert(lv_obj_get_index(pulse->obj)>lv_obj_get_index(node->dots[d]));
        }
        template_update(&saved,&fixture);
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

/* A fotóhátterű új téma HU/EN szövegei a felső mérési határnál se vágódjanak le.
 * A tényleges LVGL-fontméretet és az élő frissítés eredményét vizsgáljuk. */
static void test_calm_dashboard(const app_energy_sample_t *original)
{
    if (strcmp(template_view.id, "05-calm-home"))
        return;
    char language[sizeof(s_cfg.project.language)];
    snprintf(language, sizeof(language), "%s", s_cfg.project.language);
    app_energy_sample_t sample = *original;
    sample.power_pv_kw = sample.power_full_kw = 15;
    sample.soc_bat_pct = 100;
    sample.status = 2;
    for (unsigned english = 0; english < 2; ++english)
    {
        snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", english ? "en" : "hu");
        template_build(sample.night_mode);
        template_update(&sample, &fixture);
        lv_obj_update_layout(s_screen);
        unsigned main_values = 0;
        for (unsigned i = 0; i < template_node_count; ++i)
        {
            template_node_t *node = &template_nodes[i];
            const char *type = ts(node->spec, "type");
            if (strcmp(type, "value") && strcmp(type, "label"))
                continue;
            lv_point_t size;
            lv_text_get_size(&size, lv_label_get_text(node->obj),
                             lv_obj_get_style_text_font(node->obj, 0), 0, 0,
                             LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            assert(size.x <= lv_obj_get_width(node->obj));
            assert(size.y <= lv_obj_get_height(node->obj));
            const char *source = ts(node->spec, "source");
            if (!strcmp(type, "value") &&
                (!strcmp(source, "pv") || !strcmp(source, "home") || !strcmp(source, "soc")))
            {
                char expected[128];
                template_text(expected, source, &sample, &fixture);
                assert(!strcmp(lv_label_get_text(node->obj), expected));
                ++main_values;
            }
        }
        assert(main_values == 3);
        char frame[80];
        snprintf(frame, sizeof(frame), "calm-limits-%s-%s", english ? "en" : "hu",
                 sample.night_mode ? "dark" : "light");
        save_frame(frame);
    }
    snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", language);
    template_build(original->night_mode);
    template_update(original, &fixture);
    puts("PASS: calm home live values, HU/EN labels and 15kW/100% bounds fit actual LVGL fonts");
}

#include "orbit_test.inc"
#include "theme_switch_bench.inc"

static void test_solar_status(const app_energy_sample_t *original)
{
    if (strcmp(template_view.id, "09-solar-journey"))
        return;
    template_node_t *status = NULL;
    for (unsigned i = 0; i < template_node_count; i++)
        if (!strcmp(ts(template_nodes[i].spec, "source"), "status"))
            status = &template_nodes[i];
    assert(status && status->detail && status->bubble);
    app_energy_sample_t sample = *original;
    const char *expected[] = {"", "", "Felkészülés áramszünetre", "Szigetüzem", "Rendszerhiba", "Hálózat újra elérhető"};
    /* Mindkét jelenetben minden figyelmeztetés után külön visszatérünk normálba. */
    for (int state = 2; state <= 5; state++) {
        sample.status = state;
        template_update(&sample, &fixture);
        assert(!lv_obj_has_flag(status->obj, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(status->detail, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(status->bubble, LV_OBJ_FLAG_HIDDEN));
        assert(!strcmp(lv_label_get_text(status->obj), expected[state]));
        assert(!strcmp(lv_label_get_text(status->detail), expected[state]));
        uint32_t color = state == 5 ? tc(status->spec, "positive_color", GREEN) : tc(status->spec, "color", RED);
        assert(lv_color_eq(lv_obj_get_style_text_color(status->obj, 0), c(color)));
        assert(lv_color_eq(lv_obj_get_style_border_color(status->bubble, 0), c(color)));
        assert(lv_color_eq(lv_obj_get_style_bg_color(status->bubble, 0),
                           c(original->night_mode ? 0x13283B : 0xFFFFFF)));
        assert(lv_obj_get_style_bg_opa(status->bubble, 0) == 245);
        assert(lv_obj_get_style_radius(status->bubble, 0) == 16);
        assert(lv_obj_get_style_text_font(status->obj, 0) == APP_FONT_28);
        assert(lv_color_eq(lv_obj_get_style_text_color(status->detail, 0), c(0xFFFFFF)));
        lv_obj_update_layout(s_dash);
        assert(lv_obj_get_x(status->obj) == 40 && lv_obj_get_y(status->obj) == 108);
        assert(lv_obj_get_x(status->detail) == 41 && lv_obj_get_y(status->detail) == 109);
        assert(lv_obj_get_x(status->bubble) == 28 && lv_obj_get_y(status->bubble) == 100);
        assert(lv_obj_get_height(status->bubble) == 52);
        assert(!lv_obj_has_flag(status->bubble, LV_OBJ_FLAG_CLICKABLE));
        lv_point_t size;
        lv_text_get_size(&size, expected[state], lv_obj_get_style_text_font(status->obj, 0),
                         0, 0, 1280, LV_TEXT_FLAG_NONE);
        assert(size.x <= lv_obj_get_width(status->obj));
        assert(size.y <= lv_obj_get_height(status->obj));
        assert(lv_obj_get_width(status->bubble) == size.x + 24);
        assert(lv_obj_get_index(status->bubble) < lv_obj_get_index(status->detail));
        assert(lv_obj_get_index(status->detail) < lv_obj_get_index(status->obj));
        char frame[80];
        snprintf(frame, sizeof(frame), "solar-status-%d-%s", state,
                 original->night_mode ? "dark" : "light");
        save_frame(frame);
        sample.status = 1;
        template_update(&sample, &fixture);
        assert(lv_obj_has_flag(status->obj, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(status->detail, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(status->bubble, LV_OBJ_FLAG_HIDDEN));
    }
    template_update(original, &fixture);
    puts("PASS: solar status 28px text, contrasting bubble and white shadow, all alerts and normal recovery");
}

static void test_template_grid_restored(const app_energy_sample_t *original)
{
    char language[sizeof(s_cfg.project.language)];
    snprintf(language, sizeof(language), "%s", s_cfg.project.language);
    app_energy_sample_t sample = *original;
    sample.status = 5;
    for (int english = 0; english < 2; english++) {
        snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", english ? "en" : "hu");
        const char *expected = english ? "Grid available again" : "Hálózat újra elérhető";
        template_update(&sample, &fixture);
        bool found = false;
        for (unsigned i = 0; i < template_node_count; i++) {
            template_node_t *node = &template_nodes[i];
            if (strcmp(ts(node->spec, "source"), "status"))
                continue;
            assert(!lv_obj_has_flag(node->obj, LV_OBJ_FLAG_HIDDEN));
            assert(!strcmp(lv_label_get_text(node->obj), expected));
            bool shadow = cJSON_IsTrue(cJSON_GetObjectItem(node->spec, "shadow"));
            assert(lv_color_eq(lv_obj_get_style_text_color(node->obj, 0),
                               c(shadow ? tc(node->spec, "color", 0) : tc(node->spec, "positive_color", GREEN))));
            if (node->detail) {
                assert(!lv_obj_has_flag(node->detail, LV_OBJ_FLAG_HIDDEN));
                assert(!strcmp(lv_label_get_text(node->detail), expected));
                assert(lv_color_eq(lv_obj_get_style_text_color(node->detail, 0),
                                   c(tc(node->spec, "shadow_color", 0))));
            }
            if (shadow)
                continue;
            found = true;
            lv_point_t size;
            lv_text_get_size(&size, expected, lv_obj_get_style_text_font(node->obj, 0),
                             0, 0, 1280, LV_TEXT_FLAG_NONE);
            assert(size.x <= tn(node->spec, "w", 0));
            assert(size.y <= tn(node->spec, "h", 0));
        }
        assert(found);
        if (!english) {
            char frame[100];
            snprintf(frame, sizeof(frame), "grid-restored-%s-%s", template_view.id,
                     sample.night_mode ? "dark" : "light");
            save_frame(frame);
        }
    }
    snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", language);
    template_update(original, &fixture);
    puts("PASS: grid restored in every theme, HU/EN fits, green text and intact shadow");
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
        template_animate(.1f);lv_obj_update_layout(s_screen);
        float expected=.12f;
        assert(fabsf(disc->phase-expected)<.0001f);
        disc->phase=.999f;template_animate(0);lv_obj_update_layout(s_screen);
        int expected_width=1+(int)lroundf((tn(disc->spec,"w",40)+55)*fmin(levels[i]/8.,1.)*.999f);
        assert(lv_obj_get_width(disc->detail)==expected_width);
        assert(lv_obj_get_style_bg_opa(disc->detail,0)==0);
        assert(lv_obj_get_style_border_opa(disc->detail,0)==0);
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
    assert(fabsf(disc->phase-.62f)<.0001f&&lv_obj_get_style_border_opa(disc->detail,0)==0);
    assert(lv_obj_get_style_bg_opa(disc->detail,0)==0);
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
    puts("PASS: solar ripple fixed speed, PV-scaled radius/fade, 8kW size cap, zero/night, SOC colors, idle grid and daily production");
}

/* A fotó jobb oldali fényes íve, a teljes napnyugta előtti órában.
 * Nem csak a JSON pontjait ellenőrizzük: a valódi UI órából számolt
 * korongközéppontja legfeljebb 2 px-re lehet a mért fotóvonaltól. */
static void test_solar_sunset(const app_energy_sample_t *original){
    if(strcmp(template_view.id,"09-solar-journey"))return;
    template_node_t *disc=NULL;
    for(unsigned i=0;i<template_node_count;i++)
        if(!strcmp(ts(template_nodes[i].spec,"type"),"solar_disc"))disc=&template_nodes[i];
    assert(disc);
    static const lv_point_t ridge[]={
        {806,285},{812,295},{817,306},{823,320},{829,335},{834,350},
        {839,365},{842,380},{843,386},{845,400},{846,410},{846,416}};
    time_t saved_epoch=epoch;
    app_energy_sample_t saved_animation=s_animation_sample,s=*original;
    s.sun_position_valid=true;s.sunrise_minute=420;s.sunset_minute=1073;
    s.sun_altitude_deg=1;s.power_pv_kw=.02;
    struct tm local;localtime_r(&saved_epoch,&local);
    unsigned checked=0;
    for(int before=60;before>=0;before--){
        int minute=s.sunset_minute-before;
        local.tm_hour=minute/60;local.tm_min=minute%60;local.tm_sec=0;
        epoch=mktime(&local);template_update(&s,&fixture);
        s_animation_sample=s;template_animate(0);lv_obj_update_layout(s_screen);
        int cx=lv_obj_get_x(disc->obj)+tn(disc->spec,"w",42)/2;
        int cy=lv_obj_get_y(disc->obj)+tn(disc->spec,"h",42)/2;
        if(cy>=ridge[0].y){
            unsigned k=0;
            while(k+2<sizeof(ridge)/sizeof(ridge[0])&&cy>ridge[k+1].y)k++;
            double ratio=(double)(cy-ridge[k].y)/(ridge[k+1].y-ridge[k].y);
            double expected=ridge[k].x+ratio*(ridge[k+1].x-ridge[k].x);
            assert(fabs(cx-expected)<=2.);checked++;
        }
        if(before==30||before==5||before==0){
            char name[80];snprintf(name,sizeof(name),"solar-sunset-%s-minus-%02d",s.night_mode?"dark":"light",before);
            save_frame(name);
        }
        if(before==0)assert(cx==846&&cy==416);
    }
    assert(checked>=55);
    /* A nappali végpont az éjszakai kezdőponttal azonos: nincs ugrás. */
    s.sun_altitude_deg=-1;template_update(&s,&fixture);lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(disc->obj)+21==846&&lv_obj_get_y(disc->obj)+21==416);
    epoch=saved_epoch;s_animation_sample=saved_animation;template_update(original,&fixture);
    puts("PASS: sunset last-hour clock sweep follows measured photo ridge within 2px; continuous night transition");
}

/* A runtime frissítsen animációs mintát, képet és fényudvart lapozás nélkül. */
static void test_solar_mode_transitions(app_runtime_state_t rt,const app_energy_sample_t *original){
    if(strcmp(template_view.id,"09-solar-journey"))return;
    time_t saved_epoch=epoch;
    app_energy_sample_t s=*original;
    /* Indulás közvetlenül a sablonon: nincs korábbi Rendszer-mérés. */
    memset(&s_animation_sample,0,sizeof(s_animation_sample));
    s_dash_created=false;
    const bool modes[]={true,false,true,false};
    for(unsigned mode=0;mode<sizeof(modes)/sizeof(modes[0]);mode++){
        bool night=modes[mode];
        struct tm local;localtime_r(&saved_epoch,&local);local.tm_hour=night?20:12;epoch=mktime(&local);
        s.night_mode=night;s.sun_position_valid=true;s.sun_altitude_deg=night?-10:25;
        s.sunrise_minute=420;s.sunset_minute=1080;s.power_pv_kw=night?0:4;
        render_runtime(rt,s,true,31000000);lv_obj_update_layout(s_screen);
        assert(s_view==VIEW_DASHBOARD&&template_is_active());
        assert(s_animation_sample.night_mode==night&&s_animation_sample.sun_position_valid);
        assert(s_animation_sample.power_pv_kw==s.power_pv_kw);
        template_node_t *disc=NULL;
        for(unsigned i=0;i<template_node_count;i++)
            if(!strcmp(ts(template_nodes[i].spec,"type"),"solar_disc"))disc=&template_nodes[i];
        assert(disc&&disc->detail);
        const char *file=night?"solar_night.png":"solar_day.png";
        assert(!strcmp(ts(disc->spec,"file"),file));
        bool image_matches=false;
        for(unsigned a=0;a<template_view.asset_count;a++)if(!strcmp(template_view.assets[a].name,file)){
            const void *src=lv_image_get_src(disc->obj);
            if(src==&template_images[a])image_matches=true;
            for(unsigned r=0;r<template_view.raw_count;r++)
                if(template_view.raws[r].asset==a&&src==&template_raw_images[r])image_matches=true;
#if LV_USE_SNAPSHOT
            for(unsigned r=0;r<template_raster_count;r++)
                if(template_rasters[r].asset==a&&src==&template_rasters[r].image)image_matches=true;
#endif
        }
        assert(image_matches);
        assert(lv_obj_get_index(disc->detail)<lv_obj_get_index(disc->obj));
        assert(!lv_obj_has_flag(disc->detail,LV_OBJ_FLAG_HIDDEN));
        /* Már az első frame-en legyen megfelelő kitöltés és kiterjedés. */
        assert(lv_obj_get_width(disc->detail)==(night?56:1));
        assert(lv_obj_get_style_bg_opa(disc->detail,0)==(night?30:110));
        assert(lv_color_eq(lv_obj_get_style_bg_color(disc->detail,0),c(night?0x78BFEF:0xFFD23B)));
        assert(abs(lv_obj_get_x(disc->detail)*2+lv_obj_get_width(disc->detail)-
                   (lv_obj_get_x(disc->obj)*2+lv_obj_get_width(disc->obj)))<=1);
        int image_x=lv_obj_get_x(disc->obj),image_y=lv_obj_get_y(disc->obj);
        float phase=disc->phase;
        template_animate(.25f);lv_obj_update_layout(s_screen);
        assert(fabsf(disc->phase-fmodf(phase+.25f*(night?1.f/4.2f:1.2f),1.f))<.0001f);
        assert(lv_obj_get_x(disc->obj)==image_x&&lv_obj_get_y(disc->obj)==image_y);
        if(night){
            for(int step=0;step<24;step++){
                template_animate(.2f);lv_obj_update_layout(s_screen);
                int fill=lv_obj_get_style_bg_opa(disc->detail,0);
                assert(fill>=22&&fill<=38);
                assert(lv_obj_get_width(disc->detail)>=54&&lv_obj_get_width(disc->detail)<=58);
                assert(lv_obj_get_style_border_width(disc->detail,0)==0);
            }
            s.sun_position_valid=false;render_runtime(rt,s,true,31000000);
            assert(lv_obj_has_flag(disc->detail,LV_OBJ_FLAG_HIDDEN));
            s.sun_position_valid=true;render_runtime(rt,s,true,31000000);
            assert(!lv_obj_has_flag(disc->detail,LV_OBJ_FLAG_HIDDEN));
            assert(lv_obj_get_style_bg_opa(disc->detail,0)>=22);
        }else{
            s.power_pv_kw=0;render_runtime(rt,s,true,31000000);
            assert(lv_obj_get_style_bg_opa(disc->detail,0)==0);
        }
    }
    epoch=saved_epoch;render_runtime(rt,*original,true,31000000);
    puts("PASS: direct-template startup and day/moon/day transitions refresh image, live sample and halo on first frame; invalid/valid recovery without paging");
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
            /* A státusz láthatóságát a külön állapotváltási teszt ellenőrzi. */
            if (!strcmp(source, "status"))
                continue;
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
                /* Előre dekódolt segédfájl: a megjelenítési mérettel. */
                for(unsigned r=0;r<template_view.raw_count;r++)if(template_view.raws[r].asset==a&&actual==&template_raw_images[r]){
                    assert(template_view.raws[r].width==(unsigned)tn(node->spec,"w",128));
                    assert(template_view.raws[r].height==(unsigned)tn(node->spec,"h",128));right=true;
                }
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
            uint32_t pixel=pixel_rgb(y*1280+x);
            int dominant=(pixel>>channels[chart][layer])&255;
            int other=(pixel>>(channels[chart][layer]==16?8:16))&255;
            assert(dominant>other+5);
        }
        /* Nincs implicit kontúrkitöltés a rétegek fölött, a hiányzó
         * időablakban vagy a jövőben. Egyik pont sem rácsvonalra esik. */
        int y=area.y2-(area.y2-area.y1)*7/8;
        assert((pixel_rgb(y*1280+x)&0xffffff)==0xffffff);
        y=area.y2-(area.y2-area.y1)*3/8;
        int gap_x=area.x1+(area.x2-area.x1)*70/288;
        int future_x=area.x1+(area.x2-area.x1)*230/288;
        assert((pixel_rgb(y*1280+gap_x)&0xffffff)==0xffffff);
        assert((pixel_rgb(y*1280+future_x)&0xffffff)==0xffffff);
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
        assert((pixel_rgb(y*1280+positive_x)&0xffffff)!=0xffffff);
        if(kind==APP_CHART_BATTERY){
            uint32_t positive=pixel_rgb(y*1280+positive_x),negative=pixel_rgb((area.y1+130)*1280+negative_x);
            assert(((positive>>8)&255) > ((negative>>8)&255));
        }
        if(kind!=APP_CHART_SOC){
            assert((pixel_rgb((area.y1+130)*1280+negative_x)&0xffffff)!=0xffffff);
            assert((pixel_rgb(y*1280+negative_x)&0xffffff)==0xffffff);
        }
        assert((pixel_rgb(y*1280+gap_x)&0xffffff)==0xffffff);
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
        uint32_t pixel=pixel_rgb(y*1280+x)&0xffffff;
        if(hour==1)reference=pixel;
        assert(pixel==reference&&pixel!=0xffffff);
        assert((pixel_rgb(y*1280+x-1)&0xffffff)==0xffffff);
        assert((pixel_rgb(y*1280+x+1)&0xffffff)==0xffffff);
    }
    /* A középső skálavonal egyetlen, kontrasztos pixelsor. */
    int middle=(int)floorf((area.y1+area.y2)/2.f),x=area.x1+5;
    assert((pixel_rgb(middle*1280+x)&0xffffff)!=0xffffff);
    assert((pixel_rgb((middle-1)*1280+x)&0xffffff)==0xffffff);
    assert((pixel_rgb((middle+1)*1280+x)&0xffffff)==0xffffff);
    assert((pixel_rgb((middle+2)*1280+x)&0xffffff)==0xffffff);
    lv_obj_delete(parent);
    puts("PASS: identical one-pixel hourly grid lines and stronger horizontal scale lines in rendered pixels");
}

static void test_energy_assistant_ui(const app_energy_sample_t *original)
{
    bool night = original->night_mode;
    int64_t original_ticks = ticks;
    app_data_mode_t original_mode = s_cfg.data_recept_mode;
    s_cfg.data_recept_mode = APP_DATA_LOXONE;
    app_sim_home_reset();
    app_sim_home_config.consumers[1].suggest_start = true;
    app_sim_home_config.consumers[1].average_known = true;
    app_sim_home_config.consumers[1].average_kw = 1.5;
    assistant_prepare();
    assert(assistant_consumers.count == 7 && assistant_consumers.consumers[1].eligible);
    memset(&assistant_state, 0, sizeof(assistant_state));
    app_energy_sample_t value = *original;
    value.power_pv_kw = 5;
    value.power_full_kw = 1;
    value.power_grid_kw = -4;
    value.power_bat_kw = 0;
    value.soc_bat_pct = 100;
    value.status = 1;
    if (night)
    {
        value.power_full_kw = .5;
        value.power_pv_kw = 0;
        value.power_grid_kw = .5;
        assistant_night = (app_assistant_night_t){.ready=true, .nights=3,
            .current_kw=.5, .baseline_kw=.2};
        strcpy(assistant_night.date, "2026-10-21");
    }
    int64_t first = ticks + 1000000;
    for (int64_t time = first; time <= first + 120000000; time += 5000000)
    {
        value.received_monotonic_us = time;
        app_assistant_update(&assistant_state, time, &value, true, &assistant_consumers, &assistant_night);
    }
    assert(assistant_state.result.kind == (night ? APP_ASSISTANT_NIGHT : APP_ASSISTANT_SOLAR));
    if (template_is_active())
        template_update(&value, &fixture);
    else
        update(&value, &fixture);
    assistant_render();
    assert(assistant_box && !lv_obj_has_flag(assistant_box, LV_OBJ_FLAG_HIDDEN));
    lv_obj_update_layout(s_screen);
    int x, y, width, height;
    assert(assistant_area(&x, &y, &width, &height));
    assert(lv_obj_get_x(assistant_box) == x && lv_obj_get_y(assistant_box) == y);
    assert(x >= 0 && y >= 0 && x + width <= W && y + height <= H);
    if (template_is_active())
    {
        const cJSON *scene = cJSON_GetObjectItem(template_view.document, night ? "dark" : "light");
        const cJSON *element;
        cJSON_ArrayForEach(element, cJSON_GetObjectItem(scene, "elements"))
        {
            const char *type = ts(element, "type");
            if (strcmp(type, "value") && strcmp(type, "weather") && strcmp(type, "bar") &&
                strcmp(type, "chart") && strcmp(type, "gauge") && strcmp(type, "solar_timeline"))
                continue;
            int ex = tn(element, "x", 0), ey = tn(element, "y", 0);
            int ew = tn(element, "w", 0), eh = tn(element, "h", 0);
            assert(x + width <= ex || ex + ew <= x || y + height <= ey || ey + eh <= y);
        }
    }
    assert(lv_obj_get_width(assistant_title) == width - 62);
    assert(strstr(lv_label_get_text(assistant_title), night ? "éjszakai" : "Mosógép"));
    char file[96];
    snprintf(file, sizeof(file), "assistant-%s-%s", template_is_active() ? template_view.id : "builtin",
             night ? "dark" : "light");
    save_frame(file);
    if (!night)
    {
        char name[LX_HOME_NAME_SIZE];
        snprintf(name, sizeof(name), "%s", assistant_state.result.name);
        memset(assistant_state.result.name, 'M', LX_HOME_NAME_SIZE - 1);
        assistant_state.result.name[LX_HOME_NAME_SIZE - 1] = 0;
        assistant_render();
        lv_obj_update_layout(s_screen);
        assert(lv_obj_get_height(assistant_title) == 20 && lv_obj_get_height(assistant_detail) == 16);
        snprintf(assistant_state.result.name, sizeof(assistant_state.result.name), "%s", name);
        char language[sizeof(s_cfg.project.language)];
        snprintf(language, sizeof(language), "%s", s_cfg.project.language);
        snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "en");
        assistant_render();
        assert(strstr(lv_label_get_text(assistant_title), "Good time"));
        snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", language);
    }
    if (template_is_active() && !strcmp(template_view.id, "02-energy-paths") && !night)
    {
        char identity[sizeof(template_view.id)];
        snprintf(identity, sizeof(identity), "%s", template_view.id);
        snprintf(template_view.id, sizeof(template_view.id), "external-test");
        assert(!assistant_area(&x, &y, &width, &height));
        cJSON *scene = cJSON_GetObjectItem(template_view.document, "light");
        cJSON *area = cJSON_Parse("{\"x\":332,\"y\":16,\"w\":465,\"h\":72}");
        assert(cJSON_AddItemToObject(scene, "assistant_area", area));
        assert(assistant_area(&x, &y, &width, &height));
        cJSON_SetNumberValue(cJSON_GetObjectItem(area, "x"), 1200);
        assert(!assistant_area(&x, &y, &width, &height));
        cJSON_DeleteItemFromObjectCaseSensitive(scene, "assistant_area");
        snprintf(template_view.id, sizeof(template_view.id), "%s", identity);
    }
    lv_obj_t *box = assistant_box;
    assistant_render();
    assert(assistant_box == box); /* Nincs új widget minden frissítésnél. */
    s_diagnostics_requested = true;
    assistant_render();
    assert(lv_obj_has_flag(assistant_box, LV_OBJ_FLAG_HIDDEN));
    s_diagnostics_requested = false;
    home_request_set(2);
    assistant_render();
    assert(lv_obj_has_flag(assistant_box, LV_OBJ_FLAG_HIDDEN));
    home_request_set(0);
    assistant_render();
    unsigned commands = app_sim_home_commands;
    ticks = first + 120000000;
    lv_obj_send_event(lv_obj_get_child(assistant_box, 2), LV_EVENT_CLICKED, NULL);
    assert(assistant_state.result.kind == APP_ASSISTANT_NONE);
    assert(lv_obj_has_flag(assistant_box, LV_OBJ_FLAG_HIDDEN));
    assert(app_sim_home_commands == commands);
    s_cfg.data_recept_mode = APP_DATA_SLAVE;
    assistant_render();
    assert(lv_obj_has_flag(assistant_box, LV_OBJ_FLAG_HIDDEN));
    s_cfg.data_recept_mode = original_mode;
    memset(&assistant_state, 0, sizeof(assistant_state));
    app_sim_home_enabled = false;
    ticks = original_ticks + 256000;
    if (template_is_active())
        template_update(original, &fixture);
    else
        update(original, &fixture);
}

/* Postaláda-jel: minden gyári témán és a Rendszer nézeten látható, nem takar
 * adatelemet és az energiaasszisztens területét; kikapcsolva/kapcsolatvesztéskor
 * rejtett, és frissítéskor nem hoz létre új widgetet. */
static void test_mail_ui(const app_energy_sample_t *original)
{
    bool night = original->night_mode;
    app_data_mode_t original_mode = s_cfg.data_recept_mode;
    s_cfg.data_recept_mode = APP_DATA_LOXONE;
    app_sim_signals = (app_loxone_signals_t){.mail = {.configured = true, .valid = true, .active = true}};
    (void)app_loxone_signals(&s_loxone_signals);
    mail_render();
    assert(mail_badge && !lv_obj_has_flag(mail_badge, LV_OBJ_FLAG_HIDDEN));
    lv_obj_update_layout(s_screen);
    int x = lv_obj_get_x(mail_badge), y = lv_obj_get_y(mail_badge);
    assert(x >= 0 && y >= 0 && x + MAIL_BADGE_SIZE <= W && y + MAIL_BADGE_SIZE <= H);
    assert(y < 110); /* A fejlécben marad. */
    int ax, ay, aw, ah;
    if (assistant_area(&ax, &ay, &aw, &ah))
        assert(x + MAIL_BADGE_SIZE <= ax || ax + aw <= x || y + MAIL_BADGE_SIZE <= ay || ay + ah <= y);
    if (template_is_active())
    {
        const cJSON *scene = cJSON_GetObjectItem(template_view.document, night ? "dark" : "light");
        const cJSON *element;
        cJSON_ArrayForEach(element, cJSON_GetObjectItem(scene, "elements"))
        {
            const char *type = ts(element, "type");
            if (strcmp(type, "value") && strcmp(type, "weather") && strcmp(type, "bar") &&
                strcmp(type, "chart") && strcmp(type, "gauge") && strcmp(type, "solar_timeline"))
                continue;
            /* A rejtett normál státusz nem foglal helyet. */
            if (!strcmp(ts(element, "source"), "status") && cJSON_IsTrue(cJSON_GetObjectItem(element, "hide_when_normal")))
                continue;
            int ex = tn(element, "x", 0), ey = tn(element, "y", 0);
            int ew = tn(element, "w", 0), eh = tn(element, "h", 0);
            if (!(x + MAIL_BADGE_SIZE <= ex || ex + ew <= x || y + MAIL_BADGE_SIZE <= ey || ey + eh <= y))
            {
                printf("mail badge %d,%d overlaps %s %s at %d,%d %dx%d\n", x, y, type,
                       ts(element, "source"), ex, ey, ew, eh);
                assert(false);
            }
        }
    }
    char file[96];
    snprintf(file, sizeof(file), "mail-%s-%s", template_is_active() ? template_view.id : "builtin",
             night ? "dark" : "light");
    save_frame(file);
    lv_obj_t *badge = mail_badge;
    mail_render();
    assert(mail_badge == badge);
    app_sim_signals.mail.active = false;
    (void)app_loxone_signals(&s_loxone_signals);
    mail_render();
    assert(lv_obj_has_flag(mail_badge, LV_OBJ_FLAG_HIDDEN));
    app_sim_signals.mail = (app_loxone_signals_t){0}.mail;
    (void)app_loxone_signals(&s_loxone_signals);
    mail_render();
    assert(lv_obj_has_flag(mail_badge, LV_OBJ_FLAG_HIDDEN));
    s_cfg.data_recept_mode = original_mode;
    memset(&s_loxone_signals, 0, sizeof(s_loxone_signals));
}

/*
 * Nappal/éjjel váltás után a jelenet minden képe a saját változatára mutasson,
 * és a teljes képernyős háttér valóban dekódolt képpontokat adjon. A hibás
 * (elmaradó) sötét háttérnél a kirajzolt kép egyszínű marad.
 */
static void test_scene_images(bool night)
{
    if (!template_is_active())
        return;
    const cJSON *scene = cJSON_GetObjectItem(template_view.document, night ? "dark" : "light");
    const cJSON *element;
    bool background = false;
    cJSON_ArrayForEach(element, cJSON_GetObjectItem(scene, "elements"))
    {
        if (strcmp(ts(element, "type"), "image"))
            continue;
        const char *file = ts(element, "file");
        unsigned asset = template_view.asset_count;
        for (unsigned i = 0; i < template_view.asset_count; i++)
            if (!strcmp(template_view.assets[i].name, file))
            {
                asset = i;
                break;
            }
        assert(asset < template_view.asset_count);
        for (unsigned n = 0; n < template_node_count; n++)
        {
            if (template_nodes[n].spec != element || !template_nodes[n].obj)
                continue;
            const void *src = lv_image_get_src(template_nodes[n].obj);
            bool bound = src == &template_images[asset];
            for (unsigned r = 0; !bound && r < template_view.raw_count; r++)
                bound = src == &template_raw_images[r] && template_view.raws[r].asset == asset;
#if LV_USE_SNAPSHOT
            for (unsigned r = 0; !bound && r < template_raster_count; r++)
                bound = src == &template_rasters[r].image && template_rasters[r].asset == asset;
#endif
            assert(bound);
            if (tn(element, "w", 0) == 1280 && tn(element, "h", 0) == 800)
                background = true;
        }
    }
    if (!background)
        return;
    uint32_t first = pixel_rgb(400 * 1280 + 40);
    unsigned different = 0;
    for (unsigned y = 120; y < 760; y += 40)
        if (pixel_rgb(y * 1280 + 40) != first)
            different++;
    assert(different >= 4);
}

static unsigned widget_count(const lv_obj_t *root)
{
    unsigned count = 1;
    for (unsigned i = 0; i < lv_obj_get_child_count(root); i++)
        count += widget_count(lv_obj_get_child(root, i));
    return count;
}

/* A keretben valóban a dekódolt fényképet látjuk, nem csak egy helyes
 * fájlnevet vagy egy háttér nélküli, sikeresnek jelzett widgetfát. */
static void test_home_background_pixels(void)
{
    assert(home_buffers[0] && home_images[0].header.cf == LV_COLOR_FORMAT_RGB565);
    assert(home_buffers[0]->data_size == 1280U * 800U * 2U);
    unsigned non_white = 0;
    for (unsigned y = 150; y < 730; y += 53)
    {
        uint16_t value = *(uint16_t *)(home_buffers[0]->data + y * home_buffers[0]->header.stride + 10 * 2);
        unsigned red = value >> 11, green = (value >> 5) & 63, blue = value & 31;
        unsigned expected = (((red << 3) | (red >> 2)) << 16) |
                            (((green << 2) | (green >> 4)) << 8) | (blue << 3) | (blue >> 2);
        unsigned actual = pixel_rgb(y * 1280 + 10);
        for (unsigned shift = 0; shift <= 16; shift += 8)
            assert(abs((int)((actual >> shift) & 255) - (int)((expected >> shift) & 255)) <= 1);
        non_white += actual != 0xffffff;
    }
    assert(non_white >= 8);
    for (unsigned i = 0; i < home_assets.asset_count; i++)
        assert(home_buffers[i] && home_images[i].header.cf != LV_COLOR_FORMAT_RAW_ALPHA);
}

/* A fogyasztói zajküszöb a feliratot és az ikont együtt érintse;
 * a fejléc a külön mért tételektől független teljes házfogyasztást mutassa. */
static void test_consumer_noise(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    lx_home_values_t saved = app_sim_home_values;
    bool connected = app_sim_home_connected;
    unsigned commands = app_sim_home_commands;
    double saved_house_power = sample.power_full_kw;
    sample.power_full_kw = 2.4;
    const double power[] = {0, .0001, .0005, .0009, .000999, .001, .001001, .002};
    for (unsigned night = 0; night < 2; night++)
    {
        sample.night_mode = night;
        for (unsigned i = 0; i < app_sim_home_config.consumer_count; i++)
            app_sim_home_values.consumers[i] = (lx_home_value_t){.valid=true, .value=.000999};
        assert(home_prepare(night));
        render_runtime(runtime, sample, true, 31000000);
        assert(!strcmp(lv_label_get_text(home_total), hu() ? "2,4 kW" : "2.4 kW"));
        lv_obj_t *header = lv_obj_get_parent(home_total);
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(header, 0)),
                       tr("Ház külön mért fogyasztói", "Separately metered home consumers")));
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(header, 2)),
                       tr("A ház teljes fogyasztása", "Total home consumption")));
        int icon = home_icon_index(app_sim_home_config.consumers[0].icon);
        assert(icon >= 0 && home_gray_buffers[icon]);
        for (unsigned n = 0; n < sizeof(power)/sizeof(power[0]); n++)
        {
            app_sim_home_values.consumers[0].value = power[n];
            assert(home_prepare(night));
            render_runtime(runtime, sample, true, 31000000);
            bool off = n < 5;
            const char *text = off ? "0 W" : n == 7 ? "2 W" : "1 W";
            assert(!strcmp(lv_label_get_text(home_labels[0]), text));
            assert(lv_image_get_src(home_icons[0]) == (off ? &home_gray_images[icon] : &home_images[icon]));
            assert((lv_color_to_u32(lv_obj_get_style_text_color(home_labels[0], 0)) & 0xffffffU) ==
                   (off ? (night ? 0x9DABACU : 0x687474U) : (night ? 0x68E0D5U : 0x006B69U)));
            assert(app_sim_home_values.consumers[0].value == power[n]);
        }
        app_sim_home_values.consumers[0].valid = false;
        assert(home_prepare(night));
        render_runtime(runtime, sample, true, 31000000);
        assert(!strcmp(lv_label_get_text(home_labels[0]), tr("Nincs adat", "No data")));
        assert(!strcmp(lv_label_get_text(home_total), hu() ? "2,4 kW" : "2.4 kW"));
        sample.power_full_kw = .084;
        render_runtime(runtime, sample, true, 31000000);
        assert(!strcmp(lv_label_get_text(home_total), "84 W"));
        render_runtime(runtime, sample, false, 31000000);
        assert(!strcmp(lv_label_get_text(home_total), tr("Nincs adat", "No data")));
        render_runtime(runtime, sample, true, 3601000000LL);
        assert(!strcmp(lv_label_get_text(home_total), tr("Nincs adat", "No data")));
        app_sim_home_connected = false;
        assert(home_prepare(night));
        render_runtime(runtime, sample, true, 31000000);
        assert(!strcmp(lv_label_get_text(home_total), tr("Nincs adat", "No data")));
        app_sim_home_connected = connected;
        sample.power_full_kw = 2.4;
    }
    app_sim_home_values = saved;
    app_sim_home_connected = connected;
    sample.power_full_kw = saved_house_power;
    assert(home_prepare(false));
    sample.night_mode = false;
    render_runtime(runtime, sample, true, 31000000);
    assert(app_sim_home_commands == commands);
    puts("PASS: consumer sub-1W noise is 0W/off/gray, exact 1W active, title and independent live PowerFull header, W/kW/missing/offline, day/night and unchanged commands");
}

/* Az előző teszt DIRECT desktop módban futott, ezért nem mutatta ki a
 * firmware FULL módjának teljesképes többletmunkáját. Ugyanazt a jelenetet
 * és tickeket most a valódi PARTIAL és a korábbi FULL módban is mérjük. */
static void test_render_work(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    const char *themes[] = {"builtin", "01-live-home", "05-calm-home", "06-energy-orbit", "09-solar-journey"};
    sample.sun_position_valid = true;
    for (unsigned theme = 0; theme < sizeof(themes)/sizeof(themes[0]); theme++)
    {
        assert(app_templates_select(themes[theme]) == ESP_OK);
        template_prepare();
        render_runtime(runtime, sample, true, 31000000);
        /* A témabelépés áttűnése ne takarja el a normál animáció költségét. */
        for (unsigned tick = 0; tick < 40; tick++)
        {
            ticks += 16000;lv_tick_inc(16);lv_timer_handler();
        }
        lv_refr_now(display);
        uint64_t before = flushed_pixels;
        unsigned before_flushes = frame_flushes;
        for (unsigned tick = 0; tick < 30; tick++)
        {
            ticks += 16000;lv_tick_inc(16);lv_timer_handler();
        }
        uint64_t work = flushed_pixels - before;
        unsigned calls = frame_flushes - before_flushes;
        bool static_scene = !strcmp(themes[theme], "05-calm-home");
        if ((!calls || !work) && !static_scene)
            fprintf(stderr,"No redraw: theme=%s view=%d active=%d dash=%d started=%d phase=%f time=%lld anim=%lld\n",
                    themes[theme],s_view,template_is_active(),s_dash_created,s_started,
                    (double)f_pv.phase,(long long)ticks,(long long)s_animation_us);
        assert((calls && work) || static_scene);
        if (!theme && !full_refresh)
            assert(work < (uint64_t)30 * W * H / 2);
        printf("RENDER_WORK %s %s pixels=%llu flushes=%u equivalent_full_frames=%.2f\n",
               full_refresh ? "FULL" : "PARTIAL", themes[theme],
               (unsigned long long)work, calls, (double)work/(W*H));
    }
    puts("PASS: actual LVGL animation redraw work measured in production partial and previous full refresh modes");
}

/* Az IRQ-s touch-adapter két hardveres minta között ugyanazt a pontot
 * adja vissza. A régi sebességküszöb ilyenkor minden alkalommal lenullázta
 * a húzás összegét. Kis lépések és köztes azonos minták is lapozzanak. */
static void slow_swipe(int x, int y, int dx, int dy)
{
    touch_sample_xy(x, y, true);
    for (int step = 1; step <= 8; step++)
    {
        touch_sample_xy(x + step * dx, y + step * dy, true);
        touch_sample_xy(x + step * dx, y + step * dy, true);
    }
    touch_sample_xy(x + 8 * dx, y + 8 * dy, false);
}

/*
 * Gyors oda-vissza lapozás: a betöltőképernyő alatt érkező húzás se vesszen
 * el, a régi téma dekódolt képei már a betöltés előtt szabaduljanak fel, és
 * elakadt betöltésből a kijelző magától térjen vissza.
 */
static void test_rapid_paging(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    sample.sun_position_valid = true;
    s_animation_sample = sample;
    assert(app_templates_select("09-solar-journey") == ESP_OK);
    assert(template_loading_frame());
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    assert(s_view == VIEW_DASHBOARD && !strcmp(template_view.id, "09-solar-journey"));

    /* Lapozás a következő témára: a betöltőképernyőn már nincs korábbi raszter. */
    lv_obj_update_layout(s_screen);
    slow_swipe(640, 400, -8, 0);
    assert(template_loading_requested);
    assert(template_loading_frame() && s_view == VIEW_THEME_LOADING);
#if LV_USE_SNAPSHOT
    assert(template_raster_count == 0 && template_raster_bytes == 0);
#endif
    /* A betöltőképernyőn érkező újabb húzás sorba kerül, nem vész el. */
    lv_obj_update_layout(s_screen);
    slow_swipe(640, 400, 8, 0);
    assert(template_step_queue != 0 || template_loading_requested);
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    /* Addig futtatjuk a runtime körét, amíg a sor kiürül. */
    for (unsigned round = 0; round < 6 && (template_step_queue || template_loading_requested ||
                                           s_view != VIEW_DASHBOARD); round++)
    {
        assert(template_loading_frame());
        template_prepare();
        render_runtime(runtime, sample, true, 31000000);
    }
    assert(s_view == VIEW_DASHBOARD && !template_step_queue);
    /* Két ellentétes irányú húzás után ugyanott vagyunk, és a háttér él. */
    assert(!strcmp(template_view.id, "09-solar-journey"));
    template_update(&sample, &fixture);
    save_frame("rapid-paging");
    test_scene_images(sample.night_mode);

    /* A váltás fázisidői kimérve: az eszközön ebből derül ki, hol megy el az idő. */
    /* Asztalon az óra csak a kirajzoláskor lép, ezért a fázisok nullák lehetnek;
     * a bekötést és a sorrendet ellenőrizzük, nem a hosszukat. */
    assert(template_switch.started_us && !strcmp(template_switch.id, template_view.id));
    assert(template_switch.loading_us >= 0 && template_switch.load_us >= 0);
    assert(template_switch.build_us >= 0 && template_switch.apply_us >= 0);
    assert(template_switch.build_done_us >= template_switch.started_us);
    assert(!template_switch.frame_pending);
    printf("SWITCH_PHASES %s loading=%lld us load=%lld us apply=%lld us build=%lld us\n",
           template_switch.id, (long long)template_switch.loading_us,
           (long long)template_switch.load_us, (long long)template_switch.apply_us,
           (long long)template_switch.build_us);

    /* Elakadt betöltés: betöltőképernyő, amelyen semmi nem mozdul tovább.
     * A felépítés ilyenkor minden körben visszatér, a navigáció halott. */
    assert(app_templates_select("10-battery-diagnostics") == ESP_OK);
    assert(template_loading_frame());
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    assert(s_view == VIEW_DASHBOARD && !strcmp(template_view.id, "10-battery-diagnostics"));
    template_waiting_for_load = true;
    template_loading_screen();
    for (unsigned round = 0; round < 3; round++)
    {
        assert(template_loading_frame());
        template_prepare();
        render_runtime(runtime, sample, true, 31000000);
        assert(s_view == VIEW_THEME_LOADING && template_waiting_for_load);
    }
    ticks += TEMPLATE_LOADING_STUCK_US + 1;
    assert(template_loading_frame());
    assert(!template_waiting_for_load);
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    assert(s_view == VIEW_DASHBOARD);
    puts("PASS: rapid paging keeps swipes, frees the previous theme before loading and recovers from a stuck loading screen");
}

/*
 * Előre dekódolt segédfájlok: a betöltés elkészíti a hiányzókat, utána a kép
 * PNG-dekódolás és LVGL-méretezés nélkül kerül a jelenetbe, és a kirajzolt
 * képkocka gyakorlatilag azonos a PNG-útéval (csak az RGB565 kvantálás tér el).
 */
static void test_raw_sidecars(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    const char *theme = "05-calm-home";
    sample.sun_position_valid = true;
    s_animation_sample = sample;
    /* Tiszta kiindulás: a korábbi futás segédfájljai nélkül (a CI ugyanabban a
     * könyvtárban többször is lefuttatja a preview-t). */
    char directory[512];
    snprintf(directory, sizeof(directory), "%s/%s", getenv("ZOLEE_TEMPLATE_ROOT"), theme);
    DIR *folder = opendir(directory);
    assert(folder);
    struct dirent *entry;
    unsigned removed = 0;
    while ((entry = readdir(folder)))
    {
        size_t length = strlen(entry->d_name);
        if (length < 5 || strcmp(entry->d_name + length - 4, ".raw"))
            continue;
        char victim[640];
        snprintf(victim, sizeof(victim), "%s/%s", directory, entry->d_name);
        assert(remove(victim) == 0);
        removed++;
    }
    closedir(folder);
    /* Referencia a megszokott PNG-úton, segédfájlok nélkül. */
    setenv("ZOLEE_DISABLE_RAW", "1", 1);
    assert(app_templates_select(theme) == ESP_OK);
    assert(template_loading_frame());
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    assert(!strcmp(template_view.id, theme) && s_view == VIEW_DASHBOARD);
    template_update(&sample, &fixture);
    save_frame("raw-sidecar-reference");
    static uint32_t reference[1280 * 800];
    for (unsigned i = 0; i < 1280 * 800; i++)
        reference[i] = pixel_rgb(i);
    assert(!template_view.raw_count);
    for (unsigned i = 0; i < template_view.asset_count; i++)
        assert(template_view.assets[i].bytes);
    unsetenv("ZOLEE_DISABLE_RAW");

    /* A segédfájlokat a háttérlépés készíti el, fájlonként egy hívásban. */
    ticks += 4000000;
    unsigned steps = 0;
    while (app_templates_raw_step() && steps < 400)
        steps++;
    printf("RAW_SIDECAR removed=%u steps=%u\n", removed, steps);
    fflush(stdout);
    /* A törölt fájlok biztosan újrakészültek. */
    assert(steps >= removed && steps >= 2);

    /* Újratöltés: most már a segédfájlokból dolgozunk. */
    assert(app_templates_select("builtin") == ESP_OK);
    assert(template_loading_frame());
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    assert(app_templates_select(theme) == ESP_OK);
    assert(template_loading_frame());
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    assert(!strcmp(template_view.id, theme));
    assert(template_view.raw_count >= 2);
    char path[512];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s/day-1280x800.raw", getenv("ZOLEE_TEMPLATE_ROOT"), theme);
    assert(stat(path, &st) == 0 && st.st_size == APP_TEMPLATE_RAW_HEADER + 1280 * 800 * 2);
    unsigned without_png = 0;
    for (unsigned i = 0; i < template_view.asset_count; i++)
        if (!template_view.assets[i].bytes)
            without_png++;
    assert(without_png >= 2);
    /* A háttérhez nem kellett se dekódolás, se raszter. */
#if LV_USE_SNAPSHOT
    unsigned rasters = template_raster_count;
#endif
    template_update(&sample, &fixture);
    save_frame("raw-sidecar-loaded");
#if LV_USE_SNAPSHOT
    assert(template_raster_count <= rasters);
#endif
    unsigned worst = 0, strong = 0, background = 0;
    for (unsigned i = 0; i < 1280 * 800; i++)
    {
        uint32_t a = reference[i], b = pixel_rgb(i);
        unsigned delta = 0;
        for (unsigned shift = 0; shift <= 16; shift += 8)
        {
            unsigned channel = (unsigned)abs((int)((a >> shift) & 255) - (int)((b >> shift) & 255));
            if (channel > delta)
                delta = channel;
        }
        if (delta > worst)
            worst = delta;
        if (delta > 8)
            strong++;
        /* A fénykép-háttér alsó sávjában nincs ikon: ott csak az RGB565
         * kvantálás térhet el, a kép maga képpontra ugyanaz. */
        if (i >= 700 * 1280 && delta > 8)
            background++;
    }
    printf("RAW_SIDECAR strong=%u worst=%u background=%u\n", strong, worst, background);
    fflush(stdout);
    /* A natív méretű háttér azonos; az ikonok átméretezése a telepítéskori
     * bilineáris szűrővel készül, ezért az ikonok képpontjai térhetnek el. */
    assert(!background);
    assert(strong < 1280 * 800 / 100);
    puts("PASS: raw sidecars are built step by step in the background, replace the PNG path and render the same frame");
}

static void test_startup_navigation(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    app_template_info_t themes[16];
    char active[33];
    unsigned count = app_templates_list(themes, 16, active);
    assert(count == 11 && !home_is_open());
    unsigned resource_reads = home_resource_reads;
    sample.sun_position_valid = true;
    for (unsigned theme = 0; theme < count; theme++)
    {
        assert(app_templates_select(themes[theme].id) == ESP_OK);
        assert(template_loading_frame());
        template_prepare();
        render_runtime(runtime, sample, true, 31000000);
        for (unsigned direction = 0; direction < 2; direction++)
        {
            unsigned generation = template_pending.generation;
            lv_obj_update_layout(s_screen);
            slow_swipe(640, 400, direction ? 8 : -8, 0);
            if (!app_templates_has_pending(generation))
                fprintf(stderr, "Startup slow swipe failed: %s direction %u\n", themes[theme].id, direction);
            assert(app_templates_has_pending(generation) && template_loading_requested);
            assert(template_loading_frame());
            template_prepare();
            render_runtime(runtime, sample, true, 31000000);
        }
        assert(!strcmp(template_view.id, themes[theme].id));
        lv_obj_update_layout(s_screen);
        slow_swipe(640, 790, 0, -8);
        assert(s_diagnostics_requested);
        render_runtime(runtime, sample, true, 31000000);
        assert(s_view == VIEW_DIAGNOSTICS);
        lv_obj_update_layout(s_screen);
        touch_sample_xy(1216, 56, true);
        touch_sample_xy(1216, 56, false);
        assert(!s_diagnostics_requested);
        assert(template_loading_frame());
        template_prepare();
        render_runtime(runtime, sample, true, 31000000);
        assert(!home_is_open() && home_resource_reads == resource_reads);
    }
    puts("PASS: startup slow left/right/up swipes on all 10 themes with repeated cached touch samples, without opening Home");
}

/* Valódi pointer-események: a visszatérés utáni navigációt a közvetlen
 * home_gesture hívások nem fedik le. Az X és a következő húzás közt a
 * firmware runtime sorrendjével cseréljük le a teljes objektumfát. */
static void test_home_return_navigation(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    const char *themes[] = {"builtin", "05-calm-home", "06-energy-orbit", "09-solar-journey"};
    app_sim_home_reset();
    navigation_bind_inputs();
    navigation_bind_inputs();
    lv_indev_t *input = lv_indev_get_next(NULL);
    unsigned bound = 0;
    for (uint32_t i = 0; i < lv_indev_get_event_count(input); i++)
        if (lv_event_dsc_get_cb(lv_indev_get_event_dsc(input, i)) == template_gesture)
            bound++;
    assert(bound == 1);
    /* Elmaradó refresh-visszajelzésnél egyetlen SD-képolvasás sem indulhat.
     * A következő runtime kör a meglévő betöltőoldallal helyreállhat. */
    assert(app_templates_select("builtin") == ESP_OK);
    assert(template_loading_frame());
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    /* A 90 ms-os rajzolás után a következő esedékes kép és az input is
     * fusson le újabb 33 ms-os várakozás nélkül. */
    lv_timer_t *refresh = lv_display_get_refr_timer(display);
    lv_timer_reset(refresh);
    unsigned cadence_frames = frame_flushes;
    lv_obj_invalidate(s_screen);
    ticks += 15000;lv_tick_inc(15);lv_timer_handler();
    assert(frame_flushes > cadence_frames);
    slow_flush_ms = 90;
    lv_obj_invalidate(s_screen);
    lv_timer_ready(lv_display_get_refr_timer(display));
    lv_timer_handler();
    unsigned frames = frame_flushes;
    unsigned reads_after_frame = pointer_reads;
    lv_obj_invalidate(s_screen);
    lv_timer_handler();
    assert(pointer_reads > reads_after_frame);
    assert(frame_flushes > frames);
    puts("PASS: simulated 90 ms frame resumes due rendering and pointer polling without an added frame cooldown");
    lv_display_remove_event_cb_with_user_data(display, initial_refresh_report, NULL);
    home_request_set(1);
    unsigned reads = home_resource_reads;
    assert(!home_prepare(false));
    assert(home_resource_reads == reads && !home_assets.asset_count);
    assert(s_view == VIEW_THEME_LOADING && !home_ready_to_draw);
    lv_display_add_event_cb(display, initial_refresh_report, LV_EVENT_REFR_READY, NULL);
    assert(home_prepare(false));
    render_runtime(runtime, sample, true, 31000000);
    assert(s_view == VIEW_HOME);
    home_close(NULL);
    assert(home_prepare(false));
    template_prepare();
    render_runtime(runtime, sample, true, 31000000);
    sample.sun_position_valid = true;
    for (unsigned theme = 0; theme < sizeof(themes) / sizeof(themes[0]); theme++)
    {
        for (unsigned page = 1; page <= 2; page++)
        {
            sample.night_mode = page % 2;
            assert(app_templates_select(themes[theme]) == ESP_OK);
            assert(template_loading_frame());
            template_prepare();
            render_runtime(runtime, sample, true, 31000000);
            home_request_set(page);
            require_loading_flush = true;
            loading_flush_baseline = frame_flushes;
            home_prepare(sample.night_mode);
            require_loading_flush = false;
            render_runtime(runtime, sample, true, 31000000);
            assert(s_view == VIEW_HOME);
            lv_obj_update_layout(s_screen);
            touch_sample_xy(1204, 60, true);
            touch_sample_xy(1204, 60, false);
            assert(!home_is_open());
            assert(template_loading_frame());
            loading_flush_baseline = frame_flushes;
            home_prepare(sample.night_mode);
            assert(frame_flushes > loading_flush_baseline);
            template_prepare();
            render_runtime(runtime, sample, true, 31000000);
            assert(s_view == VIEW_DASHBOARD && !strcmp(template_view.id, themes[theme]));
            assert(!lv_obj_has_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE));
            /* A visszaálló témára szándékosan olyan érinthető fedőréteg
             * kerül, amely nem buborékoltat. A navigáció így is működjön. */
            lv_obj_t *overlay = lv_obj_create(s_screen);
            lv_obj_remove_style_all(overlay);
            lv_obj_set_pos(overlay, 0, 0);
            lv_obj_set_size(overlay, W, H);
            lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
            lv_obj_update_layout(s_screen);
            touch_sample_xy(640, 790, true);
            touch_sample_xy(640, 600, true);
            touch_sample_xy(640, 400, true);
            touch_sample_xy(640, 400, false);
            if (!s_diagnostics_requested)
                fprintf(stderr, "Return navigation failed: %s page %u\n", themes[theme], page);
            assert(s_diagnostics_requested);
            render_runtime(runtime, sample, true, 31000000);
            assert(s_view == VIEW_DIAGNOSTICS);
            lv_obj_update_layout(s_screen);
            touch_sample_xy(1216, 56, true);
            touch_sample_xy(1216, 56, false);
            assert(!s_diagnostics_requested);
            assert(template_loading_frame());
            template_prepare();
            render_runtime(runtime, sample, true, 31000000);
            for (unsigned direction = 0; direction < 2; direction++)
            {
                unsigned generation = template_pending.generation;
                int start = direction ? 300 : 1000;
                int end = direction ? 1000 : 300;
                lv_obj_update_layout(s_screen);
                touch_sample_xy(start, 400, true);
                touch_sample_xy((start + end) / 2, 400, true);
                touch_sample_xy(end, 400, true);
                touch_sample_xy(end, 400, false);
                assert(app_templates_has_pending(generation));
                assert(template_loading_requested);
                loading_flush_baseline = frame_flushes;
                assert(template_loading_frame());
                assert(frame_flushes > loading_flush_baseline);
                assert(s_view == VIEW_THEME_LOADING);
                template_prepare();
                render_runtime(runtime, sample, true, 31000000);
                assert(s_view == VIEW_DASHBOARD);
            }
            assert(!strcmp(template_view.id, themes[theme]));
        }
    }
    puts("PASS: real X close followed by bottom-edge diagnostics and both theme swipes after consumer/Home return on builtin/05/06/09 day/night");
}

static void test_home_lifecycle(app_runtime_state_t runtime, app_energy_sample_t sample)
{
    const char *themes[] = {"builtin", "05-calm-home", "06-energy-orbit"};
    char language[APP_LANGUAGE_MAX];
    snprintf(language, sizeof(language), "%s", s_cfg.project.language);
    app_sim_home_reset();
    unsigned commands = app_sim_home_commands;
    unsigned page_widgets[4][2][2] = {{{0}}};
    for (unsigned cycle = 0; cycle < 100; cycle++)
    {
        bool night = cycle % 2;
        unsigned english = (cycle / 2) % 2;
        const char *theme = themes[(cycle / 4) % 3];
        snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", english ? "en" : "hu");
        sample.night_mode = night;
        assert(app_templates_select(theme) == ESP_OK);
        template_prepare();
        render_runtime(runtime, sample, true, 31000000);
        lv_obj_update_layout(s_screen);
        lv_refr_now(display);
        assert(s_view == VIEW_DASHBOARD);
        unsigned baseline_widgets = widget_count(s_screen);
        unsigned baseline_animations = lv_anim_count_running();
        /* Egy ciklus: fogyasztók, három Otthon-lap, majd az eredeti téma.
         * Minden nézetet valóban kirajzolunk: a PNG-dekóder is részt vesz. */
        home_open_clicked(NULL);
        for (unsigned page = 0; page < 4; page++)
        {
            if (page == 1)
                home_cards_requested();
            else if (page > 1)
                home_gesture(LV_DIR_LEFT);
            home_prepare(night);
            render_runtime(runtime, sample, true, 31000000);
            lv_obj_update_layout(s_screen);
            lv_refr_now(display);
            assert(s_view == VIEW_HOME && home_ready_to_draw);
            test_home_background_pixels();
            assert(lv_anim_count_running() == 0);
            assert(home_assets.asset_count >= 2 && home_assets.asset_count <= 14);
            assert(!template_view.asset_count && !template_pending.asset_count && !template_raster_count);
            assert(home_page == (page ? 2 : 1));
            assert(!page || home_card_page == page - 1);
            const char *background = page ? (night ? "background-dark.png" : "background-light.png")
                                          : (night ? "consumers-dark.png" : "consumers-light.png");
            assert(!strcmp(home_assets.assets[0].name, background));
            unsigned count = widget_count(s_screen);
            unsigned *expected = &page_widgets[page][night][english];
            if (!*expected)
                *expected = count;
            assert(count == *expected);
            assert(app_sim_home_commands == commands);
        }
        home_close(NULL);
        home_prepare(night);
        assert(s_view == VIEW_THEME_LOADING);
        template_prepare();
        render_runtime(runtime, sample, true, 31000000);
        lv_obj_update_layout(s_screen);
        lv_refr_now(display);
        assert(s_view == VIEW_DASHBOARD && !home_is_open() && !home_page);
        if (!strcmp(theme, "builtin"))
            assert(!template_is_active());
        else
            assert(template_is_active() && !strcmp(template_view.id, theme));
        assert(!home_assets.asset_count && !home_ready_to_draw);
        for (unsigned i = 0; i < sizeof(home_gray_buffers) / sizeof(home_gray_buffers[0]); i++)
            assert(!home_gray_buffers[i] && !home_buffers[i] && !home_images[i].data);
        assert(widget_count(s_screen) == baseline_widgets);
        /* A témaválasztás rövid áttűnése a visszatéréskor már elmaradhat.
         * Plusz animáció nem maradhat, a köztes Otthon-lapokon egy sem futhat. */
        assert(lv_anim_count_running() <= baseline_animations);
        assert(app_sim_home_commands == commands);
        if ((cycle + 1) % 25 == 0)
        {
            printf("Lifecycle progress: %u/100 complete cycles\n", cycle + 1);
            fflush(stdout);
        }
    }
    snprintf(s_cfg.project.language, sizeof(s_cfg.project.language), "%s", language);
    puts("PASS: 100 actual LVGL lifecycle cycles, 400 rendered consumer/Home pages, HU/EN day/night, builtin/05/06 return, stable widgets/animations, released image buffers and no commands");
}

static void test_intercom_frame_mailbox(void)
{
    int old_mode = s_cfg.data_recept_mode;
    bool loading = template_loading_requested, waiting = template_waiting_for_load;
    s_cfg.data_recept_mode = APP_DATA_LOXONE;
    app_sim_intercom = (app_intercom_snapshot_t){.ringing=true,.connected=true,.revision=17};
    assert(intercom_prepare());
    assert(!intercom_overlay && intercom_prepared && intercom_spare);
    uint8_t pixels[24];
    memset(pixels, 0x34, sizeof(pixels));
    reject_video_lock = true;
    unsigned locks = video_lock_attempts;
    /* Már a nézet létrehozása előtt is átvehető kép, akkor is, ha a
     * régi 100 ms-os display-lock minden alkalommal időtúllépne. */
    assert(app_intercom_ui_frame(pixels, 4, 2, 12, 17));
    memset(pixels, 0x56, sizeof(pixels));
    assert(!app_intercom_ui_frame(pixels, 4, 2, 12, 16));
    intercom_render();
    assert(intercom_descriptor.header.w == 4 && intercom_descriptor.header.stride == 8);
    assert(intercom_descriptor.data_size == 16);
    for (unsigned i = 0; i < 16; i++) assert(intercom_front[i] == 0x34);
    uint8_t *shown = intercom_front;
    assert(app_intercom_ui_frame(pixels, 4, 2, 12, 17));
    for (unsigned i = 0; i < 16; i++) assert(shown[i] == 0x34);
    memset(pixels, 0x78, sizeof(pixels));
    assert(app_intercom_ui_frame(pixels, 4, 2, 12, 17));
    intercom_frame_writing = true;
    intercom_render();
    assert(intercom_front == shown);
    intercom_frame_writing = false;
    intercom_render();
    for (unsigned i = 0; i < 16; i++) assert(intercom_front[i] == 0x78);
    assert(video_lock_attempts == locks);
    app_sim_intercom.ringing = false;
    app_sim_media_idle = false;
    assert(intercom_prepare());
    intercom_render();
    assert(!intercom_overlay && intercom_front && intercom_spare);
    assert(!app_intercom_ui_frame(pixels, 4, 2, 12, 17));
    uint8_t *retained = intercom_front;
    app_sim_intercom.ringing = true;
    app_sim_intercom.revision = 18;
    assert(intercom_prepare());
    intercom_render();
    assert(intercom_front == retained);
    assert(!app_intercom_ui_frame(pixels, 4, 2, 12, 17));
    assert(app_intercom_ui_frame(pixels, 4, 2, 12, 18));
    intercom_render();
    app_sim_intercom.ringing = false;
    app_sim_media_idle = true;
    assert(!intercom_prepare());
    intercom_render();
    assert(!intercom_needs_cleanup());
    reject_video_lock = false;
    s_cfg.data_recept_mode = old_mode;
    template_loading_requested = loading;
    template_waiting_for_load = waiting;
    puts("PASS: actual camera mailbox accepts frames without LVGL lock, before overlay, latest-only, padded rows, stable displayed buffer, stale rejection and deferred cleanup");
}

static void test_energy_disabled_home(app_runtime_state_t rt)
{
    s_cfg.energy_flow_disabled = true;
    s_cfg.data_recept_mode = APP_DATA_LOXONE;
    s_cfg.display.day_brightness = 83;
    s_cfg.display.night_brightness = 24;
    app_sim_home_reset();
    app_sim_home_config.card_count = app_sim_home_config.page_count = 0;
    home_request_set(0);
    unsigned reads = home_resource_reads;
    assert(home_prepare(false));
    assert(home_empty && !home_is_open() && home_resource_reads == reads);
    render_runtime(rt, (app_energy_sample_t){0}, false, ticks);
    assert(s_view == VIEW_ENERGY_DISABLED);
    master_display_update(false, false, false);
    assert(last_pwm_brightness == 10);
    save_frame("master-energy-disabled-empty");
    for (unsigned night = 0; night < 2; night++)
    {
        app_sim_intercom = (app_intercom_snapshot_t){.ringing = true, .connected = true, .revision = 400 + night};
        bool active = intercom_prepare();
        assert(active);
        master_display_update(false, night, active);
        assert(last_pwm_brightness == (night ? 24 : 83));
        intercom_render();
        assert(intercom_overlay);
        app_sim_intercom.ringing = false;
        assert(!intercom_prepare());
        intercom_render();
        assert(home_prepare(night));
        master_display_update(false, night, false);
        render_runtime(rt, (app_energy_sample_t){0}, false, ticks);
        assert(s_view == VIEW_ENERGY_DISABLED && last_pwm_brightness == 10);
        assert(home_resource_reads == reads);
    }
    app_sim_home_reset();
    assert(home_prepare(false));
    render_runtime(rt, (app_energy_sample_t){0}, false, ticks);
    assert(s_view == VIEW_HOME && home_request_get() == 2);
    home_close(NULL);
    assert(home_request_get() == 2);
    home_open_clicked(NULL);
    assert(home_request_get() == 2);
    s_diagnostics_requested = true;
    render_runtime(rt, (app_energy_sample_t){0}, false, ticks);
    assert(s_view == VIEW_DIAGNOSTICS);
    diagnostics_close(NULL);
    assert(home_prepare(false));
    render_runtime(rt, (app_energy_sample_t){0}, false, ticks);
    assert(s_view == VIEW_HOME);
    s_cfg.energy_flow_disabled = false;
    home_close(NULL);
    assert(home_prepare(false));
    app_sim_home_enabled = false;
    puts("PASS: actual Master LVGL logo with no Home resource reads, immediate 10/83/24/10 PWM, Intercom return, Home without energy samples, blocked energy navigation and diagnostic return");
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
    rgb565 = getenv("ZOLEE_PREVIEW_RGB565") != NULL;
    lv_display_set_color_format(display,rgb565 ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_XRGB8888);
    full_refresh = getenv("ZOLEE_PREVIEW_FULL_REFRESH") != NULL;
    lv_display_set_buffers(display,
        full_refresh ? (void *)pixels : (void *)partial_pixels, NULL,
        full_refresh ? sizeof(pixels) : 800 * 50 * (rgb565 ? 2 : 4),
        full_refresh ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    assert(app_ui_init(270)==ESP_OK);
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
    unsigned changed_flushes=frame_flushes;
    label_text_if_changed(text_probe,"Changed");lv_obj_update_layout(s_screen);lv_refr_now(display);
    assert(full_refresh ? frame_flushes>changed_flushes : invalidations>0);
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
    test_intercom_frame_mailbox();
    if (getenv("ZOLEE_INTERCOM_MAILBOX_ONLY")) {
        lv_display_delete(display); lv_deinit(); return 0;
    }
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
    if (getenv("ZOLEE_ENERGY_DISABLED_ONLY") || getenv("ZOLEE_NAVIGATION_ONLY") || getenv("ZOLEE_RENDER_COST_ONLY") || getenv("ZOLEE_CONSUMER_NOISE_ONLY") || getenv("ZOLEE_THEME_SWITCH_BENCH") || getenv("ZOLEE_EXPORT_SCALED_BACKGROUNDS"))
    {
        assert(app_templates_init() == ESP_OK);
        char error[160];
        assert(app_templates_install(getenv("ZOLEE_HOME_RESOURCE_ZIP"), error) == ESP_OK);
        config.data_recept_mode = APP_DATA_LOXONE;
        app_ui_set_config(&config);
        app_runtime_state_t runtime = {.data_recept_mode = APP_DATA_LOXONE,
            .wifi_connected = true, .loxone_configured = true, .loxone_connected = true,
            .has_valid_sample = true, .last_valid_packet_monotonic_us = 31000000};
        lv_indev_t *input = lv_indev_create();
        lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(input, display);
        lv_indev_set_read_cb(input, touch_read);
        navigation_bind_inputs();
        if (getenv("ZOLEE_ENERGY_DISABLED_ONLY"))
            test_energy_disabled_home(runtime);
        else if (getenv("ZOLEE_CONSUMER_NOISE_ONLY"))
        {
            app_sim_home_reset();
            home_request_set(1);
            assert(home_prepare(false));
            render_runtime(runtime, sample, true, 31000000);
            test_consumer_noise(runtime, sample);
            home_close(NULL);
            assert(home_prepare(false));
        }
        else if (getenv("ZOLEE_RENDER_COST_ONLY"))
            test_render_work(runtime, sample);
        else if (getenv("ZOLEE_THEME_SWITCH_BENCH"))
            test_theme_switch_bench(runtime, sample);
        else if (getenv("ZOLEE_EXPORT_SCALED_BACKGROUNDS"))
            export_scaled_backgrounds(runtime, sample, getenv("ZOLEE_EXPORT_SCALED_BACKGROUNDS"));
        else
        {
            test_startup_navigation(runtime, sample);
            test_home_return_navigation(runtime, sample);
        }
        lv_indev_delete(input);
        clear();template_release_cache();app_templates_view_free(&template_view);
        lv_display_delete(display);lv_deinit();
        return 0;
    }
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
    app_battery_fixture(&fixture.battery, config.energy.battery_capacity_kwh, epoch);
    s_stats_snapshot=fixture;app_stats_get_period(false,&s_week_period);app_stats_get_period(true,&s_month_period);
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);save_frame("http-recovered-hu");
    snprintf(config.project.language,sizeof(config.project.language),"en");app_ui_set_config(&config);
    render_runtime(rt,sample,true,61000000);assert(s_view==VIEW_DATA_TIMEOUT);save_frame("http-timeout-en");
    test_signed_and_soc_charts();
    test_hourly_chart_grid();
    snprintf(config.project.language,sizeof(config.project.language),"hu");app_ui_set_config(&config);
    app_events_init();
    for(int i=0;i<20;i++)app_events_add(i%3,"Teszt esemény %02d",i);
    assert(app_diagnostics_cpu_percent(0,2000000)==100);
    assert(app_diagnostics_cpu_percent(1000000,2000000)==50);
    assert(app_diagnostics_cpu_percent(2000001,2000000)==0);
    assert(app_diagnostics_cpu_percent(0,0)==0);
    /* Zárt nézetnél nincs 200 ms-onkénti diagnosztikai lekérdezés. */
    s_diagnostics_requested=false;s_diagnostic_event_count=0;diagnostics_capture();
    assert(!s_diagnostics_captured&&s_diagnostic_event_count==0);
    /* A kör pillanatképe utáni kérés is friss adattal nyit. */
    s_diagnostics_requested=true;
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DIAGNOSTICS);
    assert(s_diagnostics_captured&&s_diagnostic_event_count==15);
    diagnostics_capture();
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DIAGNOSTICS);
    assert(s_diagnostic_event_count==15);
    assert(strstr(lv_label_get_text(s_diagnostic_log[0]),"19"));
    assert(strstr(lv_label_get_text(s_diagnostic_log[14]),"05"));
    lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(lv_obj_get_parent(s_diagnostic_log[0]))==640);
    for(int i=0;i<14;i++){
        lv_area_t row,parent;lv_obj_get_coords(s_diagnostic_values[i],&row);
        lv_obj_get_coords(lv_obj_get_parent(s_diagnostic_values[i]),&parent);
        assert(row.y2<parent.y2&&row.x2<parent.x2);
    }
    rt.data_recept_mode=APP_DATA_LOXONE;s_diagnostics.cpu_valid=true;s_diagnostics.cpu_percent[0]=67;s_diagnostics.cpu_percent[1]=29;
    s_diagnostics.total_internal_kib=512;s_diagnostics.free_internal_kib=180;
    s_diagnostics.total_psram_kib=28000;s_diagnostics.free_psram_kib=12000;
    s_diagnostics.sd_space_valid=true;s_diagnostics.sd_total_bytes=16ULL*1024*1024*1024;s_diagnostics.sd_free_bytes=12ULL*1024*1024*1024;rt.sd_ok=true;
    render_runtime(rt,sample,true,31000000);assert(strstr(lv_label_get_text(s_diagnostic_values[1]),"Loxone"));
    assert(lv_bar_get_value(s_diagnostic_bars[0])==25);assert(strstr(lv_label_get_text(s_diagnostic_values[5]),"67%"));
    save_frame("diagnostics-common");
    rt.ble_enabled=rt.ble_master=rt.ble_ready=rt.ble_advertising=true;
    rt.ble_slaves=2;snprintf(rt.ble_address,sizeof(rt.ble_address),"01:02:03:04:05:06");
    snprintf(s_cfg.data_seed_pin,sizeof(s_cfg.data_seed_pin),"012345");
    clear();
    render_runtime(rt,sample,true,31000000);
    assert(strcmp(lv_label_get_text(s_diagnostic_values[11]),"2")==0);
    assert(strstr(lv_label_get_text(s_diagnostic_values[8]),"Master"));
    assert(strstr(lv_label_get_text(s_diagnostic_values[10]),"012345"));
    assert(s_pair_window_label);
    lv_obj_send_event(lv_obj_get_parent(s_pair_window_label),LV_EVENT_CLICKED,NULL);
    assert(pairing_open_requests==1);
    pairing_fixture.seconds=60;diagnostics_render(rt);
    assert(lv_color_to_u32(lv_obj_get_style_bg_color(lv_obj_get_parent(s_pair_window_label),0))==0xFF0082FC);
    pairing_fixture.seconds=0;diagnostics_render(rt);
    assert(lv_color_to_u32(lv_obj_get_style_bg_color(lv_obj_get_parent(s_pair_window_label),0))==0xFF284C68);
    lv_obj_update_layout(s_screen);
    lv_area_t button_bounds, caption_bounds;
    lv_obj_get_coords(lv_obj_get_parent(s_pair_window_label),&button_bounds);
    lv_obj_get_coords(s_pair_window_label,&caption_bounds);
    assert(abs((button_bounds.x1+button_bounds.x2)-(caption_bounds.x1+caption_bounds.x2))<=2);
    assert(abs((button_bounds.y1+button_bounds.y2)-(caption_bounds.y1+caption_bounds.y2))<=2);
    assert(lv_obj_get_x(lv_obj_get_parent(s_diagnostic_values[0]))==32);
    assert(lv_obj_get_x(lv_obj_get_parent(s_diagnostic_values[1]))==328);
    save_frame("diagnostics-master");
    app_data_mode_t previous_mode=s_cfg.data_recept_mode;
    s_cfg.data_recept_mode=rt.data_recept_mode=APP_DATA_SLAVE;
    rt.ble_master=rt.ble_advertising=rt.wifi_connected=false;
    rt.slave_connected=rt.time_synced=rt.has_valid_sample=true;
    rt.last_valid_packet_monotonic_us=rt.slave_packet_monotonic_us=31000000;
    rt.slave_source_timeout_seconds=30;rt.ble_rssi=-62;
    snprintf(rt.ble_peer,sizeof(rt.ble_peer),"06:05:04:03:02:01");
    bool previous_started=s_started;
    app_boot_stage_t previous_boot=s_boot_stage;
    runtime_wifi_connected=false;s_started=false;
    app_ui_show_boot(APP_BOOT_NTP,"Master keresése...");
    animation_tick(NULL);assert(s_view==VIEW_NONE);
    save_frame("slave-boot-no-wifi");
    s_started=previous_started;s_boot_stage=previous_boot;runtime_wifi_connected=true;
    clear();render_runtime(rt,sample,true,31000000);
    assert(strstr(lv_label_get_text(s_diagnostic_values[1]),"Slave"));
    assert(strstr(lv_label_get_text(s_diagnostic_values[11]),"-62"));
    assert(strstr(lv_label_get_text(s_diagnostic_values[13]),"Bluetooth"));
    save_frame("diagnostics-slave");
    pairing_fixture=(app_ble_pairing_status_t){.pin_required=true,.token=7,.seconds=25};
    animation_tick(NULL);assert(s_pair_pad);
    save_frame("slave-pairing-pin");
    lv_indev_t *pin_input=lv_indev_create();lv_indev_set_type(pin_input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pin_input,display);lv_indev_set_read_cb(pin_input,touch_read);
    lv_obj_update_layout(s_screen);
    touch_sample_xy(636,580,true);touch_sample_xy(636,580,false);
    assert(!strcmp(s_pair_digits,"0"));
    memset(s_pair_digits,0,sizeof(s_pair_digits));pairing_mask_update();
    touch_sample_xy(120,200,true);touch_sample_xy(120,400,true);touch_sample_xy(120,600,true);touch_sample_xy(120,600,false);
    assert(s_diagnostics_requested);
    lv_indev_delete(pin_input);
    /* Valódi LVGL gombesemények: vezető nulla, törlés, túl hosszú és rövid PIN. */
    for(unsigned i=0;i<lv_obj_get_child_count(s_pair_pad);i++){
        lv_obj_t *child=lv_obj_get_child(s_pair_pad,(int)i);
        if(lv_obj_check_type(child,&lv_button_class)){
            lv_obj_t *caption=lv_obj_get_child(child,0);
            const char *text=lv_label_get_text(caption);
            if(!strcmp(text,"OK"))lv_obj_send_event(child,LV_EVENT_CLICKED,NULL);
        }
    }
    assert(pairing_fixture.pin_required&&!submitted_pin[0]);
    for(unsigned digit=0;digit<6;digit++){
        for(unsigned i=0;i<lv_obj_get_child_count(s_pair_pad);i++){
            lv_obj_t *child=lv_obj_get_child(s_pair_pad,(int)i);
            if(!lv_obj_check_type(child,&lv_button_class))continue;
            const char *caption=lv_label_get_text(lv_obj_get_child(child,0));
            if(caption[0]==(char)('0'+digit)&&!caption[1])lv_obj_send_event(child,LV_EVENT_CLICKED,NULL);
        }
    }
    assert(!strcmp(s_pair_digits,"012345"));
    for(unsigned i=0;i<lv_obj_get_child_count(s_pair_pad);i++){
        lv_obj_t *child=lv_obj_get_child(s_pair_pad,(int)i);
        if(lv_obj_check_type(child,&lv_button_class)&&!strcmp(lv_label_get_text(lv_obj_get_child(child,0)),"OK"))
            lv_obj_send_event(child,LV_EVENT_CLICKED,NULL);
    }
    assert(!strcmp(submitted_pin,"012345")&&!s_pair_digits[0]);
    animation_tick(NULL);assert(!s_pair_pad);
    s_diagnostics_requested=false;render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_DASHBOARD); /* Slave Wi-Fi nélkül is normál kijelző. */
    /* A kártya nélküli slave csak a beépített nézetet használhatja. */
    s_cfg.cardless_slave=rt.cardless_slave=true;sd_ok=false;
    fixture.remote=true;fixture.remote_available=false;fixture.remote_stale=true;
    s_stats_snapshot=fixture;
    render_runtime(rt,sample,true,31000000);
    assert(!template_is_active()&&template_catalogue_count()==1);
    assert(!strcmp(lv_label_get_text(s_metric[0]),"--"));
    assert(strstr(lv_label_get_text(s_sd_warn),"szinkron"));
    assert(!lv_obj_has_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN));
    assert(template_loading_frame());template_prepare();template_apply();
    assert(s_view==VIEW_DASHBOARD&&!template_is_active());
    save_frame("cardless-slave-sync");
    fixture.remote_available=true;fixture.remote_stale=false;
    fixture.reference_valid=true;fixture.reference_energy=s_cfg.energy;
    fixture.reference_energy.electricity_purchase_price=321;
    snprintf(fixture.reference_currency,sizeof(fixture.reference_currency),"EUR");
    s_stats_snapshot=fixture;
    render_runtime(rt,sample,true,31000000);
    assert(strcmp(lv_label_get_text(s_metric[0]),"--"));
    assert(lv_obj_has_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN));
    assert(s_display_energy.electricity_purchase_price==321);
    save_frame("cardless-slave-fresh");
    fixture.remote_stale=true;s_stats_snapshot=fixture;render_runtime(rt,sample,true,31000000);
    assert(strstr(lv_label_get_text(s_sd_warn),"Elavult"));
    assert(!lv_obj_has_flag(s_sd_warn,LV_OBJ_FLAG_HIDDEN));
    save_frame("cardless-slave-stale");
    s_diagnostics_requested=true;render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_DIAGNOSTICS);
    assert(strstr(lv_label_get_text(s_diagnostic_values[3]),"SD nélkül"));
    save_frame("cardless-slave-diagnostics");
    s_cfg.cardless_slave=rt.cardless_slave=false;sd_ok=true;
    fixture.remote=fixture.remote_available=fixture.remote_stale=fixture.reference_valid=false;
    s_stats_snapshot=fixture;
    s_diagnostics_requested=false;render_runtime(rt,sample,true,31000000);
    rt.slave_connected=false;render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_DATA_TIMEOUT);
    s_cfg.data_recept_mode=previous_mode;
    rt.data_recept_mode=APP_DATA_MQTT;rt.wifi_connected=true;
    s_diagnostics_requested=false;render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);
    if(getenv("ZOLEE_TEMPLATE_ROOT")){
        assert(app_templates_init()==ESP_OK);
        app_template_info_t list[APP_TEMPLATE_MAX];char active[33];
        unsigned count=app_templates_list(list,APP_TEMPLATE_MAX,active);assert(count==11);
        sample.power_pv_kw=7;sample.power_bat_kw=-2.5;sample.power_grid_kw=-2.4;sample.power_full_kw=2.1;
        sample.soc_bat_pct=74;sample.status=1;sample.temperature_c=-6;sample.has_sun_radiation=true;
        snprintf(config.project.language,sizeof(config.project.language),"hu");app_ui_set_config(&config);
        for(unsigned i=1;i<count;i++){
            assert(app_templates_select(list[i].id)==ESP_OK);
            assert(app_templates_has_pending(template_pending.generation));
            assert(template_loading_frame());assert(s_view==VIEW_THEME_LOADING);
            lv_obj_t *waiting_spinner=lv_obj_get_child(s_screen,1);
            assert(template_loading_frame());
            assert(lv_obj_get_child(s_screen,1)==waiting_spinner);
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
                test_weather_visibility(&sample);test_template_weather(&sample);test_template_motion();test_template_indicators(&sample);test_flow_rendering();test_solar_battery_route();test_legacy_solar_battery_route();test_theme_readability();test_calm_dashboard(&sample);test_orbit_dashboard(&sample);test_template_grid_restored(&sample);test_solar_status(&sample);test_solar_dashboard(&sample);test_solar_sunset(&sample);test_solar_mode_transitions(rt,&sample);
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
                    app_energy_sample_t fractional=sample;fractional.battery_temperature_c=32.6;template_update(&fractional,&fixture);
                    for(unsigned n=0;n<template_node_count;n++)if(!strcmp(ts(template_nodes[n].spec,"source"),"battery_temp")&&!strcmp(ts(template_nodes[n].spec,"type"),"value")){
                        assert(strstr(lv_label_get_text(template_nodes[n].obj),"33"));assert(!strchr(lv_label_get_text(template_nodes[n].obj),'.'));assert(!strchr(lv_label_get_text(template_nodes[n].obj),','));}
                    template_update(&sample,&fixture);
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
                app_energy_sample_t preview_saved=sample;
                if(!strcmp(list[i].id,"06-energy-orbit")){
                    /* Minden út aktív: ez volt a hardveren lassú kombináció. */
                    sample.power_pv_kw=4;sample.power_grid_kw=1;sample.power_bat_kw=1;sample.power_full_kw=6;
                    sample.soc_bat_pct=74;s_animation_sample=sample;
                    template_update(&sample,&fixture);template_animate(.05f);
                }
                if (!strcmp(list[i].id, "10-battery-diagnostics")) {
                    char text[128];
                    assert(fixture.battery.capacity_valid && fixture.battery.discharge_runs == 3);
                    template_text(text, "battery_diag_capacity_pct", &sample, &fixture);
                    assert(strstr(text, "92"));
                    app_stats_snapshot_t *empty = calloc(1, sizeof(*empty));
                    assert(empty);
                    template_text(text, "battery_diag_capacity", &sample, empty);
                    assert(!strcmp(text, "--"));
                    template_update(&sample, empty);
                    char missing[100];
                    snprintf(missing, sizeof(missing), "battery-diagnostics-empty-%s", night ? "dark" : "light");
                    save_frame(missing);
                    double saved_nominal = s_cfg.energy.battery_capacity_kwh;
                    s_cfg.energy.battery_capacity_kwh = 0;
                    template_text(text, "battery_diag_assessment", &sample, empty);
                    assert(strstr(text, "névleges"));
                    s_cfg.energy.battery_capacity_kwh = saved_nominal;
                    free(empty);
                    /* Hátralévő idő: valódi szöveg és kiemelő szín szigetüzemben. */
                    {
                        app_energy_sample_t eta = sample;
                        double saved_floor = s_cfg.energy.battery_min_soc_pct;
                        s_cfg.energy.battery_min_soc_pct = 10;
                        app_battery_eta_reset(&template_battery_eta_state);
                        eta.soc_bat_pct = 60; eta.power_bat_kw = 2.5; eta.status = 3;
                        eta.received_monotonic_us += 1;
                        template_text(text, "battery_eta", &eta, &fixture);
                        assert(!strcmp(text, "Szigetüzem · Még kb. 3 óráig bírja (10%-ig)"));
                        template_update(&eta, &fixture);
                        for (unsigned n = 0; n < template_node_count; n++)
                            if (!strcmp(ts(template_nodes[n].spec, "source"), "battery_eta"))
                                assert(lv_color_eq(lv_obj_get_style_text_color(template_nodes[n].obj, 0),
                                                   c(night ? 0xFFB45C : 0xC25A00)));
                        app_battery_eta_reset(&template_battery_eta_state);
                        eta.status = 2; eta.power_full_kw = 0.8; eta.received_monotonic_us += 1;
                        template_text(text, "battery_eta", &eta, &fixture);
                        assert(!strcmp(text, "Áramszünetkor kb. 9 óra 23 percig bírná (10%-ig)"));
                        app_battery_eta_reset(&template_battery_eta_state);
                        eta.status = 1; eta.power_bat_kw = -3; eta.received_monotonic_us += 1;
                        template_text(text, "battery_eta", &eta, &fixture);
                        assert(!strcmp(text, "Kb. 2 óra múlva tele"));
                        s_cfg.energy.battery_min_soc_pct = saved_floor;
                        app_battery_eta_reset(&template_battery_eta_state);
                    }
                    sample.soc_bat_pct = 60;
                    sample.power_bat_kw = 2.76;
                    sample.battery_temperature_c = 29;
                    s_animation_sample = sample;
                    template_update(&sample, &fixture);
                }
                char filename[100];snprintf(filename,sizeof(filename),"template-%s-%s",list[i].id,night?"dark":"light");save_frame(filename);
                test_scene_images(night);
                sample=preview_saved;s_animation_sample=sample;
                if(!night&&!strcmp(list[i].id,"01-live-home"))for(int frame=0;frame<12;frame++){
                    template_animate(.12f);snprintf(filename,sizeof(filename),"live-home-flow-%02d",frame);save_frame(filename);
                }
                test_energy_assistant_ui(&sample);
                test_mail_ui(&sample);
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
        lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,touch_read);navigation_bind_inputs();
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
        assert(s_diagnostics_requested);
        touch_sample_xy(1216,56,true);touch_sample_xy(1216,56,false);
        assert(!s_diagnostics_requested);
        assert(template_loading_requested);
        assert(template_loading_frame() && s_view == VIEW_THEME_LOADING);
        save_frame("diagnostics-close-loading");
        template_prepare();render_runtime(rt,sample,true,31000000);
        assert(s_view == VIEW_DASHBOARD);
        puts("PASS: portrait touch mapping, theme swipes, diagnostics upward opening, downward gesture exclusion and X-only closing");
        lv_indev_delete(input);
        puts("PASS: 20 template renders, common error/recovery, NightMode and actual LVGL swipe across all 11 layouts; Bezier drawing/dot agreement, zero/reverse/7kW speed cap; six package weather states");
    }
    clear();template_release_cache();app_templates_view_free(&template_view);
    /* A firmware-be kerülő apró előnézetek ugyanebből a rajzolóból készülnek. */
    app_energy_sample_t builtin_sample=sample;
    builtin_sample.status=1;builtin_sample.soc_bat_pct=74;builtin_sample.battery_temperature_c=32;
    snprintf(config.project.language,sizeof(config.project.language),"hu");app_ui_set_config(&config);
    for(int night=0;night<2;night++){
        builtin_sample.night_mode=night;builtin_sample.power_pv_kw=night?0:7;
        builtin_sample.has_sun_radiation=!night;
        build_dashboard(night);s_last_night=night;update(&builtin_sample,&fixture);
        save_frame(night?"builtin-dark":"builtin-light");
        builtin_sample.status = 5;
        update(&builtin_sample, &fixture);
        assert(!strcmp(lv_label_get_text(s_status), "Hálózat újra elérhető"));
        assert(lv_image_get_src(s_status_icon) == &app_img_status_grid_restored);
        assert(lv_color_eq(lv_obj_get_style_text_color(s_status, 0), c(tone(GREEN, night))));
        save_frame(night ? "grid-restored-builtin-dark" : "grid-restored-builtin-light");
        builtin_sample.status = 4;
        update(&builtin_sample, &fixture);
        assert(lv_image_get_src(s_status_icon) == &app_img_status_error);
        builtin_sample.status = 1;
        update(&builtin_sample, &fixture);
        test_energy_assistant_ui(&builtin_sample);
        test_mail_ui(&builtin_sample);
    }
    clear();
    memset(&template_pending,0,sizeof(template_pending));memset(&template_view,0,sizeof(template_view));
    config.data_recept_mode=APP_DATA_LOXONE;config.mqtt.data_timeout_seconds=180;app_ui_set_config(&config);
    rt=(app_runtime_state_t){.data_recept_mode=APP_DATA_LOXONE,.wifi_connected=true};
    render_runtime(rt,sample,false,31000000);assert(s_view==VIEW_LOXONE_SETUP);save_frame("loxone-setup-hu");
    rt.loxone_configured=true;
    render_runtime(rt,sample,false,31000000);assert(s_view==VIEW_LOXONE_WAIT);save_frame("loxone-connecting-hu");
    rt.loxone_connected=true;rt.has_valid_sample=true;rt.last_valid_packet_monotonic_us=31000000;
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);
    rt.loxone_connected=false;rt.has_valid_sample=false;
    render_runtime(rt,sample,false,31000000);assert(s_view==VIEW_LOXONE_ERROR);save_frame("loxone-disconnected-hu");
    rt.loxone_connected=true;rt.has_valid_sample=true;
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);
    /* Nehéz háttérművelet alatt statikus üzenet; ismételt hívás nem épít újra,
     * a művelet után a megszokott nézet magától visszatér. */
    busy_screen();assert(s_view==VIEW_BUSY && s_busy_refreshes==0);
    lv_obj_t *busy_root=s_screen;
    /* A további körök nem építenek újra, csak a három panelpuffert töltik fel. */
    for(unsigned i=1;i<=6;i++){busy_screen();assert(s_view==VIEW_BUSY && s_screen==busy_root);}
    assert(s_busy_refreshes==4);
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);
    puts("PASS: Loxone setup, initial connection, lost connection, background-busy screen and automatic dashboard recovery");
    clear();

    const char *home_zip=getenv("ZOLEE_HOME_RESOURCE_ZIP");
    assert(home_zip);
    char home_error[160];assert(app_templates_install(home_zip,home_error)==ESP_OK);
    app_sim_home_reset();
    lv_indev_t *home_input=lv_indev_create();lv_indev_set_type(home_input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(home_input,display);lv_indev_set_read_cb(home_input,touch_read);navigation_bind_inputs();
    test_raw_sidecars(rt, sample);
    test_rapid_paging(rt, sample);
    test_startup_navigation(rt, sample);
    assert(app_templates_select("builtin")==ESP_OK);template_prepare();
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);
    lv_obj_update_layout(s_screen);
    /* A valódi beágyazott házkép közepe is megnyitja, nem csak a kör széle. */
    lv_area_t house_bounds;lv_obj_get_coords(s_home_node,&house_bounds);
    int house_x=(house_bounds.x1+house_bounds.x2)/2,house_y=(house_bounds.y1+house_bounds.y2)/2;
    touch_sample_xy(house_x,house_y,true);touch_sample_xy(house_x,house_y,false);
    home_load_blocked=2;
    assert(home_is_open());home_prepare(false);render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_THEME_LOADING&&!home_ready_to_draw);
    lv_obj_t *loading_label=lv_obj_get_child(s_screen,0);
    home_prepare(false);render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_THEME_LOADING&&lv_obj_get_child(s_screen,0)==loading_label);
    home_prepare(false);render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_HOME&&!strcmp(home_assets.assets[0].name,"consumers-light.png"));
    home_close(NULL);home_prepare(false);assert(s_view==VIEW_THEME_LOADING);
    save_frame("consumers-close-loading");
    template_prepare();render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);
    assert(app_templates_select("01-live-home")==ESP_OK);template_prepare();
    render_runtime(rt,sample,true,31000000);assert(!strcmp(template_view.id,"01-live-home"));
    lv_obj_update_layout(s_screen);touch_sample_xy(640,420,true);touch_sample_xy(640,420,false);
    assert(home_is_open());home_prepare(false);render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_HOME&&home_page==1&&home_assets.asset_count<=9);
    lv_obj_update_layout(s_screen);
    assert(lv_obj_get_x(lv_obj_get_parent(home_labels[4]))==186);
    int gray_index=home_icon_index("washing-machine");
    assert(gray_index>=0&&home_gray_buffers[gray_index]);
    lv_draw_buf_t *gray=home_gray_buffers[gray_index];
    for(unsigned y=0;y<96;y++)for(unsigned x=0;x<96;x++){
        uint8_t *p=gray->data+y*gray->header.stride+4*x;assert(p[0]==p[1]&&p[1]==p[2]);
    }
    test_consumer_noise(rt, sample);
    save_frame("loxone-consumers-light");
    test_home_background_pixels();
    lv_obj_t *stable_background=lv_obj_get_child(s_screen,0);
    unsigned char *stable_asset=home_assets.assets[0].bytes;
    invalidations=0;
    lv_display_add_event_cb(display,count_invalidations,LV_EVENT_INVALIDATE_AREA,&invalidations);
    for(unsigned i=0;i<100;i++){
        home_prepare(false);render_runtime(rt,sample,true,31000000);
        animation_tick(NULL);lv_obj_update_layout(s_screen);
        assert(s_view==VIEW_HOME&&lv_obj_get_child(s_screen,0)==stable_background);
        assert(home_assets.assets[0].bytes==stable_asset);
    }
    lv_display_remove_event_cb_with_user_data(display,count_invalidations,&invalidations);
    assert(invalidations==0);
    home_prepare(true);render_runtime(rt,sample,true,31000000);save_frame("loxone-consumers-dark");
    lv_obj_update_layout(s_screen);touch_sample_xy(1204,60,true);touch_sample_xy(1204,60,false);
    assert(!home_is_open());home_prepare(false);assert(s_view==VIEW_THEME_LOADING);
    template_prepare();render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_DASHBOARD&&!strcmp(template_view.id,"01-live-home")&&!home_assets.asset_count);
    lv_obj_update_layout(s_screen);
    touch_sample_xy(640,200,true);touch_sample_xy(640,400,true);touch_sample_xy(640,600,true);touch_sample_xy(640,600,false);
    assert(home_is_open());home_prepare(false);render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_HOME&&home_page==2);
    save_frame("loxone-cards-light");
    home_prepare(true);render_runtime(rt,sample,true,31000000);save_frame("loxone-cards-dark");
    lv_obj_send_event(home_buttons[4],LV_EVENT_CLICKED,NULL);
    assert(app_sim_home_commands==1&&app_sim_home_last_action==LX_HOME_COMMAND_OFF);
    assert(!strcmp(lv_label_get_text(home_labels[4]),"BE"));
    lv_obj_send_event(home_buttons[4],LV_EVENT_CLICKED,NULL);assert(app_sim_home_commands==1);
    home_prepare(true);render_runtime(rt,sample,true,31000000);
    assert(lv_obj_has_state(home_buttons[4],LV_STATE_DISABLED));
    app_sim_home_pending=false;app_sim_home_values.cards[4].value=0;
    home_prepare(true);render_runtime(rt,sample,true,31000000);
    assert(!strcmp(lv_label_get_text(home_labels[4]),"KI"));
    app_sim_home_connected=false;home_prepare(true);render_runtime(rt,sample,true,31000000);
    assert(lv_obj_has_state(home_buttons[6],LV_STATE_DISABLED));
    assert(strstr(lv_label_get_text(home_labels[0]),"Nincs adat"));
    save_frame("loxone-cards-offline");
    app_sim_home_connected = true;
    app_sim_home_pending = false;
    home_prepare(false); render_runtime(rt,sample,true,31000000);
    assert(home_dots[0] && home_dots[1] && home_dots[2] && !home_dots[3]);
    assert(home_card_page == 0 && home_card_count == 8);
    /* Gombon induló balra húzás nem küld parancsot a felengedéskor. */
    lv_obj_update_layout(s_screen);
    touch_sample_xy(150,504,true); touch_sample_xy(85,504,true);
    touch_sample_xy(10,504,true); touch_sample_xy(10,504,false);
    assert(home_request_get() == 3 && app_sim_home_commands == 1);
    app_sim_home_values.cards[8].value = 19.8;
    /* Lapváltáskor a dekódolt háttér és logó ugyanaz a puffer marad: nincs
     * újabb háttérolvasás és -dekódolás, csak az ikonok cserélődnek. */
    lv_draw_buf_t *kept_background = home_buffers[0], *kept_logo = home_buffers[1];
    unsigned char *kept_background_png = home_assets.assets[0].bytes;
    unsigned full_reads = home_resource_reads, icon_reads = home_icon_reads;
    home_icon_blocked = 1;
    home_prepare(false); render_runtime(rt,sample,true,31000000);
    assert(s_view == VIEW_THEME_LOADING && !home_ready_to_draw);
    assert(home_buffers[0] == kept_background && home_buffers[1] == kept_logo && !home_buffers[2]);
    home_prepare(false); render_runtime(rt,sample,true,31000000);
    assert(home_resource_reads == full_reads && home_icon_reads == icon_reads + 2);
    assert(home_buffers[0] == kept_background && home_buffers[1] == kept_logo);
    assert(home_assets.assets[0].bytes == kept_background_png);
    lv_obj_update_layout(s_screen); lv_refr_now(display);
    test_home_background_pixels();
    assert(home_card_page == 1 && home_first_card == 8 && home_card_count == 2);
    assert(home_assets.asset_count == 4 && !strcmp(home_assets.assets[0].name,"background-light.png"));
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(s_screen,1),0)),"Kert"));
    assert(strstr(lv_label_get_text(home_labels[0]),"19,8"));
    save_frame("loxone-cards-second-light");
    /* A második lap gombja a globális, helyes kártyaazonosítóra küld. */
    touch_sample_xy(790,288,true); touch_sample_xy(790,288,false);
    assert(app_sim_home_commands == 2 && !strcmp(app_sim_home_last_id,"card-9"));
    touch_sample_xy(1000,650,true); touch_sample_xy(700,650,true);
    touch_sample_xy(300,650,true); touch_sample_xy(300,650,false);
    assert(home_request_get() == 4 && app_sim_home_commands == 2);
    home_prepare(false); render_runtime(rt,sample,true,31000000);
    assert(home_card_page == 2 && home_card_count == 0 && home_assets.asset_count == 2);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(s_screen,1),0)),"Vendégszoba"));
    save_frame("loxone-cards-empty-light");
    touch_sample_xy(300,650,true); touch_sample_xy(700,650,true);
    touch_sample_xy(1000,650,true); touch_sample_xy(1000,650,false);
    assert(home_request_get() == 3);
    /* Sérült ikon a csak-ikonos betöltésben: az egész oldal felszabadul, és a
     * következő kör a hátteret is újra betölti. */
    home_icon_corrupt = true; full_reads = home_resource_reads;
    home_prepare(false); render_runtime(rt,sample,true,31000000);
    assert(s_view == VIEW_THEME_LOADING && !home_ready_to_draw && !home_buffers[0]);
    home_prepare(false); render_runtime(rt,sample,true,31000000);
    assert(home_resource_reads == full_reads + 1 && s_view == VIEW_HOME && home_ready_to_draw);
    lv_obj_update_layout(s_screen); lv_refr_now(display);
    test_home_background_pixels();
    assert(home_card_page == 1);
    /* Napszakváltásnál a háttér más fájl: teljes betöltés. */
    full_reads = home_resource_reads;
    home_prepare(true); render_runtime(rt,sample,true,31000000);
    assert(home_resource_reads == full_reads + 1 && !strcmp(home_assets.assets[0].name,"background-dark.png"));
    home_prepare(false); render_runtime(rt,sample,true,31000000);
    assert(home_resource_reads == full_reads + 2 && !strcmp(home_assets.assets[0].name,"background-light.png"));
    puts("PASS: Home card paging keeps the decoded background/logo, reloads only icons, and falls back to a full load on icon failure or day/night change");
    home_gesture(LV_DIR_LEFT); home_prepare(false); render_runtime(rt,sample,true,31000000);
    home_gesture(LV_DIR_LEFT); home_prepare(false); render_runtime(rt,sample,true,31000000);
    assert(home_card_page == 0 && home_request_get() == 2);
    /* Egyetlen konfigurált lapnál eltűnnek a pontok. */
    app_sim_home_config.page_count = 1; app_sim_home_config.card_count = 8;
    home_prepare(true); render_runtime(rt,sample,true,31000000);
    assert(!home_dots[0]);
    for (unsigned i = 8; i < 12; i++) {
        app_sim_home_config.cards[i] = app_sim_home_config.cards[0];
        snprintf(app_sim_home_config.cards[i].id,LX_HOME_ID_SIZE,"extra-%u",i);
        app_sim_home_values.cards[i] = (lx_home_value_t){.value=21+i*.1,.valid=true};
    }
    app_sim_home_config.card_count = app_sim_home_config.pages[0].card_count = 12;
    app_sim_home_pending = false;
    home_prepare(false); render_runtime(rt,sample,true,31000000);
    lv_obj_update_layout(s_screen);
    assert(home_card_count == 12 && home_assets.asset_count <= 14);
    assert(lv_obj_get_y(lv_obj_get_parent(home_labels[11])) == 552);
    assert(lv_obj_get_y(lv_obj_get_parent(home_message)) == 754);
    assert(lv_obj_get_style_bg_opa(lv_obj_get_parent(home_message),0) == LV_OPA_COVER);
    save_frame("loxone-cards-twelve-light");
    puts("PASS: actual LVGL titled pages, footer dots, both swipe directions/wrap, empty page, active images only, no command on swipe and correct off-first-page command ID");
    home_close(NULL);home_prepare(false);template_prepare();render_runtime(rt,sample,true,31000000);
    assert(!home_is_open()&&!home_assets.asset_count&&!strcmp(template_view.id,"01-live-home"));
    home_request_set(1);home_load_blocked=15;
    for(unsigned i=0;i<14;i++){
        home_prepare(false);render_runtime(rt,sample,true,31000000);
        assert(home_is_open()&&s_view==VIEW_THEME_LOADING&&!home_assets.asset_count);
    }
    home_prepare(false);assert(!home_is_open()&&!home_assets.asset_count);
    assert(template_loading_frame());template_prepare();render_runtime(rt,sample,true,31000000);
    assert(s_view==VIEW_DASHBOARD);
    puts("PASS: resource-lock retry preserves one loading screen, bounded permanent failure returns to theme, stable consumers cause no redraw and native house-image touch works");
    /* Sérült háttérnél és egy később dekódolt ikonnál is visszavonjuk
     * az egész előtöltést. Az azonos betöltőkép marad, majd jó adattal feláll. */
    home_close(NULL);home_prepare(false);template_prepare();render_runtime(rt,sample,true,31000000);
    for (int corrupt = 0; corrupt <= 2; corrupt += 2)
    {
        home_open_clicked(NULL);
        home_corrupt_asset = corrupt;
        home_prepare(false);render_runtime(rt,sample,true,31000000);
        assert(s_view == VIEW_THEME_LOADING && !home_ready_to_draw && !home_assets.asset_count);
        for (unsigned i = 0; i < 14; i++)
            assert(!home_buffers[i] && !home_gray_buffers[i]);
        lv_obj_t *loading = lv_obj_get_child(s_screen, 0);
        home_load_blocked = 1;
        home_prepare(false);render_runtime(rt,sample,true,31000000);
        assert(lv_obj_get_child(s_screen, 0) == loading);
        home_prepare(false);render_runtime(rt,sample,true,31000000);
        assert(s_view == VIEW_HOME && home_ready_to_draw);
        lv_obj_update_layout(s_screen);lv_refr_now(display);
        test_home_background_pixels();
        home_close(NULL);home_prepare(false);template_prepare();render_runtime(rt,sample,true,31000000);
    }
    puts("PASS: corrupt background/icon never displays an incomplete Home page; decoded buffers roll back and retry succeeds");
    test_home_return_navigation(rt, sample);
    test_home_lifecycle(rt, sample);
    test_energy_disabled_home(rt);
    app_sim_home_enabled=false;
    lv_indev_delete(home_input);
    clear();template_release_cache();app_templates_view_free(&template_view);
    puts("PASS: actual LVGL consumer/cards light/dark pages, 7-item centering, real grayscale, separate command/feedback, pending/offline controls, resource cleanup and exact theme return");
    app_test_slave_display_input(display);
    {
        /* Master jelenlétvezérlés a tényleges runtime-úton: párosítás nélkül
         * változatlan, éjjel 0 -> teljes sötét, 1 -> éjszakai fényerő, az első
         * érintés csak ébreszt, hibaoldal és kapcsolatvesztés látható marad. */
        config.data_recept_mode = APP_DATA_LOXONE;
        config.energy_flow_disabled = false;
        config.display.day_brightness = 74;
        config.display.night_brightness = 21;
        config.display.touch_wake_seconds = 30;
        app_ui_set_config(&config);
        assert(s_master_display_ready && s_slave_display.profile.blank_on_presence_loss &&
               s_slave_display.profile.presence_timeout_seconds == 30);
        lv_indev_t *master_input = lv_indev_create();
        lv_indev_set_type(master_input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(master_input, display);
        lv_indev_set_read_cb(master_input, touch_read);
        app_runtime_state_t master_rt = {
            .data_recept_mode = APP_DATA_LOXONE, .loxone_connected = true, .loxone_configured = true,
            .wifi_connected = true, .has_valid_sample = true, .last_valid_packet_monotonic_us = ticks,
        };
        runtime_view_t saved_view = s_view;
        s_view = VIEW_DASHBOARD;
        sample.night_mode = true;
        sample.received_monotonic_us = ticks;
        memset(&app_sim_signals, 0, sizeof(app_sim_signals));
        (void)app_loxone_signals(&s_loxone_signals);
        master_presence_update(&master_rt, &sample, true, ticks);
        assert(!s_presence_control && !s_slave_input.input); /* Nincs párosítás: régi működés. */
        app_sim_signals.presence = (app_loxone_signal_t){.configured = true, .valid = true, .active = true};
        (void)app_loxone_signals(&s_loxone_signals);
        master_presence_update(&master_rt, &sample, true, ticks);
        assert(s_presence_control && s_slave_input.input == master_input);
        /* A nappali -> éjszakai átmenet fokozatos (1%/kör), ahogy eddig is. */
        assert(s_brightness == 73);
        for (int i = 0; i < 60; ++i)
            master_presence_update(&master_rt, &sample, true, ticks + i);
        assert(s_brightness == 21 && last_pwm_brightness == 21);
        app_sim_signals.presence.active = false;
        (void)app_loxone_signals(&s_loxone_signals);
        master_presence_update(&master_rt, &sample, true, ticks + 1000);
        assert(s_brightness == 0 && last_pwm_brightness == 0);
        /* Az első érintés csak ébreszt, és az ébrenléti idő után újra sötét. */
        assert(app_slave_display_pointer(&s_slave_display, true, ticks + 2000));
        assert(app_slave_display_pointer(&s_slave_display, false, ticks + 3000));
        master_presence_update(&master_rt, &sample, true, ticks + 4000);
        assert(s_brightness == 21 && last_pwm_brightness == 21);
        master_presence_update(&master_rt, &sample, true, ticks + 31000000);
        assert(s_brightness == 0);
        /* Ismeretlen jelenlét vagy hibaoldal mellett nincs sötét kijelző. */
        app_sim_signals.presence.valid = false;
        (void)app_loxone_signals(&s_loxone_signals);
        master_presence_update(&master_rt, &sample, true, ticks + 31100000);
        assert(s_brightness == 21);
        app_sim_signals.presence.valid = true;
        (void)app_loxone_signals(&s_loxone_signals);
        s_view = VIEW_WIFI_ERROR;
        master_presence_update(&master_rt, &sample, true, ticks + 31200000);
        assert(s_brightness == 21);
        s_view = VIEW_DASHBOARD;
        master_presence_update(&master_rt, &sample, true, ticks + 31300000);
        assert(s_brightness == 0);
        /* Nappal a jelenlét nem sötétít; a fényerő fokozatosan éri el a nappali értéket. */
        sample.night_mode = false;
        master_presence_update(&master_rt, &sample, true, ticks + 31400000);
        assert(s_brightness == 74);
        /* A párosítás törlése visszaadja az eredeti vezérlést. */
        memset(&app_sim_signals, 0, sizeof(app_sim_signals));
        (void)app_loxone_signals(&s_loxone_signals);
        master_presence_update(&master_rt, &sample, true, ticks + 31500000);
        assert(!s_presence_control);
        s_view = saved_view;
        lv_indev_delete(master_input);
        assert(!s_slave_input.input);
        memset(&s_loxone_signals, 0, sizeof(s_loxone_signals));
        puts("PASS: actual Master Loxone presence: unpaired unchanged, night 0 blanks, 1 and touch wake to night PWM, wake-only touch, error/unknown visible, day unaffected");
    }
    /* A tényleges Master-hardver Slave runtime-ja, külön napszak- és PWM-ellenőrzéssel. */
    config.data_recept_mode = APP_DATA_SLAVE;
    config.display.day_brightness = 83;
    config.display.night_brightness = 17;
    app_ui_set_config(&config);
    assert(s_slave_display_ready && !s_slave_display.profile.blank_at_night);
    app_slave_display_profile_t slave_profile = s_slave_display.profile;
    slave_profile.blank_at_night = true;
    assert(app_slave_display_profile(&s_slave_display, &slave_profile, ticks));
    lv_indev_t *slave_input = lv_indev_create();
    lv_indev_set_type(slave_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(slave_input, display);
    lv_indev_set_read_cb(slave_input, touch_read);
    app_runtime_state_t slave_rt = {
        .data_recept_mode = APP_DATA_SLAVE, .slave_connected = true, .has_valid_sample = true,
        .last_valid_packet_monotonic_us = ticks, .slave_packet_monotonic_us = ticks,
    };
    sample.night_mode = false;
    slave_display_update(&slave_rt, &sample, true, ticks);
    assert(s_brightness == 83 && last_pwm_brightness == 83 && s_slave_input.input == slave_input);
    sample.night_mode = true;
    slave_display_update(&slave_rt, &sample, true, ticks);
    assert(s_brightness == 0 && last_pwm_brightness == 0);
    pairing_fixture.pin_required = true;
    slave_display_update(&slave_rt, &sample, true, ticks);
    assert(s_brightness == 17 && last_pwm_brightness == 17);
    pairing_fixture.pin_required = false;
    slave_rt.slave_connected = false;
    sample.night_mode = false;
    slave_display_update(&slave_rt, &sample, false, ticks);
    assert(s_brightness == 0 && last_pwm_brightness == 0 && s_slave_display.night);
    slave_rt.slave_connected = true;
    slave_rt.slave_source_stale = true;
    slave_display_update(&slave_rt, &sample, true, ticks);
    assert(s_brightness == 0 && last_pwm_brightness == 0 && s_slave_display.night);
    slave_rt.slave_source_stale = false;
    slave_display_update(&slave_rt, &sample, true, ticks);
    assert(s_brightness == 83 && last_pwm_brightness == 83);
    sample.night_mode = true;
    lv_indev_delete(slave_input);
    slave_display_update(&slave_rt, &sample, true, ticks);
    assert(s_brightness == 17 && last_pwm_brightness == 17 && !s_slave_input.input);
    puts("PASS: actual Slave runtime own day/night PWM, PIN visibility, stale/disconnected night retention and missing-touch fallback");
    lv_display_delete(display);lv_deinit();return 0;
}
