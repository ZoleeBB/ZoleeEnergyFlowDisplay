/* Korlátos, átirányítást nem követő HTTP-olvasó. A válasz munkaterülete a PSRAM-ban él. */
#include "app_inverter_net.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RESPONSE_LIMIT (48U * 1024U)
typedef struct {
    char *data;
    size_t size, capacity;
    char *token;
    size_t token_size;
    bool failed;
} response_t;

static void copy_token(response_t *r, const char *value)
{
    size_t size = strcspn(value, ";\r\n");
    if (size && size < r->token_size) {
        memcpy(r->token, value, size);
        r->token[size] = 0;
    }
}

static esp_err_t collect(esp_http_client_event_t *event)
{
    response_t *r = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && r->token && event->header_key && event->header_value) {
        if (!strcasecmp(event->header_key, "XSRF-TOKEN"))
            copy_token(r, event->header_value);
        else if (!strcasecmp(event->header_key, "Set-Cookie") &&
                 !strncasecmp(event->header_value, "XSRF-TOKEN=", 11))
            copy_token(r, event->header_value + 11);
    }
    if (event->event_id != HTTP_EVENT_ON_DATA || !event->data_len || r->failed)
        return ESP_OK;
    if (event->data_len < 0 || (size_t)event->data_len > RESPONSE_LIMIT - r->size) {
        r->failed = true;
        return ESP_FAIL;
    }
    size_t needed = r->size + (size_t)event->data_len + 1;
    if (needed > r->capacity) {
        size_t capacity = r->capacity ? r->capacity * 2 : 2048;
        if (capacity < needed) capacity = needed;
        if (capacity > RESPONSE_LIMIT + 1) capacity = RESPONSE_LIMIT + 1;
        char *data = heap_caps_realloc(r->data, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!data) {
            r->failed = true;
            return ESP_ERR_NO_MEM;
        }
        r->data = data;
        r->capacity = capacity;
    }
    memcpy(r->data + r->size, event->data, event->data_len);
    r->size += event->data_len;
    r->data[r->size] = '\0';
    return ESP_OK;
}

/* A szolgáltatói válasz lehet sérült. A rekurzív JSON-parser előtt egyetlen
 * bejárás korlátozza a veremmélységet, az idézett JSON-szöveget átugorva. */
static bool nesting_allowed(const char *data, size_t length)
{
    unsigned depth = 0;
    bool string = false, escape = false;
    for (size_t i = 0; i < length; ++i) {
        char ch = data[i];
        if (!ch) return false;
        if (string) {
            if (escape) escape = false;
            else if (ch == '\\') escape = true;
            else if (ch == '"') string = false;
        } else if (ch == '"') string = true;
        else if (ch == '{' || ch == '[') {
            if (++depth > 20) return false;
        } else if (ch == '}' || ch == ']') {
            if (!depth) return false;
            --depth;
        }
    }
    return !depth && !string;
}

cJSON *app_inverter_json_request(const char *url, const char *method, const char *body,
                                 const char *const headers[][2], size_t header_count,
                                 char *response_token, size_t token_capacity, const char *trusted_pem)
{
    response_t r = {.token=response_token, .token_size=token_capacity};
    if (response_token && token_capacity) response_token[0] = 0;
    if (!url || !method || (strcmp(method, "GET") && strcmp(method, "POST"))) return NULL;
    esp_http_client_config_t cfg = {
        .url=url, .event_handler=collect, .user_data=&r, .timeout_ms=4000,
        .disable_auto_redirect=true, .buffer_size=1536, .buffer_size_tx=1536
    };
    if (!strncmp(url, "https://", 8)) {
        if (trusted_pem && *trusted_pem) cfg.cert_pem = trusted_pem;
        else cfg.crt_bundle_attach = esp_crt_bundle_attach;
    } else if (strncmp(url, "http://", 7)) return NULL;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return NULL;
    esp_err_t err = esp_http_client_set_method(client, !strcmp(method, "POST") ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    for (size_t i = 0; err == ESP_OK && i < header_count; ++i) {
        if (!headers[i][0] || !headers[i][1] || strpbrk(headers[i][1], "\r\n")) err = ESP_ERR_INVALID_ARG;
        else err = esp_http_client_set_header(client, headers[i][0], headers[i][1]);
    }
    if (err == ESP_OK && body) err = esp_http_client_set_post_field(client, body, strlen(body));
    if (err == ESP_OK) err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    bool complete = esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);
    cJSON *result = NULL;
    if (err == ESP_OK && status >= 200 && status < 300 && complete && !r.failed && r.data && nesting_allowed(r.data,r.size))
        result = cJSON_ParseWithOpts(r.data, NULL, true);
    if (!result && response_token && token_capacity) response_token[0] = 0;
    free(r.data);
    return result;
}
