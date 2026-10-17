/* Asztali adapter: ugyanaz a firmware UI, parser, ingest és állapottároló.
 * Kizárólag a hardver és az SD-statisztika helyett használ demó adatokat. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#ifdef _MSC_VER
static struct tm *localtime_r(const time_t *t, struct tm *out)
{ return localtime_s(out,t)==0 ? out : NULL; }
#endif
#include "../main/app_ui.c"
#include "app_ingest.h"

static lv_display_t *display;
static uint32_t pixels[1280*800];
static int64_t ticks;
static app_stats_snapshot_t fixture;
static unsigned scene;
static bool paused;
static app_config_t config;
static unsigned packets;
static bool sd_ok=true;
static int night_override=-1;
#ifdef _WIN32
static HWND window;
#endif

int64_t esp_timer_get_time(void){return ticks;}
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg){(void)cfg;return display;}
esp_err_t bsp_display_lock(int timeout){(void)timeout;return ESP_OK;}
void bsp_display_unlock(void){}
void bsp_display_backlight_on(void){}
void bsp_display_brightness_set(int value){(void)value;}
int xTaskCreate(void (*f)(void *),const char *n,unsigned st,void *a,unsigned p,void *h)
{(void)f;(void)n;(void)st;(void)a;(void)p;(void)h;return 1;}
void vTaskDelay(unsigned ms){(void)ms;}
bool app_storage_is_ok(void){return sd_ok;}
void app_stats_get_snapshot(app_stats_snapshot_t *out){*out=fixture;}
/* A demó történet memóriában él; nem ír a valódi SD adataira. */
void app_stats_process_sample(const app_energy_sample_t *s)
{
    static int64_t previous;
    double hours=previous ? (s->received_monotonic_us-previous)/3600000000.0 : 0;
    if(hours>30.0/3600)hours=0;
    previous=s->received_monotonic_us;
    fixture.totals.production_kwh+=s->power_pv_kw*hours;
    fixture.totals.consumption_kwh+=s->power_full_kw*hours;
    fixture.totals.grid_import_kwh+=fmax(0,s->power_grid_kw)*hours;
    fixture.totals.grid_export_kwh+=fmax(0,-s->power_grid_kw)*hours;
    fixture.totals.battery_charge_kwh+=fmax(0,-s->power_bat_kw)*hours;
    fixture.totals.battery_discharge_kwh+=fmax(0,s->power_bat_kw)*hours;
    time_t now=time(NULL);struct tm tmv;localtime_r(&now,&tmv);
    int i=tmv.tm_hour*12+tmv.tm_min/5;
    app_stats_slot_t *slot=&fixture.slots[i];
    unsigned count=slot->sample_count++;
    slot->valid=true;
    slot->pv_kw+=(float)((s->power_pv_kw-slot->pv_kw)/(count+1));
    slot->load_kw+=(float)((s->power_full_kw-slot->load_kw)/(count+1));
    slot->grid_kw+=(float)((s->power_grid_kw-slot->grid_kw)/(count+1));
    slot->battery_kw+=(float)((s->power_bat_kw-slot->battery_kw)/(count+1));
    app_energy_split_average(&slot->split,app_energy_split(s->power_pv_kw,s->power_full_kw,s->power_bat_kw),(float)(count+1));
}
static void flush(lv_display_t *disp,const lv_area_t *area,uint8_t *map)
{
    (void)area;(void)map;
#ifdef _WIN32
    if(window && lv_display_flush_is_last(disp))InvalidateRect(window,NULL,FALSE);
#endif
    lv_display_flush_ready(disp);
}

/* Napenergia/export, hálózati import, akkumerítés, eső, hó, éjszaka,
 * felkészülés, szigetüzem és hiba. Minden csomag teljes protokollüzenet. */
static void send_demo(void)
{
    static const double pv[]={7,2,0,0.5,0.05,0,4,3,0};
    static const double bat[]={-2,-1,2,0,0,1.5,-1,0,0};
    static const double load[]={2,4,3,2,1.5,2,2,3,1};
    unsigned i=scene%9;
    char json[768],error[160];
    int night=night_override>=0 ? night_override : i==5;
    int status=i==6?2:i==7?3:i==8?4:1;
    int n=snprintf(json,sizeof(json),
        "{\"PowerGrid\":%.3f,\"PowerBat\":%.3f,\"PowerPV\":%.3f,"
        "\"PowerFull\":%.3f,\"SocBat\":%.1f,\"Temperature\":%.1f,"
        "\"WindSpeed\":%u,\"HasSunRadiation\":%d,\"HasPrecipitation\":%d,"
        "\"Status\":%d,\"NightMode\":%d,\"BatteryTemperature\":%d,\"SunPosition\":\"180#%d#420#1080\"}",
        load[i]-pv[i]-bat[i],bat[i],pv[i],load[i],65.0+i*2.3,
        i==4?-6.0:18.2,12+i*3,i==0||i==6,i==3||i==4,status,night,i==8?52:i==6?43:32,night?-25:30);
    if(n<0 || (size_t)n>=sizeof(json) || app_ingest_process(APP_DATA_HTTP,json,(size_t)n,error,sizeof(error))!=ESP_OK){
        fprintf(stderr,"Demo packet rejected: %s\n",error);exit(2);
    }
    packets++;
    printf("Demo %u: %s\n",i+1,json);
}
void app_stats_get_period(bool month,app_stats_period_t *out){
    memset(out,0,sizeof(*out));out->count=month?31:7;
    for(unsigned i=0;i<out->count;i++){
        out->days[i].valid=i!=2;out->days[i].totals=fixture.totals;
        out->days[i].totals.production_kwh=18+i%7*2;out->days[i].totals.consumption_kwh=24+i%5;
        out->days[i].totals.grid_import_kwh=5+i%4;
        if(out->days[i].valid)out->totals.grid_import_kwh+=out->days[i].totals.grid_import_kwh;
    }
}
static void render_step(unsigned elapsed)
{
    static unsigned until_ui=200;
    ticks+=(int64_t)elapsed*1000;
    lv_tick_inc(elapsed);
    until_ui+=elapsed;
    if(until_ui>=200){
        until_ui=0;
        app_runtime_state_t rt;app_energy_sample_t sample={0};
        app_state_get_runtime(&rt);
        bool have=app_state_get_sample(&sample);
        app_stats_get_snapshot(&s_stats_snapshot);
        app_stats_get_period(false,&s_week_period);app_stats_get_period(true,&s_month_period);
        diagnostics_capture();template_prepare();render_runtime(rt,sample,have,ticks);
    }
    lv_timer_handler();
}
static void init_demo(void)
{
    ticks=1000;
    app_state_init();app_state_set_data_mode(APP_DATA_HTTP);
    app_state_set_wifi_connected(true);app_state_set_http_ready(true);
    app_state_set_sd_ok(true);app_state_set_config_ok(true);app_state_set_time_synced(true);
    if(app_ingest_init(APP_DATA_HTTP)!=ESP_OK)exit(2);
    lv_init();display=lv_display_create(1280,800);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display,flush);
    if(app_ui_init(270)!=ESP_OK)exit(2);
    snprintf(config.project.title,sizeof(config.project.title),"Otthoni energia - DEMO");
    snprintf(config.project.language,sizeof(config.project.language),"hu");
    snprintf(config.project.currency,sizeof(config.project.currency),"Ft");
    config.data_recept_mode=APP_DATA_HTTP;config.http.data_timeout_seconds=30;
    config.display.day_brightness=80;config.display.night_brightness=20;
    config.energy.electricity_purchase_price=65;config.energy.feed_in_price=5;
    config.energy.battery_capacity_kwh=15;
    config.energy.co2_factor_kg_per_kwh=.381;config.energy.tree_absorption_kg_per_year=22;
    app_ui_set_config(&config);s_started=true;
    fixture.has_yesterday=true;
    time_t now=time(NULL);struct tm tmv;localtime_r(&now,&tmv);
    strftime(fixture.date,sizeof(fixture.date),"%Y-%m-%d",&tmv);
    int current=tmv.tm_hour*12+tmv.tm_min/5;
    for(int i=0;i<current;i++){
        float pv=fmaxf(0,sinf((i-72)*3.14159265f/144))*7;
        float load=1.5f+.7f*sinf(i*.13f);
        float battery=pv>load?-(pv-load)*.35f:(i%36<18?load*.55f:0);
        /* Felkészülés: hajnalban hálózati akkutöltés, házfogyasztástól elkülönítve. */
        if(i>=48 && i<60)battery=-2;
        float grid=load-pv-battery;
        fixture.slots[i]=(app_stats_slot_t){.valid=true,.sample_count=60,
            .pv_kw=pv,.load_kw=load,.grid_kw=grid,.battery_kw=battery,.soc_pct=fminf(100,20+pv*10),.soc_valid=true,
            .split=app_energy_split(pv,load,battery),.chart_gap=(i==current/3)};
        app_energy_split_t split=fixture.slots[i].split;
        fixture.slots[i].energy=(app_energy_totals_t){.production_kwh=pv/12,
            .pv_home_kwh=split.pv_home/12,.pv_export_kwh=split.pv_export/12,.pv_stored_kwh=split.pv_stored/12};
        fixture.totals.pv_home_kwh+=split.pv_home/12;fixture.totals.pv_export_kwh+=split.pv_export/12;
        fixture.totals.pv_stored_kwh+=split.pv_stored/12;fixture.totals.grid_home_kwh+=split.grid_home/12;
        fixture.totals.battery_home_kwh+=split.battery_home/12;
        fixture.totals.battery_charge_kwh+=fmaxf(0,-battery)/12;fixture.totals.battery_discharge_kwh+=fmaxf(0,battery)/12;
        fixture.totals.production_kwh+=pv/12;
        fixture.totals.consumption_kwh+=load/12;
        fixture.totals.grid_import_kwh+=fmaxf(0,grid)/12;
        fixture.totals.grid_export_kwh+=fmaxf(0,-grid)/12;
    }
    fixture.completed_interval_totals=fixture.totals;
    fixture.yesterday_completed_interval_totals=fixture.totals;
    fixture.yesterday_completed_interval_totals.production_kwh*=.85;
    fixture.yesterday_completed_interval_totals.consumption_kwh*=1.15;
    fixture.comparison_ready=current>0;fixture.yesterday_comparison_partial=current>0;
    fixture.comparison_paired_slots=(unsigned)current;
    fixture.comparison_today_totals=fixture.completed_interval_totals;
    fixture.comparison_yesterday_totals=fixture.yesterday_completed_interval_totals;
    send_demo();render_step(16);
}
static int self_test(void)
{
    app_energy_sample_t initial;app_state_get_sample(&initial);
    if(!initial.sun_position_valid||config.energy.battery_capacity_kwh!=15)return 22;
    /* A határértékeken is a tényleges sáv indikátorszínét ellenőrizzük. */
    const double levels[]={0,19.9,20,50,50.1,100};
    const uint32_t colors[]={RED,RED,ORANGE,ORANGE,GREEN,GREEN};
    for(int night=0;night<2;night++){
        for(unsigned i=0;i<sizeof(levels)/sizeof(levels[0]);i++){
            app_energy_sample_t sample;app_state_get_sample(&sample);
            sample.soc_bat_pct=levels[i];sample.night_mode=night!=0;
            app_state_set_sample(&sample);render_step(200);
            if(!lv_color_eq(lv_obj_get_style_bg_color(s_battery_bar,LV_PART_INDICATOR),
                c(tone(colors[i],night!=0))))return 17;
        }
    }
    /* Wind thresholds and recovery must change both weight and color. */
    const double winds[]={35.0,35.1,50.0,50.1,35.0};
    for(int night=0;night<2;night++){
        for(unsigned i=0;i<sizeof(winds)/sizeof(winds[0]);i++){
            app_energy_sample_t sample;app_state_get_sample(&sample);
            sample.wind_speed_kmh=winds[i];sample.night_mode=night!=0;
            app_state_set_sample(&sample);render_step(200);
            uint32_t expected=winds[i]>50?RED:winds[i]>35?ORANGE:night?TEXT_DARK:TEXT_LIGHT;
            if(!lv_color_eq(lv_obj_get_style_text_color(s_wind_speed,0),c(tone(expected,night!=0))))return 18;
            if((lv_obj_get_style_text_font(s_wind_speed,0)==APP_FONT_WIND_BOLD_16)!=(winds[i]>35))return 19;
        }
        const uint32_t accents[]={GREEN,ORANGE,BLUE,RED};
        for(int status=1;status<=4;status++){
            app_energy_sample_t sample;app_state_get_sample(&sample);sample.status=status;
            app_state_set_sample(&sample);render_step(200);
            if(lv_obj_get_style_border_width(s_status_card,0)!=0)return 20;
            if(!lv_color_eq(lv_obj_get_style_bg_color(s_status_bubble,0),c(tone(accents[status-1],night!=0))))return 21;
        }
    }
    for(scene=0;scene<9;scene++){
        send_demo();render_step(200);
        if(s_view!=VIEW_DASHBOARD)return 3;
        lv_obj_update_layout(lv_screen_active());
        if(lv_obj_get_height(s_battery_bar)<=lv_obj_get_width(s_battery_bar))return 13;
        for(int j=0;j<8;j++)render_step(200);
    }
    app_energy_sample_t before,after;app_state_get_sample(&before);
    char error[160];
    if(app_ingest_process(APP_DATA_HTTP,"{}",2,error,sizeof(error))==ESP_OK)return 4;
    app_state_get_sample(&after);
    if(before.received_monotonic_us!=after.received_monotonic_us)return 5;
    render_step(30001);if(s_view!=VIEW_DATA_TIMEOUT)return 6;
    send_demo();render_step(200);if(s_view!=VIEW_DASHBOARD)return 7;
    app_state_set_wifi_connected(false);render_step(200);if(s_view!=VIEW_WIFI_ERROR)return 8;
    app_state_set_wifi_connected(true);scene=0;send_demo();render_step(200);
    if(s_view!=VIEW_DASHBOARD || f_pv.phase<=0)return 9;
    lv_refr_now(display);
    FILE *f=fopen("simulator-demo.ppm","wb");if(!f)return 10;
    fprintf(f,"P6\n1280 800\n255\n");
    for(int i=0;i<1280*800;i++){
        unsigned char rgb[3]={(unsigned char)(pixels[i]>>16),(unsigned char)(pixels[i]>>8),(unsigned char)pixels[i]};
        fwrite(rgb,1,3,f);
    }
    fclose(f);printf("PASS: %u demo packets, all scenes, validation, timeout, recovery, animation.\n",packets);
    return 0;
}
#ifdef _WIN32
/* A teljes ablak memóriában készül el; csak a kész kép kerül a képernyőre.
 * A GDI puffer csak méretváltáskor foglalódik újra. */
static HDC paint_dc;
static HBITMAP paint_bitmap;
static HGDIOBJ paint_original;
static int paint_width,paint_height;
static void release_paint_buffer(void)
{
    if(paint_dc){
        if(paint_original)SelectObject(paint_dc,paint_original);
        if(paint_bitmap)DeleteObject(paint_bitmap);
        DeleteDC(paint_dc);
    }
    paint_dc=NULL;paint_bitmap=NULL;paint_original=NULL;
    paint_width=paint_height=0;
}
static bool paint_frame(HDC target,int width,int height)
{
    if(width<=0 || height<=0)return true;
    if(!paint_dc || width!=paint_width || height!=paint_height){
        HDC next_dc=CreateCompatibleDC(target);
        if(!next_dc)return false;
        HBITMAP next_bitmap=CreateCompatibleBitmap(target,width,height);
        if(!next_bitmap){DeleteDC(next_dc);return false;}
        HGDIOBJ original=SelectObject(next_dc,next_bitmap);
        if(!original || original==HGDI_ERROR){DeleteObject(next_bitmap);DeleteDC(next_dc);return false;}
        release_paint_buffer();
        paint_dc=next_dc;paint_bitmap=next_bitmap;paint_original=original;
        paint_width=width;paint_height=height;
    }
    RECT r={0,0,width,height};
    FillRect(paint_dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));
    int w=width,h=w*800/1280;if(h>height){h=height;w=h*1280/800;}
    if(w<=0 || h<=0)return true;
    BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=1280;bi.bmiHeader.biHeight=-800;bi.bmiHeader.biPlanes=1;
    bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    int copied=StretchDIBits(paint_dc,(width-w)/2,(height-h)/2,w,h,0,0,1280,800,
        pixels,&bi,DIB_RGB_COLORS,SRCCOPY);
    if(copied==0 || copied==GDI_ERROR)return false;
    return BitBlt(target,0,0,width,height,paint_dc,0,0,SRCCOPY)!=0;
}
static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    static int drag_x;
    switch(msg){
    case WM_LBUTTONDOWN:drag_x=(short)LOWORD(lp);SetCapture(hwnd);return 0;
    case WM_LBUTTONUP:{int delta=(short)LOWORD(lp)-drag_x;ReleaseCapture();if(abs(delta)>60)app_templates_step(delta<0?1:-1);return 0;}
    case WM_ERASEBKGND:
        /* A háttér és a képarány miatti sávok is a kész képpel együtt frissülnek. */
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
        if(!paint_frame(dc,r.right,r.bottom))
            fprintf(stderr,"GDI frame presentation failed: %lu\n",GetLastError());
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;}
        if(wp==VK_SPACE){paused=!paused;SetWindowTextA(hwnd,paused?"Zolee DEMO - PAUSED (30s timeout)":"Zolee DEMO - 5s packets");}
        if(wp=='N'){night_override=night_override==1?0:1;send_demo();}
        if(wp=='S'){sd_ok=!sd_ok;}
        if(wp=='W'){app_runtime_state_t rt;app_state_get_runtime(&rt);app_state_set_wifi_connected(!rt.wifi_connected);}
        if(wp=='I'){s_diagnostics_requested=!s_diagnostics_requested;diagnostics_capture();}
        if(wp==VK_RIGHT||wp==VK_LEFT)app_templates_step(wp==VK_RIGHT?1:-1);
        if(wp=='D'){scene=(scene+1)%9;send_demo();}
        return 0;
    case WM_DESTROY:release_paint_buffer();PostQuitMessage(0);return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
/* Ugyanazt a GDI megjelenítési utat ellenőrzi a Windows CI, mint az ablak.
 * Két méret, ismételt rajzolás és fekete képarány-sávok ellenőrzése. */
static int paint_self_test(void)
{
    const int sizes[2][2]={{640,550},{1280,800}};
    for(int i=0;i<2;i++){
        int w=sizes[i][0],h=sizes[i][1];
        HDC dc=CreateCompatibleDC(NULL);if(!dc)return 14;
        BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;
        bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
        void *data=NULL;HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&data,NULL,0);
        if(!bitmap){DeleteDC(dc);return 15;}
        HGDIOBJ original=SelectObject(dc,bitmap);
        bool ok=paint_frame(dc,w,h) && paint_frame(dc,w,h);
        GdiFlush();
        uint32_t *frame=data;
        if(i==0)ok=ok && (frame[0]&0xffffff)==0;
        if(i==1)ok=ok && (frame[0]&0xffffff)==(pixels[0]&0xffffff);
        unsigned visible=0;for(int n=0;n<w*h;n++)if(frame[n]&0xffffff)visible++;
        ok=ok && visible>(unsigned)(w*h/2);
        SelectObject(dc,original);DeleteObject(bitmap);DeleteDC(dc);
        if(!ok){release_paint_buffer();return 16;}
    }
    release_paint_buffer();puts("PASS: buffered GDI presentation, resize, repeated paint and letterboxing.");
    return 0;
}
#endif
int main(int argc,char **argv)
{
    app_events_init();app_events_add(APP_EVENT_INFO,"Desktop simulator started.");
    app_templates_init();
    if(argc>1&&!strcmp(argv[1],"--self-test"))app_templates_select("builtin");
    init_demo();int result=0;
    if(argc>1 && strcmp(argv[1],"--self-test")==0){
        result=self_test();
#ifdef _WIN32
        if(result==0)result=paint_self_test();
#endif
    }
    else {
#ifdef _WIN32
        SetProcessDPIAware();
        HINSTANCE instance=GetModuleHandleA(NULL);
        WNDCLASSA wc={0};wc.lpfnWndProc=wndproc;wc.hInstance=instance;
        wc.lpszClassName="ZoleeSimulator";wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        if(!RegisterClassA(&wc))return 11;
        RECT rect={0,0,1280,800};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        window=CreateWindowA(wc.lpszClassName,"Zolee DEMO | Left/Right: template | D: scene | N: night | Space: pause | W: Wi-Fi | S: SD",
            WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,NULL,NULL,instance,NULL);
        if(!window)return 12;
        ShowWindow(window,SW_SHOW);
        ULONGLONG previous=GetTickCount64(),next=previous+5000;
        bool running=true;
        while(running){
            MSG msg;while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){
                if(msg.message==WM_QUIT){running=false;break;}
                TranslateMessage(&msg);DispatchMessage(&msg);
            }
            if(!running)break;
            ULONGLONG now=GetTickCount64();
            unsigned elapsed=(unsigned)(now-previous);previous=now;
            /* Debugger megállításakor a szimuláció órája is megáll. */
            if(elapsed>250)elapsed=16;
            if(now>=next){next=now+5000;if(!paused){scene=(scene+1)%9;send_demo();}}
            render_step(elapsed);Sleep(16);
        }
#else
        fprintf(stderr,"Window mode requires Windows; use --self-test on other hosts.\n");result=1;
#endif
    }
    clear();lv_display_delete(display);lv_deinit();return result;
}
