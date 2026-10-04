#include "app_stats.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
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
#define STATS_VERSION 1

static const char *TAG = "stats";

typedef struct {
    char date[11];
    app_energy_totals_t totals;
    app_stats_slot_t slots[APP_STATS_SLOT_COUNT];
    bool valid;
} day_data_t;

static SemaphoreHandle_t s_mutex;
static app_config_t s_cfg;
static day_data_t s_today;
static day_data_t s_yesterday;
static bool s_have_prev;
static bool s_task_started;
static app_energy_sample_t s_prev;
static int64_t s_last_save_us;

static void date_for(time_t t, char out[11])
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, 11, "%Y-%m-%d", &tmv);
}

static int slot_for(time_t t)
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    return tmv.tm_hour * 12 + tmv.tm_min / 5;
}

static time_t slot_boundary_for(time_t t)
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    tmv.tm_min = (tmv.tm_min / 5) * 5;
    tmv.tm_sec = 0;
    tmv.tm_isdst = -1;
    return mktime(&tmv);
}

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

static bool same_local_date(time_t a, time_t b)
{
    char da[11];
    char db[11];
    date_for(a, da);
    date_for(b, db);
    return strcmp(da, db) == 0;
}

static void yesterday_for(time_t now, char out[11])
{
    struct tm tmv;
    localtime_r(&now, &tmv);
    tmv.tm_mday--;
    tmv.tm_isdst = -1;
    time_t previous = mktime(&tmv);
    date_for(previous, out);
}

static void totals_add(app_energy_totals_t *dst, const app_energy_totals_t *src)
{
    dst->production_kwh += src->production_kwh;
    dst->consumption_kwh += src->consumption_kwh;
    dst->grid_import_kwh += src->grid_import_kwh;
    dst->grid_export_kwh += src->grid_export_kwh;
    dst->battery_charge_kwh += src->battery_charge_kwh;
    dst->battery_discharge_kwh += src->battery_discharge_kwh;
}

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

    if (grid >= 0) {
        result.grid_import_kwh = grid * hours;
    } else {
        result.grid_export_kwh = -grid * hours;
    }

    if (battery >= 0) {
        result.battery_discharge_kwh = battery * hours;
    } else {
        result.battery_charge_kwh = -battery * hours;
    }

    return result;
}

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

static void slot_average_add(app_stats_slot_t *slot, const app_energy_sample_t *sample)
{
    slot->sample_count++;
    float n = (float)slot->sample_count;

    slot->pv_kw += ((float)sample->power_pv_kw - slot->pv_kw) / n;
    slot->load_kw += ((float)sample->power_full_kw - slot->load_kw) / n;
    slot->grid_kw += ((float)sample->power_grid_kw - slot->grid_kw) / n;
    slot->battery_kw += ((float)sample->power_bat_kw - slot->battery_kw) / n;
    slot->valid = true;
}

static void add_slot_energy(int slot, const app_energy_totals_t *energy)
{
    if (slot >= 0 && slot < APP_STATS_SLOT_COUNT) {
        totals_add(&s_today.slots[slot].energy, energy);
    }
}

static void integrate_pair(
    const app_energy_sample_t *previous,
    const app_energy_sample_t *current)
{
    double seconds = (double)(current->received_epoch_s - previous->received_epoch_s);
    if (seconds <= 0 ||
        seconds > (double)s_cfg.statistics.max_integration_gap_seconds) {
        return;
    }

    int previous_slot = slot_for((time_t)previous->received_epoch_s);
    int current_slot = slot_for((time_t)current->received_epoch_s);

    if (previous_slot == current_slot) {
        app_energy_totals_t e = energy_between(previous, current, seconds / 3600.0);
        totals_add(&s_today.totals, &e);
        add_slot_energy(current_slot, &e);
        return;
    }

    if (same_local_date(
            (time_t)previous->received_epoch_s,
            (time_t)current->received_epoch_s) &&
        current_slot == previous_slot + 1) {

        time_t boundary = slot_boundary_for((time_t)current->received_epoch_s);
        double before = (double)((int64_t)boundary - previous->received_epoch_s);
        double after = (double)(current->received_epoch_s - (int64_t)boundary);

        if (before > 0 && after >= 0 && before + after > 0) {
            double fraction = before / (before + after);
            app_energy_sample_t middle = interpolate_sample(
                previous,
                current,
                fraction,
                (int64_t)boundary);

            app_energy_totals_t first = energy_between(previous, &middle, before / 3600.0);
            app_energy_totals_t second = energy_between(&middle, current, after / 3600.0);

            totals_add(&s_today.totals, &first);
            totals_add(&s_today.totals, &second);
            add_slot_energy(previous_slot, &first);
            add_slot_energy(current_slot, &second);
            return;
        }
    }

    app_energy_totals_t e = energy_between(previous, current, seconds / 3600.0);
    totals_add(&s_today.totals, &e);
    add_slot_energy(previous_slot, &e);
}

static void path_for(const char *date, char *out, size_t out_len)
{
    snprintf(out, out_len, STATS_DIR "/%s.json", date);
}

static void temp_path_for(const char *date, char *out, size_t out_len)
{
    snprintf(out, out_len, STATS_DIR "/%s.json.tmp", date);
}

static void backup_path_for(const char *date, char *out, size_t out_len)
{
    snprintf(out, out_len, STATS_DIR "/%s.json.bak", date);
}

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
    return obj;
}

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

    return true;
}

static cJSON *day_to_json(const day_data_t *day)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "version", STATS_VERSION);
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
        cJSON_AddItemToObject(obj, "energy", totals_to_json(&slot->energy));
        cJSON_AddItemToArray(slots, obj);
    }

    return root;
}

static bool day_from_json(cJSON *root, const char *expected_date, day_data_t *out)
{
    if (!cJSON_IsObject(root)) {
        return false;
    }

    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *date = cJSON_GetObjectItemCaseSensitive(root, "date");
    cJSON *totals = cJSON_GetObjectItemCaseSensitive(root, "totals");
    cJSON *slots = cJSON_GetObjectItemCaseSensitive(root, "slots");

    if (!cJSON_IsNumber(version) ||
        version->valueint != STATS_VERSION ||
        !cJSON_IsString(date) ||
        strcmp(date->valuestring, expected_date) != 0 ||
        !cJSON_IsArray(slots) ||
        cJSON_GetArraySize(slots) != APP_STATS_SLOT_COUNT) {
        return false;
    }

    day_data_t parsed = {0};
    snprintf(parsed.date, sizeof(parsed.date), "%s", expected_date);

    if (!totals_from_json(totals, &parsed.totals)) {
        return false;
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
            count->valuedouble < 0 ||
            !cJSON_IsNumber(pv) || !isfinite(pv->valuedouble) ||
            !cJSON_IsNumber(load) || !isfinite(load->valuedouble) ||
            !cJSON_IsNumber(grid) || !isfinite(grid->valuedouble) ||
            !cJSON_IsNumber(battery) || !isfinite(battery->valuedouble) ||
            !totals_from_json(energy, &parsed.slots[i].energy)) {
            return false;
        }

        parsed.slots[i].valid = cJSON_IsTrue(valid);
        parsed.slots[i].sample_count = (uint32_t)count->valuedouble;
        parsed.slots[i].pv_kw = (float)pv->valuedouble;
        parsed.slots[i].load_kw = (float)load->valuedouble;
        parsed.slots[i].grid_kw = (float)grid->valuedouble;
        parsed.slots[i].battery_kw = (float)battery->valuedouble;
    }

    parsed.valid = true;
    *out = parsed;
    return true;
}

static esp_err_t read_day_file(
    const char *path,
    const char *expected_date,
    day_data_t *out)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
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
    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    bool ok = root && day_from_json(root, expected_date, out);
    cJSON_Delete(root);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t load_day(const char *date, day_data_t *out)
{
    char path[64];
    char backup[72];
    char temp[72];
    path_for(date, path, sizeof(path));
    backup_path_for(date, backup, sizeof(backup));
    temp_path_for(date, temp, sizeof(temp));

    esp_err_t main_error = read_day_file(path, date, out);
    if (main_error == ESP_OK) {
        remove(backup);
        remove(temp);
        return ESP_OK;
    }

    esp_err_t backup_error = read_day_file(backup, date, out);
    if (backup_error == ESP_OK) {
        remove(path);
        if (rename(backup, path) == 0) {
            app_events_add(
                APP_EVENT_WARNING,
                "Statisztika helyreállítva biztonsági másolatból: %s",
                date);
        }
        remove(temp);
        return ESP_OK;
    }

    return main_error != ESP_ERR_NOT_FOUND ? main_error : backup_error;
}

static void mark_corrupt(const char *date)
{
    char path[64];
    char corrupt[80];
    path_for(date, path, sizeof(path));
    snprintf(corrupt, sizeof(corrupt), "%s.corrupt", path);
    remove(corrupt);
    (void)rename(path, corrupt);
}

static esp_err_t save_locked(const day_data_t *day)
{
    if (!app_storage_is_ok()) {
        return ESP_ERR_INVALID_STATE;
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
        app_storage_mark_fault("stats ideiglenes fájl megnyitás sikertelen");
        return ESP_FAIL;
    }

    size_t len = strlen(json);
    bool write_ok = fwrite(json, 1, len, file) == len;
    cJSON_free(json);

    if (write_ok) {
        write_ok = fflush(file) == 0 && fsync(fileno(file)) == 0;
    }
    fclose(file);

    if (!write_ok) {
        remove(temp);
        app_storage_mark_fault("stats fájl írás sikertelen");
        return ESP_FAIL;
    }

    remove(backup);
    if (rename(path, backup) != 0 && errno != ENOENT) {
        remove(temp);
        app_storage_mark_fault("stats biztonsági másolat készítése sikertelen");
        return ESP_FAIL;
    }

    if (rename(temp, path) != 0) {
        (void)rename(backup, path);
        remove(temp);
        app_storage_mark_fault("stats fájl cseréje sikertelen");
        return ESP_FAIL;
    }

    remove(backup);
    return ESP_OK;
}

static void reset_today(const char *date)
{
    memset(&s_today, 0, sizeof(s_today));
    snprintf(s_today.date, sizeof(s_today.date), "%s", date);
    s_today.valid = true;
    s_have_prev = false;
}

static void cleanup_old_files(const char *today, const char *yesterday)
{
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

        if (strcmp(date, today) == 0 ||
            strcmp(date, yesterday) == 0) {
            continue;
        }

        char path[128];
        snprintf(path, sizeof(path), STATS_DIR "/%s", entry->d_name);
        (void)remove(path);
    }

    closedir(dir);
}

static void roll_day(time_t now)
{
    char current_date[11];
    date_for(now, current_date);

    if (strcmp(current_date, s_today.date) == 0) {
        return;
    }

    if (s_today.valid && app_storage_is_ok()) {
        (void)save_locked(&s_today);
    }

    s_yesterday = s_today;
    reset_today(current_date);
    s_last_save_us = esp_timer_get_time();

    if (app_storage_is_ok()) {
        cleanup_old_files(s_today.date, s_yesterday.date);
    }

    app_events_add(APP_EVENT_INFO, "Új nap indult: %s", current_date);
}

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

esp_err_t app_stats_init(const app_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    s_cfg = *cfg;

    if (mkdir(STATS_DIR, 0775) != 0 && errno != EEXIST) {
        app_storage_mark_fault("stats könyvtár nem hozható létre");
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
        if (err != ESP_ERR_NOT_FOUND) {
            mark_corrupt(yesterday);
            app_events_add(
                APP_EVENT_WARNING,
                "Sérült tegnapi statisztika archiválva; összehasonlítás kikapcsolva.");
        }
        memset(&s_yesterday, 0, sizeof(s_yesterday));
    }

    cleanup_old_files(today, yesterday);
    s_have_prev = false;
    s_last_save_us = esp_timer_get_time();

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Statistics initialized for %s", today);
    return ESP_OK;
}

void app_stats_process_sample(const app_energy_sample_t *sample)
{
    if (!sample || !s_mutex) {
        return;
    }

    time_t now = (time_t)sample->received_epoch_s;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    bool crossed_midnight =
        s_have_prev &&
        !same_local_date(
            (time_t)s_prev.received_epoch_s,
            now);

    if (crossed_midnight) {
        double total_seconds =
            (double)(sample->received_epoch_s - s_prev.received_epoch_s);

        if (total_seconds > 0 &&
            total_seconds <=
                (double)s_cfg.statistics.max_integration_gap_seconds) {

            time_t boundary = local_day_start(now);

            if (s_prev.received_epoch_s < (int64_t)boundary &&
                sample->received_epoch_s >= (int64_t)boundary) {

                double before =
                    (double)((int64_t)boundary - s_prev.received_epoch_s);
                double fraction = before / total_seconds;

                app_energy_sample_t middle = interpolate_sample(
                    &s_prev,
                    sample,
                    fraction,
                    (int64_t)boundary);

                /*
                 * First close the previous day exactly at local midnight.
                 * Then roll the daily file and account the remainder in the
                 * new day's first five-minute slot.
                 */
                integrate_pair(&s_prev, &middle);
                roll_day(now);
                integrate_pair(&middle, sample);
            } else {
                roll_day(now);
            }
        } else {
            roll_day(now);
        }
    } else {
        roll_day(now);
        if (s_have_prev) {
            integrate_pair(&s_prev, sample);
        }
    }

    int slot = slot_for(now);
    if (slot >= 0 && slot < APP_STATS_SLOT_COUNT) {
        slot_average_add(&s_today.slots[slot], sample);
    }

    s_prev = *sample;
    s_have_prev = true;

    xSemaphoreGive(s_mutex);
}

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

    out->has_yesterday = s_yesterday.valid;
    if (s_yesterday.valid) {
        sum_completed_slots(
            &s_yesterday,
            completed_slots,
            &out->yesterday_completed_interval_totals);
    }

    xSemaphoreGive(s_mutex);
}

esp_err_t app_stats_save_now(void)
{
    if (!s_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = save_locked(&s_today);
    if (err == ESP_OK) {
        s_last_save_us = esp_timer_get_time();
    }
    xSemaphoreGive(s_mutex);
    return err;
}

static void stats_task(void *arg)
{
    (void)arg;

    while (true) {
        time_t now = time(NULL);

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        roll_day(now);
        bool due =
            (esp_timer_get_time() - s_last_save_us) >=
            (int64_t)s_cfg.statistics.save_interval_minutes *
                60LL * 1000000LL;
        xSemaphoreGive(s_mutex);

        if (due && app_storage_is_ok()) {
            (void)app_stats_save_now();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_stats_start_task(void)
{
    if (s_task_started) {
        return;
    }

    s_task_started = true;
    xTaskCreate(stats_task, "stats", 6144, NULL, 4, NULL);
}
