/* A tizenöt perces HTTPS-kérést a már létező OTA-worker végzi. Ez alatt a
 * helyi Modbus/MQTT energiaadatok saját öt másodperces ciklusa tovább fut. */
#include "app_inverter_weather.h"
#include "app_inverter_settings.h"
#include "app_inverter_net.h"
#include "app_ota.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    int64_t measured;
    double lat, lon, temp, humidity, wind, rain, sunshine;
    bool valid_temp, valid_humidity, valid_wind, valid_rain, valid_sun;
} weather_t;
static portMUX_TYPE weather_lock = portMUX_INITIALIZER_UNLOCKED;
/* A lekérdezés állapotát csak az inverteres Master foglalja. */
static weather_t *latest;
static bool pending;
static int64_t next_request;
static double requested_lat, requested_lon;

static bool field(const cJSON *o, const char *name, double low, double high, double *value)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o,name);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < low || v->valuedouble > high) return false;
    *value = v->valuedouble;
    return true;
}

static void weather_fetch(void *unused)
{
    (void)unused;
    double lat, lon;
    uint32_t generation;
    weather_t w = {0};
    if (!app_ota_operation_active() && app_inverter_settings_coords(&lat,&lon,&generation)) {
        char url[384];
        snprintf(url,sizeof(url),"https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
                 "&timeformat=unixtime&current=temperature_2m,relative_humidity_2m,wind_speed_10m,"
                 "precipitation,direct_normal_irradiance&wind_speed_unit=kmh",lat,lon);
        cJSON *json = app_inverter_json_request(url,"GET",NULL,NULL,0,NULL,0,NULL);
        const cJSON *data = cJSON_GetObjectItemCaseSensitive(json,"current");
        double stamp;
        time_t now = time(NULL);
        if (field(data,"time",now-2700,now+60,&stamp)) {
            w.measured = (int64_t)stamp;
            w.lat = lat;
            w.lon = lon;
            w.valid_temp = field(data,"temperature_2m",-35,50,&w.temp);
            w.valid_humidity = field(data,"relative_humidity_2m",0,100,&w.humidity);
            w.valid_wind = field(data,"wind_speed_10m",0,150,&w.wind);
            w.valid_rain = field(data,"precipitation",0,300,&w.rain);
            w.valid_sun = field(data,"direct_normal_irradiance",0,1600,&w.sunshine);
        }
        cJSON_Delete(json);
    }
    portENTER_CRITICAL(&weather_lock);
    if (w.measured && latest) *latest = w;
    pending = false;
    portEXIT_CRITICAL(&weather_lock);
}

void app_inverter_weather_start(void)
{
    double lat, lon;
    uint32_t gen;
    int64_t now = esp_timer_get_time();
    if (!app_inverter_settings_coords(&lat,&lon,&gen)) return;
    if (!latest) {
        weather_t *allocated = heap_caps_calloc(1,sizeof(*latest),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!allocated) return;
        portENTER_CRITICAL(&weather_lock);
        latest = allocated;
        portEXIT_CRITICAL(&weather_lock);
    }
    portENTER_CRITICAL(&weather_lock);
    bool start = !pending && (now >= next_request || lat != requested_lat || lon != requested_lon);
    if (start) pending = true;
    portEXIT_CRITICAL(&weather_lock);
    if (!start) return;
    if (app_ota_background_post(weather_fetch,NULL)) {
        requested_lat = lat;
        requested_lon = lon;
        next_request = now + INT64_C(900000000);
    } else {
        portENTER_CRITICAL(&weather_lock);
        pending = false;
        portEXIT_CRITICAL(&weather_lock);
    }
}

void app_inverter_weather_apply(app_energy_sample_t *s)
{
    double lat, lon;
    uint32_t gen;
    if (!app_inverter_settings_coords(&lat,&lon,&gen)) return;
    weather_t w = {0};
    portENTER_CRITICAL(&weather_lock);
    if (latest) w = *latest;
    portEXIT_CRITICAL(&weather_lock);
    time_t now = time(NULL);
    bool fresh = w.measured && w.lat == lat && w.lon == lon && w.measured <= now+60 && now-w.measured < 2700;
    s->temperature_valid = fresh && w.valid_temp;
    s->temperature_c = w.temp;
    s->wind_speed_valid = fresh && w.valid_wind;
    s->wind_speed_kmh = w.wind;
    s->relative_humidity_valid = fresh && w.valid_humidity;
    s->relative_humidity_pct = (uint8_t)lround(w.humidity);
    s->precipitation_valid = fresh && w.valid_rain;
    s->has_precipitation = w.rain > 0;
    s->sun_radiation_valid = fresh && w.valid_sun;
    s->has_sun_radiation = w.sunshine >= 120;
}
