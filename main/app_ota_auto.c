/* Napváltáshoz kötött, kikapcsolható GitHub OTA; nincs induláskori frissítés. */
#include "app_ota.h"
#include "app_ota_release.h"
#include "app_events.h"
#include "app_stats.h"
#include "app_wifi.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

static atomic_flag s_operation = ATOMIC_FLAG_INIT;
static TaskHandle_t s_auto_task;
static uint32_t s_settle_seconds;

bool app_ota_try_begin_operation(void)
{
    return !atomic_flag_test_and_set(&s_operation);
}

void app_ota_end_operation(void)
{
    atomic_flag_clear(&s_operation);
}

typedef struct
{
    char location[2048];
} redirect_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    redirect_t *redirect = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        !strcasecmp(event->header_key, "Location"))
    {
        const char *value = event->header_value;
        if (!value || strlen(value) >= sizeof(redirect->location))
            return ESP_FAIL;
        snprintf(redirect->location, sizeof(redirect->location), "%s", value);
    }
    return ESP_OK;
}

/* Minden új TLS kapcsolat ellenőrzi a tanúsítványláncot és a hostnevet.
 * Az automatikus redirect tiltott: HTTP-ra és idegen hostra nem követünk át. */
static esp_http_client_handle_t open_download(const char *url)
{
    redirect_t *redirect = calloc(1, sizeof(*redirect));
    char *current = malloc(2048);
    if (!redirect || !current)
    {
        free(redirect);
        free(current);
        return NULL;
    }
    snprintf(current, 2048, "%s", url);
    esp_http_client_handle_t client = NULL;
    for (unsigned attempt = 0; attempt < 5; attempt++)
    {
        if (!app_ota_download_url_allowed(current))
            break;
        redirect->location[0] = 0;
        esp_http_client_config_t config = {
            .url = current,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 10000,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .disable_auto_redirect = true,
            .event_handler = http_event,
            .user_data = redirect,
            .user_agent = "ZoleeEnergyFlowDisplay-auto-ota",
        };
        client = esp_http_client_init(&config);
        if (!client)
            break;
        esp_http_client_set_header(client, "Accept", "application/octet-stream");
        if (!strncmp(current, "https://api.github.com/", 23))
        {
            esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
            esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
        }
        esp_err_t err = esp_http_client_open(client, 0);
        if (err == ESP_OK && esp_http_client_fetch_headers(client) >= 0)
        {
            int status = esp_http_client_get_status_code(client);
            if (status == 200)
            {
                /* A user_data a kliens végéig él; a hívó szabadítja fel. */
                free(current);
                return client;
            }
            if ((status == 301 || status == 302 || status == 303 || status == 307 ||
                 status == 308) &&
                redirect->location[0])
            {
                snprintf(current, 2048, "%s", redirect->location);
                esp_http_client_cleanup(client);
                client = NULL;
                continue;
            }
        }
        esp_http_client_cleanup(client);
        client = NULL;
        break;
    }
    free(redirect);
    free(current);
    return NULL;
}

static void close_download(esp_http_client_handle_t client)
{
    void *data = NULL;
    esp_http_client_get_user_data(client, &data);
    esp_http_client_cleanup(client);
    free(data);
}

static char *download_text(const char *url, size_t limit)
{
    esp_http_client_handle_t client = open_download(url);
    if (!client)
        return NULL;
    char *text = malloc(limit + 1);
    size_t used = 0;
    int64_t deadline = esp_timer_get_time() + 30000000LL;
    bool complete = false;
    if (text)
    {
        while (used < limit && esp_timer_get_time() < deadline)
        {
            int count = esp_http_client_read(client, text + used, (int)(limit - used));
            if (count <= 0)
            {
                complete = count == 0 && esp_http_client_is_complete_data_received(client);
                break;
            }
            used += (size_t)count;
        }
    }
    close_download(client);
    if (!text || !complete || memchr(text, 0, used))
    {
        free(text);
        return NULL;
    }
    text[used] = 0;
    return text;
}

static esp_err_t install_release(const app_ota_release_t *release,
                                 const esp_partition_t *target,
                                 const uint8_t expected[32])
{
    esp_http_client_handle_t client = open_download(release->url);
    if (!client)
        return ESP_FAIL;
    esp_ota_handle_t ota = 0;
    bool ota_open = false;
    esp_err_t result = ESP_FAIL;
    uint8_t *buffer = malloc(4096);
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    if (!buffer || mbedtls_sha256_starts(&hash, 0) != 0)
        goto done;

    int64_t length = esp_http_client_get_content_length(client);
    if (length > 0 && length != release->size)
        goto done;
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + 180000000LL;
    while (received < release->size && esp_timer_get_time() < deadline)
    {
        /* Az első 288 bájtban még flash-írás előtt ellenőrizzük a projektet
         * és a release-be írt verziót. Rövid TCP-olvasásokkal is működik. */
        size_t wanted = received == 0 ? 288 : 4096;
        if (wanted > release->size - received)
            wanted = release->size - received;
        size_t count = 0;
        while (count < wanted && esp_timer_get_time() < deadline)
        {
            int got = esp_http_client_read(client, (char *)buffer + count, (int)(wanted - count));
            if (got <= 0)
                goto done;
            count += (size_t)got;
        }
        if (count != wanted)
            goto done;
        if (!received)
        {
            esp_app_desc_t description;
            memcpy(&description, buffer + 32, sizeof(description));
            const esp_app_desc_t *running = esp_app_get_description();
            if (buffer[0] != 0xE9 || description.magic_word != ESP_APP_DESC_MAGIC_WORD ||
                memcmp(description.version, release->tag, strlen(release->tag) + 1) ||
                memcmp(description.project_name,
                       running->project_name,
                       sizeof(description.project_name)))
                goto done;
            result = esp_ota_begin(target, release->size, &ota);
            if (result != ESP_OK)
                goto done;
            ota_open = true;
        }
        result = ESP_FAIL;
        if (mbedtls_sha256_update(&hash, buffer, count) != 0 ||
            esp_ota_write(ota, buffer, count) != ESP_OK)
            goto done;
        received += count;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (received != release->size)
        goto done;
    /* A deklarált méreten túli vagy csonka HTTP-válasz sem fogadható el. */
    char extra;
    if (esp_http_client_read(client, &extra, 1) != 0 ||
        !esp_http_client_is_complete_data_received(client))
        goto done;
    uint8_t actual[32];
    if (mbedtls_sha256_finish(&hash, actual) != 0 || memcmp(actual, expected, 32))
        goto done;
    result = esp_ota_end(ota);
    ota_open = false;
    if (result != ESP_OK)
        goto done;
    /* Az előző nap utolsó mintaköze már lezárult; mindkét napot kiírjuk. */
    result = app_stats_save_now();
    if (result == ESP_OK)
        result = esp_ota_set_boot_partition(target);
done:
    if (ota_open)
        esp_ota_abort(ota);
    mbedtls_sha256_free(&hash);
    free(buffer);
    close_download(client);
    return result;
}

/* Külön hívható lépés, hogy a teljes letöltés/aktiválás hibautai tesztelhetők legyenek. */
static void check_release(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running || (esp_ota_get_state_partition(running, &state) == ESP_OK &&
                     state == ESP_OTA_IMG_PENDING_VERIFY))
        return;
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target)
        return;
    app_events_add(APP_EVENT_INFO, "Automatikus frissítés: GitHub-release ellenőrzése.");
    char *json = download_text(APP_OTA_RELEASE_API, 65536);
    if (!json)
        goto failed;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json, &end, true);
    app_ota_release_t release;
    bool available = root && app_ota_release_parse(
                                 root, esp_app_get_description()->version, target->size, &release);
    bool valid_json = root != NULL;
    cJSON_Delete(root);
    free(json);
    if (!valid_json)
        goto failed;
    if (!available)
    {
        app_events_add(APP_EVENT_INFO, "Automatikus frissítés: nincs újabb, teljes stabil kiadás.");
        return;
    }
    /* Visszagörgetett verziót nem próbálunk minden éjjel újratelepíteni. */
    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    esp_app_desc_t invalid_desc;
    if (invalid && esp_ota_get_partition_description(invalid, &invalid_desc) == ESP_OK &&
        !memcmp(invalid_desc.version, release.tag, strlen(release.tag) + 1))
    {
        app_events_add(APP_EVENT_WARNING,
                       "Automatikus frissítés: %s korábban visszaállítva, kihagyva.",
                       release.tag);
        return;
    }
    char *checksums = download_text(release.checksums_url, 16384);
    uint8_t expected[32];
    bool checksum_ok = checksums && app_ota_checksum_parse(checksums, release.filename, expected);
    free(checksums);
    if (!checksum_ok)
        goto failed;
    app_events_add(APP_EVENT_INFO, "Automatikus frissítés letöltése: %s", release.tag);
    if (install_release(&release, target, expected) != ESP_OK)
        goto failed;
    app_events_add(APP_EVENT_INFO, "Automatikus frissítés kész: %s; újraindítás.", release.tag);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return;
failed:
    app_events_add(APP_EVENT_WARNING,
                   "Automatikus frissítés sikertelen; a jelenlegi firmware megmarad. Új próba a "
                   "következő napváltáskor.");
}

static void day_changed(void)
{
    if (s_auto_task)
        xTaskNotifyGive(s_auto_task);
}

static void auto_update_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* A napváltást átfedő, megengedett mintaköz végét megvárjuk.
         * Ezalatt a statisztika, a BLE és a kijelző zavartalanul fut. */
        vTaskDelay(pdMS_TO_TICKS((s_settle_seconds + 1) * 1000U));
        if (!app_wifi_is_connected())
        {
            app_events_add(
                APP_EVENT_WARNING,
                "Automatikus frissítés kihagyva: nincs Wi-Fi. Új próba a következő napváltáskor.");
            continue;
        }
        if (!app_ota_try_begin_operation())
        {
            app_events_add(APP_EVENT_WARNING,
                           "Automatikus frissítés kihagyva: másik OTA/újraindítás folyamatban.");
            continue;
        }
        check_release();
        app_ota_end_operation();
    }
}

esp_err_t app_ota_start_auto_update(const app_config_t *cfg)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;
    if (!cfg->ota.auto_update || s_auto_task)
        return ESP_OK;
    s_settle_seconds = cfg->statistics.max_integration_gap_seconds;
    if (xTaskCreate(auto_update_task, "auto_ota", 8192, NULL, 3, &s_auto_task) != pdPASS)
        return ESP_ERR_NO_MEM;
    app_stats_set_day_change_callback(day_changed);
    return ESP_OK;
}
