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
bool app_storage_is_ok(void){return sd_ok;}
static void flush(lv_display_t *disp,const lv_area_t *area,uint8_t *map)
{(void)area;(void)map;lv_display_flush_ready(disp);}

static void save_frame(const char *name)
{
    lv_obj_update_layout(lv_screen_active());
    for(int i=0;i<4;i++){ticks+=16000;lv_tick_inc(16);lv_timer_handler();}
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
    fixture.totals=(app_energy_totals_t){.production_kwh=17.3,.consumption_kwh=11.8,.grid_import_kwh=2.1,.grid_export_kwh=8.4,.battery_charge_kwh=3.2,.battery_discharge_kwh=1.4};
    fixture.completed_interval_totals=fixture.totals;
    fixture.yesterday_completed_interval_totals=(app_energy_totals_t){.production_kwh=15.4,.consumption_kwh=12.8,.grid_import_kwh=3.1,.grid_export_kwh=6.4,.battery_charge_kwh=3.5,.battery_discharge_kwh=1.1};
    fixture.has_yesterday=true;
    for(int i=0;i<176;i++){
        float daylight=fmaxf(0,sinf((i-72)*3.14159265f/144));
        fixture.slots[i]=(app_stats_slot_t){.valid=true,.sample_count=60,.pv_kw=daylight*7,
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
    s_stats_snapshot=fixture;
    render_runtime(rt,sample,true,31000000);assert(s_view==VIEW_DASHBOARD);save_frame("http-recovered-hu");
    snprintf(config.project.language,sizeof(config.project.language),"en");app_ui_set_config(&config);
    render_runtime(rt,sample,true,61000000);assert(s_view==VIEW_DATA_TIMEOUT);save_frame("http-timeout-en");
    /* Következő állapotváltáskor az összes korábbi chart/ikon objektum felszabadul. */
    clear();lv_display_delete(display);lv_deinit();return 0;
}
