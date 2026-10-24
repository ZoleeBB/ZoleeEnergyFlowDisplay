/* A valódi HTTP-kezelők: hitelesítés, korlátok, darabolás és mód szerinti elérés. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "app_loxone.h"
#include "esp_http_server.h"
#include "app_slave_presence_service.h"
#include "app_templates.h"
#include "app_intercom_audio.h"
#include "app_ringtones.h"
#include "app_storage.h"
#include "app_intercom_control.h"
esp_err_t app_storage_lock(void) { return ESP_OK; }
bool app_storage_is_ok(void) { return true; }
const char *app_templates_root(void) { return "/tmp"; }
void app_storage_unlock(void) {}
cJSON *app_ringtones_list(void) { return cJSON_CreateArray(); }
esp_err_t app_ringtones_install(const char *path, char error[160]) { (void)path; (void)error; return ESP_OK; }
static bool ringtone_query, ringtone_file = true, task_fail, async_fail;
static unsigned media_loads, media_sends, async_complete, deleted_tasks, queued;
static void (*media_worker[4])(void *);
static void *media_arguments[4];
uint8_t *app_ringtones_load(const char *name, size_t *size) {
    assert(!strcmp(name, "tubular-chime.wav"));
    media_loads++;
    if (!ringtone_file) return NULL;
    *size = 64;
    return calloc(1, *size);
}
bool app_intercom_control_media_key(const char *key) { (void)key; return false; }
bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out) { memset(out, 0, sizeof(*out)); return true; }
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *q, size_t n) {
    (void)r; snprintf(q, n, "name=tubular-chime.wav"); return ringtone_query ? ESP_OK : ESP_FAIL;
}
esp_err_t httpd_query_key_value(const char *q, const char *k, char *v, size_t n) {
    (void)q; (void)k; snprintf(v, n, "tubular-chime.wav"); return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *r, const char *b, int n) {
    (void)r; assert(b && n == 64); media_sends++; return ESP_OK;
}
esp_err_t httpd_req_async_handler_begin(httpd_req_t *request, httpd_req_t **copy) {
    if (async_fail) return ESP_FAIL;
    *copy = malloc(sizeof(**copy)); assert(*copy); **copy = *request; return ESP_OK;
}
esp_err_t httpd_req_async_handler_complete(httpd_req_t *request) {
    async_complete++; free(request); return ESP_OK;
}
int xTaskCreate(void (*function)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle) {
    (void)name; (void)handle; assert(stack == 4096 && priority == 3);
    if (task_fail) return 0;
    assert(queued < 4); media_worker[queued] = function; media_arguments[queued++] = arg; return 1;
}
void vTaskDelete(void *handle) { assert(!handle); deleted_tasks++; }
static bool home_resources_installed;
bool app_templates_home_info(app_template_info_t *info, unsigned *generation)
{
    if (generation) *generation = 1;
    if (info) info->version = 3;
    return home_resources_installed;
}
static app_config_t s_config;
static bool authorized = true, header_ok = true, broken;
static int status_code, requested, registered;
static const char *input;
static size_t position;
static unsigned receive_timeouts;
static char response[4096];
static bool authorize(httpd_req_t *req) { (void)req; if (!authorized) status_code=401; return authorized; }
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s) { (void)r;(void)s;return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v) { (void)r;assert(!strcmp(k,"Cache-Control")&&!strcmp(v,"no-store"));return ESP_OK; }
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s) { (void)r;assert(sscanf(s,"%d",&status_code)==1);return ESP_OK; }
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s) { (void)r;snprintf(response,sizeof(response),"%s",s);return ESP_OK; }
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *data, int size)
{
    (void)req;
    assert(size >= 0);
    if (data && size > 0) {
        size_t used = strlen(response);
        size_t count = (size_t)size;
        if (count > sizeof(response) - used - 1) count = sizeof(response) - used - 1;
        memcpy(response + used, data, count);
        response[used + count] = 0;
    }
    return ESP_OK;
}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int code,const char *s) { status_code=code;return httpd_resp_sendstr(r,s); }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t n) { (void)r;if(!strcmp(key,"X-Zolee-Media"))return ESP_FAIL;assert(!strcmp(key,"X-Zolee-Loxone")&&n>=2);if(!header_ok)return ESP_FAIL;strcpy(out,"1");return ESP_OK; }
int httpd_req_recv(httpd_req_t *r,char *out,size_t n) { (void)r;if(receive_timeouts){receive_timeouts--;return HTTPD_SOCK_ERR_TIMEOUT;}if(broken)return -1;if(n>3)n=3;memcpy(out,input+position,n);position+=n;return (int)n; }
esp_err_t httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *route) { (void)server;assert(route->handler);registered++;return ESP_OK; }
cJSON *app_loxone_web_state(void) { return cJSON_CreateObject(); }
esp_err_t app_loxone_request(const cJSON *root,bool save) { (void)root;(void)save;return ESP_OK; }
cJSON *app_loxone_home_web_state(void) { return cJSON_Parse("{\"schema\":1,\"consumers\":[],\"cards\":[]}"); }
esp_err_t app_loxone_home_request(const cJSON *root,char error[LX_HOME_ERROR_SIZE]) {
    requested++;
    lx_home_config_t *home=calloc(1,sizeof(*home));assert(home);
    bool ok=root&&lx_home_config_parse(root,home,error);free(home);
    return ok?ESP_OK:ESP_ERR_INVALID_ARG;
}
static app_slave_presence_snapshot_t slave_state;
static bool slave_ready = true, stale_update;
static int slave_updates;
bool app_slave_presence_service_snapshot(app_slave_presence_snapshot_t *out)
{
    *out = slave_state;
    return slave_ready;
}
bool app_slave_presence_service_update(const app_slave_identity_t *identity,
                                       const app_slave_presence_profile_t *profile,
                                       uint32_t expected)
{
    if (stale_update || expected != slave_state.revision) return false;
    int index = app_slave_presence_find(&slave_state.table, identity);
    bool changed;
    if (index < 0 || !app_slave_presence_update(&slave_state.table, (unsigned)index, profile, &changed)) return false;
    slave_updates++;
    if (changed) slave_state.revision++;
    return true;
}
#include "../main/app_loxone_web.inc"
static void post(const char *body)
{
    input=body;position=0;status_code=200;response[0]=0;
    httpd_req_t request={.method=HTTP_POST,.uri="/api/loxone/home",.content_len=strlen(body)};
    loxone_home_post(&request);
}
static void slave_post_size(const char *body, size_t size)
{
    input = body;
    position = 0;
    status_code = 200;
    response[0] = 0;
    httpd_req_t request = {.method=HTTP_POST, .uri="/api/loxone/slaves", .content_len=size};
    slave_presence_post(&request);
}
static void slave_post(const char *body) { slave_post_size(body, strlen(body)); }
static void slave_get(void)
{
    status_code = 200;
    response[0] = 0;
    httpd_req_t request = {.method=HTTP_GET, .uri="/api/loxone/slaves"};
    slave_presence_get(&request);
}
static int allocation, fail_at;
static void *fail_malloc(size_t size) { return ++allocation == fail_at ? NULL : malloc(size); }
static void slave_tests(void)
{
    const char *body = "{\"schema\":1,\"revision\":4294967295,\"id\":\"ble-0-060504030201\",\"profile\":{\"name\":\"Nappali <img>\",\"presence_uuid\":\"11223344-5566-7788-99aabbccddeeff00\",\"timeout_seconds\":42,\"blank_at_night\":true}}";
    s_config.data_seed_enabled = true;
    slave_state.revision = UINT32_MAX;
    slave_state.saved_revision = 123;
    slave_state.save_error = ESP_FAIL;
    for (unsigned i = 0; i < 3; ++i)
    {
        app_slave_identity_t identity = {.type=0, .address={1,2,3,4,5,(uint8_t)(6+i)}};
        assert(app_slave_presence_register(&slave_state.table, &identity, "Slave") == (int)i);
    }
    slave_get();
    assert(status_code == 200);
    cJSON *json = cJSON_Parse(response);
    assert(json && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(json,"profiles")) == 3);
    assert(cJSON_GetObjectItemCaseSensitive(json,"revision")->valuedouble == UINT32_MAX);
    assert(cJSON_GetObjectItemCaseSensitive(json,"saved_revision")->valuedouble == 123);
    assert(cJSON_GetObjectItemCaseSensitive(json,"save_error")->valuedouble == ESP_FAIL);
    assert(!strstr(response,"\"video_password\"") && !strstr(response,"\"password\"") && !strstr(response,"counter") && !strstr(response,"address"));
    cJSON_Delete(json);
    authorized = false;
    slave_get(); assert(status_code == 401);
    slave_post(body); assert(status_code == 401 && !slave_updates);
    authorized = true;
    header_ok = false;
    slave_post(body); assert(status_code == 400 && !slave_updates);
    header_ok = true;
    broken = true;
    slave_post(body); assert(status_code == 400 && !slave_updates);
    broken = false;
    s_config.data_seed_enabled = false;
    slave_post(body); assert(status_code == 503 && !slave_updates);
    slave_get(); assert(status_code == 200 && strstr(response,"\"enabled\":false") && strstr(response,"\"profiles\":[]"));
    s_config.data_seed_enabled = true;
    slave_ready = false;
    slave_post(body); assert(status_code == 503 && !slave_updates);
    slave_ready = true;
    stale_update = true;
    slave_post(body); assert(status_code == 409 && !slave_updates);
    stale_update = false;
    /* A hibás kérés nem módosíthat egyetlen profilt sem. */
    const char *invalid[] = {
        "{}", "[]", "{\"schema\":1}",
        "{\"schema\":1,\"revision\":1,\"id\":\"ble-0-060504030201\",\"profile\":null}",
        "{\"schema\":1,\"schema\":1,\"id\":\"ble-0-060504030201\",\"profile\":{}}"
    };
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
    {
        slave_post(invalid[i]);
        assert(status_code == 400 && !slave_updates);
    }
    const char *fields[] = {"schema", "revision", "id", "name", "presence_uuid", "timeout_seconds", "blank_at_night"};
    const char *values[] = {"2", "1.5", "\"ble-0-990504030201\"", "\"\\u0000hidden\"", "\"wrong\"", "86401", "1"};
    for (unsigned i = 0; i < 7; ++i)
    {
        json = cJSON_Parse(body);
        cJSON *parent = i < 3 ? json : cJSON_GetObjectItemCaseSensitive(json,"profile");
        assert(cJSON_ReplaceItemInObjectCaseSensitive(parent,fields[i],cJSON_Parse(values[i])));
        char *bad = cJSON_PrintUnformatted(json);
        slave_post(bad); assert(status_code == 400 && !slave_updates);
        free(bad); cJSON_Delete(json);
    }
    char altered[4096];
    snprintf(altered,sizeof(altered),"%s trailing",body);
    slave_post(altered); assert(status_code == 400 && !slave_updates);
    memcpy(altered,body,strlen(body)+1); altered[20] = 0;
    slave_post_size(altered,strlen(body)); assert(status_code == 400 && !slave_updates);
    memset(altered,' ',sizeof(altered));
    slave_post_size(altered,2049); assert(status_code == 400 && !slave_updates);
    /* A valódi U+0000 tiltott, a két backslash utáni u0000 szöveg megengedett. */
    assert(!slave_json_text("\\u0000",6));
    assert(slave_json_text("\\\\u0000",7));
    slave_state.revision = 7;
    slave_post(body); assert(status_code == 409 && !slave_updates);
    slave_state.revision = UINT32_MAX;
    slave_post(body); assert(status_code == 202 && slave_updates == 1 && strstr(response,"accepted"));
    assert(!strcmp(slave_state.table.peers[0].profile.name,"Nappali <img>"));
    assert(slave_state.table.peers[0].profile.timeout_seconds == 42 && slave_state.table.peers[0].profile.blank_at_night);
    assert(!strcmp(slave_state.table.peers[1].profile.name,"Slave"));
    assert(slave_state.saved_revision == 123); /* A HTTP nem ígér NVS-commitot. */
    /* Minden cJSON-allokáció hibája tiszta 500-as válasz legyen. */
    cJSON_Hooks hooks = {.malloc_fn=fail_malloc, .free_fn=free};
    cJSON_InitHooks(&hooks);
    bool reached_success = false;
    for (fail_at = 1; fail_at < 200; ++fail_at)
    {
        allocation = 0;
        slave_get();
        if (status_code == 200) { reached_success = true; break; }
        assert(status_code == 500);
    }
    cJSON_InitHooks(NULL);
    assert(reached_success);
    puts("PASS: actual Slave HTTP, three stable IDs, auth/header/mode gate, fragmented strict bounded JSON, uint32 revision/conflict, validation, accepted vs durable and OOM sweep");
}
int main(void)
{
    s_config.data_recept_mode=APP_DATA_HTTP;assert(loxone_register(NULL)==ESP_OK&&!registered);
    s_config.data_recept_mode=APP_DATA_SLAVE;assert(loxone_register(NULL)==ESP_OK&&!registered);
    s_config.data_recept_mode=APP_DATA_MQTT;assert(loxone_register(NULL)==ESP_OK&&!registered);
    s_config.data_recept_mode=APP_DATA_LOXONE;assert(loxone_register(NULL)==ESP_OK&&registered==10);
    authorized=false;post("{\"schema\":1}");assert(status_code==401&&!requested);
    authorized=true;header_ok=false;post("{\"schema\":1}");assert(status_code==400&&!requested);
    header_ok=true;broken=true;post("{\"schema\":1}");assert(status_code==400&&!requested);broken=false;
    post("{\"schema\":3}");assert(status_code==400&&requested==1&&strstr(response,"schema"));
    post("{\"schema\":1} trailing");assert(status_code==400);
    post("{\"schema\":1,\"consumers\":[],\"cards\":[]}");assert(status_code==202&&strstr(response,"accepted"));
    char *large=malloc(LX_HOME_JSON_MAX+2);memset(large,' ',LX_HOME_JSON_MAX+1);large[LX_HOME_JSON_MAX+1]=0;int before=requested;
    post(large);assert(status_code==400&&requested==before);free(large);
    httpd_req_t request={.method=HTTP_GET,.uri="/api/loxone/home"};
    authorized=false;status_code=200;loxone_home_get(&request);assert(status_code==401);
    authorized=true;status_code=200;loxone_home_get(&request);assert(status_code==200&&strstr(response,"consumers")&&!strstr(response,"\"video_password\"") && !strstr(response,"\"password\""));
    cJSON *home_state = cJSON_Parse(response);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(home_state,"resources_installed")));
    cJSON_Delete(home_state);
    home_resources_installed = true;
    loxone_home_get(&request);
    home_state = cJSON_Parse(response);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(home_state,"resources_installed")));
    assert(cJSON_GetObjectItem(home_state,"resources_version")->valueint == 3);
    cJSON_Delete(home_state);
    slave_tests();
    char upload_path[] = "/tmp/ringtone-upload-test-XXXXXX";
    int upload_fd = mkstemp(upload_path); assert(upload_fd >= 0); close(upload_fd);
    char upload_error[160] = "";
    input = "0123456789abcdefghij"; position = 0;
    request.content_len = 20;
    receive_timeouts = 2;
    assert(receive_sd_zip(&request, upload_path, upload_error));
    FILE *uploaded = fopen(upload_path, "rb"); assert(uploaded);
    char uploaded_bytes[21] = {0}; assert(fread(uploaded_bytes, 1, 20, uploaded) == 20); fclose(uploaded);
    assert(!strcmp(uploaded_bytes, input));
    position = 0; broken = true;
    assert(!receive_sd_zip(&request, upload_path, upload_error) && strstr(upload_error, "fogadása"));
    broken = false; position = 0; receive_timeouts = 4;
    assert(!receive_sd_zip(&request, upload_path, upload_error) && receive_timeouts == 0);
    assert(!receive_sd_zip(&request, "/missing-directory/upload.zip", upload_error) && strstr(upload_error, "errno="));
    remove(upload_path); request.content_len = 0;
    ringtone_query = true;
    status_code = 200;
    unsigned completed = async_complete;
    assert(ringtone_get(&request) == ESP_OK && queued == 1);
    assert(!media_loads && !media_sends); /* A HTTP-kezelő nem vár SD-re vagy hangküldésre. */
    loxone_home_get(&request); /* Más webes kérés a függő hangküldés mellett kiszolgálható. */
    assert(status_code == 200);
    media_worker[0](media_arguments[0]); queued = 0;
    assert(media_loads == 1 && media_sends == 1 && async_complete == completed + 1 && deleted_tasks == 1);
    for (unsigned i = 0; i < 4; i++) assert(ringtone_get(&request) == ESP_OK);
    assert(queued == 4 && atomic_load(&s_ringtone_responses) == 4);
    ringtone_get(&request); assert(queued == 4 && atomic_load(&s_ringtone_responses) == 4);
    for (unsigned i = 0; i < 4; i++) media_worker[i](media_arguments[i]);
    queued = 0;
    assert(atomic_load(&s_ringtone_responses) == 0);
    task_fail = true; ringtone_get(&request); task_fail = false;
    assert(!queued && atomic_load(&s_ringtone_responses) == 0);
    async_fail = true; ringtone_get(&request); async_fail = false;
    assert(!queued && atomic_load(&s_ringtone_responses) == 0);
    ringtone_file = false; ringtone_get(&request);
    media_worker[0](media_arguments[0]); queued = 0;
    assert(status_code == 404 && atomic_load(&s_ringtone_responses) == 0);
    puts("PASS: actual async ringtone HTTP, no SD/send on server task, concurrent requests, bounded workers and balanced failure cleanup");
    puts("PASS: actual home HTTP handlers, MQTT/HTTP/Slave exclusion, authorization, header, partial receive, bounds, strict JSON, validation details and async 202");
}
