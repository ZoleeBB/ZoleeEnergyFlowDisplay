/* A 4B teljes első beállítása: NVS-, szerverindítási és részleges regisztrációs
 * hiba után ugyanaz a kapcsolat és egyszer létrehozott választó marad meg. */
#include <assert.h>
#include <stdlib.h>
#include "../firmware/slave-waveshare-4b/main/slave_4b_setup.c"

static unsigned timers, joins, writes, clock_reads, starts, stops, retries, saved, restarted;
static bool serving, accepted, web, timed;
static const httpd_uri_t *save;
static app_config_t cfg;

esp_err_t bsp_display_lock(int timeout) { assert(timeout > 0); return 1; }
void bsp_display_unlock(void) {}
lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), unsigned interval, void *arg)
{ assert(cb && interval && !arg); ++timers; return (lv_timer_t *)&timers; }
lv_obj_t *lv_screen_active(void) { abort(); }
void app_setup_wifi_snapshot(app_setup_wifi_state_t *out) { (void)out; abort(); }
bool app_setup_ui_update(lv_obj_t *p, int w, int h, const app_setup_wifi_state_t *v)
{ (void)p; (void)w; (void)h; (void)v; abort(); }
esp_err_t app_setup_wifi_provision(app_config_t *c)
{ assert(c == &cfg && !joins++); strcpy(c->wifi.ssid, "Otthon"); return ESP_OK; }
esp_err_t app_boot_config_save_setup_wifi(const app_config_t *c)
{ assert(c == &cfg && !strcmp(c->wifi.ssid,"Otthon")); return ++writes == 1 ? ESP_FAIL : ESP_OK; }
esp_err_t app_time_init(const app_config_t *c) { assert(writes == 2 && c == &cfg && timed); return ESP_OK; }
esp_err_t app_time_sync_once(void) { return ++clock_reads == 1 ? ESP_FAIL : ESP_OK; }
void app_ota_wait_for_validation(void) { assert(clock_reads == 2 && !starts); accepted = true; }
void app_setup_wifi_set_phase(app_setup_phase_t phase)
{
    if (phase == APP_SETUP_WAIT_TIME) { assert(writes == 2 && !timed); timed = true; }
    else { assert(phase == APP_SETUP_WEB && accepted && save && !web); web = true; }
}
uint32_t esp_random(void) { return 0xacedf00d; }
void app_events_add(app_event_level_t level, const char *fmt, ...) { (void)fmt; assert(level == APP_EVENT_WARNING); }
esp_err_t httpd_start(httpd_handle_t *server, const httpd_config_t *settings)
{
    assert(accepted && !serving && settings->stack_size && settings->lru_purge_enable);
    if (++starts == 1) return ESP_ERR_NO_MEM;
    serving = true;
    *server = &serving;
    return ESP_OK;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route)
{
    assert(server == &serving && serving && route->user_ctx && route->handler);
    if (route->method == HTTP_POST) {
        if (starts == 2) return ESP_ERR_NO_MEM;
        save = route;
    }
    return ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t server)
{ assert(server == &serving && serving); ++stops; serving = false; return ESP_OK; }
int httpd_req_recv(httpd_req_t *req, char *out, size_t size)
{ (void)req; assert(size == 1); *out = 'x'; return 1; }
esp_err_t httpd_query_key_value(const char *b,const char *key,char *out,size_t len)
{
    assert(!strcmp(b, "x"));
    snprintf(out, len, "%s", !strcmp(key,"mode") ? "slave" : "acedf00dacedf00d");
    return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *type) { (void)r; (void)type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *v) { (void)r; (void)key; (void)v; abort(); }
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r,const char *chunk) { (void)r; (void)chunk; abort(); }
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *text) { (void)r; assert(text && saved); return ESP_OK; }
esp_err_t httpd_resp_send_err(httpd_req_t *r,int code,const char *text) { (void)r; (void)code; (void)text; abort(); }
esp_err_t app_boot_config_complete(const char *json,size_t len,app_config_t *out,char *error,size_t cap)
{
    (void)out; (void)error; (void)cap;
    assert(len && strstr(json, "\"DataReceptMode\":\"slave\"") && strstr(json, "\"ssid\":\"Otthon\""));
    ++saved;
    return ESP_OK;
}
void vTaskDelay(TickType_t ticks)
{
    if (ticks == pdMS_TO_TICKS(5000)) { assert(++retries <= 3); return; }
    if (ticks == pdMS_TO_TICKS(1000)) { assert(clock_reads == 1); return; }
    assert(ticks == pdMS_TO_TICKS(200) && web && !saved);
    httpd_req_t req = {.content_len=1, .user_ctx=save->user_ctx};
    assert(save->handler(&req) == ESP_OK);
}
void esp_restart(void) { assert(saved && !serving); ++restarted; }
int main(void)
{
    assert(slave_4b_setup_run(&cfg) == ESP_OK);
    assert(timers == 1 && joins == 1 && retries == 3 && starts == 3 && stops == 2 && restarted == 1);
    return 0;
}
