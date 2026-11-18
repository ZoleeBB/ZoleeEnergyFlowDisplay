/* Valódi profil, ütemező és időjáráskezelő, ellenőrzött órával és válaszokkal. */
#include "app_inverter.h"
#include "app_inverter_settings.h"
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
    assert(cfg->host[0]); reads++; s->power_pv_kw=2.5; s->status=1; *stamp=source_time; return true;
}
bool app_ota_operation_active(void) { return false; }
bool app_ota_background_post(void (*f)(void *),void *arg) { assert(!arg && !meteo_job); meteo_job=f; return true; }
cJSON *app_inverter_json_request(const char *url,const char *method,const char *b,
                                 const char *const h[][2],size_t n,char *token,size_t cap,const char *pem) {
    assert(strstr(url,"https://api.open-meteo.com/") && !strcmp(method,"GET") && !n && !h && !b && !token && !cap && !pem);
    weather++;
    char json[240]; snprintf(json,sizeof(json),"{\"current\":{\"time\":%ld,\"temperature_2m\":0,"
      "\"wind_speed_10m\":0,\"relative_humidity_2m\":50,\"precipitation\":0,\"direct_normal_irradiance\":200}}",(long)clock_now);
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

int main(void)
{
    app_inverter_schedule_t schedule;
    runtime.data_recept_mode=APP_DATA_LOXONE;
    assert(!app_inverter_settings_get() && !app_inverter_display_schedule(NULL,&schedule) && !allocations);
    runtime.data_recept_mode=APP_DATA_INVERTER; runtime.time_synced=runtime.wifi_connected=true;
    tick(0); assert(!configured && !reads && !weather);
    cJSON *s=settings("fronius_http");
    assert(app_inverter_settings_save(s)==ESP_OK && commits==1);
    assert(app_inverter_display_schedule(NULL,&schedule) && schedule.touch_wake_seconds==30);
    tick(0); assert(configured && reads==1 && samples==1 && weather==1 && breaks==1);
    tick(4); assert(reads==1 && weather==1);
    tick(1); assert(reads==2 && samples==2 && last.temperature_valid && last.temperature_c==0 && last.battery_unavailable);
    fail_save=true;
    assert(app_inverter_settings_save(s)==ESP_FAIL); fail_save=false;
    cJSON_Delete(s); s=settings("solax_cloud");
    cJSON_AddStringToObject(s,"secret","TEST-FIXTURE");
    cJSON_AddStringToObject(s,"device","123456");
    assert(app_inverter_settings_save(s)==ESP_OK); source_time=clock_now;
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
    puts("PASS inverter mode isolation, NVS atomic save, weather and cloud timing, stale sample dedup");
}
