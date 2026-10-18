/* A valódi letöltő és aktiváló kód hibainjektált HTTP/flash környezettel. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/version.h"
#include "mbedtls/sha256.h"
#if MBEDTLS_VERSION_MAJOR < 3
#define mbedtls_sha256_starts mbedtls_sha256_starts_ret
#define mbedtls_sha256_update mbedtls_sha256_update_ret
#define mbedtls_sha256_finish mbedtls_sha256_finish_ret
#endif
#include "../main/app_ota_auto.c"

static esp_app_desc_t running_desc = {.version = "v2.0.0",
                                      .project_name = "zolee_energy_flow_display"};
static esp_partition_t partition = {.size = 8388608};
static unsigned char image[800], written[800];
static size_t written_size;
static char metadata[1500], sums[200];
static int begins, aborts, ends, saves, boots, restarts, requests;
static bool fail_save, fail_end, truncate, invalid_version, wrong_checksum, bad_redirect,
    wifi = true;
struct http_client
{
    esp_http_client_config_t config;
    const unsigned char *body;
    size_t size, offset;
    int status;
};

esp_err_t esp_crt_bundle_attach(void *p)
{
    (void)p;
    return ESP_OK;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(config->crt_bundle_attach == esp_crt_bundle_attach && config->disable_auto_redirect);
    struct http_client *c = calloc(1, sizeof(*c));
    assert(c);
    c->config = *config;
    c->status = 200;
    requests++;
    if (!strcmp(config->url, APP_OTA_RELEASE_API))
    {
        c->body = (void *)metadata;
        c->size = strlen(metadata);
    }
    else if (strstr(config->url, "SHA256SUMS.txt"))
    {
        c->body = (void *)sums;
        c->size = strlen(sums);
    }
    else
    {
        c->body = image;
        c->size = sizeof(image);
        if (truncate)
            c->size--;
    }
    if (bad_redirect)
        c->status = 302;
    return c;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v)
{
    (void)c;
    (void)k;
    (void)v;
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int n)
{
    (void)c;
    (void)n;
    return ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    if (bad_redirect)
    {
        esp_http_client_event_t e = {.event_id = HTTP_EVENT_ON_HEADER,
                                     .user_data = c->config.user_data,
                                     .header_key = "Location",
                                     .header_value = "http://github.com/unsafe"};
        c->config.event_handler(&e);
    }
    return c->size;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c)
{
    return c->status;
}
int64_t esp_http_client_get_content_length(esp_http_client_handle_t c)
{
    return c->size;
}
int esp_http_client_read(esp_http_client_handle_t c, char *out, int n)
{
    size_t count = (size_t)n;
    if (count > 17)
        count = 17;
    if (count > c->size - c->offset)
        count = c->size - c->offset;
    memcpy(out, c->body + c->offset, count);
    c->offset += count;
    return (int)count;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c)
{
    return c->offset == c->size;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{
    free(c);
    return ESP_OK;
}
esp_err_t esp_http_client_get_user_data(esp_http_client_handle_t c, void **data)
{
    *data = c->config.user_data;
    return ESP_OK;
}
const esp_app_desc_t *esp_app_get_description(void)
{
    return &running_desc;
}
const esp_partition_t *esp_ota_get_running_partition(void)
{
    return &partition;
}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p)
{
    (void)p;
    return &partition;
}
const esp_partition_t *esp_ota_get_last_invalid_partition(void)
{
    return invalid_version ? &partition : NULL;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *s)
{
    (void)p;
    *s = 0;
    return ESP_OK;
}
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *d)
{
    (void)p;
    memset(d, 0, sizeof(*d));
    strcpy(d->version, "v3.0.0");
    return ESP_OK;
}
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t n, esp_ota_handle_t *h)
{
    (void)p;
    assert(n == sizeof(image));
    begins++;
    *h = 1;
    return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *data, size_t n)
{
    assert(h == 1 && written_size + n <= sizeof(written));
    memcpy(written + written_size, data, n);
    written_size += n;
    return ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t h)
{
    (void)h;
    aborts++;
    return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t h)
{
    (void)h;
    ends++;
    return fail_end ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{
    (void)p;
    assert(saves > 0 && !fail_save && ends > 0 && !fail_end);
    boots++;
    return ESP_OK;
}
esp_err_t app_stats_save_now(void)
{
    saves++;
    return fail_save ? ESP_FAIL : ESP_OK;
}
void app_stats_set_day_change_callback(void (*c)(void))
{
    (void)c;
}
bool app_wifi_is_connected(void)
{
    return wifi;
}
void app_events_add(app_event_level_t l, const char *fmt, ...)
{
    (void)l;
    (void)fmt;
}
int64_t esp_timer_get_time(void)
{
    static int64_t t;
    return ++t;
}
void esp_restart(void)
{
    restarts++;
}
void vTaskDelay(TickType_t t)
{
    (void)t;
}
int xTaskCreate(void (*f)(void *), const char *n, unsigned s, void *a, unsigned p, void *h)
{
    (void)f;
    (void)n;
    (void)s;
    (void)a;
    (void)p;
    *(TaskHandle_t *)h = (void *)1;
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t t)
{
    (void)t;
}
unsigned ulTaskNotifyTake(int c, TickType_t t)
{
    (void)c;
    (void)t;
    return 1;
}

static void reset(void)
{
    begins = aborts = ends = saves = boots = restarts = requests = 0;
    written_size = 0;
    fail_save = fail_end = truncate = invalid_version = wrong_checksum = bad_redirect = false;
    memset(image, 0, sizeof(image));
    image[0] = 0xe9;
    esp_app_desc_t desc = {.magic_word = ESP_APP_DESC_MAGIC_WORD,
                           .version = "v3.0.0",
                           .project_name = "zolee_energy_flow_display"};
    memcpy(image + 32, &desc, sizeof(desc));
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    assert(!mbedtls_sha256_starts(&ctx, 0));
    assert(!mbedtls_sha256_update(&ctx, image, sizeof(image)));
    assert(!mbedtls_sha256_finish(&ctx, hash));
    mbedtls_sha256_free(&ctx);
    for (unsigned i = 0; i < 32; i++)
        snprintf(sums + i * 2, 3, "%02x", hash[i]);
    strcat(sums, "  ZoleeEnergyFlowDisplay-v3.0.0-ota.bin\n");
    snprintf(metadata,
             sizeof(metadata),
             "{\"tag_name\":\"v3.0.0\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":"
             "\"ZoleeEnergyFlowDisplay-v3.0.0-ota.bin\",\"size\":800,\"state\":\"uploaded\","
             "\"browser_download_url\":\"" APP_OTA_RELEASE_BASE
             "v3.0.0/"
             "ZoleeEnergyFlowDisplay-v3.0.0-ota.bin\"},{\"name\":\"SHA256SUMS.txt\",\"size\":100,"
             "\"state\":\"uploaded\",\"browser_download_url\":\"" APP_OTA_RELEASE_BASE
             "v3.0.0/SHA256SUMS.txt\"}]}");
}
int main(void)
{
    assert(sizeof(esp_app_desc_t) == 256);
    assert(app_ota_try_begin_operation());
    assert(!app_ota_try_begin_operation());
    app_ota_end_operation();
    assert(app_ota_try_begin_operation());
    app_ota_end_operation();
    reset();
    check_release();
    assert(begins == 1 && ends == 1 && saves == 1 && boots == 1 && restarts == 1 &&
           written_size == 800 && !aborts);
    assert(!memcmp(written, image, 800));
    reset();
    image[799] ^= 1;
    check_release();
    assert(aborts == 1 && !ends && !saves && !boots && !restarts);
    reset();
    fail_save = true;
    check_release();
    assert(ends == 1 && saves == 1 && !boots && !restarts);
    reset();
    fail_end = true;
    check_release();
    assert(ends == 1 && !saves && !boots);
    reset();
    truncate = true;
    check_release();
    assert(!begins && !boots);
    reset();
    image[48] = 'X';
    check_release();
    assert(!begins && !boots);
    reset();
    invalid_version = true;
    check_release();
    assert(requests == 1 && !begins && !boots);
    reset();
    bad_redirect = true;
    check_release();
    assert(requests == 1 && !begins && !boots);
    reset();
    strcpy(metadata, "invalid JSON");
    check_release();
    assert(!begins && !boots);
    reset();
    sums[0] = '!';
    check_release();
    assert(requests == 2 && !begins && !boots);
    puts("PASS: actual auto OTA short reads, SHA256, version, SD/flash errors, rollback "
         "suppression, HTTPS redirect and exclusive operation");
    return 0;
}
