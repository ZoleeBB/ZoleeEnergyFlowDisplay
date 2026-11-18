/* Az IDF HTTP eseményei több részben érkeznek. A valódi válaszgyűjtő és
 * JSON-olvasó fut, csak az átvitel és a PSRAM-foglalás helyettesített. */
#include "app_inverter_net.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct http_client {
    esp_http_client_config_t config;
    esp_http_client_method_t method;
    const char *body, *request_key, *request_value;
};
static struct http_client client;
static char *payload;
static size_t payload_len, fragment;
static int received_status, extra_len, opened, performed, closed;
static bool complete, fail_alloc;
static esp_err_t perform_result;
static char *response_key, *response_value;

esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }
void *heap_caps_realloc(void *ptr, size_t bytes, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) && bytes <= 48 * 1024 + 1);
    return fail_alloc ? NULL : realloc(ptr, bytes);
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    assert(cfg->disable_auto_redirect && cfg->timeout_ms > 0 && cfg->event_handler);
    client = (struct http_client){.config = *cfg};
    opened++;
    return &client;
}
esp_err_t esp_http_client_set_method(esp_http_client_handle_t c, esp_http_client_method_t method)
{
    c->method = method;
    return ESP_OK;
}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t c, const char *body, int length)
{
    assert(length == (int)strlen(body));
    c->body = body;
    return ESP_OK;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value)
{
    c->request_key = key;
    c->request_value = value;
    return ESP_OK;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t c)
{
    ++performed;
    esp_http_client_event_t event = {.user_data = c->config.user_data};
    if (response_key) {
        event.event_id = HTTP_EVENT_ON_HEADER;
        event.header_key = response_key;
        event.header_value = response_value;
        if (c->config.event_handler(&event) != ESP_OK) return ESP_FAIL;
    }
    event.event_id = HTTP_EVENT_ON_DATA;
    for (size_t at = 0; at < payload_len;) {
        size_t n = payload_len - at;
        if (n > fragment) n = fragment;
        event.data = payload + at;
        event.data_len = (int)n;
        if (c->config.event_handler(&event) != ESP_OK) return ESP_FAIL;
        at += n;
    }
    if (extra_len) {
        event.data_len = extra_len;
        event.data = "";
        /* Akkor is a fogadó saját hibajelzője érvényesüljön, ha az IDF
         * teljesnek jelöli az átvitelt és eldobja a visszahívás hibakódját. */
        assert(c->config.event_handler(&event) != ESP_OK);
    }
    return perform_result;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { assert(c == &client); return received_status; }
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { assert(c == &client); return complete; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { assert(c == &client); ++closed; return ESP_OK; }

static void response(char *json)
{
    payload = json;
    payload_len = strlen(json);
    fragment = 3;
    received_status = 200;
    complete = true;
    fail_alloc = false;
    extra_len = 0;
    perform_result = ESP_OK;
    response_key = "xSrF-tOkEn";
    response_value = "captured";
}
static cJSON *get(char *token, size_t size)
{
    return app_inverter_json_request("https://inverter.test/status", "GET", NULL, NULL, 0, token, size, NULL);
}
static void rejected(void)
{
    char token[32] = "previous";
    cJSON *result = get(token, sizeof(token));
    assert(!result && !token[0] && closed == opened);
}
int main(void)
{
    char token[32];
    response("{\"current\":{}}");
    cJSON *json = get(token, sizeof(token));
    assert(cJSON_IsObject(cJSON_GetObjectItem(json, "current")) && !strcmp(token, "captured"));
    assert(client.method == HTTP_METHOD_GET && client.config.crt_bundle_attach == esp_crt_bundle_attach);
    assert(!client.config.cert_pem && !client.body);
    cJSON_Delete(json);

    response("{\"current\":{\"time\":123,\"temperature_2m\":-2.5,\"wind_speed_10m\":0,\"relative_humidity_2m\":85}}");
    response_key = "SeT-cOOkIE"; response_value = "XSRF-TOKEN=from-cookie; Path=/; Secure";
    const char *const headers[][2] = {{"Content-Type", "application/json"}};
    json = app_inverter_json_request("https://collector.test/query", "POST", "{}", headers, 1, token, sizeof(token), "TEST CA");
    assert(!strcmp(token,"from-cookie") && client.method == HTTP_METHOD_POST && !strcmp(client.body,"{}"));
    assert(!strcmp(client.request_key,"Content-Type") && !strcmp(client.request_value,"application/json"));
    assert(!client.config.crt_bundle_attach && !strcmp(client.config.cert_pem,"TEST CA"));
    assert(cJSON_GetObjectItem(cJSON_GetObjectItem(json,"current"),"temperature_2m")->valuedouble == -2.5);
    cJSON_Delete(json);

    response("{\"text\":\"escaped quote \\\" and braces [[[[[[[[[[[[[[[[[[[[[\"}");
    response_key = "Set-Cookie"; response_value = "unrelated=none; XSRF-TOKEN=wrong";
    json = app_inverter_json_request("http://collector.test/", "GET", NULL, NULL, 0, token, sizeof(token), NULL);
    assert(json && !*token && !client.config.cert_pem && !client.config.crt_bundle_attach);
    cJSON_Delete(json);
    response("{}"); json = get(token, 4); assert(json && !*token); cJSON_Delete(json);
    for (int status = 301; status <= 308; ++status) { response("{}"); received_status = status; rejected(); }
    response("{}"); received_status = 401; rejected();
    response("{}"); complete = false; rejected();
    response("{}"); perform_result = ESP_FAIL; rejected();
    response("{}"); fail_alloc = true; rejected();
    response(""); rejected();
    response("{"); rejected();
    response("{} unexpected"); rejected();
    response("{}"); extra_len = -1; rejected();
    response("{}"); extra_len = 48 * 1024; rejected();
    char nul[] = {'{', '}', '\0', 'x'};
    response(nul); payload_len = sizeof(nul); rejected();
    response("{\"field\\u0000suffix\":0}"); rejected();
    response("{\"text\":\"\\\\u0000\"}"); json = get(token, sizeof(token)); assert(json); cJSON_Delete(json);
    char nested[48];
    for (int depth = 20; depth <= 21; ++depth) {
        memset(nested, '[', depth); memset(nested + depth, ']', depth); nested[depth*2] = 0;
        response(nested); json = get(token, sizeof(token));
        if (depth == 20) { assert(json); cJSON_Delete(json); }
        else assert(!json && !*token);
    }
    size_t limit = 48 * 1024;
    char *large = malloc(limit + 2); assert(large);
    memset(large, ' ', limit + 1); large[0] = '{'; large[1] = '}'; large[limit] = 0;
    response(large); fragment = 511; json = get(token, sizeof(token)); assert(json); cJSON_Delete(json);
    large[limit] = ' '; large[limit+1] = 0; response(large); fragment = 511; rejected();
    free(large);

    response("{}");
    int before = performed;
    const char *const bad[][2] = {{"X-Token","abc\r\nextra"}};
    assert(!app_inverter_json_request("https://inverter.test", "GET", NULL, bad, 1, token, sizeof(token), NULL));
    assert(!app_inverter_json_request("file:///dev/null", "GET", NULL, NULL, 0, token, sizeof(token), NULL));
    assert(!app_inverter_json_request("https://inverter.test", "DELETE", NULL, NULL, 0, token, sizeof(token), NULL));
    assert(!app_inverter_json_request("https://inverter.test", "GET", NULL, NULL, 1, token, sizeof(token), NULL));
    assert(performed == before && opened == closed);
    puts("PASS: real inverter HTTP collector, TLS trust selection, XSRF headers/cookies, byte limits, depth, NUL and partial/redirect rejection");
}
