/* A tényleges firmware UI-jának headless LVGL renderje. Nincs külön mockup-implementáció. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "../../main/app_ui.c"

static lv_display_t *display;
static uint32_t pixels[1280*800];
static int64_t ticks;
static bool sd_ok=true;
static app_stats_snapshot_t fixture;
static time_t epoch;
static lv_point_t touch_point;
static bool touch_pressed;
static void touch_read(lv_indev_t *input,lv_indev_data_t *data){
    (void)input;data->point=touch_point;data->state=touch_pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
static void touch_sample(int x,bool pressed){
    touch_point=(lv_point_t){x,400};touch_pressed=pressed;ticks+=40000;lv_tick_inc(40);lv_timer_handler();
}

time_t host_time(time_t *out){if(out)*out=epoch;return epoch;}
int64_t esp_timer_get_time(void){return ticks;}
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg){(void)cfg;return display;}
esp_err_t bsp_display_lock(int timeout){(void)timeout;return ESP_OK;}
void bsp_display_unlock(void){}
void bsp_display_backlight_on(void){}
void bsp_display_brightness_set(int value){(void)value;}
int xTaskCreate(void (*f)(void *),const char *n,unsigned st,void *a,unsigned p,void *h)
{(void)f;(void)n;(void)st;(void)a;(void)p;(void)h;return 1;}
void vTaskDelay(unsigned ms){(void)ms;}
void app_state_get_runtime(app_runtime_state_t *out){*out=(app_runtime_state_t){.wifi_connected=true,.mqtt_connected=true};}
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
}

/* Exercise actual line objects and dot positions, including reverse/zero/cap. */
static void test_template_motion(void){
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];
        if(strcmp(ts(node->spec,"type"),"flow"))continue;
        if(!strcmp(ts(node->spec,"curve"),"bezier")){
            unsigned controls=cJSON_GetArraySize(cJSON_GetObjectItem(node->spec,"points"));
            assert(node->point_count==(controls==4?33:65));
            const cJSON *last=cJSON_GetArrayItem(cJSON_GetObjectItem(node->spec,"points"),controls-1);
            assert(fabs(node->points[node->point_count-1].x-tn(last,"x",0))<.01);
            assert(fabs(node->points[node->point_count-1].y-tn(last,"y",0))<.01);
        }
        double original=node->power;
        node->phase=.5f;node->power=7;template_animate(.02f);float capped=node->phase;
        assert(capped>.5f);
        node->phase=.5f;node->power=15;template_animate(.02f);assert(fabs(node->phase-capped)<.00001);
        node->phase=.5f;node->power=-7;template_animate(.02f);assert(node->phase<.5f);
        node->phase=.5f;node->power=0;template_animate(.02f);
        for(int d=0;d<4;d++)assert(lv_obj_has_flag(node->dots[d],LV_OBJ_FLAG_HIDDEN));
        node->power=original;
    }
    template_animate(.02f);lv_obj_update_layout(s_screen);
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];if(strcmp(ts(node->spec,"type"),"flow")||fabs(node->power)<.001)continue;
        for(int d=0;d<4;d++){
            float x=lv_obj_get_x(node->dots[d])+3-tn(node->spec,"x",0),y=lv_obj_get_y(node->dots[d])+3-tn(node->spec,"y",0),distance=10000;
            for(unsigned k=1;k<node->point_count;k++){
                float ax=node->points[k-1].x,ay=node->points[k-1].y,dx=node->points[k].x-ax,dy=node->points[k].y-ay;
                float sq=dx*dx+dy*dy,t=sq?((x-ax)*dx+(y-ay)*dy)/sq:0;t=fmaxf(0,fminf(1,t));
                distance=fminf(distance,hypotf(x-ax-t*dx,y-ay-t*dy));
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
                    for(int d=0;d<4;d++)assert(lv_color_eq(lv_obj_get_style_bg_color(node->dots[d],0),c(expected)));
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

static void test_template_weather(const app_energy_sample_t *sample){
    static const char *keys[]={"sunny","partly_cloudy","cloudy","rain","snow","night"};
    for(unsigned i=0;i<template_node_count;i++){
        template_node_t *node=&template_nodes[i];const cJSON *files=cJSON_GetObjectItem(node->spec,"files");
        if(strcmp(ts(node->spec,"type"),"weather")||!files)continue;
        for(int k=0;k<6;k++){
            app_energy_sample_t state=*sample;state.night_mode=k==5;state.has_precipitation=k==3||k==4;
            state.has_sun_radiation=k==0;state.power_pv_kw=k==1?2:0;state.temperature_c=k==4?-6:20;
            template_update(&state,&fixture);assert(weather_kind(&state)==(weather_kind_t)k);
            bool matched=false;
            for(unsigned a=0;a<template_view.asset_count;a++)if(!strcmp(ts(files,keys[k]),template_view.assets[a].name)){
                assert(lv_image_get_src(node->obj)==&template_images[a]);matched=true;
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

int main(void)
{
    setenv("TZ","UTC0",1);tzset();
    struct tm t={.tm_year=126,.tm_mon=9,.tm_mday=5,.tm_hour=14,.tm_min=37};epoch=mktime(&t);
    lv_init();display=lv_display_create(1280,800);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display,flush);
    assert(app_ui_init()==ESP_OK);
    app_config_t config={0};
    snprintf(config.project.title,sizeof(config.project.title),"Otthoni energia");
    snprintf(config.project.language,sizeof(config.project.language),"hu");
    snprintf(config.project.currency,sizeof(config.project.currency),"Ft");
    config.energy.electricity_purchase_price=65;config.energy.feed_in_price=5;
    config.energy.co2_factor_kg_per_kwh=.381;config.energy.tree_absorption_kg_per_year=22;
    app_ui_set_config(&config);s_started=true;
    fixture.totals=(app_energy_totals_t){.production_kwh=17.3,.consumption_kwh=11.8,.grid_import_kwh=2.1,.grid_export_kwh=8.4,.battery_charge_kwh=3.2,.battery_discharge_kwh=1.4,.pv_home_kwh=8.3,.pv_export_kwh=5.8,.pv_stored_kwh=3.2,.grid_home_kwh=2.1,.battery_home_kwh=1.4};
    fixture.completed_interval_totals=fixture.totals;
    fixture.yesterday_completed_interval_totals=(app_energy_totals_t){.production_kwh=15.4,.consumption_kwh=12.8,.grid_import_kwh=3.1,.grid_export_kwh=6.4,.battery_charge_kwh=3.5,.battery_discharge_kwh=1.1};
    fixture.has_yesterday=true;
    for(int i=0;i<176;i++){
        float daylight=fmaxf(0,sinf((i-72)*3.14159265f/144));
        fixture.slots[i]=(app_stats_slot_t){.valid=true,.sample_count=60,.soc_pct=20+daylight*80,.soc_valid=true,.pv_kw=daylight*7,
            .load_kw=1.5f+.7f*sinf(i*.13f),.grid_kw=1.4f*sinf(i*.05f),.battery_kw=2.f*cosf(i*.04f)};
        fixture.slots[i].split=app_energy_split(fixture.slots[i].pv_kw,fixture.slots[i].load_kw,fixture.slots[i].battery_kw);
    }
    app_energy_sample_t sample={.power_pv_kw=7,.power_full_kw=2.1,.power_grid_kw=-4.5,.power_bat_kw=.4,
        .soc_bat_pct=87.4,.temperature_c=18.2,.wind_speed_kmh=12,.battery_temperature_c=35,.has_sun_radiation=true,.status=1};
    build_dashboard(false);s_last_night=false;update(&sample,&fixture);save_frame("hu-day");
    test_stacked_charts();
    assert(f_pv.phase>0.f);
    sd_ok=false;sample.status=3;sample.soc_bat_pct=19.9;update(&sample,&fixture);save_frame("hu-island-sd-error");
    sd_ok=true;sample.status=2;sample.power_bat_kw=-3.5;sample.soc_bat_pct=100;update(&sample,&fixture);save_frame("hu-preparing");
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
    app_diagnostics_capture(&s_diagnostics);s_diagnostics_requested=true;
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DIAGNOSTICS);save_frame("diagnostics-common");
    s_diagnostics_requested=false;render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);
    if(getenv("ZOLEE_TEMPLATE_ROOT")){
        assert(app_templates_init()==ESP_OK);
        app_template_info_t list[APP_TEMPLATE_MAX];char active[33];
        unsigned count=app_templates_list(list,APP_TEMPLATE_MAX,active);assert(count==9);
        sample.power_pv_kw=7;sample.power_bat_kw=-2.5;sample.power_grid_kw=-2.4;sample.power_full_kw=2.1;
        sample.soc_bat_pct=74;sample.status=1;sample.temperature_c=-6;sample.has_sun_radiation=true;
        snprintf(config.project.language,sizeof(config.project.language),"hu");app_ui_set_config(&config);
        for(unsigned i=1;i<count;i++){
            assert(app_templates_select(list[i].id)==ESP_OK);template_prepare();
            for(int night=0;night<2;night++){
                sample.night_mode=night;rt.wifi_connected=true;
                render_runtime(rt,sample,true,31000000);assert(template_is_active());assert(s_view==VIEW_DASHBOARD);
                const cJSON *scene=cJSON_GetObjectItem(template_view.document,night?"dark":"light");
                assert(lv_color_eq(lv_obj_get_style_bg_color(s_screen,0),c(tc(scene,"background",0))));
                test_template_weather(&sample);test_template_motion();test_template_indicators(&sample);
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
        assert((pixels[784*1280+553]&0xffffff)==tone(GREEN,sample.night_mode));
        lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,touch_read);
        for(unsigned i=1;i<=count;i++){
            lv_obj_update_layout(s_screen);
            touch_sample(1050,true);touch_sample(850,true);touch_sample(650,true);touch_sample(650,false);
            template_prepare();render_runtime(rt,sample,true,31000000);
            if(strcmp(template_view.id,list[i%count].id))fprintf(stderr,"Swipe %u: got %s expected %s\n",i,template_view.id,list[i%count].id);
            assert(!strcmp(template_view.id,list[i%count].id));
        }
        lv_indev_delete(input);
        puts("PASS: 16 template renders, common error/recovery, NightMode and actual LVGL swipe across all 9 layouts; Bezier drawing/dot agreement, zero/reverse/7kW speed cap; six package weather states");
    }
    clear();template_release_cache();app_templates_view_free(&template_view);
    lv_display_delete(display);lv_deinit();return 0;
}
