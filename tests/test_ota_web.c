/* A tényleges kézi OTA-végpont hitelesítése és aszinkron válaszai. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "app_ota.h"

static bool s_hu = true, authorized = true, connected = true, header_ok = true;
static esp_err_t start_result = ESP_OK;
static int status = 200, starts;
static const char *response;
static bool authorize(httpd_req_t *req)
{
    (void)req;
    if (!authorized)
        status = 401;
    return authorized;
}
bool app_wifi_is_connected(void) { return connected; }
esp_err_t app_ota_request_auto_update(void) { starts++; return start_result; }
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t) { (void)r; (void)t; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *k, const char *v)
{
    (void)r;
    assert(!strcmp(k, "Cache-Control") && !strcmp(v, "no-store"));
    return ESP_OK;
}
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s)
{
    (void)r;
    assert(sscanf(s, "%d", &status) == 1);
    return ESP_OK;
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *k, char *out, size_t n)
{
    (void)r;
    assert(!strcmp(k, "X-Zolee-OTA") && n == 2);
    if (!header_ok)
        return ESP_FAIL;
    strcpy(out, "1");
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *s)
{
    (void)r;
    response = s;
    return ESP_OK;
}
#include "../main/app_ota_web.inc"
int main(void)
{
    httpd_req_t req = {.method = HTTP_POST, .uri = "/api/ota/auto"};
    authorized = false;
    ota_auto_post(&req);
    assert(status == 401 && !starts);
    authorized = true;
    header_ok = false;
    ota_auto_post(&req);
    assert(status == 403 && !starts);
    header_ok = true;
    req.content_len = 1;
    ota_auto_post(&req);
    assert(status == 400 && !starts);
    req.content_len = 0;
    connected = false;
    ota_auto_post(&req);
    assert(status == 503 && !starts && strstr(response, "Wi-Fi"));
    connected = true;
    start_result = ESP_ERR_INVALID_STATE;
    ota_auto_post(&req);
    assert(status == 409 && starts == 1);
    start_result = ESP_ERR_NO_MEM;
    ota_auto_post(&req);
    assert(status == 503 && starts == 2);
    start_result = ESP_OK;
    ota_auto_post(&req);
    assert(status == 202 && starts == 3 && strstr(response, "háttérben"));
    s_hu = false;
    ota_auto_post(&req);
    assert(status == 202 && strstr(response, "background"));
    puts("PASS: actual manual OTA HTTP auth/header, offline, busy, OOM and asynchronous acceptance");
    return 0;
}
