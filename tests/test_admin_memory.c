/* A valódi webes szerializáló minden allokációs pontján külön hibát idézünk elő. */
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_events.h"
#include "cJSON.h"
#include "esp_http_server.h"

static app_config_t s_config;
static unsigned attempts, live, fail_at, status;
static char response[4096];

static void *tracked_malloc(size_t bytes)
{
    if (++attempts == fail_at) return NULL;
    void *memory = malloc(bytes);
    if (memory) ++live;
    return memory;
}

static void tracked_free(void *memory)
{
    if (memory) {
        assert(live);
        --live;
        free(memory);
    }
}

static bool authorize(httpd_req_t *req) { return req != NULL; }
static bool app_boot_config_setup_pending(void) { return false; }
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *value) { (void)req; (void)value; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *value)
{
    (void)req; (void)key; (void)value;
    return ESP_OK;
}
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *value)
{
    (void)req;
    status = (unsigned)atoi(value);
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *body)
{
    (void)req;
    assert(body && strlen(body) < sizeof(response));
    strcpy(response, body);
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *req, const char *body, int size)
{
    assert(size == HTTPD_RESP_USE_STRLEN);
    return httpd_resp_sendstr(req, body);
}
#include "settings_functions.inc"

static unsigned check_response(bool saving, unsigned failure)
{
    httpd_req_t req = {0};
    attempts = 0;
    fail_at = failure;
    status = 200;
    response[0] = 0;
    esp_err_t result = saving ? settings_response(&req, 200, "OK", false, &s_config) : settings_get(&req);
    assert(result == ESP_OK && !live);
    unsigned count = attempts;
    fail_at = 0;
    cJSON *json = cJSON_Parse(response);
    assert(json);
    if (failure) {
        assert(status == 500 && cJSON_IsFalse(cJSON_GetObjectItem(json, "ok")));
        assert(!cJSON_GetObjectItem(json, "config"));
    } else {
        assert(status == 200);
        cJSON *cfg = saving ? cJSON_GetObjectItem(json, "config") : json;
        assert(cJSON_IsObject(cfg));
        assert(cJSON_IsNull(cJSON_GetObjectItem(cJSON_GetObjectItem(cfg, "ota"), "password")));
        assert(!strstr(response, "existing-secret"));
    }
    cJSON_Delete(json);
    assert(!live);
    return count;
}

static unsigned check_keep(unsigned failure)
{
    cJSON *form = cJSON_Parse("{\"wifi\":{\"password\":null}}");
    assert(form);
    attempts = 0;
    fail_at = failure;
    bool ok = keep_secret(form, "wifi", "existing-secret");
    unsigned count = attempts;
    fail_at = 0;
    cJSON *password = cJSON_GetObjectItem(cJSON_GetObjectItem(form, "wifi"), "password");
    if (failure) assert(!ok && cJSON_IsNull(password));
    else assert(ok && cJSON_IsString(password) && !strcmp(password->valuestring, "existing-secret"));
    cJSON_Delete(form);
    assert(!live);
    return count;
}

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static bool s_hu = true, empty_events, no_psram;
static unsigned chunks, stop_chunk, buffers;
static void *heap_caps_malloc(size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) && !buffers);
    if (no_psram) return NULL;
    ++buffers;
    return malloc(size);
}

static void release_events(void *events) { assert(events && buffers == 1); --buffers; free(events); }

size_t app_events_snapshot(app_event_entry_t *events, size_t count)
{
    assert(buffers && count == APP_EVENT_LOG_CAPACITY);
    if (empty_events) return 0;
    memset(events, 0, sizeof(*events) * 2);
    memset(events[0].text, '"', sizeof(events[0].text) - 1);
    strcpy(events[0].text_en, "English");
    strcpy(events[1].text, "Legfrissebb");
    strcpy(events[1].text_en, "Most recent");
    return 2;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *data, int size)
{
    (void)req;
    if (++chunks == stop_chunk) return ESP_FAIL;
    size_t len = size < 0 ? strlen(data) : (size_t)size;
    assert(len < sizeof(response) - strlen(response));
    strncat(response, data, len);
    return ESP_OK;
}

#define free release_events
#include "event_functions.inc"
#undef free

static unsigned check_events(unsigned stop)
{
    chunks = 0;
    response[0] = 0;
    stop_chunk = stop;
    esp_err_t result = send_recent_events(NULL);
    assert(!buffers && result == (stop ? ESP_FAIL : ESP_OK));
    if (!stop) {
        if (no_psram) assert(strstr(response, s_hu ? "nem tölthető" : "could not be loaded"));
        else if (empty_events) assert(strstr(response, s_hu ? "Még nincs" : "No events"));
        else {
            assert(strstr(response, "</td></tr></table></div>"));
            if (s_hu) {
                const char *quoted = strstr(response, "&quot;");
                assert(quoted && strstr(response, "Legfrissebb") && strstr(response, "Legfrissebb") < quoted);
                for (unsigned i = 0; i < APP_EVENT_TEXT_MAX - 1; ++i, quoted += 6)
                    assert(!strncmp(quoted, "&quot;", 6));
            } else {
                const char *newest = strstr(response, "Most recent");
                const char *previous = strstr(response, "English");
                assert(newest && previous && newest < previous);
            }
        }
    }
    return chunks;
}

int main(void)
{
    cJSON_Hooks hooks = {.malloc_fn=tracked_malloc, .free_fn=tracked_free};
    cJSON_InitHooks(&hooks);
    strcpy(s_config.ota.password, "existing-secret");
    unsigned gets = check_response(false, 0), saves = check_response(true, 0), keeps = check_keep(0);
    for (unsigned i = 1; i <= gets; ++i) check_response(false, i);
    for (unsigned i = 1; i <= saves; ++i) check_response(true, i);
    for (unsigned i = 1; i <= keeps; ++i) check_keep(i);
    printf("PASS: settings OOM at %u GET, %u response and %u password-retention allocations, no partial response or leaks (%zu-byte config)\n",
           gets, saves, keeps, sizeof(app_config_t));
    unsigned sends = check_events(0);
    for (unsigned i = 1; i <= sends; ++i) check_events(i);
    s_hu = false;
    check_events(0);
    empty_events = true;
    check_events(0);
    no_psram = true;
    check_events(0);
    printf("PASS: home event snapshot uses %zu bytes of PSRAM, newest first, full escaping, empty/OOM and every interrupted send\n",
           APP_EVENT_LOG_CAPACITY * sizeof(app_event_entry_t));
    return 0;
}
