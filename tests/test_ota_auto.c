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
static int fail_request, open_error, http_status;
/* Tartós kapcsolódási hiba: az újrapróbálkozás sem talál működő kapcsolatot. */
static bool fail_repeat;
static bool header_error, read_error, fail_begin, fail_write, fail_boot;
static size_t fail_after;
static int expire_request;
static const char *redirect_target;
static bool fail_task;
static int task_deletes;
static void (*scheduled_task)(void *);
static char logs[32768];
static size_t log_size;
static bool fail_save, fail_end, truncate, invalid_version, wrong_checksum, bad_redirect,
    wifi = true;
struct http_client
{
    esp_http_client_config_t config;
    const unsigned char *body;
    size_t size, offset;
    int status, request;
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
    c->request = requests;
    if (requests == fail_request && http_status)
        c->status = http_status;
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
    if (bad_redirect || (redirect_target && requests == 1))
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
    (void)n;
    /* A valódi kliens is visszautasítja a TX-pufferbe nem férő kérősort. */
    if (strlen(c->config.url) + 16 >= (size_t)c->config.buffer_size_tx)
        return 0x7003;
    return c->request == fail_request || (fail_repeat && c->request >= fail_request) ? open_error
                                                                                      : ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    if (c->request == fail_request && header_error)
        return -1;
    if (bad_redirect || (redirect_target && c->request == 1))
    {
        esp_http_client_event_t e = {.event_id = HTTP_EVENT_ON_HEADER,
                                     .user_data = c->config.user_data,
                                     .header_key = "Location",
                                     .header_value = (char *)(redirect_target ? redirect_target : "http://github.com/unsafe")};
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
    if (read_error && c->request == fail_request && c->offset >= fail_after)
        return -1;
    size_t count = (size_t)n;
    if (read_error && c->request == fail_request && count > fail_after - c->offset)
        count = fail_after - c->offset;
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
int esp_http_client_get_errno(esp_http_client_handle_t c)
{
    return c->request == fail_request ? 110 : 0;
}
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t c,
                                                       int *tls_code, int *flags)
{
    *tls_code = c->request == fail_request ? -9984 : 0;
    *flags = c->request == fail_request ? 4 : 0;
    return c->request == fail_request ? 0x800d : ESP_OK;
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
    if (fail_begin)
        return ESP_FAIL;
    *h = 1;
    return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *data, size_t n)
{
    assert(h == 1 && written_size + n <= sizeof(written));
    if (fail_write)
        return ESP_FAIL;
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
    return fail_boot ? ESP_FAIL : ESP_OK;
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
    char line[APP_EVENT_TEXT_MAX];
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    /* A tényleges 128 bájtos eseménypufferben is olvasható maradjon a hiba. */
    assert(length >= 0 && length < (int)sizeof(line));
    assert(log_size + (size_t)length + 2 < sizeof(logs));
    memcpy(logs + log_size, line, (size_t)length);
    log_size += (size_t)length;
    logs[log_size++] = '\n';
    logs[log_size] = 0;
}
int64_t esp_timer_get_time(void)
{
    static int64_t t;
    t += requests == expire_request ? 31000000 : 1;
    return t;
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
    if (fail_task)
        return 0;
    scheduled_task = f;
    if (h)
        *(TaskHandle_t *)h = (void *)1;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task)
{
    (void)task;
    task_deletes++;
}
size_t heap_caps_get_free_size(uint32_t caps)
{
    return caps & MALLOC_CAP_INTERNAL ? 65536 : 8388608;
}
size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    return caps & MALLOC_CAP_INTERNAL ? 32768 : 4194304;
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
    fail_request = open_error = http_status = expire_request = 0;
    fail_repeat = false;
    header_error = read_error = fail_begin = fail_write = fail_boot = false;
    fail_after = log_size = 0;
    logs[0] = 0;
    redirect_target = NULL;
    fail_task = false;
    scheduled_task = NULL;
    task_deletes = 0;
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
    assert(strstr(logs, "szakasz: release") && strstr(logs, "szakasz: SHA256-lista"));
    assert(strstr(logs, "szakasz: firmware") && strstr(logs, "800/800 B"));
    assert(strstr(logs, "szakasz: SHA256") && strstr(logs, "szakasz: képellenőrzés"));
    assert(strstr(logs, "szakasz: statisztika") && strstr(logs, "szakasz: aktiválás"));
    reset();
    image[799] ^= 1;
    check_release();
    assert(aborts == 1 && !ends && !saves && !boots && !restarts);
    assert(strstr(logs, "eltérő SHA256 ellenőrzőösszeg"));
    reset();
    fail_save = true;
    check_release();
    assert(ends == 1 && saves == 1 && !boots && !restarts);
    assert(strstr(logs, "SD/statisztika mentése"));
    reset();
    fail_end = true;
    check_release();
    assert(ends == 1 && !saves && !boots);
    assert(strstr(logs, "esp_ota_end"));
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
    reset();
    expire_request = 1;
    check_release();
    assert(strstr(logs, "válasz időlimit (30 s)") && !begins && !boots);
    reset();
    fail_request = 1;
    read_error = true;
    fail_after = 34;
    check_release();
    assert(strstr(logs, "HTTP olvasási hiba") && strstr(logs, "34/"));
    assert(!begins && !boots);
    reset();
    redirect_target = "https://release-assets.githubusercontent.com/firmware?token=SECRET";
    fail_request = 2;
    open_error = 0x7003;
    fail_repeat = true;
    ota_trace_t trace = {.stage = "redirect-teszt"};
    assert(!open_download(APP_OTA_RELEASE_API, &trace));
    /* Tartós hiba: két újrapróbálkozás után adja fel, az okot megtartva. */
    assert(strstr(logs, "új kapcsolódási kísérlet 3 s múlva (2/2)"));
    assert(!strstr(logs, "túl sok átirányítás"));
    assert(strstr(logs, "release-assets.githubusercontent.com"));
    assert(!strstr(logs, "SECRET") && !strstr(logs, "token="));
    reset();
    /* Átmeneti kapcsolódási hiba (pl. pillanatnyi belső RAM-hiány) után a
     * következő kísérlet már sikerül, nem kell napváltásig várni. */
    fail_request = 1;
    open_error = 0x7003;
    ota_trace_t flaky = {.stage = "átmeneti hiba"};
    esp_http_client_handle_t recovered = open_download(APP_OTA_RELEASE_API, &flaky);
    assert(recovered && requests == 2);
    close_download(recovered);
    assert(strstr(logs, "új kapcsolódási kísérlet 3 s múlva (1/2)"));
    reset();
    char long_redirect[1800];
    strcpy(long_redirect, "https://release-assets.githubusercontent.com/firmware?token=SECRET");
    size_t prefix = strlen(long_redirect);
    memset(long_redirect + prefix, 'a', 1600 - prefix);
    long_redirect[1600] = 0;
    redirect_target = long_redirect;
    ota_trace_t long_trace = {.stage = "hosszú URL"};
    esp_http_client_handle_t download = open_download(APP_OTA_RELEASE_API, &long_trace);
    assert(download && download->config.buffer_size_tx > 1600);
    close_download(download);
    assert(!strstr(logs, "SECRET") && strstr(logs, "URL=1600 B"));
    assert(strstr(logs, "max-int=32768") && strstr(logs, "max-PSRAM=4194304"));
    reset();
    wifi = false;
    assert(app_ota_request_auto_update() == ESP_ERR_INVALID_STATE && !scheduled_task);
    wifi = true;
    assert(app_ota_try_begin_operation());
    assert(app_ota_request_auto_update() == ESP_ERR_INVALID_STATE);
    app_ota_end_operation();
    fail_task = true;
    assert(app_ota_request_auto_update() == ESP_ERR_NO_MEM);
    assert(app_ota_try_begin_operation());
    app_ota_end_operation();
    fail_task = false;
    assert(app_ota_request_auto_update() == ESP_OK && scheduled_task);
    assert(!requests && !begins); /* A HTTP kérés nem végzi el az OTA-t. */
    assert(app_ota_request_auto_update() == ESP_ERR_INVALID_STATE);
    scheduled_task(NULL);
    assert(restarts == 1 && task_deletes == 1);
    assert(app_ota_try_begin_operation());
    app_ota_end_operation();
    /* A hálózati hiba szakaszának és részleteinek megkülönböztetése. */
    for (int request = 1; request <= 3; request++)
    {
        reset();
        fail_request = request;
        open_error = 0x7003;
        fail_repeat = true;
        check_release();
        const char *stage = request == 1 ? "[release]" : request == 2 ? "[SHA256-lista]" : "[firmware]";
        assert(strstr(logs, stage) && strstr(logs, "kód=28675"));
        assert(strstr(logs, "errno=110") && strstr(logs, "TLS=0x800d"));
        assert(strstr(logs, "mbedTLS=-9984") && strstr(logs, "cert=0x4"));
        assert(strstr(strstr(logs, "OTA hálózat"), "OTA RAM"));
        assert(!begins && !boots && !restarts);
    }
    reset();
    fail_request = 1;
    http_status = 403;
    check_release();
    assert(strstr(logs, "HTTP 403") && strstr(logs, "kód=403"));
    assert(!begins && !boots);
    reset();
    fail_request = 1;
    header_error = true;
    check_release();
    assert(strstr(logs, "HTTP fejléc olvasási hiba"));
    assert(!begins && !boots);
    reset();
    fail_request = 3;
    read_error = true;
    fail_after = 400;
    check_release();
    assert(strstr(logs, "400/800 B") && strstr(logs, "firmware olvasási hiba"));
    assert(aborts == 1 && !boots && !restarts);
    reset();
    fail_begin = true;
    check_release();
    assert(strstr(logs, "esp_ota_begin") && !boots);
    reset();
    fail_write = true;
    check_release();
    assert(strstr(logs, "esp_ota_write") && aborts == 1 && !boots);
    reset();
    fail_boot = true;
    check_release();
    assert(strstr(logs, "esp_ota_set_boot_partition") && !restarts);
    puts("PASS: actual auto OTA short reads, SHA256, version, SD/flash errors, rollback "
         "suppression, HTTPS redirect, per-stage logs, TLS/socket codes, timeouts, bounded "
         "connection retry and exclusive operation");
    return 0;
}
