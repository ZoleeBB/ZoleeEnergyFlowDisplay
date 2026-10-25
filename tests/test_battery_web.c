/* A valódi webes végpontok és a kiadott HTML: auth, szándékos reset, hibajelzés. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_stats.h"
#include "app_events.h"
#include "esp_http_server.h"
static bool s_hu = true, allowed = true, header_ok = true, local = true;
static int code = 200, resets;
static esp_err_t reset_result = ESP_OK;
static char output[24000];
static bool authorize(httpd_req_t *req) { (void)req; if (!allowed) code = 401; return allowed; }
bool app_stats_get_battery(app_stats_battery_t *out) {
    memset(out, 0, sizeof(*out));
    out->nominal_kwh = 15;
    out->report.available = true;
    out->report.cycles = resets ? 0 : 2;
    return local;
}
esp_err_t app_stats_reset_battery(void) { if (!reset_result) resets++; return reset_result; }
void app_events_add(app_event_level_t level, const char *fmt, ...) { (void)level; (void)fmt; }
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *value) { (void)req; (void)value; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *value) { (void)req; (void)key; (void)value; return ESP_OK; }
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *value) {
    (void)req; snprintf(output, sizeof(output), "%s", value); return ESP_OK;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *value, int size) {
    (void)req; assert(size == HTTPD_RESP_USE_STRLEN);
    assert(strlen(output) + strlen(value) < sizeof(output)); strcat(output, value); return ESP_OK;
}
esp_err_t httpd_resp_send_err(httpd_req_t *req, int status, const char *value) { code = status; return httpd_resp_sendstr(req, value); }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *key, char *value, size_t size) {
    (void)req; assert(!strcmp(key, "X-Zolee-Battery")); snprintf(value, size, "1"); return header_ok ? ESP_OK : ESP_FAIL;
}
#include "../main/app_battery_web.inc"
int main(void) {
    httpd_req_t req = {0};
    allowed = false; battery_get(&req); assert(code == 401);
    battery_reset_post(&req); assert(code == 401 && !resets);
    allowed = true; header_ok = false; battery_reset_post(&req); assert(code == 400 && !resets);
    header_ok = true; req.content_len = 1; battery_reset_post(&req); assert(code == 400 && !resets);
    req.content_len = 0; reset_result = ESP_FAIL; battery_reset_post(&req); assert(code == 500 && !resets);
    reset_result = ESP_OK; battery_reset_post(&req); assert(resets == 1);
    battery_get(&req);
    cJSON *json = cJSON_Parse(output); assert(json);
    assert(cJSON_GetObjectItem(json, "cycles")->valuedouble == 0);
    assert(!cJSON_GetObjectItem(json, "capacity_kwh"));
    cJSON_Delete(json);
    local = false; battery_get(&req); json = cJSON_Parse(output);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(json, "local"))); cJSON_Delete(json);
    output[0] = 0; battery_section(&req); fputs(output, stdout);
    fputs("PASS: battery HTTP auth, explicit reset, failure response, nullable estimates and rendered section\n", stderr);
}
