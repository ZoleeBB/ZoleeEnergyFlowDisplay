/* Valódi profil, ütemező és időjáráskezelő, ellenőrzött órával és válaszokkal. */
#include "app_inverter.h"
#include "app_inverter_settings.h"
#include "app_inverter_identity.h"
#include "app_inverter_net.h"
#include "app_inverter_weather.h"
#include "app_ota.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

static app_runtime_state_t runtime;
static app_energy_sample_t last;
static app_energy_sample_t measurement = {.power_pv_kw = 2.5, .status = 1};
static double weather_temp, weather_wind;
static int allocations, commits, weather, reads, samples, breaks;
static bool fail_save, configured;
static time_t clock_now = 1782010800;
static int64_t source_time, mono = 1000000;
static void (*meteo_job)(void *);
static unsigned char nvs_data[sizeof(app_inverter_settings_t)];
static size_t nvs_size;
time_t __wrap_time(time_t *out) { if (out) *out=clock_now; return clock_now; }
int64_t esp_timer_get_time(void) { return mono; }
void *heap_caps_calloc(size_t count,size_t bytes,unsigned caps) {
    assert(caps); allocations++; return calloc(count,bytes);
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    pthread_mutex_t *m=malloc(sizeof(*m)); assert(!pthread_mutex_init(m,NULL)); return m;
}
void vSemaphoreDelete(SemaphoreHandle_t m) { pthread_mutex_destroy(m); free(m); }
int xSemaphoreTake(SemaphoreHandle_t m,unsigned t) { (void)t; return !pthread_mutex_lock(m); }
int xSemaphoreGive(SemaphoreHandle_t m) { return !pthread_mutex_unlock(m); }
bool app_inverter_link(uint32_t ip,const uint8_t mac[6],bool probe,app_inverter_link_t *out)
{
    (void)ip; (void)mac; (void)probe; (void)out;
    assert(!"a DNS-es és a felhős teszt nem igényel ARP-t");
    return false;
}
void app_inverter_link_idle(void) {}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key)
{
    assert(h==1 && !strcmp(key,"lan_pin"));
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_open(const char *n,int mode,nvs_handle_t *h) {
    assert(!strcmp(n,"inverter_v4")); (void)mode; *h=1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h==1); }
esp_err_t nvs_commit(nvs_handle_t h) { assert(h==1); commits++; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *v,size_t size) {
    assert(h==1 && !strcmp(key,"profile1") && size<=sizeof(nvs_data));
    if (fail_save) return ESP_FAIL;
    memcpy(nvs_data,v,size); nvs_size=size; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *v,size_t *size) {
    assert(h==1 && !strcmp(key,"profile1")); if (!nvs_size) return ESP_ERR_NOT_FOUND;
    assert(*size>=nvs_size); memcpy(v,nvs_data,nvs_size); *size=nvs_size; return ESP_OK;
}
void app_state_get_runtime(app_runtime_state_t *out) { *out=runtime; }
bool app_state_get_sample(app_energy_sample_t *out) { *out=last; return runtime.has_valid_sample; }
void app_state_set_sample(const app_energy_sample_t *s) { last=*s; runtime.has_valid_sample=true; }
void app_state_clear_sample(void) { runtime.has_valid_sample=false; }
void app_state_set_inverter_timeout(uint16_t s) { runtime.inverter_timeout_seconds=s; }
void app_state_set_inverter_ready(bool ready) { configured=ready; }
void app_stats_break_continuity(void) { breaks++; }
void app_stats_process_sample(const app_energy_sample_t *s) { assert(s->received_epoch_s>0); samples++; }
void app_inverter_http_reset(void) {}
void app_inverter_victron_close(void) {}
bool app_inverter_victron_read(const app_inverter_config_t *cfg,app_energy_sample_t *s) {
    (void)cfg;(void)s; abort();
}
bool app_inverter_modbus_read(const app_inverter_config_t *cfg,app_energy_sample_t *s,int64_t *stamp) {
    (void)cfg;(void)s;(void)stamp; abort();
}
bool app_inverter_http_read(const app_inverter_config_t *cfg,app_energy_sample_t *s,int64_t *stamp) {
    assert(cfg->host[0]); reads++; *s=measurement; *stamp=source_time; return true;
}
bool app_ota_operation_active(void) { return false; }
bool app_ota_background_post(void (*f)(void *),void *arg) { assert(!arg && !meteo_job); meteo_job=f; return true; }
cJSON *app_inverter_json_request(const char *url,const char *method,const char *b,
                                 const char *const h[][2],size_t n,char *token,size_t cap,const char *pem) {
    assert(strstr(url,"https://api.open-meteo.com/") && !strcmp(method,"GET") && !n && !h && !b && !token && !cap && !pem);
    weather++;
    char json[240]; snprintf(json,sizeof(json),"{\"current\":{\"time\":%ld,\"temperature_2m\":%.1f,"
      "\"wind_speed_10m\":%.1f,\"relative_humidity_2m\":50,\"precipitation\":0,\"direct_normal_irradiance\":200}}",(long)clock_now,weather_temp,weather_wind);
    return cJSON_Parse(json);
}
static void tick(int seconds) {
    mono += seconds*INT64_C(1000000); clock_now += seconds; app_inverter_tick();
    if (meteo_job) { meteo_job(NULL); meteo_job=NULL; }
}
static cJSON *settings(const char *profile) {
    cJSON *s=cJSON_Parse("{\"host\":\"unit.local\",\"port\":80,\"unit\":1,\"battery\":false,\"latitude\":0,\"longitude\":0}");
    cJSON_AddStringToObject(s,"profile",profile); return s;
}

/* A korábbi bináris NVS-formátum független másolata. A PEM változó
 * hosszban volt elmentve, a régi igazítás hoston és P4-en is számít. */
typedef struct {
    unsigned version;
    struct {
        char profile[40], host[128], device[96], user[96], secret[160], key[160];
        uint16_t port;
        uint8_t unit;
        bool battery;
        const char *trusted_pem;
    } driver;
    double latitude, longitude;
    app_inverter_schedule_t master;
    struct {
        char id[APP_SLAVE_PRESENCE_ID_SIZE];
        app_inverter_schedule_t schedule;
    } peers[APP_SLAVE_PRESENCE_MAX_PEERS];
    char pem[3072];
} stored_v1_t;

static void register_settings_tests(const char *persisted)
{
    runtime.data_recept_mode = APP_DATA_INVERTER;
    stored_v1_t previous = {.version=1, .driver={.profile="huawei_modbus", .host="site.local",
        .unit=7, .port=1502, .battery=true}, .latitude=47.25, .longitude=18.5,
        .master={.touch_wake_seconds=59}, .pem="old certificate"};
    previous.peers[0].schedule.touch_wake_seconds = 23;
    strcpy(previous.peers[0].id, "ble-0-112233445566");
    nvs_size = offsetof(stored_v1_t, pem) + strlen(previous.pem) + 1;
    memcpy(nvs_data, &previous, nvs_size);
    assert(app_inverter_settings_open());
    app_inverter_settings_t snapshot;
    uint32_t revision, source, last_source;
    assert(app_inverter_settings_snapshot(&snapshot, &revision, &source));
    assert(snapshot.version == 2 && !snapshot.driver.registers.mask && !commits);
    assert(snapshot.driver.unit == 7 && snapshot.driver.port == 1502 && snapshot.driver.battery);
    assert(!strcmp(snapshot.driver.trusted_pem, previous.pem) && snapshot.latitude == 47.25);
    assert(snapshot.master.touch_wake_seconds == 59 && snapshot.peers[0].schedule.touch_wake_seconds == 23);

    cJSON *form = settings("huawei_modbus");
    cJSON_AddItemToObject(form, "registerOverrides", cJSON_Parse("{\"pv\":0,\"ac\":32080}"));
    assert(app_inverter_settings_save(form) == ESP_OK);
    assert(app_inverter_settings_snapshot(&snapshot, &revision, &source));
    assert(snapshot.driver.registers.mask == 1 && snapshot.driver.registers.address[0] == 0);
    FILE *image = fopen(persisted, "wb");
    assert(image && fwrite(nvs_data, 1, nvs_size, image) == nvs_size);
    assert(!fclose(image));
    cJSON *get = app_inverter_settings_get();
    cJSON *values = cJSON_GetObjectItem(get, "registerOverrides");
    assert(cJSON_GetArraySize(values) == 1 && cJSON_GetObjectItem(values, "pv")->valueint == 0);
    cJSON_Delete(get);
    cJSON_DeleteItemFromObject(form, "registerOverrides");
    assert(app_inverter_settings_save(form) == ESP_OK);
    assert(app_inverter_settings_snapshot(&snapshot, &revision, &last_source));
    assert(last_source == source && snapshot.driver.registers.mask == 1);
    const char *invalid[] = {"null", "[]", "{\"unknown\":1}", "{\"pv\":1.5}",
                             "{\"pv\":-1}", "{\"pv\":65535}", "{\"pv\":0,\"pv\":1}"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
        cJSON_AddItemToObject(form, "registerOverrides", cJSON_Parse(invalid[i]));
        assert(app_inverter_settings_save(form) == ESP_ERR_INVALID_ARG);
        cJSON_DeleteItemFromObject(form, "registerOverrides");
    }
    cJSON_AddItemToObject(form, "registerOverrides", cJSON_CreateObject());
    assert(app_inverter_settings_save(form) == ESP_OK);
    assert(app_inverter_settings_snapshot(&snapshot, &revision, &last_source));
    assert(last_source != source && !snapshot.driver.registers.mask);
    cJSON_ReplaceItemInObject(form, "profile", cJSON_CreateString("fronius_http"));
    cJSON_AddNumberToObject(cJSON_GetObjectItem(form, "registerOverrides"), "pv", 123);
    assert(app_inverter_settings_save(form) == ESP_ERR_INVALID_ARG);
    cJSON_Delete(form);
    puts("PASS legacy NVS migration, per-field register editing, sparse defaults, exact zero and reset");
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "registers")) {
        register_settings_tests(argv[2]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "restart")) {
        FILE *image = fopen(argv[2], "rb");
        assert(image);
        nvs_size = fread(nvs_data, 1, sizeof(nvs_data), image);
        assert(nvs_size && !ferror(image) && !fclose(image));
        runtime.data_recept_mode = APP_DATA_INVERTER;
        cJSON *view = app_inverter_settings_get();
        cJSON *values = cJSON_GetObjectItem(view, "registerOverrides");
        assert(cJSON_GetArraySize(values) == 1 && cJSON_IsNumber(cJSON_GetObjectItem(values, "pv")) &&
               cJSON_GetObjectItem(values, "pv")->valueint == 0 && !commits);
        cJSON_Delete(view);
        return 0;
    }
    app_inverter_schedule_t schedule;
    runtime.data_recept_mode=APP_DATA_LOXONE;
    assert(!app_inverter_settings_local_name("unit.local", 10));
    assert(!app_inverter_settings_get() && !app_inverter_display_schedule(NULL,&schedule) && !allocations);
    runtime.data_recept_mode=APP_DATA_INVERTER; runtime.time_synced=runtime.wifi_connected=true;
    tick(0); assert(!configured && !reads && !weather);
    cJSON *s=settings("solax_http");
    assert(app_inverter_settings_save(s)==ESP_ERR_INVALID_ARG && commits==0);
    cJSON_Delete(s);
    s=settings("fronius_http");
    cJSON_ReplaceItemInObject(s,"host",cJSON_CreateString("-unit.local"));
    assert(app_inverter_settings_save(s) == ESP_ERR_INVALID_ARG);
    cJSON_ReplaceItemInObject(s,"host",cJSON_CreateString("Unit.LOCAL."));
    assert(app_inverter_settings_save(s)==ESP_OK && commits==1);
    assert(app_inverter_display_schedule(NULL,&schedule) && schedule.touch_wake_seconds==30);
    assert(app_inverter_settings_local_name("UNIT.LOCAL", 10));
    assert(!app_inverter_settings_local_name("other.local", 11));
    tick(0); assert(configured && reads==1 && samples==1 && weather==1 && breaks==1);
    tick(4); assert(reads==1 && weather==1);
    tick(1); assert(reads==2 && samples==2 && last.temperature_valid && last.temperature_c==0 && last.battery_unavailable);
    fail_save=true;
    assert(app_inverter_settings_save(s)==ESP_FAIL); fail_save=false;
    cJSON_Delete(s); s=settings("solax_cloud");
    cJSON_AddStringToObject(s,"secret","TEST-FIXTURE");
    cJSON_AddStringToObject(s,"device","123456");
    assert(app_inverter_settings_save(s)==ESP_OK); source_time=clock_now;
    assert(!app_inverter_settings_local_name("unit.local", 10));
    tick(0); assert(reads==3 && samples==3 && breaks==2 && last.integration_period_s==300);
    cJSON_DeleteItemFromObject(s,"secret"); assert(app_inverter_settings_save(s)==ESP_OK);
    tick(1); assert(reads==3);
    tick(299); assert(reads==4 && samples==3 && last.received_epoch_s==source_time);
    cJSON *view=app_inverter_settings_get(); char *text=cJSON_PrintUnformatted(view);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(view,"hasSecret")) && !strstr(text,"TEST-FIXTURE") &&
           cJSON_GetObjectItem(view,"measurementAgeSeconds")->valueint==300);
    free(text); cJSON_Delete(view);
    cJSON_ReplaceItemInObject(s,"host",cJSON_CreateString("other.local"));
    assert(app_inverter_settings_save(s)==ESP_ERR_INVALID_ARG);
    tick(595); assert(weather==2);
    cJSON_Delete(s);
    s=settings("fronius_http");
    cJSON_ReplaceItemInObject(s,"battery",cJSON_CreateTrue());
    assert(app_inverter_settings_save(s)==ESP_OK);
    measurement = (app_energy_sample_t){.status=1, .power_pv_kw=APP_DIRECT_POWER_MAX_KW,
        .power_full_kw=NAN, .power_grid_kw=-80, .power_bat_kw=45, .soc_bat_pct=102,
        .battery_temperature_valid=true, .battery_temperature_c=81};
    source_time=0;
    tick(5);
    assert(last.power_pv_kw==APP_DIRECT_POWER_MAX_KW && last.power_grid_kw==-80 && last.power_bat_kw==45);
    assert(!last.battery_temperature_valid && last.home_unavailable && last.soc_unavailable);
    measurement.battery_temperature_c=0;
    measurement.power_bat_kw=500;
    tick(5);
    assert(last.battery_temperature_valid && last.battery_temperature_c==0 && last.battery_power_unavailable);
    int before=samples;
    measurement.power_pv_kw=APP_DIRECT_POWER_MAX_KW+.01;
    tick(5);
    assert(samples==before);
    measurement.power_pv_kw=20;
    static const double temperatures[]={-35,50,-35.1,50.1};
    static const double winds[]={0,150,150.1,-.1};
    for (unsigned i=0; i<4; ++i) {
        weather_temp=temperatures[i]; weather_wind=winds[i];
        tick(900); tick(5);
        assert(last.power_pv_kw==20 && last.relative_humidity_valid);
        assert(last.temperature_valid==(i<2) && last.wind_speed_valid==(i<2));
        if (i<2) assert(last.temperature_c==weather_temp && last.wind_speed_kmh==weather_wind);
    }
    cJSON_Delete(s);
    puts("PASS inverter mode isolation, NVS atomic save, weather and cloud timing, stale sample dedup");
}
