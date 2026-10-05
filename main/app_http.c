/* Szándékosan hitelesítés nélküli, csak a kiválasztott HTTP módban aktív adatfogadó. */
#include "app_http.h"
#include "app_ingest.h"
#include "app_state.h"
#include "esp_timer.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_BODY_BYTES 4096
#define RECEIVE_DEADLINE_US 5000000LL

static esp_err_t reject(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    /* Az olvasatlan kérésmaradék nem használható következő kérésként. */
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
}

static bool json_content_type(httpd_req_t *req)
{
    char type[128];
    size_t length = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (!length || length >= sizeof(type) ||
        httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK) return false;
    char *end = strchr(type, ';');
    if (!end) end = type + strlen(type);
    while (end > type && isspace((unsigned char)end[-1])) --end;
    *end = 0;
    char *start = type;
    while (isspace((unsigned char)*start)) ++start;
    return strcasecmp(start, "application/json") == 0;
}

static esp_err_t energy_post(httpd_req_t *req)
{
    if (req->method != HTTP_POST) {
        httpd_resp_set_hdr(req, "Allow", "POST");
        return reject(req, "405 Method Not Allowed", "{\"ok\":false,\"error\":\"method\"}");
    }
    app_runtime_state_t state;
    app_state_get_runtime(&state);
    if (state.data_recept_mode != APP_DATA_HTTP)
        return reject(req, "409 Conflict", "{\"ok\":false,\"error\":\"mqtt_mode\"}");
    if (!state.http_ready || !state.config_ok || !state.time_synced)
        return reject(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"not_ready\"}");
    if (req->content_len > MAX_BODY_BYTES)
        return reject(req, "413 Payload Too Large", "{\"ok\":false,\"error\":\"size\"}");
    if (!json_content_type(req))
        return reject(req, "415 Unsupported Media Type", "{\"ok\":false,\"error\":\"content_type\"}");
    if (!req->content_len)
        return reject(req, "400 Bad Request", "{\"ok\":false,\"error\":\"empty\"}");
    char *body = malloc(req->content_len + 1);
    if (!body) return reject(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"memory\"}");
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + RECEIVE_DEADLINE_US;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT || esp_timer_get_time() >= deadline) {
            free(body);
            return reject(req, "408 Request Timeout", "{\"ok\":false,\"error\":\"timeout\"}");
        }
        if (n <= 0) {
            free(body);
            return reject(req, "400 Bad Request", "{\"ok\":false,\"error\":\"incomplete\"}");
        }
        received += (size_t)n;
    }
    body[received] = 0;
    char error[160];
    esp_err_t result = app_ingest_process(APP_DATA_HTTP, body, received, error, sizeof(error));
    free(body);
    if (result == ESP_ERR_INVALID_STATE)
        return reject(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"not_ready\"}");
    if (result != ESP_OK)
        return reject(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid_payload\"}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t app_http_register(httpd_handle_t server)
{
    const httpd_uri_t route = { .uri = "/api/energy", .method = HTTP_ANY, .handler = energy_post };
    return httpd_register_uri_handler(server, &route);
}
