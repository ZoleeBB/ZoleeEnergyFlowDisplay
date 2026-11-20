/* Az inverterprofil és a heti programok az NVS egyetlen atomi bejegyzésében élnek.
 * Az állandó PSRAM-területet kizárólag az aktív inverteres Master kapja meg. */
#include "app_inverter.h"
#include "app_inverter_discovery.h"
#include "app_inverter_settings.h"
#include "app_inverter_identity.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const app_inverter_profile_t profiles[] = {
    {"huawei_modbus", INV_MODBUS, 5}, {"goodwe_modbus", INV_MODBUS, 5},
    {"sungrow_modbus", INV_MODBUS, 5}, {"solax_modbus", INV_MODBUS, 5},
    {"victron_modbus", INV_MODBUS, 5}, {"solaredge_modbus", INV_MODBUS, 5},
    {"deye_v5", INV_MODBUS, 5}, {"deye_modbus", INV_MODBUS, 5},
    {"deye_rtu", INV_MODBUS, 5}, {"deye_lse3", INV_MODBUS, 5},
    {"solplanet_modbus", INV_MODBUS, 5},
    {"sofar_modbus", INV_MODBUS, 5}, {"sofar_v5", INV_MODBUS, 5},
    {"fronius_http", INV_HTTP, 5}, {"solax_http", INV_HTTP, 5},
    {"solplanet_http", INV_HTTP, 5}, {"solplanet_https", INV_HTTP, 5},
    {"fronius_cloud", INV_HTTP, 300}, {"huawei_cloud", INV_HTTP, 3600},
    {"solax_cloud", INV_HTTP, 300}, {"solplanet_cloud", INV_HTTP, 1200},
    {"victron_mqtt", INV_MQTT, 5}
};
static _Atomic(SemaphoreHandle_t) mux;
static app_inverter_settings_t *active;
static uint32_t revision, source_revision;
static int64_t last_measurement;
static char active_ipv4[16];
static const char *status_text = "Beállításra vár";

const app_inverter_profile_t *app_inverter_profile(const char *id)
{
    if (id) for (size_t i = 0; i < sizeof(profiles)/sizeof(*profiles); ++i)
        if (strcmp(profiles[i].id, id) == 0) return &profiles[i];
    return NULL;
}

static void *ps_alloc(size_t size)
{
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void lock(void) { xSemaphoreTake(atomic_load(&mux), portMAX_DELAY); }
void app_inverter_settings_unlock(void) { xSemaphoreGive(atomic_load(&mux)); }

static bool normalize_host(app_inverter_config_t *driver)
{
    char *host = driver->host;
    size_t len = strnlen(host, sizeof(driver->host));
    if (!len || len == sizeof(driver->host)) return false;
    if (host[len-1] == '.') host[--len] = 0;
    size_t label_len = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)host[i];
        if (ch == '.') {
            if (!label_len || host[i-1] == '-') return false;
            label_len = 0;
        } else {
            if (!isalnum(ch) && (ch != '-' || !label_len)) return false;
            if (++label_len > 63) return false;
            host[i] = (char)tolower(ch);
        }
    }
    return label_len && host[len-1] != '-';
}

bool app_inverter_settings_local_name(const char *host, size_t length)
{
    if (!host || !atomic_load(&mux)) return false;
    lock();
    const app_inverter_profile_t *p = active ? app_inverter_profile(active->driver.profile) : NULL;
    bool match = p && p->seconds == 5 && strlen(active->driver.host) == length &&
                 !strncasecmp(host, active->driver.host, length);
    app_inverter_settings_unlock();
    return match;
}

static bool direct_master(void)
{
    app_runtime_state_t state;
    app_state_get_runtime(&state);
    return state.data_recept_mode == APP_DATA_INVERTER;
}

/* Az első v4 tesztfirmware profilja a tanúsítványmutató után rögtön a
 * koordinátákat tárolta. A célplatform igazítását a fordítóval számoltatjuk. */
typedef struct {
    unsigned version;
    _Alignas(app_inverter_config_t) unsigned char driver[offsetof(app_inverter_config_t, registers)];
    double latitude;
} inverter_v1_prefix_t;

static bool decode_profile(app_inverter_settings_t *s, size_t size)
{
    const size_t pem_offset = offsetof(app_inverter_settings_t, pem);
    if (s->version == 1) {
        const size_t old_tail = offsetof(inverter_v1_prefix_t, latitude);
        const size_t new_tail = offsetof(app_inverter_settings_t, latitude);
        const size_t shift = new_tail - old_tail;
        if (size <= pem_offset - shift || size > pem_offset + sizeof(s->pem) - shift)
            return false;
        memmove(&s->latitude, (unsigned char *)s + old_tail, size - old_tail);
        memset(&s->driver.registers, 0, sizeof(s->driver.registers));
        s->version = 2;
        size += shift;
    }
    if (s->version != 2 || size <= pem_offset || size > pem_offset + sizeof(s->pem) ||
        !memchr(s->pem, 0, size - pem_offset)) return false;
    const app_inverter_config_t *c = &s->driver;
    if (!memchr(c->profile, 0, sizeof(c->profile)) || !app_inverter_profile(c->profile) ||
        !memchr(c->host, 0, sizeof(c->host)) || !memchr(c->device, 0, sizeof(c->device)) ||
        !memchr(c->user, 0, sizeof(c->user)) || !memchr(c->secret, 0, sizeof(c->secret)) ||
        !memchr(c->key, 0, sizeof(c->key))) return false;
    for (size_t i = 0; i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i) {
        if (!memchr(s->peers[i].id, 0, sizeof(s->peers[i].id))) return false;
    }
    return true;
}

bool app_inverter_settings_open(void)
{
    if (!direct_master()) return false;
    if (!atomic_load(&mux)) {
        SemaphoreHandle_t candidate = xSemaphoreCreateMutex(), expected = NULL;
        if (!candidate) return false;
        if (!atomic_compare_exchange_strong(&mux, &expected, candidate)) vSemaphoreDelete(candidate);
    }
    lock();
    if (!active) {
        app_inverter_settings_t *s = ps_alloc(sizeof(*s));
        if (s) {
            size_t len = sizeof(*s);
            nvs_handle_t h;
            esp_err_t err = nvs_open("inverter_v4", NVS_READONLY, &h);
            if (err == ESP_OK) {
                err = nvs_get_blob(h, "profile1", s, &len);
                nvs_close(h);
            }
            if (err != ESP_OK || !decode_profile(s, len) || !normalize_host(&s->driver)) {
                memset(s, 0, sizeof(*s));
                s->master.touch_wake_seconds = 30;
            }
            s->driver.trusted_pem = NULL;
            active = s;
            revision = source_revision = 1;
        }
    }
    bool ok = active != NULL;
    app_inverter_settings_unlock();
    return ok;
}

static bool integer(const cJSON *v, int min, int max, int *value)
{
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < min ||
        v->valuedouble > max || v->valuedouble != v->valueint) return false;
    *value = v->valueint;
    return true;
}
#define ITEM(o,key) cJSON_GetObjectItemCaseSensitive((o), (key))

static bool string_field(const cJSON *o, const char *key, char *dest, size_t size, bool required)
{
    const cJSON *v = ITEM(o,key);
    if (!v) return !required;
    if (!cJSON_IsString(v) || (required && !*v->valuestring) || strlen(v->valuestring) >= size) return false;
    strcpy(dest, v->valuestring);
    return true;
}

static bool read_schedule(const cJSON *v, app_inverter_schedule_t *s)
{
    int n;
    if (!cJSON_IsObject(v) || !integer(ITEM(v,"touchWakeSeconds"), 1, 3600, &n)) return false;
    s->touch_wake_seconds = n;
    const cJSON *days = ITEM(v,"days");
    if (!cJSON_IsArray(days) || cJSON_GetArraySize(days) != 7) return false;
    for (int i = 0; i < 7; ++i) {
        const cJSON *day = cJSON_GetArrayItem(days, i);
        const cJSON *on = ITEM(day,"enabled");
        int start, end;
        if (!cJSON_IsBool(on) || !integer(ITEM(day,"start"), 0, 1439, &start) ||
            !integer(ITEM(day,"end"), 0, 1439, &end)) return false;
        s->days[i] = (app_inverter_day_window_t){.enabled=cJSON_IsTrue(on), .start_minute=start, .end_minute=end};
    }
    return true;
}

static bool peer_id_valid(const char *id)
{
    if (!id || strlen(id) != APP_SLAVE_PRESENCE_ID_SIZE - 1 || strncmp(id,"ble-",4) ||
        (id[4] != '0' && id[4] != '1') || id[5] != '-') return false;
    return strspn(id + 6, "0123456789abcdef") == 12;
}

static bool read_register_overrides(const cJSON *values, app_inverter_config_t *config)
{
    if (!values) return true;
    if (!cJSON_IsObject(values)) return false;
    size_t count = 0;
    const app_inverter_register_t *schema = app_inverter_registers(config->profile, &count);
    uint32_t seen = 0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, values) {
        size_t i = 0;
        while (i < count && strcmp(entry->string, schema[i].key)) ++i;
        if (i >= count || i >= APP_INVERTER_REGISTER_MAX) return false;
        uint32_t bit = UINT32_C(1) << i;
        int address;
        if ((seen & bit) || !integer(entry, 0, 65536 - schema[i].words, &address))
            return false;
        seen |= bit;
        if (address != schema[i].address) {
            config->registers.mask |= bit;
            config->registers.address[i] = (uint16_t)address;
        }
    }
    return true;
}

static bool read_form(const cJSON *o, app_inverter_settings_t *s)
{
    if (!cJSON_IsObject(o)) return false;
    app_inverter_config_t *c = &s->driver;
#define STR(key, field, required) string_field(o, key, c->field, sizeof(c->field), required)
    if (!STR("profile",profile,true) || !app_inverter_profile(c->profile) || !STR("host",host,true) ||
        !STR("device",device,false) || !STR("user",user,false) || !STR("secret",secret,false) ||
        !STR("key",key,false) || !string_field(o,"trustedPem",s->pem,sizeof(s->pem),false)) return false;
#undef STR
    if (!normalize_host(c)) return false;
    int port, unit;
    if (!integer(ITEM(o,"port"), 1, 65535, &port) || !integer(ITEM(o,"unit"), 0, 255, &unit)) return false;
    c->unit = unit; c->port = port;
    const cJSON *bat = ITEM(o,"battery"), *lat = ITEM(o,"latitude"), *lon = ITEM(o,"longitude");
    if (!cJSON_IsBool(bat) || !cJSON_IsNumber(lat) || !isfinite(lat->valuedouble) || fabs(lat->valuedouble)>90 ||
        !cJSON_IsNumber(lon) || !isfinite(lon->valuedouble) || fabs(lon->valuedouble)>180) return false;
    c->battery = cJSON_IsTrue(bat);
    s->latitude = lat->valuedouble; s->longitude = lon->valuedouble;
    s->master.touch_wake_seconds = 30;
    if (ITEM(o,"schedule") && !read_schedule(ITEM(o,"schedule"), &s->master)) return false;
    const cJSON *peers = ITEM(o,"slaveSchedules"), *entry;
    if (peers && !cJSON_IsObject(peers)) return false;
    unsigned i = 0;
    cJSON_ArrayForEach(entry, peers) {
        if (i == APP_SLAVE_PRESENCE_MAX_PEERS || !peer_id_valid(entry->string) ||
            !read_schedule(entry, &s->peers[i].schedule)) return false;
        strcpy(s->peers[i++].id, entry->string);
    }
    s->version = 2;
    return read_register_overrides(ITEM(o,"registerOverrides"), c);
}

static bool same_endpoint(const app_inverter_config_t *a, const app_inverter_config_t *b)
{
    return !strcmp(a->profile,b->profile) && !strcmp(a->host,b->host) && a->port==b->port &&
           !strcmp(a->device,b->device) && a->unit==b->unit && !strcmp(a->user,b->user);
}

static bool credentials_ready(const app_inverter_config_t *c)
{
    if (!strcmp(c->profile,"victron_mqtt"))
        return strlen(c->device)==12 && strspn(c->device,"0123456789abcdefABCDEF")==12;
    if (!strcmp(c->profile,"deye_v5") || !strcmp(c->profile,"sofar_v5")) return c->device[0];
    if (!strcmp(c->profile,"solplanet_http") || !strcmp(c->profile,"solplanet_https")) return c->device[0];
    if (!strcmp(c->profile,"huawei_cloud")) return c->device[0] && c->user[0] && c->secret[0];
    if (!strcmp(c->profile,"fronius_cloud")) return c->device[0] && c->key[0] && c->secret[0];
    if (!strcmp(c->profile,"solax_cloud")) return c->device[0] && c->secret[0];
    if (!strcmp(c->profile,"solax_http")) return c->secret[0];
    if (!strcmp(c->profile,"solplanet_cloud")) return c->key[0] && c->user[0] && c->secret[0];
    return true;
}

esp_err_t app_inverter_settings_save(const cJSON *object)
{
    if (!app_inverter_settings_open()) return ESP_ERR_INVALID_STATE;
    app_inverter_settings_t *candidate = ps_alloc(sizeof(*candidate));
    if (!candidate) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (!read_form(object, candidate)) goto done;
    lock();
    const cJSON *expected = ITEM(object,"revision");
    if (expected && (!cJSON_IsNumber(expected) || expected->valuedouble != revision)) {
        err = ESP_ERR_INVALID_STATE;
        app_inverter_settings_unlock();
        goto done;
    }
    bool same = same_endpoint(&active->driver, &candidate->driver);
    /* A régebbi webes űrlap nem küldi ezt a kulcsot. Azonos profilnál
     * megőrizzük a címeket; az explicit üres objektum visszaállítja a gyáriakat. */
    if (!ITEM(object,"registerOverrides") && !strcmp(candidate->driver.profile, active->driver.profile))
        candidate->driver.registers = active->driver.registers;
    const cJSON *rebind_field = ITEM(object,"rebindDevice");
    bool rebind = cJSON_IsTrue(rebind_field);
    struct in_addr ip;
    if ((rebind_field && !cJSON_IsBool(rebind_field)) || (rebind &&
        (!same || app_inverter_profile(candidate->driver.profile)->seconds != 5 ||
         inet_pton(AF_INET,candidate->driver.host,&ip) != 1))) {
        app_inverter_settings_unlock();
        goto done;
    }
    if (same) {
        if (!candidate->driver.secret[0]) strcpy(candidate->driver.secret, active->driver.secret);
        if (!candidate->driver.key[0]) strcpy(candidate->driver.key, active->driver.key);
        if (!ITEM(object,"trustedPem")) strcpy(candidate->pem, active->pem);
    }
    if (credentials_ready(&candidate->driver)) {
        nvs_handle_t h;
        err = nvs_open("inverter_v4", NVS_READWRITE, &h);
        if (err == ESP_OK) {
            size_t size = offsetof(app_inverter_settings_t, pem) + strlen(candidate->pem) + 1;
            err = nvs_set_blob(h, "profile1", candidate, size);
            if (err == ESP_OK && rebind) err = app_inverter_identity_forget(h);
            if (err == ESP_OK) err = nvs_commit(h);
            nvs_close(h);
        }
        if (err == ESP_OK) {
            if (!same || rebind || active->driver.battery != candidate->driver.battery ||
                memcmp(&active->driver.registers, &candidate->driver.registers,
                       sizeof(candidate->driver.registers)) != 0) {
                source_revision++;
                last_measurement = 0;
                active_ipv4[0] = 0;
            }
            app_inverter_settings_t *old = active;
            active = candidate;
            candidate = old;
            revision++;
            status_text = "Első lekérdezésre vár";
        }
    }
    app_inverter_settings_unlock();
done:
    free(candidate);
    return err;
}

bool app_inverter_settings_snapshot(app_inverter_settings_t *out, uint32_t *generation, uint32_t *source)
{
    if (!atomic_load(&mux)) return false;
    lock();
    bool ok = active && active->driver.profile[0];
    if (ok) {
        *out = *active;
        out->driver.trusted_pem = out->pem[0] ? out->pem : NULL;
        *generation = revision;
        *source = source_revision;
    }
    app_inverter_settings_unlock();
    return ok;
}

bool app_inverter_settings_lock_version(uint32_t generation)
{
    if (!atomic_load(&mux)) return false;
    lock();
    if (active && revision == generation) return true;
    app_inverter_settings_unlock();
    return false;
}

bool app_inverter_settings_current(uint32_t generation)
{
    if (!app_inverter_settings_lock_version(generation)) return false;
    app_inverter_settings_unlock();
    return true;
}

bool app_inverter_settings_coords(double *lat, double *lon, uint32_t *generation)
{
    if (!atomic_load(&mux)) return false;
    lock();
    bool ok = active && active->driver.profile[0];
    if (ok) { *lat = active->latitude; *lon = active->longitude; *generation = revision; }
    app_inverter_settings_unlock();
    return ok;
}

bool app_inverter_settings_ready(void)
{
    double lat, lon;
    uint32_t rev;
    return app_inverter_settings_coords(&lat, &lon, &rev);
}

bool app_inverter_display_schedule(const char *id, app_inverter_schedule_t *out)
{
    if (!out || !atomic_load(&mux)) return false;
    lock();
    bool ok = active && active->driver.profile[0];
    if (ok) {
        *out = active->master;
        for (unsigned i = 0; id && i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
            if (!strcmp(active->peers[i].id,id)) { *out = active->peers[i].schedule; break; }
    }
    app_inverter_settings_unlock();
    return ok;
}

void app_inverter_report_ipv4_locked(const char *address)
{
    snprintf(active_ipv4,sizeof(active_ipv4),"%s",address);
}
void app_inverter_report(uint32_t generation, const char *status, int64_t measured, bool read_failed)
{
    if (app_inverter_settings_lock_version(generation)) {
        if (read_failed) app_state_set_inverter_read_failed(true);
        status_text = status;
        if (measured > last_measurement) last_measurement = measured;
        app_inverter_settings_unlock();
    }
}

static cJSON *schedule_json(const app_inverter_schedule_t *s)
{
    cJSON *o = cJSON_CreateObject(), *days = cJSON_AddArrayToObject(o,"days");
    if (!days || !cJSON_AddNumberToObject(o,"touchWakeSeconds",s->touch_wake_seconds)) goto fail;
    for (unsigned i=0; i<7; ++i) {
        cJSON *d = cJSON_CreateObject();
        if (!d || !cJSON_AddItemToArray(days,d)) { cJSON_Delete(d); goto fail; }
        if (!cJSON_AddBoolToObject(d,"enabled",s->days[i].enabled) ||
            !cJSON_AddNumberToObject(d,"start",s->days[i].start_minute) ||
            !cJSON_AddNumberToObject(d,"end",s->days[i].end_minute)) goto fail;
    }
    return o;
fail:
    cJSON_Delete(o);
    return NULL;
}

/* Az állapotfrissítéshez nem készítünk másolatot a PEM-ről, a gyári profilokról
 * és a heti időprogramokról. A teljes űrlap ugyanazokat az állapotmezőket kapja. */
static cJSON *web_settings(bool status_only)
{
    if (!app_inverter_settings_open()) return NULL;
    cJSON *o = cJSON_CreateObject();
    lock();
    const app_inverter_config_t *c = &active->driver;
#define ADD(f,k,v) if (!cJSON_Add##f##ToObject(o,k,v)) goto fail
    ADD(String,"profile",c->profile);
    ADD(String,"syncStatus",status_text);
    if (active_ipv4[0]) { ADD(String,"activeIpv4",active_ipv4); }
    if (last_measurement) { ADD(Number,"measurementAgeSeconds",fmax(0,difftime(time(NULL),last_measurement))); }
    if (status_only) {
        app_inverter_settings_unlock();
        return o;
    }
    ADD(Number,"revision",revision);
    ADD(String,"host",c->host); ADD(String,"device",c->device);
    ADD(String,"user",c->user); ADD(Bool,"hasKey",c->key[0]); ADD(Bool,"hasSecret",c->secret[0]);
    ADD(Number,"port",c->port); ADD(Number,"unit",c->unit); ADD(Bool,"battery",c->battery);
    ADD(String,"trustedPem",active->pem);
    if (c->profile[0]) { ADD(Number,"latitude",active->latitude); ADD(Number,"longitude",active->longitude); }
#undef ADD
    cJSON *discoverable = cJSON_AddArrayToObject(o, "discoverable");
    if (!discoverable) goto fail;
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        if (!app_inverter_discovery_supported(profiles[i].id)) continue;
        cJSON *id = cJSON_CreateString(profiles[i].id);
        if (!id || !cJSON_AddItemToArray(discoverable, id)) {
            cJSON_Delete(id);
            goto fail;
        }
    }
    cJSON *registers = cJSON_AddObjectToObject(o, "registerOverrides");
    if (!registers) goto fail;
    size_t count = 0;
    const app_inverter_register_t *schema = app_inverter_registers(c->profile, &count);
    for (size_t i = 0; i < count; ++i) {
        if ((c->registers.mask & (UINT32_C(1) << i)) &&
            !cJSON_AddNumberToObject(registers, schema[i].key, c->registers.address[i])) goto fail;
    }
    cJSON *schedule = schedule_json(&active->master);
    if (!schedule || !cJSON_AddItemToObject(o,"schedule",schedule)) { cJSON_Delete(schedule); goto fail; }
    cJSON *peers = cJSON_AddObjectToObject(o,"slaveSchedules");
    if (!peers) goto fail;
    for (unsigned i=0; i<APP_SLAVE_PRESENCE_MAX_PEERS; ++i) {
        if (!active->peers[i].id[0]) continue;
        schedule = schedule_json(&active->peers[i].schedule);
        if (!schedule || !cJSON_AddItemToObject(peers,active->peers[i].id,schedule)) { cJSON_Delete(schedule); goto fail; }
    }
    app_inverter_settings_unlock();
    return o;
fail:
    app_inverter_settings_unlock();
    cJSON_Delete(o);
    return NULL;
}

cJSON *app_inverter_settings_get(void)
{
    return web_settings(false);
}

cJSON *app_inverter_status_get(void)
{
    return web_settings(true);
}
