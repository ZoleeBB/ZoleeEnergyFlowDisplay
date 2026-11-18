/* Az inverter minden hálózati művelete a már létező main feladaton fut.
 * A mintavétel és a környezeti adatok frissülése egymástól független. */
#include "app_inverter.h"
#include "app_inverter_identity.h"
#include "app_inverter_settings.h"
#include "app_inverter_weather.h"
#include "app_ota.h"
#include "app_stats.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

typedef struct {
    app_inverter_settings_t config;
    app_inverter_identity_t identity;
    uint32_t revision, source;
    int64_t due, latest_epoch, overlay_due;
    const app_inverter_profile_t *profile;
} worker_t;

static worker_t *worker;
static portMUX_TYPE sun_lock = portMUX_INITIALIZER_UNLOCKED;
static _Atomic uint32_t source_timeout = 30;
typedef struct {
    int64_t minute;
    uint32_t revision;
    app_solar_position_t sun;
} solar_cache_t;
/* Egyetlen mutató kerül az általános módok BSS-ébe. */
static solar_cache_t *solar_cache;

bool app_inverter_sun_now(app_energy_sample_t *s)
{
    double lat, lon;
    uint32_t revision;
    if (!s || !app_inverter_settings_coords(&lat,&lon,&revision)) return false;
    time_t now = time(NULL);
    app_solar_position_t sun;
    portENTER_CRITICAL(&sun_lock);
    bool cached = solar_cache && solar_cache->revision == revision && solar_cache->minute == now/60;
    if (cached) sun = solar_cache->sun;
    portEXIT_CRITICAL(&sun_lock);
    if (!cached) {
        if (!app_inverter_solar(now,lat,lon,&sun)) return false;
        portENTER_CRITICAL(&sun_lock);
        if (solar_cache) *solar_cache = (solar_cache_t){.minute=now/60,.revision=revision,.sun=sun};
        portEXIT_CRITICAL(&sun_lock);
    }
    s->sun_azimuth_deg = sun.azimuth;
    s->sun_altitude_deg = sun.altitude;
    s->sunrise_minute = sun.rise_minute;
    s->sunset_minute = sun.set_minute;
    s->sun_position_valid = true;
    s->night_mode = sun.night;
    return true;
}

uint32_t app_inverter_sample_timeout_s(void) { return atomic_load(&source_timeout); }

static void reload(worker_t *w)
{
    uint32_t rev, source;
    if (!app_inverter_settings_snapshot(&w->config,&rev,&source)) return;
    const app_inverter_profile_t *profile = app_inverter_profile(w->config.driver.profile);
    bool changed = w->source != source;
    app_inverter_http_reset();
    app_inverter_victron_close();
    app_inverter_identity_load(&w->identity,&w->config.driver,profile->seconds == 5);
    if (changed) {
        app_stats_break_continuity();
        app_state_clear_sample();
        w->latest_epoch = 0;
    }
    /* Ugyanannál a felhőforrásnál az ismételt mentés sem lépi át a kvótát. */
    if (changed || profile->seconds == 5) w->due = 0;
    w->revision = rev;
    w->source = source;
    w->profile = profile;
    uint16_t timeout = profile->seconds == 5 ? 30 : app_cloud_sample_timeout(profile->seconds);
    atomic_store(&source_timeout,timeout);
    app_state_set_inverter_timeout(timeout);
    app_state_set_inverter_ready(true);
}

static void overlay(app_energy_sample_t *s)
{
    app_inverter_sun_now(s);
    app_inverter_weather_apply(s);
}

static void measure(worker_t *w, int64_t now)
{
    app_energy_sample_t s = {0};
    int64_t epoch = 0;
    w->due = now + (int64_t)w->profile->seconds * 1000000;
    app_inverter_config_t *cfg = &w->config.driver;
    char address[16];
    const char *problem = app_inverter_identity_prepare(&w->identity,address);
    if (problem) {
        if (w->profile->transport == INV_MQTT) app_inverter_victron_close();
        app_inverter_report(w->revision,problem,0);
        return;
    }
    if (address[0] && strcmp(cfg->host,address)) {
        if (w->profile->transport == INV_MQTT) app_inverter_victron_close();
        strcpy(cfg->host,address);
    }
    bool ok;
    switch (w->profile->transport) {
        case INV_MODBUS: ok = app_inverter_modbus_read(cfg,&s,&epoch); break;
        case INV_HTTP: ok = app_inverter_http_read(cfg,&s,&epoch); break;
        case INV_MQTT: ok = app_inverter_victron_read(cfg,&s); break;
        default: return;
    }
    if (!app_inverter_settings_current(w->revision)) return;
    if (!ok || !isfinite(s.power_pv_kw) || s.power_pv_kw < 0 || s.power_pv_kw > APP_DIRECT_POWER_MAX_KW) {
        app_inverter_report(w->revision,"Az inverter nem adott érvényes választ",0);
        return;
    }
    time_t wall = time(NULL);
    int64_t age_us = esp_timer_get_time();
    bool cloud = w->profile->seconds > 5;
    if (!epoch && !cloud) epoch = wall;
    if (epoch <= 0 || epoch > wall+60 || wall-epoch >= app_inverter_sample_timeout_s()) {
        app_inverter_report(w->revision,"Elavult vagy hibás mérési idő",epoch > 0 && epoch <= wall ? epoch : 0);
        return;
    }
    if (epoch <= w->latest_epoch) {
        app_inverter_report(w->revision,"Újabb mérésre vár",0);
        return;
    }
    s.received_epoch_s = epoch;
    s.received_monotonic_us = age_us - (wall > epoch ? wall-epoch : 0)*INT64_C(1000000);
    s.integration_period_s = cloud ? w->profile->seconds : 0;
    s.battery_unavailable |= !cfg->battery;
    s.grid_unavailable |= !isfinite(s.power_grid_kw) || fabs(s.power_grid_kw) > APP_DIRECT_POWER_MAX_KW;
    s.home_unavailable |= !(s.power_full_kw >= 0 && s.power_full_kw <= APP_DIRECT_POWER_MAX_KW);
    s.battery_power_unavailable |= s.battery_unavailable || !isfinite(s.power_bat_kw) ||
                                   fabs(s.power_bat_kw) > APP_DIRECT_POWER_MAX_KW;
    s.soc_unavailable |= s.battery_unavailable || !(s.soc_bat_pct >= 0 && s.soc_bat_pct <= 100);
    s.battery_temperature_valid &= !s.battery_unavailable && app_battery_temperature_in_range(s.battery_temperature_c);
    /* Régi megjelenítők is véges számmal számoljanak. Az érvényességet kizárólag a jelző dönti el. */
    if (s.grid_unavailable) s.power_grid_kw = 0;
    if (s.home_unavailable) s.power_full_kw = 0;
    if (s.battery_power_unavailable) s.power_bat_kw = 0;
    if (s.soc_unavailable) s.soc_bat_pct = 0;
    if (!s.battery_temperature_valid) s.battery_temperature_c = 0;
    problem = app_inverter_identity_verify(&w->identity);
    if (problem) {
        if (w->profile->transport == INV_MQTT) app_inverter_victron_close();
        app_inverter_report(w->revision,problem,0);
        return;
    }
    overlay(&s);
    if (app_inverter_settings_lock_version(w->revision)) {
        problem = app_inverter_identity_commit(&w->identity);
        if (problem) {
            app_inverter_settings_unlock();
            app_inverter_report(w->revision,problem,0);
            return;
        }
        app_inverter_report_ipv4_locked(w->identity.active);
        app_stats_process_sample(&s);
        app_state_set_sample(&s);
        w->latest_epoch = epoch;
        app_inverter_settings_unlock();
    }
    app_inverter_report(w->revision,"Szinkronizálva",epoch);
}

void app_inverter_tick(void)
{
    if (!worker) {
        if (!app_inverter_settings_open() || !app_inverter_settings_ready()) return;
        worker = heap_caps_calloc(1,sizeof(*worker),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!worker) return;
        solar_cache_t *cache = heap_caps_calloc(1,sizeof(*cache),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        portENTER_CRITICAL(&sun_lock);
        solar_cache = cache;
        portEXIT_CRITICAL(&sun_lock);
    }
    if (!app_inverter_settings_current(worker->revision)) reload(worker);
    if (!worker->profile || app_ota_operation_active()) return;
    app_runtime_state_t state;
    app_state_get_runtime(&state);
    if (!state.time_synced) return;
    int64_t now = esp_timer_get_time();
    if (state.wifi_connected && now >= worker->due) measure(worker,now);
    if (state.wifi_connected) app_inverter_weather_start();
    if (now >= worker->overlay_due) {
        worker->overlay_due = now + INT64_C(5000000);
        app_energy_sample_t s;
        if (app_state_get_sample(&s)) {
            overlay(&s);
            if (app_inverter_settings_lock_version(worker->revision)) {
                app_state_set_sample(&s);
                app_inverter_settings_unlock();
            }
        }
    }
}
