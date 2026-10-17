/* A valódi alkalmazásmodulok host regressziós tesztjei, fiktív idővel és SD-vel. */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "cJSON.h"
#include "app_config.h"
#include "app_payload.h"
#include "app_stats.h"
#include "app_storage.h"
#include "app_events.h"
#include "app_flow_model.h"
#include "freertos/semphr.h"

static time_t fake_epoch;
static int64_t fake_us;
static bool sd_ok=true;
static bool during_sync;
static int rename_fail_at,rename_calls;
static app_config_t cfg;
static app_stats_snapshot_t snapshot;
static int tests;

time_t host_time(time_t *out){if(out)*out=fake_epoch;return fake_epoch;}
int64_t esp_timer_get_time(void){return fake_us;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){
    pthread_mutex_t *m=malloc(sizeof(*m));assert(m);pthread_mutex_init(m,NULL);return m;
}
int xSemaphoreTake(SemaphoreHandle_t m,unsigned timeout){(void)timeout;assert(pthread_mutex_lock(m)==0);return 1;}
int xSemaphoreGive(SemaphoreHandle_t m){assert(pthread_mutex_unlock(m)==0);return 1;}
int xTaskCreate(void (*f)(void *),const char *n,unsigned st,void *a,unsigned pr,void *h)
{(void)f;(void)n;(void)st;(void)a;(void)pr;(void)h;return 1;}
void vTaskDelay(unsigned t){(void)t;}
bool app_storage_is_ok(void){return sd_ok;}
void app_storage_mark_fault(const char *reason){(void)reason;sd_ok=false;}
esp_err_t app_storage_lock(void){return ESP_OK;}
void app_storage_unlock(void){}

static time_t instant(const char *date,int hour,int minute,int second){
    struct tm t={.tm_hour=hour,.tm_min=minute,.tm_sec=second,.tm_isdst=-1};
    int y,m,d;assert(sscanf(date,"%d-%d-%d",&y,&m,&d)==3);
    t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;return mktime(&t);
}
static void clock_at(time_t t){fake_epoch=t;fake_us+=1000000;}
static app_energy_sample_t sample(time_t t,double power,double grid,double battery){
    return (app_energy_sample_t){.power_pv_kw=power,.power_full_kw=power,
        .power_grid_kw=grid,.power_bat_kw=battery,.received_epoch_s=t,
        .received_monotonic_us=fake_us,.status=1,.soc_bat_pct=50};
}
static void feed(time_t t,double power,double grid,double battery){
    clock_at(t);app_energy_sample_t s=sample(t,power,grid,battery);app_stats_process_sample(&s);
}
static void near(double a,double b){if(fabs(a-b)>1e-7){fprintf(stderr,"Expected %.12f; got %.12f\n",b,a);abort();}}
static void clean(void){
    DIR *dir=opendir(APP_SD_MOUNT_POINT "/stats");
    if(dir){struct dirent *entry;while((entry=readdir(dir))){
        if(entry->d_name[0]=='.')continue;
        char path[512];snprintf(path,sizeof(path),APP_SD_MOUNT_POINT "/stats/%s",entry->d_name);unlink(path);
    }closedir(dir);}
}
static void reset(time_t t){
    clean();clock_at(t);sd_ok=true;during_sync=false;rename_fail_at=0;rename_calls=0;
    app_config_set_defaults(&cfg);assert(app_stats_init(&cfg)==ESP_OK);
}
static cJSON *read_json(const char *date){
    char path[256];snprintf(path,sizeof(path),APP_SD_MOUNT_POINT "/stats/%s.json",date);
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    char *text=calloc(1,n+1);assert(text);assert(fread(text,1,n,f)==(size_t)n);fclose(f);
    cJSON *root=cJSON_Parse(text);free(text);assert(root);return root;
}
static void write_json(const char *date,cJSON *root){
    char path[256];snprintf(path,sizeof(path),APP_SD_MOUNT_POINT "/stats/%s.json",date);
    char *text=cJSON_PrintUnformatted(root);assert(text);FILE *f=fopen(path,"wb");assert(f);
    fputs(text,f);fclose(f);cJSON_free(text);
}
static double total(cJSON *root){return cJSON_GetObjectItem(cJSON_GetObjectItem(root,"totals"),"production_kwh")->valuedouble;}
int __real_fsync(int fd);
int __wrap_fsync(int fd){
    if(during_sync){
        during_sync=false;
        /* Ez a hívás deadlockolna, ha az SD I/O még mindig a statisztikai mutexet tartaná. */
        feed(fake_epoch+5,7,0,0);
        app_stats_get_snapshot(&snapshot);
        assert(snapshot.totals.production_kwh>0);
    }
    return __real_fsync(fd);
}
int __real_rename(const char *from,const char *to);
int __wrap_rename(const char *from,const char *to){
    if(rename_fail_at && ++rename_calls==rename_fail_at){errno=EIO;return -1;}
    return __real_rename(from,to);
}

static void test_integrals(void){
    time_t t=instant("2026-10-05",12,0,0);reset(t);
    feed(t,7,5,-5);feed(t+10,7,-5,5);app_stats_get_snapshot(&snapshot);
    near(snapshot.totals.production_kwh,70./3600);
    near(snapshot.totals.grid_import_kwh,12.5/3600);
    near(snapshot.totals.grid_export_kwh,12.5/3600);
    near(snapshot.totals.battery_charge_kwh,12.5/3600);
    near(snapshot.totals.battery_discharge_kwh,12.5/3600);
    feed(t+41,15,15,15);app_stats_get_snapshot(&snapshot);
    near(snapshot.totals.production_kwh,70./3600);tests++;
}
static void test_midnight(bool background_first){
    time_t t=instant("2026-10-04",23,59,58);reset(t);feed(t,7,0,0);
    if(background_first){clock_at(t+2);app_stats_tick();}
    feed(t+5,7,0,0);app_stats_get_snapshot(&snapshot);
    assert(strcmp(snapshot.date,"2026-10-05")==0);near(snapshot.totals.production_kwh,21./3600);
    assert(app_stats_save_now()==ESP_OK);cJSON *day=read_json("2026-10-04");near(total(day),14./3600);cJSON_Delete(day);
    assert(app_stats_init(&cfg)==ESP_OK);app_stats_get_snapshot(&snapshot);near(snapshot.totals.production_kwh,21./3600);tests++;
}
static void test_completed_interval(void){
    time_t t=instant("2026-10-04",14,34,55);reset(t);
    feed(t,6,0,0);feed(t+10,6,0,0);
    time_t today=instant("2026-10-05",14,34,55);
    feed(today,3,0,0);feed(today+10,3,0,0);clock_at(today+125);
    app_stats_get_snapshot(&snapshot);assert(snapshot.has_yesterday);
    near(snapshot.completed_interval_totals.production_kwh,15./3600);
    near(snapshot.yesterday_completed_interval_totals.production_kwh,30./3600);
    /* Kevés, de egyező időablak: van tegnapi adat, ám százalékot
     * legalább egy óra közös mérésből számítunk. */
    assert(snapshot.comparison_paired_slots==1);
    assert(!snapshot.comparison_ready);
    near(snapshot.comparison_today_totals.production_kwh,15./3600);
    near(snapshot.comparison_yesterday_totals.production_kwh,30./3600);
    tests++;
}
static void test_sd_recovery(void){
    time_t t=instant("2026-10-04",23,59,58);reset(t);feed(t,7,0,0);
    sd_ok=false;clock_at(t+2);app_stats_tick();feed(t+5,7,0,0);
    clock_at(t+33);sd_ok=true;app_stats_tick();
    cJSON *y=read_json("2026-10-04"),*d=read_json("2026-10-05");
    near(total(y),14./3600);near(total(d),21./3600);cJSON_Delete(y);cJSON_Delete(d);tests++;
}
static void test_nonblocking_save(void){
    time_t t=instant("2026-10-05",12,0,0);reset(t);feed(t,7,0,0);
    during_sync=true;assert(app_stats_save_now()==ESP_OK);
    app_stats_get_snapshot(&snapshot);near(snapshot.totals.production_kwh,35./3600);
    assert(app_stats_save_now()==ESP_OK);cJSON *d=read_json("2026-10-05");near(total(d),35./3600);cJSON_Delete(d);tests++;
}
static void test_tmp_and_backup(void){
    time_t t=instant("2026-10-05",12,0,0);reset(t);feed(t,7,0,0);feed(t+5,7,0,0);
    /* Első mentésnél a fsync már kész, de a végső átnevezés hibát ad. */
    rename_fail_at=2;assert(app_stats_save_now()!=ESP_OK);assert(!sd_ok);
    rename_fail_at=0;sd_ok=true;assert(app_stats_init(&cfg)==ESP_OK);
    app_stats_get_snapshot(&snapshot);near(snapshot.totals.production_kwh,35./3600);
    assert(app_stats_save_now()==ESP_OK);
    feed(t+10,7,0,0);feed(t+15,7,0,0);assert(app_stats_save_now()==ESP_OK);
    cJSON *d=read_json("2026-10-05");near(total(d),70./3600);cJSON_Delete(d);
    /* Új, teljes tmp + régebbi főfájl: revision szerint a tmp nyer. */
    assert(rename(APP_SD_MOUNT_POINT "/stats/2026-10-05.json",APP_SD_MOUNT_POINT "/stats/2026-10-05.json.tmp")==0);
    assert(rename(APP_SD_MOUNT_POINT "/stats/2026-10-05.json.bak",APP_SD_MOUNT_POINT "/stats/2026-10-05.json")==0);
    assert(app_stats_init(&cfg)==ESP_OK);app_stats_get_snapshot(&snapshot);near(snapshot.totals.production_kwh,70./3600);
    /* Recovery already promoted tmp, so another power loss cannot lose it. */
    d=read_json("2026-10-05");near(total(d),70./3600);cJSON_Delete(d);
    assert(access(APP_SD_MOUNT_POINT "/stats/2026-10-05.json.tmp",F_OK)!=0);
    assert(app_stats_save_now()==ESP_OK);
    FILE *f=fopen(APP_SD_MOUNT_POINT "/stats/2026-10-05.json","wb");assert(f);fputs("corrupt",f);fclose(f);
    assert(app_stats_init(&cfg)==ESP_OK);app_stats_get_snapshot(&snapshot);near(snapshot.totals.production_kwh,70./3600);tests++;
}
static void test_corrupt_schema(void){
    for(int scenario=0;scenario<4;scenario++){
        time_t t=instant("2026-10-05",12,0,0);reset(t);feed(t,7,0,0);feed(t+5,7,0,0);
        assert(app_stats_save_now()==ESP_OK);cJSON *d=read_json("2026-10-05");
        cJSON *slot=cJSON_GetArrayItem(cJSON_GetObjectItem(d,"slots"),144);
        if(scenario==0)cJSON_SetNumberValue(cJSON_GetObjectItem(slot,"count"),1.5);
        if(scenario==1)cJSON_SetNumberValue(cJSON_GetObjectItem(slot,"count"),4294967296.);
        if(scenario==2)cJSON_SetNumberValue(cJSON_GetObjectItem(slot,"pv"),16);
        write_json("2026-10-05",d);cJSON_Delete(d);
        if(scenario==3){FILE *f=fopen(APP_SD_MOUNT_POINT "/stats/2026-10-05.json","ab");fputs(" junk",f);fclose(f);}
        assert(app_stats_init(&cfg)==ESP_OK);app_stats_get_snapshot(&snapshot);near(snapshot.totals.production_kwh,0);
    }tests++;
}
static const char *payload="{\"PowerGrid\":1,\"PowerBat\":-2,\"PowerPV\":3,\"SocBat\":50,\"PowerFull\":2,\"Temperature\":20,\"WindSpeed\":5,\"HasSunRadiation\":1,\"HasPrecipitation\":0,\"Status\":1,\"NightMode\":0,\"BatteryTemperature\":30}";
static void test_payload(void){
    app_energy_sample_t result={0};char error[160];
    assert(app_payload_parse(payload,strlen(payload),&result,error,sizeof(error))==ESP_OK);
    /* Visszafelé kompatibilitás és nappálya bemenet validálása. */
    assert(!result.sun_position_valid);
    cJSON *solar_root=cJSON_Parse(payload);
    cJSON_AddStringToObject(solar_root,"SunPosition","327.445#-46.562#420#1077");
    char *solar_payload=cJSON_PrintUnformatted(solar_root);
    assert(app_payload_parse(solar_payload,strlen(solar_payload),&result,error,sizeof(error))==ESP_OK);
    assert(result.sun_position_valid);
    near(result.sun_azimuth_deg,327.445);near(result.sun_altitude_deg,-46.562);
    assert(result.sunrise_minute==420 && result.sunset_minute==1077);
    cJSON_free(solar_payload);
    cJSON_ReplaceItemInObject(solar_root,"SunPosition",cJSON_CreateNull());
    solar_payload=cJSON_PrintUnformatted(solar_root);
    assert(app_payload_parse(solar_payload,strlen(solar_payload),&result,error,sizeof(error))==ESP_OK);
    assert(!result.sun_position_valid);
    cJSON_free(solar_payload);
    const char *bad_sun[]={"361#0#420#1077","120#91#420#1077","120#0#1077#420","120#0#420#1077junk","120#0#420",NULL};
    for(unsigned si=0;bad_sun[si];si++){
        cJSON_ReplaceItemInObject(solar_root,"SunPosition",cJSON_CreateString(bad_sun[si]));
        solar_payload=cJSON_PrintUnformatted(solar_root);
        assert(app_payload_parse(solar_payload,strlen(solar_payload),&result,error,sizeof(error))!=ESP_OK);
        cJSON_free(solar_payload);
    }
    cJSON_Delete(solar_root);
    near(result.power_bat_kw,-2);
    assert(result.temperature_valid && result.wind_speed_valid && result.sun_radiation_valid && result.precipitation_valid);
    const char *weather_fields[]={"Temperature","WindSpeed","HasSunRadiation","HasPrecipitation"};
    for(unsigned mask=0;mask<16;mask++){
        cJSON *weather=cJSON_Parse(payload);
        for(unsigned j=0;j<4;j++)cJSON_ReplaceItemInObject(weather,weather_fields[j],
            mask&(1u<<j)?cJSON_CreateNumber(0):cJSON_CreateNull());
        char *weather_text=cJSON_PrintUnformatted(weather);
        assert(app_payload_parse(weather_text,strlen(weather_text),&result,error,sizeof(error))==ESP_OK);
        assert(result.temperature_valid==!!(mask&1));assert(result.wind_speed_valid==!!(mask&2));
        assert(result.sun_radiation_valid==!!(mask&4));assert(result.precipitation_valid==!!(mask&8));
        near(result.temperature_c,0);near(result.wind_speed_kmh,0);
        assert(!result.has_sun_radiation && !result.has_precipitation);
        cJSON_free(weather_text);cJSON_Delete(weather);
    }
    for(unsigned j=0;j<4;j++){
        cJSON *weather=cJSON_Parse(payload);cJSON_DeleteItemFromObject(weather,weather_fields[j]);
        char *weather_text=cJSON_PrintUnformatted(weather);
        assert(app_payload_parse(weather_text,strlen(weather_text),&result,error,sizeof(error))!=ESP_OK);
        cJSON_free(weather_text);cJSON_Delete(weather);
    }
    assert(app_payload_parse(payload,strlen(payload),&result,error,sizeof(error))==ESP_OK);
    assert(result.temperature_valid && result.wind_speed_valid && result.sun_radiation_valid && result.precipitation_valid);
    for(unsigned j=0;j<12;j++){
        const char *all[]={"PowerGrid","PowerBat","PowerPV","SocBat","PowerFull","Temperature","WindSpeed","HasSunRadiation","HasPrecipitation","Status","NightMode","BatteryTemperature"};
        cJSON *d=cJSON_Parse(payload);cJSON_ReplaceItemInObject(d,all[j],cJSON_CreateString("null"));
        char *text=cJSON_PrintUnformatted(d);assert(app_payload_parse(text,strlen(text),&result,error,sizeof(error))!=ESP_OK);
        cJSON_free(text);cJSON_ReplaceItemInObject(d,all[j],cJSON_CreateNull());text=cJSON_PrintUnformatted(d);
        bool optional=j>=5 && j<=8;assert((app_payload_parse(text,strlen(text),&result,error,sizeof(error))==ESP_OK)==optional);
        cJSON_free(text);cJSON_Delete(d);
    }
    const char *fields[]={"SocBat","Status","NightMode","PowerPV","Temperature","BatteryTemperature","WindSpeed"};
    double bad[]={101,1.5,2,-.051,-36,81,-1};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        cJSON *d=cJSON_Parse(payload);cJSON_SetNumberValue(cJSON_GetObjectItem(d,fields[i]),bad[i]);char *text=cJSON_PrintUnformatted(d);
        result.power_pv_kw=123;assert(app_payload_parse(text,strlen(text),&result,error,sizeof(error))!=ESP_OK);near(result.power_pv_kw,123);
        cJSON_free(text);cJSON_Delete(d);
    }
    cJSON *d=cJSON_Parse(payload);cJSON_AddNumberToObject(d,"powergrid",2);char *text=cJSON_PrintUnformatted(d);
    assert(app_payload_parse(text,strlen(text),&result,error,sizeof(error))!=ESP_OK);cJSON_free(text);cJSON_Delete(d);
    d=cJSON_Parse(payload);cJSON_DeleteItemFromObject(d,"PowerPV");cJSON_AddNumberToObject(d,"powerpv",-.02);cJSON_AddStringToObject(d,"extra","ignored");text=cJSON_PrintUnformatted(d);
    assert(app_payload_parse(text,strlen(text),&result,error,sizeof(error))==ESP_OK);near(result.power_pv_kw,0);cJSON_free(text);cJSON_Delete(d);
    char garbage[512];snprintf(garbage,sizeof(garbage),"%s junk",payload);assert(app_payload_parse(garbage,strlen(garbage),&result,error,sizeof(error))!=ESP_OK);tests++;
}
static void test_config(void){
    app_config_t defaults;app_config_set_defaults(&defaults);assert(defaults.statistics.save_interval_minutes==15);
    assert(defaults.display.rotation_degrees==270);
    char error[256];const char *path=APP_SD_MOUNT_POINT "/config.json";
    for(int interval=60;interval<=61;interval++){
        FILE *f=fopen(path,"wb");assert(f);
        fprintf(f,"{\"project\":{\"language\":\"EN\"},\"wifi\":{\"ssid\":\"test\"},\"mqtt\":{\"broker\":\"localhost\"},\"statistics\":{\"save_interval_minutes\":%d}}",interval);fclose(f);
        esp_err_t error_code=app_config_load_file(path,&cfg,error,sizeof(error));
        assert((interval==60)==(error_code==ESP_OK));if(interval==60)assert(strcmp(cfg.project.language,"en")==0);
    }
    /* Régi config, mindkét új állás, érvénytelen értékek. */
    const struct {const char *part;int expected;} cases[]={
        {"",270}, {",\"display\":{}",270},
        {",\"display\":{\"day_brightness\":55}",270},
        {",\"display\":{\"rotation_degrees\":270}",270},
        {",\"display\":{\"rotation_degrees\":90}",90},
        {",\"display\":{\"rotation_degrees\":180}",-1},
        {",\"display\":{\"rotation_degrees\":275}",-1},
        {",\"display\":{\"rotation_degrees\":0}",-1},
        {",\"display\":{\"rotation_degrees\":90.5}",-1},
        {",\"display\":{\"rotation_degrees\":\"90\"}",-1},
        {",\"display\":{\"rotation_degrees\":null}",-1}
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        FILE *f=fopen(path,"wb");assert(f);
        fprintf(f,"{\"wifi\":{\"ssid\":\"test\"},\"mqtt\":{\"broker\":\"localhost\"}%s}",cases[i].part);
        fclose(f);
        esp_err_t rc=app_config_load_file(path,&cfg,error,sizeof(error));
        assert((rc==ESP_OK)==(cases[i].expected>=0));
        if(rc==ESP_OK)assert(cfg.display.rotation_degrees==cases[i].expected);
    }
    tests++;
}
static void test_flow(void){
    near(app_flow_advance(.4f,7,false,0),.4f);
    near(app_flow_advance(.4f,0,true,10),.4f);
    near(app_flow_advance(.4f,15,true,.1f),app_flow_advance(.4f,7,true,.1f));
    float phase=.4f;phase=app_flow_advance(phase,7,true,.016f);phase=app_flow_advance(phase,7,false,.016f);near(phase,.4f);
    phase=app_flow_advance(.001f,7,false,.016f);assert(phase>=0 && phase<1);
    float slow=app_flow_advance(0,.1,true,1);
    float medium=app_flow_advance(0,1,true,1);
    float fast=app_flow_advance(0,7,true,.5f);
    assert(slow>0 && slow<medium && medium<fast);
    tests++;
}
/* Fizikai példák, irányváltás ötperces ablakon belül, mentés és adathézag. */
static void test_energy_split(void){
    app_energy_split_t v=app_energy_split(3,2,-4);
    near(v.pv_home,2);near(v.pv_stored,1);near(v.pv_export,0);near(v.grid_home,0);
    v=app_energy_split(0,2,-4);near(v.grid_home,2);near(v.pv_stored,0);
    v=app_energy_split(7,2,-2);near(v.pv_home,2);near(v.pv_stored,2);near(v.pv_export,3);
    v=app_energy_split(1,5,3);near(v.pv_home,1);near(v.battery_home,3);near(v.grid_home,1);
    v=app_energy_split(0,0,0);near(v.grid_home,0);near(v.pv_export,0);
    time_t t=instant("2026-10-05",12,0,0);reset(t);
    app_energy_sample_t a=sample(t,4,0,-2);a.power_full_kw=2;
    app_stats_process_sample(&a);
    a=sample(t+5,0,0,2);a.power_full_kw=2;app_stats_process_sample(&a);
    app_stats_get_snapshot(&snapshot);
    v=snapshot.slots[144].split;
    near(v.pv_home,1);near(v.pv_stored,1);near(v.battery_home,1);
    assert(app_stats_save_now()==ESP_OK);assert(app_stats_init(&cfg)==ESP_OK);
    app_stats_get_snapshot(&snapshot);near(snapshot.slots[144].split.pv_stored,1);
    /* A v1 adatok nem vesznek el: becslés a régi átlagokból. */
    cJSON *d=read_json("2026-10-05");cJSON_SetNumberValue(cJSON_GetObjectItem(d,"version"),1);
    write_json("2026-10-05",d);cJSON_Delete(d);assert(app_stats_init(&cfg)==ESP_OK);
    app_stats_get_snapshot(&snapshot);near(snapshot.slots[144].split.pv_home,2);
    reset(t);feed(t,2,0,0);feed(t+301,2,0,0);app_stats_get_snapshot(&snapshot);
    assert(snapshot.slots[144].chart_gap && snapshot.slots[145].chart_gap);
    assert(app_stats_save_now()==ESP_OK);assert(app_stats_init(&cfg)==ESP_OK);
    app_stats_get_snapshot(&snapshot);assert(snapshot.slots[144].chart_gap);
    tests++;
}
static void test_events(void){
    app_event_entry_t entries[2];app_events_add(APP_EVENT_INFO,"Új nap indult: %s","2026-10-05");
    size_t n=app_events_snapshot(entries,2);assert(n>0);assert(strstr(entries[n-1].text_en,"New day started"));tests++;
}
#include "http_cases.inc"
static void test_period_metrics(void){
    time_t t=instant("2026-10-05",12,0,0);reset(t);
    app_energy_sample_t a=sample(t,6,-2,-2);a.power_full_kw=2;
    clock_at(t);app_stats_process_sample(&a);a.received_epoch_s=t+10;clock_at(t+10);app_stats_process_sample(&a);
    app_stats_get_snapshot(&snapshot);
    near(snapshot.totals.pv_home_kwh,20./3600);near(snapshot.totals.pv_stored_kwh,20./3600);
    near(snapshot.totals.pv_export_kwh,20./3600);near(snapshot.slots[144].soc_pct,50);
    app_stats_metrics_t m=app_stats_metrics(&snapshot.totals,&cfg.energy);
    near(m.self_sufficiency_pct,100);near(m.self_consumption_pct,200./3);
    app_stats_period_t week;app_stats_get_period(false,&week);assert(week.count==7);
    assert(!strcmp(week.days[0].date,"2026-10-05")&&week.days[0].valid&&!week.days[1].valid);
    assert(app_stats_save_now()==ESP_OK);
    clock_at(instant("2026-10-10",12,0,0));assert(app_stats_init(&cfg)==ESP_OK);
    app_stats_get_period(true,&week);assert(week.count==31&&week.days[4].valid);
    near(week.totals.pv_home_kwh,20./3600);
    app_energy_totals_t zero={0};m=app_stats_metrics(&zero,&cfg.energy);near(m.self_consumption_pct,0);
    clock_at(instant("2028-02-10",12,0,0));assert(app_stats_init(&cfg)==ESP_OK);
    app_stats_get_period(true,&week);assert(week.count==29);
    /* Régi slot átlagába az első új SOC-minta nem hígulhat bele. */
    reset(t);feed(t,2,0,0);assert(app_stats_save_now()==ESP_OK);
    cJSON *old=read_json("2026-10-05");cJSON_SetNumberValue(cJSON_GetObjectItem(old,"version"),2);
    write_json("2026-10-05",old);cJSON_Delete(old);assert(app_stats_init(&cfg)==ESP_OK);
    a=sample(t+5,2,0,0);a.soc_bat_pct=80;clock_at(t+5);app_stats_process_sample(&a);
    app_stats_get_snapshot(&snapshot);near(snapshot.slots[144].soc_pct,80);
    tests++;
}
int main(void){
    setenv("TZ","UTC0",1);tzset();app_state_init();app_events_init();
    test_payload();test_config();test_flow();test_events();
    test_integrals();test_midnight(false);test_midnight(true);test_completed_interval();
    test_sd_recovery();test_nonblocking_save();test_tmp_and_backup();test_corrupt_schema();
    /* Budapesti helyi éjfél: ugyanaz az elvárt elosztás. */
    setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",1);tzset();test_midnight(true);
    test_period_metrics();
    test_mode_config();test_http();test_energy_split();
    clean();printf("PASS: %d regression groups (cJSON %s)\n",tests,cJSON_Version());return 0;
}
