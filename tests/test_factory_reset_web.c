/* A valódi kezelőt izolált HTTP-, NVS- és RTOS-helyettesítőkkel futtatjuk. */
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_boot_config.h"
#include "app_storage.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "freertos/task.h"

typedef void *TaskHandle_t;
static bool s_hu, s_restart_pending;
static app_ota_config_t s_auth_cfg;
static const char s_expected_auth[] = "Basic YTpi";
static atomic_flag s_operation = ATOMIC_FLAG_INIT;
static atomic_bool s_worker_ready, s_operation_active;
static void *s_worker_wake;
static const char *authorization = s_expected_auth, *settings_header = "1", *media = "application/json";
static const char *input, *response;
static size_t available;
static int read_calls, timeout_reads, create_calls, requests, deleted, notify_count, reboots;
static int response_status, read_delay_us;
static int64_t now;
static bool close_session, replied, busy_at_reply, lose_response;
static bool have_worker, marker, require_sd, sd_ok, internal_profile, no_task_memory;
static esp_err_t marker_result = ESP_OK;
static void (*entry)(void *);
static unsigned task_slot;
static bool first_wait;

static const char *header(const char *name)
{
    if (!strcmp(name, "Authorization")) return authorization;
    if (!strcmp(name, "X-Zolee-Settings")) return settings_header;
    assert(!strcmp(name, "Content-Type"));
    return media;
}

size_t httpd_req_get_hdr_value_len(httpd_req_t *req, const char *name)
{
    (void)req;
    const char *value = header(name);
    return value ? strlen(value) : 0;
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *name, char *out, size_t size)
{
    (void)req;
    const char *value = header(name);
    if (!value || strlen(value) >= size) return ESP_FAIL;
    strcpy(out, value);
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t *req, char *out, size_t size)
{
    (void)req;
    ++read_calls;
    now += read_delay_us;
    if (timeout_reads > 0) {
        --timeout_reads;
        return HTTPD_SOCK_ERR_TIMEOUT;
    }
    if (size > 3) size = 3;
    if (size > available) size = available;
    memcpy(out, input, size);
    available -= size;
    input += size;
    return (int)size;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *value)
{
    (void)req;
    assert(sscanf(value, "%d", &response_status) == 1);
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *value)
{
    (void)req;
    assert(!strcmp(value, "text/plain; charset=utf-8"));
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value)
{
    (void)req;
    if (!strcmp(name, "Connection")) {
        assert(!strcmp(value, "close"));
        close_session = true;
    } else if (!strcmp(name, "Cache-Control")) {
        assert(!strcmp(value, "no-store"));
    } else if (!strcmp(name, "Allow")) {
        assert(!strcmp(value, "POST"));
    } else {
        assert(!strcmp(name, "WWW-Authenticate") && strstr(value, "Basic"));
    }
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *text)
{
    (void)req;
    assert(*text);
    response = text;
    replied = true;
    busy_at_reply = s_operation_active;
    if (response_status == 202)
        assert(marker && s_restart_pending && have_worker && !notify_count && !reboots &&
               strstr(text, s_hu ? "elfogadva" : "accepted"));
    return lose_response ? ESP_FAIL : ESP_OK;
}

bool app_storage_is_ok(void) { return sd_ok; }
bool app_boot_config_internal(void) { return internal_profile; }
int64_t esp_timer_get_time(void) { return now; }
typedef unsigned esp_partition_t;
typedef enum { ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID,
               ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED } esp_ota_img_states_t;
#define ESP_ERR_NOT_SUPPORTED 0x106
static unsigned partitions[2], boot_partition;
static esp_ota_img_states_t image_state = ESP_OTA_IMG_VALID;
static esp_err_t image_result = ESP_OK;
static const esp_partition_t *esp_ota_get_running_partition(void) { return &partitions[0]; }
static const esp_partition_t *esp_ota_get_boot_partition(void) { return &partitions[boot_partition]; }
static esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *state)
{
    assert(p == &partitions[0]);
    if (image_result == ESP_OK) *state = image_state;
    return image_result;
}
static void xSemaphoreGive(void *sem) { (void)sem; assert(false); }

int xTaskCreate(void (*worker)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle)
{
    assert(!have_worker && !marker && s_operation_active && !arg && stack && priority && name);
    ++create_calls;
    if (no_task_memory) return 0;
    have_worker = true;
    entry = worker;
    *(TaskHandle_t *)handle = &task_slot;
    return pdPASS;
}

esp_err_t app_factory_reset_request(bool sd_required)
{
    assert(have_worker && !marker && !notify_count && !reboots && s_operation_active);
    ++requests;
    require_sd = sd_required;
    marker = marker_result == ESP_OK;
    return marker_result;
}

void vTaskDelete(void *worker)
{
    assert(worker == &task_slot && have_worker && !marker && !notify_count);
    have_worker = false;
    ++deleted;
}

static void xTaskNotifyGive(TaskHandle_t worker)
{
    assert(worker == &task_slot && have_worker && marker && replied && s_restart_pending);
    ++notify_count;
}

static unsigned ulTaskNotifyTake(int clear, TickType_t wait)
{
    assert(clear == pdTRUE && wait == portMAX_DELAY && notify_count == 1);
    if (first_wait) {
        first_wait = false;
        return 0;
    }
    return 1;
}

void vTaskDelay(TickType_t wait) { assert(wait && replied && marker && have_worker); }
static void esp_restart(void) { assert(marker && notify_count == 1); ++reboots; }

#include "reset_routes.inc"

static void request(int method, const char *data, size_t actual, size_t declared, int status)
{
    httpd_req_t req = {.method=method, .uri="/api/factory-reset", .content_len=declared};
    input = data;
    available = actual;
    response_status = 200;
    close_session = replied = false;
    read_calls = 0;
    esp_err_t result = factory_reset_post(&req);
    assert(replied && response_status == status);
    if (close_session || lose_response) assert(result == ESP_FAIL);
    if (status != 401) assert(busy_at_reply);
}

static void post(const char *body, int status)
{
    request(HTTP_POST, body, strlen(body), strlen(body), status);
}

static void next_boot(void)
{
    assert(have_worker && notify_count == 1 && !reboots);
    first_wait = true;
    entry(NULL);
    assert(!first_wait && reboots == 1);
    have_worker = marker = s_restart_pending = false;
    notify_count = reboots = 0;
    app_ota_end_operation();
}

int main(void)
{
    static const char valid[] = "{\"confirm\":true}";
    strcpy(s_auth_cfg.username, "a");
    authorization = NULL;
    post(valid, 401);
    authorization = "Basic different";
    post(valid, 401);
    assert(!read_calls && !create_calls && !s_operation_active);
    authorization = s_expected_auth;
    assert(app_ota_try_begin_operation());
    post(valid, 409);
    assert(!read_calls && !create_calls && s_operation_active);
    app_ota_end_operation();

    request(HTTP_GET, valid, strlen(valid), strlen(valid), 405);
    settings_header = NULL;
    post(valid, 403);
    settings_header = "11";
    post(valid, 403);
    settings_header = "1";
    const char *media_types[] = {NULL, "text/plain", "application/x-www-form-urlencoded",
                                 "multipart/form-data; boundary=abc", "application/jsonextra"};
    for (size_t i = 0; i < sizeof(media_types) / sizeof(media_types[0]); ++i) {
        media = media_types[i];
        post(valid, 415);
        assert(!read_calls && !s_operation_active);
    }
    media = "Application/JSON ; charset=UTF-8";
    request(HTTP_POST, valid, strlen(valid), 129, 413);
    assert(!read_calls && close_session);
    static const char *const invalid[] = {
        "", " ", "true", "[]", "{}", "{\"confirm\":false}", "{\"confirm\":1}", "{\"confirm\":\"true\"}",
        "{\"Confirm\":true}", "{\"confirm\":null}", "{\"confirm\":true}{}", "{\"confirm\":true}junk",
        "{\"confirm\":true,\"confirm\":false}", "{\"confirm\":true,\"extra\":true}", "{\"confirm\":tr ue}",
        "{\"confirm\":true,}", "{\"confirm\":true}\v", "{\"con firm\":true}",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        post(invalid[i], 400);
        assert(close_session && !s_operation_active);
    }
    static const char embedded_nul[] = "{\"confirm\":true}\0hidden";
    request(HTTP_POST, embedded_nul, sizeof(embedded_nul) - 1, sizeof(embedded_nul) - 1, 400);
    request(HTTP_POST, valid, 5, strlen(valid), 400);
    timeout_reads = 100;
    read_delay_us = 1000000;
    post(valid, 408);
    assert(read_calls == 5);
    timeout_reads = 0;
    post(valid, 408);
    assert(read_calls == 5);
    read_delay_us = 0;
    assert(!marker && !have_worker && !s_restart_pending && !s_operation_active && !create_calls && !requests);

    boot_partition = 1;
    post(valid, 409);
    assert(!marker && !have_worker && !create_calls && !requests);
    boot_partition = 0;
    app_ota_end_operation();
    static const esp_ota_img_states_t unavailable[] = {
        ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED
    };
    for (size_t i = 0; i < sizeof(unavailable) / sizeof(unavailable[0]); ++i) {
        image_state = unavailable[i];
        post(valid, 409);
        assert(!marker && !have_worker && !create_calls && !requests && !s_operation_active);
    }
    image_result = ESP_FAIL;
    post(valid, 503);
    assert(!marker && !have_worker && !create_calls && !requests && !s_operation_active);
    image_result = ESP_OK;
    image_state = ESP_OTA_IMG_VALID;
    no_task_memory = true;
    post(valid, 503);
    assert(!marker && !s_restart_pending && !s_operation_active && create_calls == 1 && !requests);
    no_task_memory = false;
    marker_result = ESP_FAIL;
    post(valid, 503);
    assert(!have_worker && !marker && !s_restart_pending && !s_operation_active && requests == 1 && deleted == 1);
    marker_result = ESP_ERR_INVALID_STATE;
    for (unsigned language = 0; language < 2; ++language) {
        s_hu = language == 1;
        post(valid, 409);
        assert(strstr(response, s_hu ? "eredeti SD" : "original SD"));
        assert(!have_worker && !marker && !s_restart_pending && !s_operation_active);
    }
    int requests_before_success = requests;
    marker_result = ESP_OK;
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        image_result = scenario == 1 ? ESP_ERR_NOT_FOUND : scenario == 3 ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
        image_state = scenario == 2 ? ESP_OTA_IMG_UNDEFINED : ESP_OTA_IMG_VALID;
        sd_ok = scenario == 0;
        internal_profile = scenario != 1;
        s_hu = scenario == 1;
        lose_response = scenario == 2;
        if (scenario == 2) s_auth_cfg.username[0] = '\0';
        timeout_reads = 1;
        post("\n { \t\"confirm\" \r: true \n} \t", 202);
        assert(require_sd == (scenario < 2) && s_operation_active && marker && !reboots);
        assert(!app_ota_try_begin_operation());
        lose_response = false;
        post(valid, 409);
        assert(!read_calls && notify_count == 1 && requests == requests_before_success + (int)scenario + 1);
        next_boot();
    }
    puts("PASS: factory reset HTTP auth, exclusive lock, strict JSON, bounded reads and durable reboot reservation");
    return 0;
}
