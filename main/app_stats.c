/*
 * Napi energia- és teljesítménystatisztika.
 *
 * A modul az érvényes MQTT mintákból trapézszabállyal számolja a napi
 * energiamennyiségeket, 5 perces átlagpontokat készít a grafikonokhoz,
 * 15 percenként biztonságosan ment microSD-re, és a tegnapi azonos időszakot
 * használja összehasonlítási alapként.
 */
#include "app_stats.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_events.h"
#include "app_storage.h"

#define STATS_DIR APP_SD_MOUNT_POINT "/stats"
#define STATS_FILE_MAX (512 * 1024)
#define STATS_VERSION 3

static const char *TAG = "stats";

typedef struct {
    char date[11];
    app_energy_totals_t totals;
    app_stats_slot_t slots[APP_STATS_SLOT_COUNT];
    bool valid;
    uint32_t revision;
    uint32_t saved_revision;
} day_data_t;

static SemaphoreHandle_t s_mutex;
/* Az I/O külön zárat kap; a mérési adat mutexét csak rövid másolás tartja. */
static SemaphoreHandle_t s_save_mutex;
static app_config_t s_cfg;
static day_data_t s_today;
static day_data_t s_yesterday;
static app_stats_day_t s_history[APP_STATS_HISTORY_DAYS];
static bool s_have_prev;
static bool s_task_started;
static app_energy_sample_t s_prev;
static int64_t s_last_save_us;

/* Helyi dátum előállítása YYYY-MM-DD formában. */
static void date_for(time_t t, char out[11])
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, 11, "%Y-%m-%d", &tmv);
}

/* Helyi időpont 0..287 közötti ötperces slot-indexre alakítása. */
static int slot_for(time_t t)
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    return tmv.tm_hour * 12 + tmv.tm_min / 5;
}

/* Az adott helyi nap pontos 00:00 időpontja, DST-t is figyelembe véve. */
static time_t local_day_start(time_t t)
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    tmv.tm_isdst = -1;
    return mktime(&tmv);
}

/* A tegnapi helyi dátum kiszámítása naptári művelettel. */
static void yesterday_for(time_t now, char out[11])
{
    struct tm tmv;
    localtime_r(&now, &tmv);
    tmv.tm_mday--;
    tmv.tm_isdst = -1;
    time_t previous = mktime(&tmv);
    date_for(previous, out);
}

/* Energiaösszesítők mezőnkénti összeadása. */
static void totals_add(app_energy_totals_t *dst, const app_energy_totals_t *src)
{
    dst->production_kwh += src->production_kwh;
    dst->consumption_kwh += src->consumption_kwh;
    dst->grid_import_kwh += src->grid_import_kwh;
    dst->grid_export_kwh += src->grid_export_kwh;
    dst->battery_charge_kwh += src->battery_charge_kwh;
    dst->battery_discharge_kwh += src->battery_discharge_kwh;
#define ADD(field) dst->field += src->field
    ADD(pv_home_kwh); ADD(pv_export_kwh); ADD(pv_stored_kwh); ADD(grid_home_kwh); ADD(battery_home_kwh);
#undef ADD
}

/* Két teljesítményminta közötti energia számítása trapézszabállyal. */
static app_energy_totals_t energy_between(
    const app_energy_sample_t *a,
    const app_energy_sample_t *b,
    double hours)
{
    app_energy_totals_t result = {0};

    double pv = (a->power_pv_kw + b->power_pv_kw) * 0.5;
    double load = (a->power_full_kw + b->power_full_kw) * 0.5;
    double grid = (a->power_grid_kw + b->power_grid_kw) * 0.5;
    double battery = (a->power_bat_kw + b->power_bat_kw) * 0.5;

    result.production_kwh = pv * hours;
    result.consumption_kwh = load * hours;

    /*
     * A hálózat és az akkumulátor előjeles teljesítménye két minta között
     * lineárisan változik. Ha az előjel közben megfordul, a nullátmetszést
     * külön kezeljük, különben az import/export vagy töltés/kisütés részben
     * kioltaná egymást.
     */
    if (a->power_grid_kw >= 0 && b->power_grid_kw >= 0) {
        result.grid_import_kwh = grid * hours;
    } else if (a->power_grid_kw <= 0 && b->power_grid_kw <= 0) {
        result.grid_export_kwh = -grid * hours;
    } else {
        double fraction =
            a->power_grid_kw /
            (a->power_grid_kw - b->power_grid_kw);

        double first_hours = hours * fraction;
        double second_hours = hours - first_hours;

        if (a->power_grid_kw > 0) {
            result.grid_import_kwh =
                0.5 * a->power_grid_kw * first_hours;
            result.grid_export_kwh =
                0.5 * (-b->power_grid_kw) * second_hours;
        } else {
            result.grid_export_kwh =
                0.5 * (-a->power_grid_kw) * first_hours;
            result.grid_import_kwh =
                0.5 * b->power_grid_kw * second_hours;
        }
    }

    if (a->power_bat_kw >= 0 && b->power_bat_kw >= 0) {
        result.battery_discharge_kwh = battery * hours;
    } else if (a->power_bat_kw <= 0 && b->power_bat_kw <= 0) {
        result.battery_charge_kwh = -battery * hours;
    } else {
        double fraction =
            a->power_bat_kw /
            (a->power_bat_kw - b->power_bat_kw);

        double first_hours = hours * fraction;
        double second_hours = hours - first_hours;

        if (a->power_bat_kw > 0) {
            result.battery_discharge_kwh =
                0.5 * a->power_bat_kw * first_hours;
            result.battery_charge_kwh =
                0.5 * (-b->power_bat_kw) * second_hours;
        } else {
            result.battery_charge_kwh =
                0.5 * (-a->power_bat_kw) * first_hours;
            result.battery_discharge_kwh =
                0.5 * b->power_bat_kw * second_hours;
        }
    }

    /* A min/max felosztás töréspontjainál vágunk, így az energiaarányok
     * előjelváltás és PV/ház keresztezés esetén sem mintaszám-függők. */
    double cuts[5]={0,1}; int n=2;
    double av[]={a->power_pv_kw-a->power_full_kw,a->power_bat_kw,
                 a->power_pv_kw-a->power_full_kw+a->power_bat_kw};
    double bv[]={b->power_pv_kw-b->power_full_kw,b->power_bat_kw,
                 b->power_pv_kw-b->power_full_kw+b->power_bat_kw};
    for(int i=0;i<3;i++)if(av[i]*bv[i]<0)cuts[n++]=av[i]/(av[i]-bv[i]);
    for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(cuts[j]<cuts[i]){double t=cuts[i];cuts[i]=cuts[j];cuts[j]=t;}
    for(int i=1;i<n;i++){
        double x=cuts[i-1],y=cuts[i];
        app_energy_split_t u=app_energy_split(a->power_pv_kw+(b->power_pv_kw-a->power_pv_kw)*x,
            a->power_full_kw+(b->power_full_kw-a->power_full_kw)*x,a->power_bat_kw+(b->power_bat_kw-a->power_bat_kw)*x);
        app_energy_split_t v=app_energy_split(a->power_pv_kw+(b->power_pv_kw-a->power_pv_kw)*y,
            a->power_full_kw+(b->power_full_kw-a->power_full_kw)*y,a->power_bat_kw+(b->power_bat_kw-a->power_bat_kw)*y);
#define INTEGRATE(field) result.field##_kwh+=(u.field+v.field)*.5*hours*(y-x)
        INTEGRATE(pv_home); INTEGRATE(pv_export); INTEGRATE(pv_stored); INTEGRATE(grid_home); INTEGRATE(battery_home);
#undef INTEGRATE
    }
    return result;
}

/* Lineáris köztes teljesítményminta képzése slot- vagy napváltási határhoz. */
static app_energy_sample_t interpolate_sample(
    const app_energy_sample_t *a,
    const app_energy_sample_t *b,
    double fraction,
    int64_t epoch_s)
{
    app_energy_sample_t x = *a;
    x.power_grid_kw = a->power_grid_kw + (b->power_grid_kw - a->power_grid_kw) * fraction;
    x.power_bat_kw = a->power_bat_kw + (b->power_bat_kw - a->power_bat_kw) * fraction;
    x.power_pv_kw = a->power_pv_kw + (b->power_pv_kw - a->power_pv_kw) * fraction;
    x.power_full_kw = a->power_full_kw + (b->power_full_kw - a->power_full_kw) * fraction;
    x.received_epoch_s = epoch_s;
    return x;
}

/* Futó átlag frissítése az aktuális ötperces grafikon-slotban. */
static void slot_average_add(app_stats_slot_t *slot, const app_energy_sample_t *sample)
{
    slot->sample_count++;
    float n = (float)slot->sample_count;

    slot->pv_kw += ((float)sample->power_pv_kw - slot->pv_kw) / n;
    slot->load_kw += ((float)sample->power_full_kw - slot->load_kw) / n;
    slot->grid_kw += ((float)sample->power_grid_kw - slot->grid_kw) / n;
    slot->battery_kw += ((float)sample->power_bat_kw - slot->battery_kw) / n;
    app_energy_split_average(&slot->split,app_energy_split(sample->power_pv_kw,
        sample->power_full_kw,sample->power_bat_kw),n);
    slot->soc_sample_count++;
    slot->soc_pct += ((float)sample->soc_bat_pct-slot->soc_pct)/(float)slot->soc_sample_count;
    slot->soc_valid = true;
    slot->valid = true;
}

/* Az intervallum bal végének helyi dátuma dönti el a mai/tegnapi célstruktúrát. */
static day_data_t *day_for(time_t epoch)
{
    char date[11];
    date_for(epoch,date);
    if(s_today.valid && strcmp(date,s_today.date)==0)return &s_today;
    if(s_yesterday.valid && strcmp(date,s_yesterday.date)==0)return &s_yesterday;
    return NULL;
}

/* Ötperces és éjféli határokon is pontosan vágunk; a háttértask sorrendjétől függetlenül. */
static void integrate_pair(const app_energy_sample_t *previous,const app_energy_sample_t *current)
{
    int64_t duration=current->received_epoch_s-previous->received_epoch_s;
    if(duration<=0 || duration>s_cfg.statistics.max_integration_gap_seconds)return;
    int64_t cursor=previous->received_epoch_s;
    while(cursor<current->received_epoch_s){
        struct tm local;
        time_t epoch=(time_t)cursor;
        localtime_r(&epoch,&local);
        int64_t next=cursor+300-(local.tm_min%5)*60-local.tm_sec;
        if(next>current->received_epoch_s)next=current->received_epoch_s;
        app_energy_sample_t a=interpolate_sample(previous,current,
            (double)(cursor-previous->received_epoch_s)/duration,cursor);
        app_energy_sample_t b=interpolate_sample(previous,current,
            (double)(next-previous->received_epoch_s)/duration,next);
        day_data_t *day=day_for((time_t)cursor);
        if(day){
            app_energy_totals_t energy=energy_between(&a,&b,(next-cursor)/3600.);
            totals_add(&day->totals,&energy);
            totals_add(&day->slots[slot_for((time_t)cursor)].energy,&energy);
            day->revision++;
        }
        cursor=next;
    }
}

/* A napi fő statisztikai fájl elérési útjának összeállítása. */
static void path_for(const char *date, char *out, size_t out_len)
{
    snprintf(out, out_len, STATS_DIR "/%s.json", date);
}

/* Az atomi mentés ideiglenes fájlútvonalának összeállítása. */
static void temp_path_for(const char *date, char *out, size_t out_len)
{
    snprintf(out, out_len, STATS_DIR "/%s.json.tmp", date);
}

/* Az atomi mentés biztonsági másolatának fájlútvonala. */
static void backup_path_for(const char *date, char *out, size_t out_len)
{
    snprintf(out, out_len, STATS_DIR "/%s.json.bak", date);
}

/* Energiaösszesítő JSON objektummá alakítása. */
static cJSON *totals_to_json(const app_energy_totals_t *totals)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return NULL;
    }

    cJSON_AddNumberToObject(obj, "production_kwh", totals->production_kwh);
    cJSON_AddNumberToObject(obj, "consumption_kwh", totals->consumption_kwh);
    cJSON_AddNumberToObject(obj, "grid_import_kwh", totals->grid_import_kwh);
    cJSON_AddNumberToObject(obj, "grid_export_kwh", totals->grid_export_kwh);
    cJSON_AddNumberToObject(obj, "battery_charge_kwh", totals->battery_charge_kwh);
    cJSON_AddNumberToObject(obj, "battery_discharge_kwh", totals->battery_discharge_kwh);
#define SAVE_ENERGY(field) cJSON_AddNumberToObject(obj,#field,totals->field)
    SAVE_ENERGY(pv_home_kwh); SAVE_ENERGY(pv_export_kwh); SAVE_ENERGY(pv_stored_kwh);
    SAVE_ENERGY(grid_home_kwh); SAVE_ENERGY(battery_home_kwh);
#undef SAVE_ENERGY
    return obj;
}

/* Energiaösszesítő szigorú visszaolvasása JSON objektumból. */
static bool totals_from_json(cJSON *obj, app_energy_totals_t *totals)
{
    if (!cJSON_IsObject(obj)) {
        return false;
    }

    const char *names[] = {
        "production_kwh",
        "consumption_kwh",
        "grid_import_kwh",
        "grid_export_kwh",
        "battery_charge_kwh",
        "battery_discharge_kwh"
    };

    double *values[] = {
        &totals->production_kwh,
        &totals->consumption_kwh,
        &totals->grid_import_kwh,
        &totals->grid_export_kwh,
        &totals->battery_charge_kwh,
        &totals->battery_discharge_kwh
    };

    for (int i = 0; i < 6; i++) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, names[i]);
        if (!cJSON_IsNumber(value) ||
            !isfinite(value->valuedouble) ||
            value->valuedouble < 0) {
            return false;
        }
        *values[i] = value->valuedouble;
    }

#define LOAD_ENERGY(field) do { cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,#field); \
    if(v && (!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0))return false; \
    totals->field=v?v->valuedouble:0; } while(0)
    LOAD_ENERGY(pv_home_kwh); LOAD_ENERGY(pv_export_kwh); LOAD_ENERGY(pv_stored_kwh);
    LOAD_ENERGY(grid_home_kwh); LOAD_ENERGY(battery_home_kwh);
#undef LOAD_ENERGY
    return true;
}

/* Egy teljes napi állapot JSON reprezentációjának felépítése. */
static cJSON *day_to_json(const day_data_t *day)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "version", STATS_VERSION);
    cJSON_AddNumberToObject(root, "revision", day->revision);
    cJSON_AddStringToObject(root, "date", day->date);
    cJSON_AddItemToObject(root, "totals", totals_to_json(&day->totals));

    cJSON *slots = cJSON_AddArrayToObject(root, "slots");
    if (!slots) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < APP_STATS_SLOT_COUNT; i++) {
        const app_stats_slot_t *slot = &day->slots[i];
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(root);
            return NULL;
        }

        cJSON_AddBoolToObject(obj, "valid", slot->valid);
        cJSON_AddNumberToObject(obj, "count", slot->sample_count);
        cJSON_AddNumberToObject(obj, "pv", slot->pv_kw);
        cJSON_AddNumberToObject(obj, "load", slot->load_kw);
        cJSON_AddNumberToObject(obj, "grid", slot->grid_kw);
        cJSON_AddNumberToObject(obj, "battery", slot->battery_kw);
        cJSON_AddNumberToObject(obj,"soc",slot->soc_pct);
        cJSON_AddBoolToObject(obj,"soc_valid",slot->soc_valid);
        cJSON_AddNumberToObject(obj,"soc_count",slot->soc_sample_count);
        cJSON_AddItemToObject(obj, "energy", totals_to_json(&slot->energy));
        cJSON_AddBoolToObject(obj,"chart_gap",slot->chart_gap);
        cJSON *split=cJSON_AddObjectToObject(obj,"split");
#define SAVE_SPLIT(field) cJSON_AddNumberToObject(split,#field,slot->split.field)
        SAVE_SPLIT(pv_home); SAVE_SPLIT(pv_export); SAVE_SPLIT(pv_stored);
        SAVE_SPLIT(grid_home); SAVE_SPLIT(battery_home);
#undef SAVE_SPLIT
        cJSON_AddItemToArray(slots, obj);
    }

    return root;
}

/* Mentett napi JSON szerkezet, verzió, dátum és minden slot validálása. */
static bool day_from_json(cJSON *root, const char *expected_date, day_data_t *out)
{
    if (!cJSON_IsObject(root)) {
        return false;
    }

    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *date = cJSON_GetObjectItemCaseSensitive(root, "date");
    cJSON *revision = cJSON_GetObjectItemCaseSensitive(root, "revision");
    cJSON *totals = cJSON_GetObjectItemCaseSensitive(root, "totals");
    cJSON *slots = cJSON_GetObjectItemCaseSensitive(root, "slots");

    if (!cJSON_IsNumber(version) ||
        (version->valuedouble != 1 && version->valuedouble != 2 && version->valuedouble != STATS_VERSION) ||
        !cJSON_IsString(date) ||
        strcmp(date->valuestring, expected_date) != 0 ||
        !cJSON_IsArray(slots) ||
        cJSON_GetArraySize(slots) != APP_STATS_SLOT_COUNT) {
        return false;
    }

    /* A napi struktúra nagyobb a main task verménél. */
    day_data_t *parsed = calloc(1, sizeof(*parsed));
    if (!parsed) {errno=ENOMEM;return false;}
    snprintf(parsed->date, sizeof(parsed->date), "%s", expected_date);
    if(revision){
        if(!cJSON_IsNumber(revision) || !isfinite(revision->valuedouble) ||
           revision->valuedouble<0 || revision->valuedouble>UINT32_MAX ||
           floor(revision->valuedouble)!=revision->valuedouble)goto invalid;
        parsed->revision=(uint32_t)revision->valuedouble;
    }
    parsed->saved_revision=parsed->revision;

    if (!totals_from_json(totals, &parsed->totals)) {
        goto invalid;
    }

    for (int i = 0; i < APP_STATS_SLOT_COUNT; i++) {
        cJSON *obj = cJSON_GetArrayItem(slots, i);
        cJSON *valid = cJSON_GetObjectItemCaseSensitive(obj, "valid");
        cJSON *count = cJSON_GetObjectItemCaseSensitive(obj, "count");
        cJSON *pv = cJSON_GetObjectItemCaseSensitive(obj, "pv");
        cJSON *load = cJSON_GetObjectItemCaseSensitive(obj, "load");
        cJSON *grid = cJSON_GetObjectItemCaseSensitive(obj, "grid");
        cJSON *battery = cJSON_GetObjectItemCaseSensitive(obj, "battery");
        cJSON *energy = cJSON_GetObjectItemCaseSensitive(obj, "energy");

        if (!cJSON_IsBool(valid) ||
            !cJSON_IsNumber(count) ||
            !isfinite(count->valuedouble) || count->valuedouble < 0 ||
            count->valuedouble > UINT32_MAX || floor(count->valuedouble)!=count->valuedouble ||
            (cJSON_IsTrue(valid)!=(count->valuedouble>0)) ||
            !cJSON_IsNumber(pv) || !isfinite(pv->valuedouble) || pv->valuedouble<0 || pv->valuedouble>15 ||
            !cJSON_IsNumber(load) || !isfinite(load->valuedouble) || load->valuedouble<0 || load->valuedouble>15 ||
            !cJSON_IsNumber(grid) || !isfinite(grid->valuedouble) || fabs(grid->valuedouble)>15 ||
            !cJSON_IsNumber(battery) || !isfinite(battery->valuedouble) || fabs(battery->valuedouble)>15 ||
            !totals_from_json(energy, &parsed->slots[i].energy)) {
            goto invalid;
        }

        parsed->slots[i].valid = cJSON_IsTrue(valid);
        parsed->slots[i].sample_count = (uint32_t)count->valuedouble;
        parsed->slots[i].pv_kw = (float)pv->valuedouble;
        parsed->slots[i].load_kw = (float)load->valuedouble;
        parsed->slots[i].grid_kw = (float)grid->valuedouble;
        parsed->slots[i].battery_kw = (float)battery->valuedouble;
        app_stats_slot_t *slot=&parsed->slots[i];
        if(version->valuedouble>=3){
            cJSON *soc=cJSON_GetObjectItemCaseSensitive(obj,"soc");
            cJSON *soc_valid=cJSON_GetObjectItemCaseSensitive(obj,"soc_valid");
            if(!cJSON_IsNumber(soc)||!isfinite(soc->valuedouble)||soc->valuedouble<0||soc->valuedouble>100||!cJSON_IsBool(soc_valid))goto invalid;
            slot->soc_pct=soc->valuedouble;slot->soc_valid=cJSON_IsTrue(soc_valid);
            cJSON *soc_count=cJSON_GetObjectItemCaseSensitive(obj,"soc_count");
            if(soc_count){
                if(!cJSON_IsNumber(soc_count)||soc_count->valuedouble<0||soc_count->valuedouble>slot->sample_count||floor(soc_count->valuedouble)!=soc_count->valuedouble)goto invalid;
                slot->soc_sample_count=(uint32_t)soc_count->valuedouble;
            }else slot->soc_sample_count=slot->soc_valid?slot->sample_count:0;
            if(slot->soc_valid!=(slot->soc_sample_count>0))goto invalid;
        }
        if(version->valuedouble==1){
            /* Régi fájlokból csak az átlagok szerinti közelítés lehetséges. */
            slot->split=app_energy_split(slot->pv_kw,slot->load_kw,slot->battery_kw);
        }else{
            cJSON *gap=cJSON_GetObjectItemCaseSensitive(obj,"chart_gap");
            cJSON *split=cJSON_GetObjectItemCaseSensitive(obj,"split");
            if(!cJSON_IsBool(gap) || !cJSON_IsObject(split))goto invalid;
            slot->chart_gap=cJSON_IsTrue(gap);
#define LOAD_SPLIT(field) do { \
    cJSON *v=cJSON_GetObjectItemCaseSensitive(split,#field); \
    if(!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>15)goto invalid; \
    slot->split.field=(float)v->valuedouble; \
} while(0)
            LOAD_SPLIT(pv_home); LOAD_SPLIT(pv_export); LOAD_SPLIT(pv_stored);
            LOAD_SPLIT(grid_home); LOAD_SPLIT(battery_home);
#undef LOAD_SPLIT
            if(fabs(slot->split.pv_home+slot->split.pv_export+slot->split.pv_stored-slot->pv_kw)>.001 ||
               fabs(slot->split.pv_home+slot->split.grid_home+slot->split.battery_home-slot->load_kw)>.001)goto invalid;
        }
    }

    /* A teljes napi energia és a slotok összege nem mondhat ellent egymásnak. */
    app_energy_totals_t sum={0};
    for(int i=0;i<APP_STATS_SLOT_COUNT;i++)totals_add(&sum,&parsed->slots[i].energy);
#define MATCH_TOTAL(field) if(fabs(sum.field-parsed->totals.field)>1e-6)goto invalid
    MATCH_TOTAL(production_kwh); MATCH_TOTAL(consumption_kwh);
    MATCH_TOTAL(grid_import_kwh); MATCH_TOTAL(grid_export_kwh);
    MATCH_TOTAL(battery_charge_kwh); MATCH_TOTAL(battery_discharge_kwh);
    if(version->valuedouble>=3){
        MATCH_TOTAL(pv_home_kwh);MATCH_TOTAL(pv_export_kwh);MATCH_TOTAL(pv_stored_kwh);
        MATCH_TOTAL(grid_home_kwh);MATCH_TOTAL(battery_home_kwh);
        if(fabs(parsed->totals.pv_home_kwh+parsed->totals.pv_export_kwh+parsed->totals.pv_stored_kwh-parsed->totals.production_kwh)>1e-5)goto invalid;
        if(fabs(parsed->totals.pv_home_kwh+parsed->totals.grid_home_kwh+parsed->totals.battery_home_kwh-parsed->totals.consumption_kwh)>1e-5)goto invalid;
    }
#undef MATCH_TOTAL
    if(version->valuedouble<3){
        /* Régi napokra az átlagolt forrásarányok energiaarányos becslése. */
        for(int i=0;i<APP_STATS_SLOT_COUNT;i++){
            app_stats_slot_t *slot=&parsed->slots[i];
            double pv=slot->pv_kw,load=slot->load_kw;
            slot->energy.pv_home_kwh=pv>0?slot->energy.production_kwh*slot->split.pv_home/pv:0;
            slot->energy.pv_export_kwh=pv>0?slot->energy.production_kwh*slot->split.pv_export/pv:0;
            slot->energy.pv_stored_kwh=pv>0?slot->energy.production_kwh*slot->split.pv_stored/pv:0;
            slot->energy.grid_home_kwh=load>0?slot->energy.consumption_kwh*slot->split.grid_home/load:0;
            slot->energy.battery_home_kwh=load>0?slot->energy.consumption_kwh*slot->split.battery_home/load:0;
        }
        parsed->totals.pv_home_kwh=parsed->totals.pv_export_kwh=parsed->totals.pv_stored_kwh=0;
        parsed->totals.grid_home_kwh=parsed->totals.battery_home_kwh=0;
        for(int i=0;i<APP_STATS_SLOT_COUNT;i++){
            app_energy_totals_t *e=&parsed->slots[i].energy;
            parsed->totals.pv_home_kwh+=e->pv_home_kwh;parsed->totals.pv_export_kwh+=e->pv_export_kwh;
            parsed->totals.pv_stored_kwh+=e->pv_stored_kwh;parsed->totals.grid_home_kwh+=e->grid_home_kwh;
            parsed->totals.battery_home_kwh+=e->battery_home_kwh;
        }
    }
    parsed->valid = true;
    *out = *parsed;
    free(parsed);
    return true;

invalid:
    free(parsed);
    return false;
}

/* Egy napi JSON fájl méretkorlátos beolvasása és validálása. */
static esp_err_t read_day_file(
    const char *path,
    const char *expected_date,
    day_data_t *out)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return errno==ENOENT?ESP_ERR_NOT_FOUND:ESP_FAIL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    long size = ftell(file);
    if (size <= 0 || size > STATS_FILE_MAX) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    rewind(file);

    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);

    if (read != (size_t)size) {
        free(buffer);
        return ESP_FAIL;
    }

    buffer[read] = 0;
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithLengthOpts(buffer,(size_t)size,&end,false);
    if(root)while(end<buffer+size && isspace((unsigned char)*end))end++;
    errno=0;
    bool ok=root && end==buffer+size && day_from_json(root,expected_date,out);
    bool allocation_failed=errno==ENOMEM;
    free(buffer);
    cJSON_Delete(root);
    return ok ? ESP_OK : allocation_failed ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_ARG;
}

/* A legújabb érvényes fő/tmp/bak állapot nyer; félbemaradt első mentés is helyreáll. */
static esp_err_t load_day(const char *date,day_data_t *out)
{
    char paths[3][72];
    path_for(date,paths[0],sizeof(paths[0]));
    temp_path_for(date,paths[1],sizeof(paths[1]));
    backup_path_for(date,paths[2],sizeof(paths[2]));
    day_data_t *candidate=malloc(sizeof(*candidate));
    if(!candidate)return ESP_ERR_NO_MEM;
    bool found=false;
    int selected=0;
    esp_err_t error=ESP_ERR_NOT_FOUND;
    for(int i=0;i<3;i++){
        esp_err_t result=read_day_file(paths[i],date,candidate);
        if(result==ESP_OK){
            if(!found || candidate->revision>out->revision){
                *out=*candidate;
                selected=i;
                found=true;
            }
        }else if(result!=ESP_ERR_NOT_FOUND)error=result;
    }
    free(candidate);
    if(!found)return error;
    if(selected!=0){
        /* Promote before any later save can truncate .tmp. The selected valid
         * candidate remains intact even if removing/replacing main fails. */
        if (selected == 1) {
            /* Keep the previous main as fallback; never touch the winning tmp. */
            if (remove(paths[2]) != 0 && errno != ENOENT) return ESP_FAIL;
            if (rename(paths[0], paths[2]) != 0 && errno != ENOENT) return ESP_FAIL;
        } else if (remove(paths[0]) != 0 && errno != ENOENT) return ESP_FAIL;
        if (rename(paths[selected], paths[0]) != 0) return ESP_FAIL;
        out->saved_revision=out->revision;
        app_events_add(APP_EVENT_WARNING,"Statisztika helyreállítva: %s",date);
    }
    return ESP_OK;
}

/* Sérült főfájl megőrzése .corrupt néven diagnosztikai célból. */
static void mark_corrupt(const char *date)
{
    char path[64];
    char corrupt[80];
    path_for(date, path, sizeof(path));
    snprintf(corrupt, sizeof(corrupt), "%s.corrupt", path);
    remove(corrupt);
    (void)rename(path, corrupt);
}

/* Napi állapot biztonságos mentése: .tmp írás + fsync + .bak csere. Csak a külön SD I/O-zárat tartja. */
static esp_err_t save_day_file(const day_data_t *day)
{
    if (!app_storage_is_ok()) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Futás közbeni SD-csere után egy üres, de érvényes kártya is bekerülhet.
     * Ilyenkor a /stats könyvtárat újra létrehozzuk az első mentés előtt.
     */
    if (mkdir(STATS_DIR, 0775) != 0 && errno != EEXIST) {
        app_storage_mark_fault(
            "STATS_MKDIR");
        return ESP_FAIL;
    }

    cJSON *root = day_to_json(day);
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }

    char path[64];
    char temp[72];
    char backup[72];
    path_for(day->date, path, sizeof(path));
    temp_path_for(day->date, temp, sizeof(temp));
    backup_path_for(day->date, backup, sizeof(backup));

    FILE *file = fopen(temp, "wb");
    if (!file) {
        cJSON_free(json);
        app_storage_mark_fault("STATS_OPEN");
        return ESP_FAIL;
    }

    size_t len = strlen(json);
    bool write_ok = fwrite(json, 1, len, file) == len;
    cJSON_free(json);

    if (write_ok) {
        write_ok = fflush(file) == 0 && fsync(fileno(file)) == 0;
    }
    if(fclose(file)!=0)write_ok=false;

    if (!write_ok) {
        remove(temp);
        app_storage_mark_fault("STATS_WRITE");
        return ESP_FAIL;
    }

    remove(backup);
    if (rename(path, backup) != 0 && errno != ENOENT) {
        app_storage_mark_fault("STATS_BACKUP");
        return ESP_FAIL;
    }

    if (rename(temp, path) != 0) {
        (void)rename(backup, path);
        app_storage_mark_fault("STATS_RENAME");
        return ESP_FAIL;
    }

    /* Az előző jó főfájl .bak formában megmarad sérülés esetére. */
    return ESP_OK;
}

/* A mai nap RAM struktúrájának nullázása új dátummal. */
static void reset_today(const char *date)
{
    memset(&s_today, 0, sizeof(s_today));
    snprintf(s_today.date, sizeof(s_today.date), "%s", date);
    s_today.valid = true;
    s_today.revision=1;
}

/* A mai és tegnapi dátumnál régebbi statisztikai állományok törlése. */
static void cleanup_old_files(const char *today, const char *yesterday)
{
    (void)yesterday;
    struct tm oldest;time_t epoch=time(NULL);localtime_r(&epoch,&oldest);
    oldest.tm_mday-=APP_STATS_HISTORY_DAYS-1;oldest.tm_isdst=-1;
    char cutoff[11];date_for(mktime(&oldest),cutoff);
    DIR *dir = opendir(STATS_DIR);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len < 10 ||
            entry->d_name[4] != '-' ||
            entry->d_name[7] != '-') {
            continue;
        }

        char date[11] = {0};
        memcpy(date, entry->d_name, 10);

        if (strcmp(date, cutoff)>=0 && strcmp(date, today)<=0) {
            continue;
        }

        char path[sizeof(STATS_DIR)+256];
        snprintf(path, sizeof(path), STATS_DIR "/%s", entry->d_name);
        (void)remove(path);
    }

    closedir(dir);
}

static bool day_has_any_samples(const day_data_t *day){
    if(!day->valid)return false;
    for(int i=0;i<APP_STATS_SLOT_COUNT;i++)if(day->slots[i].valid)return true;
    return false;
}
static void history_put(const day_data_t *day){
    if(!day_has_any_samples(day))return;
    unsigned index=0;
    for(unsigned i=0;i<APP_STATS_HISTORY_DAYS;i++){
        if(!strcmp(s_history[i].date,day->date)){index=i;goto put;}
        if(strcmp(s_history[i].date,s_history[index].date)<0)index=i;
    }
put: s_history[index]=(app_stats_day_t){.valid=true,.totals=day->totals};
    snprintf(s_history[index].date,11,"%s",day->date);
}

/* RAM-napváltás I/O nélkül. Az előző minta megmarad az éjféli intervallum vágásához. */
static void roll_day(time_t now)
{
    char current[11],yesterday[11];
    date_for(now,current);
    if(strcmp(current,s_today.date)<=0)return;
    yesterday_for(now,yesterday);
    history_put(&s_today);
    if(s_today.valid && strcmp(s_today.date,yesterday)==0)s_yesterday=s_today;
    else memset(&s_yesterday,0,sizeof(s_yesterday));
    reset_today(current);
    app_events_add(APP_EVENT_INFO,"Új nap indult: %s",current);
}

/* Csak a már lezárt ötperces slotok energiáinak összegzése az összehasonlításhoz. */
static void sum_completed_slots(
    const day_data_t *day,
    int completed_slots,
    app_energy_totals_t *out)
{
    memset(out, 0, sizeof(*out));

    if (!day->valid) {
        return;
    }

    if (completed_slots < 0) {
        completed_slots = 0;
    }
    if (completed_slots > APP_STATS_SLOT_COUNT) {
        completed_slots = APP_STATS_SLOT_COUNT;
    }

    for (int i = 0; i < completed_slots; i++) {
        totals_add(out, &day->slots[i].energy);
    }
}

/* Igaz, ha a vizsgált lezárt időszakban legalább egy valódi mérési slot létezik. */
static bool day_has_samples_until(
    const day_data_t *day,
    int completed_slots)
{
    if (!day->valid) {
        return false;
    }

    if (completed_slots < 0) {
        completed_slots = 0;
    }
    if (completed_slots > APP_STATS_SLOT_COUNT) {
        completed_slots = APP_STATS_SLOT_COUNT;
    }

    for (int i = 0; i < completed_slots; i++) {
        const app_stats_slot_t *slot=&day->slots[i];
        /* A mentett régi slotoknak lehet 0 a sample_count értéke,
         * miközben az integrált energia érvényes. Ezt sem dobhatjuk el. */
        const app_energy_totals_t *en=&slot->energy;
        bool has_energy=fabs(en->production_kwh)>1e-9 ||
            fabs(en->consumption_kwh)>1e-9 ||
            fabs(en->grid_import_kwh)>1e-9 ||
            fabs(en->grid_export_kwh)>1e-9 ||
            fabs(en->battery_charge_kwh)>1e-9 ||
            fabs(en->battery_discharge_kwh)>1e-9;
        if ((slot->valid && slot->sample_count>0) || has_energy)
            return true;
    }

    return false;
}

/* Mentett kWh adat akkor is érvényes, ha a régebbi
 * statisztika-formátumban a sample_count nulla. */
static int comparable_slots(const day_data_t *day,int completed_slots)
{
    if(!day->valid)return 0;
    int count=0;
    if(completed_slots>APP_STATS_SLOT_COUNT)completed_slots=APP_STATS_SLOT_COUNT;
    for(int i=0;i<completed_slots;i++){
        const app_stats_slot_t *slot=&day->slots[i];
        const app_energy_totals_t *en=&slot->energy;
        bool energy=fabs(en->production_kwh)>1e-9||
                    fabs(en->consumption_kwh)>1e-9||
                    fabs(en->grid_import_kwh)>1e-9||
                    fabs(en->grid_export_kwh)>1e-9||
                    fabs(en->battery_charge_kwh)>1e-9||
                    fabs(en->battery_discharge_kwh)>1e-9;
        if((slot->valid&&slot->sample_count>0)||energy)count++;
    }
    return count;
}

/* Statisztikai tárhely inicializálása és a mai/tegnapi állapot visszatöltése. */
esp_err_t app_stats_init(const app_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (!s_save_mutex)s_save_mutex=xSemaphoreCreateMutex();
    if (!s_mutex || !s_save_mutex) {
        return ESP_ERR_NO_MEM;
    }

    s_cfg = *cfg;

    if (mkdir(STATS_DIR, 0775) != 0 && errno != EEXIST) {
        app_storage_mark_fault("STATS_MKDIR");
        return ESP_FAIL;
    }

    time_t now = time(NULL);
    char today[11];
    char yesterday[11];
    date_for(now, today);
    yesterday_for(now, yesterday);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    memset(&s_today, 0, sizeof(s_today));
    memset(&s_yesterday, 0, sizeof(s_yesterday));

    esp_err_t err = load_day(today, &s_today);
    if (err != ESP_OK) {
        if(err==ESP_ERR_NO_MEM || err==ESP_FAIL){xSemaphoreGive(s_mutex);return err;}
        if (err != ESP_ERR_NOT_FOUND) {
            mark_corrupt(today);
            app_events_add(
                APP_EVENT_WARNING,
                "Sérült mai statisztika archiválva; a nap nulláról indul.");
        }
        reset_today(today);
    }

    err = load_day(yesterday, &s_yesterday);
    if (err != ESP_OK) {
        if(err==ESP_ERR_NO_MEM || err==ESP_FAIL){xSemaphoreGive(s_mutex);return err;}
        if (err != ESP_ERR_NOT_FOUND) {
            mark_corrupt(yesterday);
            app_events_add(
                APP_EVENT_WARNING,
                "Sérült tegnapi statisztika archiválva; összehasonlítás kikapcsolva.");
        }
        memset(&s_yesterday, 0, sizeof(s_yesterday));
    }

    memset(s_history,0,sizeof(s_history));
    day_data_t *historical=malloc(sizeof(*historical));
    if(!historical){xSemaphoreGive(s_mutex);return ESP_ERR_NO_MEM;}
    for(int i=2;i<APP_STATS_HISTORY_DAYS;i++){
        struct tm tmv;localtime_r(&now,&tmv);tmv.tm_mday-=i;tmv.tm_isdst=-1;
        char date[11];date_for(mktime(&tmv),date);
        if(load_day(date,historical)==ESP_OK)history_put(historical);
    }
    free(historical);
    cleanup_old_files(today, yesterday);
    s_have_prev = false;
    s_last_save_us = esp_timer_get_time();

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Statistics initialized for %s", today);
    return ESP_OK;
}

/* Egy új érvényes MQTT minta beépítése a napi energiába és az ötperces átlagba. */
void app_stats_process_sample(const app_energy_sample_t *sample)
{
    if (!sample || !s_mutex) {
        return;
    }

    time_t now = (time_t)sample->received_epoch_s;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    roll_day(now);
    if(s_have_prev){
        integrate_pair(&s_prev,sample);
        int64_t gap=sample->received_epoch_s-s_prev.received_epoch_s;
        if(gap>(int64_t)app_config_data_timeout(&s_cfg)){
            /* Az érintett ötperces ablakok kimaradnak; nem lesz kitalált összekötés. */
            int64_t cursor=s_prev.received_epoch_s;
            if(now-cursor>2*86400)cursor=now-2*86400;
            while(cursor<=now){
                day_data_t *missing=day_for((time_t)cursor);
                if(missing){missing->slots[slot_for((time_t)cursor)].chart_gap=true;missing->revision++;}
                struct tm tmv;time_t epoch=(time_t)cursor;localtime_r(&epoch,&tmv);
                cursor+=300-(tmv.tm_min%5)*60-tmv.tm_sec;
            }
        }
    }
    day_data_t *day=day_for(now);
    if(day){
        slot_average_add(&day->slots[slot_for(now)],sample);
        day->revision++;
    }

    s_prev = *sample;
    s_have_prev = true;

    xSemaphoreGive(s_mutex);
}

/* Szálbiztos snapshot készítése a UI számára, tegnapi azonos időszakos referenciával. */
void app_stats_get_snapshot(app_stats_snapshot_t *out)
{
    if (!out || !s_mutex) {
        return;
    }

    int completed_slots = slot_for(time(NULL));

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    memset(out, 0, sizeof(*out));
    snprintf(out->date, sizeof(out->date), "%s", s_today.date);
    out->totals = s_today.totals;
    memcpy(out->slots, s_today.slots, sizeof(out->slots));

    sum_completed_slots(
        &s_today,
        completed_slots,
        &out->completed_interval_totals);

    out->has_yesterday =
        day_has_samples_until(
            &s_yesterday,
            completed_slots);

    if (out->has_yesterday) {
        sum_completed_slots(
            &s_yesterday,
            completed_slots,
            &out->yesterday_completed_interval_totals);
        /* Mindkét napon pontosan 0:00..aktuális lezárt ötperces
         * idősáv; a hiányos nappal nem szabad teljes napot összevetni. */
        int valid_today=comparable_slots(&s_today,completed_slots);
        int valid_yesterday=comparable_slots(&s_yesterday,completed_slots);
        out->yesterday_comparison_partial=completed_slots>0 &&
            (valid_today*100<completed_slots*85 ||
             valid_yesterday*100<completed_slots*85);
    }

    xSemaphoreGive(s_mutex);
}

/* Mindkét megtartott nap konzisztens másolatának mentése. A lassú I/O nem fogja a mérési zárat. */
esp_err_t app_stats_save_now(void)
{
    if(!s_mutex || !s_save_mutex)return ESP_ERR_INVALID_STATE;
    if(!app_storage_is_ok())return ESP_ERR_INVALID_STATE;
    day_data_t *copy=malloc(sizeof(*copy));
    if(!copy)return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_save_mutex,portMAX_DELAY);
    esp_err_t error=app_storage_lock();
    if(error!=ESP_OK){xSemaphoreGive(s_save_mutex);free(copy);return error;}
    char today[11],yesterday[11];
    for(int i=0;i<2;i++){
        xSemaphoreTake(s_mutex,portMAX_DELAY);
        *copy=i==0?s_yesterday:s_today;
        xSemaphoreGive(s_mutex);
        if(!copy->valid)continue;
        error=save_day_file(copy);
        if(error!=ESP_OK)break;
        xSemaphoreTake(s_mutex,portMAX_DELAY);
        /* Napváltás történhet írás közben: dátum alapján keressük a mentett példányt. */
        day_data_t *day=strcmp(copy->date,s_today.date)==0?&s_today:
            strcmp(copy->date,s_yesterday.date)==0?&s_yesterday:NULL;
        if(day)day->saved_revision=copy->revision;
        xSemaphoreGive(s_mutex);
    }
    if(error==ESP_OK){
        xSemaphoreTake(s_mutex,portMAX_DELAY);
        snprintf(today,sizeof(today),"%s",s_today.date);
        snprintf(yesterday,sizeof(yesterday),"%s",s_yesterday.date);
        s_last_save_us=esp_timer_get_time();
        xSemaphoreGive(s_mutex);
        cleanup_old_files(today,yesterday);
    }
    app_storage_unlock();
    xSemaphoreGive(s_save_mutex);
    free(copy);
    return error;
}

void app_stats_update_config(const app_statistics_config_t *cfg)
{
    if (!cfg || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_cfg.statistics = *cfg;
    xSemaphoreGive(s_mutex);
}

/* A karbantartás külön is hívható a napváltási sorrend regressziós tesztjeiben. */
void app_stats_tick(void)
{
    static bool previous_sd_ok=true;
    time_t now=time(NULL);
    bool sd_ok=app_storage_is_ok();
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    roll_day(now);
    bool due=(esp_timer_get_time()-s_last_save_us)>=
        (int64_t)s_cfg.statistics.save_interval_minutes*60LL*1000000LL;
    /* Megvárjuk az utolsó éjféli mintaköz végét, mielőtt tegnapot végleg elmentjük. */
    bool yesterday_dirty=s_yesterday.valid && s_yesterday.revision!=s_yesterday.saved_revision &&
        now-local_day_start(now)>=s_cfg.statistics.max_integration_gap_seconds;
    xSemaphoreGive(s_mutex);
    if(sd_ok && (due || yesterday_dirty || !previous_sd_ok)){
        if(app_stats_save_now()!=ESP_OK){previous_sd_ok=false;return;}
    }
    previous_sd_ok=sd_ok;
}

/* Másodperces RAM-karbantartás; a mentés végig külön I/O-zárral fut. */
static void stats_task(void *arg)
{
    (void)arg;
    while(true){
        app_stats_tick();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t app_stats_start_task(void)
{
    if(s_task_started)return ESP_OK;
    if(xTaskCreate(stats_task,"stats",6144,NULL,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    s_task_started=true;
    return ESP_OK;
}

/* Naptári hét (hétfőtől) vagy aktuális hónap; hiányzó nap nem nulla mérés. */
void app_stats_get_period(bool month,app_stats_period_t *out){
    if(!out)return;
    memset(out,0,sizeof(*out));if(!s_mutex)return;
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);
    local.tm_hour=local.tm_min=local.tm_sec=0;
    if(month)local.tm_mday=1;else local.tm_mday-=(local.tm_wday+6)%7;
    int original_month=local.tm_mon;out->count=month?31:7;
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    for(unsigned i=0;i<out->count;i++){
        struct tm day=local;day.tm_mday+=(int)i;day.tm_isdst=-1;time_t epoch=mktime(&day);
        if(month&&day.tm_mon!=original_month){out->count=i;break;}
        app_stats_day_t *target=&out->days[i];date_for(epoch,target->date);
        const day_data_t *current=!strcmp(target->date,s_today.date)?&s_today:
            !strcmp(target->date,s_yesterday.date)?&s_yesterday:NULL;
        if(current&&day_has_any_samples(current)){target->valid=true;target->totals=current->totals;}
        else for(unsigned j=0;j<APP_STATS_HISTORY_DAYS;j++)if(!strcmp(target->date,s_history[j].date)){*target=s_history[j];break;}
        if(target->valid)totals_add(&out->totals,&target->totals);
    }
    xSemaphoreGive(s_mutex);
}
