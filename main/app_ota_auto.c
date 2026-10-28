/* Napváltáshoz kötött, kikapcsolható GitHub OTA; nincs induláskori frissítés. */
#include "app_ota.h"
#include "app_ota_release.h"
#include "app_events.h"
#include "app_stats.h"
#include "app_wifi.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

static atomic_flag s_operation = ATOMIC_FLAG_INIT;
/* Napváltáskor egyszer fut le; állandó feladat nem köti le a belső RAM-ot. */
static volatile bool s_auto_busy;
static bool s_auto_enabled;
static uint32_t s_settle_seconds;

bool app_ota_try_begin_operation(void)
{
    return !atomic_flag_test_and_set(&s_operation);
}

void app_ota_end_operation(void)
{
    atomic_flag_clear(&s_operation);
}

/* Egy ellenőrzés diagnosztikája: a rövid összegzésben is megmarad a hibafázis.
 * URL-t nem naplózunk: az átirányítás aláírt lekérdezési paramétert tartalmazhat. */
typedef struct
{
    const char *stage;
    const char *reason;
    int code;
    size_t received, total;
} ota_trace_t;

static void ota_memory(const char *stage)
{
    app_events_add(APP_EVENT_INFO, "OTA RAM [%s]: int=%u; max-int=%u; PSRAM=%u; max-PSRAM=%u B",
                   stage,
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static void ota_error(ota_trace_t *trace, const char *reason, int code)
{
    if (code == ESP_ERR_NO_MEM)
        ota_memory(trace->stage);
    trace->reason = reason;
    trace->code = code;
    app_events_add(APP_EVENT_WARNING, "OTA hiba [%s]: %s; kód=%d; %u/%u B",
                   trace->stage, reason, code,
                   (unsigned)trace->received, (unsigned)trace->total);
}

static void network_error(esp_http_client_handle_t client, ota_trace_t *trace,
                          const char *reason, int code)
{
    ota_error(trace, reason, code);
    int tls_code = 0, flags = 0;
    esp_err_t tls = esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &flags);
    app_events_add(APP_EVENT_WARNING,
                   "OTA hálózat [%s]: errno=%d; TLS=0x%x; mbedTLS=%d; cert=0x%x",
                   trace->stage, esp_http_client_get_errno(client),
                   (unsigned)tls, tls_code, (unsigned)flags);
    /* A titkosító/DMA alréteg általános -1 kódot is adhat memóriahiánynál.
     * Ezért a hiba pillanatát is mérjük, nem csak a kapcsolódás előtti állapotot. */
    ota_memory(trace->stage);
}

static void ota_stage(ota_trace_t *trace, const char *stage)
{
    trace->stage = stage;
    app_events_add(APP_EVENT_INFO, "OTA szakasz: %s", stage);
}

typedef struct
{
    char location[2048];
    bool location_too_long;
} redirect_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    redirect_t *redirect = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        !strcasecmp(event->header_key, "Location"))
    {
        const char *value = event->header_value;
        if (!value || strlen(value) >= sizeof(redirect->location))
        {
            redirect->location_too_long = true;
            return ESP_FAIL;
        }
        snprintf(redirect->location, sizeof(redirect->location), "%s", value);
    }
    return ESP_OK;
}

/* Minden új TLS kapcsolat ellenőrzi a tanúsítványláncot és a hostnevet.
 * Az automatikus redirect tiltott: HTTP-ra és idegen hostra nem követünk át. */
static esp_http_client_handle_t open_download(const char *url, ota_trace_t *trace)
{
    redirect_t *redirect = calloc(1, sizeof(*redirect));
    char *current = malloc(2048);
    if (!redirect || !current)
    {
        free(redirect);
        free(current);
        ota_error(trace, "nincs memória HTTP-klienshez", ESP_ERR_NO_MEM);
        return NULL;
    }
    snprintf(current, 2048, "%s", url);
    esp_http_client_handle_t client = NULL;
    unsigned connect_retries = 0;
    for (unsigned attempt = 0; attempt < 5; attempt++)
    {
        if (!app_ota_download_url_allowed(current))
        {
            ota_error(trace, "tiltott HTTPS átirányítás", ESP_FAIL);
            break;
        }
        /* Csak a host jelenik meg, a signed URL és annak tokenje soha. */
        const char *host = current + 8;
        const char *slash = strchr(host, '/');
        app_events_add(APP_EVENT_INFO, "OTA [%s]: HTTPS %.*s; lépés=%u/5; timeout=10 s",
                       trace->stage, (int)(slash - host), host, attempt + 1);
        redirect->location[0] = 0;
        redirect->location_too_long = false;
        /* Az ESP-IDF a teljes GET kérősort ebbe a pufferbe írja.
         * A GitHub aláírt átirányítása 1024 bájtnál hosszabb is lehet. */
        size_t tx_size = strlen(current) + 128;
        if (tx_size < 1024)
            tx_size = 1024;
        app_events_add(APP_EVENT_INFO, "OTA [%s]: URL=%u B; TX-puffer=%u B",
                       trace->stage, (unsigned)strlen(current), (unsigned)tx_size);
        ota_memory(trace->stage);
        esp_http_client_config_t config = {
            .url = current,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 10000,
            .buffer_size = 4096,
            .buffer_size_tx = (int)tx_size,
            .disable_auto_redirect = true,
            .event_handler = http_event,
            .user_data = redirect,
            .user_agent = "ZoleeEnergyFlowDisplay-auto-ota",
        };
        client = esp_http_client_init(&config);
        if (!client)
        {
            ota_error(trace, "HTTP inicializálás sikertelen", ESP_ERR_NO_MEM);
            break;
        }
        esp_http_client_set_header(client, "Accept", "application/octet-stream");
        if (!strncmp(current, "https://api.github.com/", 23))
        {
            esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
            esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
        }
        esp_err_t err = esp_http_client_open(client, 0);
        int64_t header_result = err == ESP_OK ? esp_http_client_fetch_headers(client) : -1;
        if (err == ESP_OK && header_result >= 0)
        {
            int status = esp_http_client_get_status_code(client);
            app_events_add(APP_EVENT_INFO, "OTA [%s]: HTTP %d; Content-Length=%" PRId64,
                           trace->stage, status, esp_http_client_get_content_length(client));
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
        if (redirect->location_too_long)
            ota_error(trace, "túl hosszú Location fejléc", ESP_ERR_INVALID_SIZE);
        else if (err != ESP_OK)
        {
            network_error(client, trace, "HTTPS kapcsolódás/küldés sikertelen", err);
            /* A TLS-kontextus már a kapcsolódás előtt elbukhat, ha éppen kevés a
             * szabad belső RAM (más feladat nagy foglalást tart). Rövid szünet
             * után kétszer újrapróbáljuk, mielőtt napváltásig feladnánk. */
            if (++connect_retries <= 2)
            {
                esp_http_client_cleanup(client);
                client = NULL;
                app_events_add(APP_EVENT_INFO, "OTA [%s]: új kapcsolódási kísérlet 3 s múlva (%u/2).",
                               trace->stage, connect_retries);
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
        }
        else if (header_result < 0)
            network_error(client, trace, "HTTP fejléc olvasási hiba", (int)header_result);
        else
            ota_error(trace, "nem 200-as HTTP válasz", esp_http_client_get_status_code(client));
        esp_http_client_cleanup(client);
        client = NULL;
        break;
    }
    if (!client && !trace->reason)
        ota_error(trace, "túl sok átirányítás (max. 5)", ESP_FAIL);
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

static char *download_text(const char *url, size_t limit, ota_trace_t *trace)
{
    trace->received = trace->total = 0;
    esp_http_client_handle_t client = open_download(url, trace);
    if (!client)
        return NULL;
    ota_memory(trace->stage);
    char *text = malloc(limit + 1);
    size_t used = 0;
    int64_t expected_length = esp_http_client_get_content_length(client);
    trace->total = expected_length > 0 ? (size_t)expected_length : 0;
    app_events_add(APP_EVENT_INFO, "OTA [%s]: válasz letöltése; limit=%u B",
                   trace->stage, (unsigned)limit);
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
                if (!complete)
                    network_error(client, trace, count < 0 ? "HTTP olvasási hiba" : "csonka HTTP válasz", count);
                break;
            }
            used += (size_t)count;
            trace->received = used;
        }
    }
    if (!text)
        ota_error(trace, "nincs memória a válaszhoz", ESP_ERR_NO_MEM);
    else if (!complete && !trace->reason)
        ota_error(trace, used == limit ? "válaszméret-korlát" : "válasz időlimit (30 s)", ESP_FAIL);
    else if (complete && memchr(text, 0, used))
        ota_error(trace, "nulla bájt a szöveges válaszban", ESP_FAIL);
    close_download(client);
    if (!text || !complete || memchr(text, 0, used))
    {
        free(text);
        return NULL;
    }
    text[used] = 0;
    app_events_add(APP_EVENT_INFO, "OTA [%s]: letöltés kész; %u B", trace->stage, (unsigned)used);
    return text;
}

static esp_err_t install_release(const app_ota_release_t *release,
                                 const esp_partition_t *target,
                                 const uint8_t expected[32], ota_trace_t *trace)
{
    trace->received = 0;
    trace->total = release->size;
    esp_http_client_handle_t client = open_download(release->url, trace);
    if (!client)
        return ESP_FAIL;
    esp_ota_handle_t ota = 0;
    bool ota_open = false;
    esp_err_t result = ESP_FAIL;
    uint8_t *buffer = malloc(4096);
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    if (!buffer)
    {
        ota_error(trace, "nincs memória a firmware pufferhez", ESP_ERR_NO_MEM);
        goto done;
    }
    int hash_error = mbedtls_sha256_starts(&hash, 0);
    if (hash_error != 0)
    {
        ota_error(trace, "SHA256 inicializálás", hash_error);
        goto done;
    }
    trace->total = release->size;

    int64_t length = esp_http_client_get_content_length(client);
    if (length > 0 && length != release->size)
    {
        app_events_add(APP_EVENT_WARNING, "OTA firmware: HTTP méret=%" PRId64 "; várt=%u B",
                       length, (unsigned)release->size);
        ota_error(trace, "eltérő firmware méret", ESP_ERR_INVALID_SIZE);
        goto done;
    }
    app_events_add(APP_EVENT_INFO, "OTA firmware: adatfogadás indul; várt=%u B; időlimit=180 s",
                   (unsigned)release->size);
    size_t received = 0;
    unsigned next_progress = 25;
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
            {
                network_error(client, trace, got < 0 ? "firmware olvasási hiba" : "firmware csonka", got);
                goto done;
            }
            count += (size_t)got;
            trace->received = received + count;
        }
        if (count != wanted)
        {
            ota_error(trace, "firmware időlimit (180 s)", ESP_FAIL);
            goto done;
        }
        if (!received)
        {
            esp_app_desc_t description;
            memcpy(&description, buffer + 32, sizeof(description));
            const esp_app_desc_t *running = esp_app_get_description();
            ota_stage(trace, "képfejléc");
            trace->received = count;
            trace->total = release->size;
            if (buffer[0] != 0xE9 || description.magic_word != ESP_APP_DESC_MAGIC_WORD)
            {
                ota_error(trace, "érvénytelen ESP képfejléc", ESP_FAIL);
                goto done;
            }
            if (memcmp(description.version, release->tag, strlen(release->tag) + 1))
            {
                app_events_add(APP_EVENT_WARNING, "OTA verzió: kapott=%.31s; várt=%.31s",
                               description.version, release->tag);
                ota_error(trace, "eltérő firmware verzió", ESP_FAIL);
                goto done;
            }
            if (memcmp(description.project_name, running->project_name, sizeof(description.project_name)))
            {
                app_events_add(APP_EVENT_WARNING, "OTA projekt: kapott=%.31s; várt=%.31s",
                               description.project_name, running->project_name);
                ota_error(trace, "másik projekthez készült kép", ESP_FAIL);
                goto done;
            }
            ota_stage(trace, "flash-nyitás");
            result = esp_ota_begin(target, release->size, &ota);
            if (result != ESP_OK)
            {
                ota_error(trace, "esp_ota_begin", result);
                goto done;
            }
            ota_stage(trace, "firmware");
            trace->received = count;
            trace->total = release->size;
            ota_open = true;
        }
        result = ESP_FAIL;
        hash_error = mbedtls_sha256_update(&hash, buffer, count);
        if (hash_error != 0)
        {
            ota_error(trace, "SHA256 számítás", hash_error);
            goto done;
        }
        esp_err_t write_error = esp_ota_write(ota, buffer, count);
        if (write_error != ESP_OK)
        {
            ota_error(trace, "esp_ota_write", write_error);
            goto done;
        }
        received += count;
        if (received * 100ULL / release->size >= next_progress)
        {
            app_events_add(APP_EVENT_INFO, "OTA firmware: %u%%; %u/%u B fogadva/flash-be írva",
                           (unsigned)(received * 100ULL / release->size),
                           (unsigned)received, (unsigned)release->size);
            next_progress = (unsigned)(received * 100ULL / release->size) + 25;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (received != release->size)
    {
        ota_error(trace, "firmware időlimit (180 s)", ESP_FAIL);
        goto done;
    }
    /* A deklarált méreten túli vagy csonka HTTP-válasz sem fogadható el. */
    char extra;
    int extra_count = esp_http_client_read(client, &extra, 1);
    if (extra_count != 0 || !esp_http_client_is_complete_data_received(client))
    {
        network_error(client, trace, "firmware HTTP lezárás/méret", extra_count);
        goto done;
    }
    ota_stage(trace, "SHA256");
    uint8_t actual[32];
    hash_error = mbedtls_sha256_finish(&hash, actual);
    if (hash_error != 0 || memcmp(actual, expected, 32))
    {
        ota_error(trace, hash_error ? "SHA256 véglegesítés" : "eltérő SHA256 ellenőrzőösszeg", hash_error ? hash_error : ESP_FAIL);
        goto done;
    }
    ota_stage(trace, "képellenőrzés");
    result = esp_ota_end(ota);
    ota_open = false;
    if (result != ESP_OK)
    {
        ota_error(trace, "esp_ota_end", result);
        goto done;
    }
    /* Az előző nap utolsó mintaköze már lezárult; mindkét napot kiírjuk. */
    ota_stage(trace, "statisztika");
    result = app_stats_save_now();
    if (result != ESP_OK)
        ota_error(trace, "SD/statisztika mentése", result);
    else
    {
        ota_stage(trace, "aktiválás");
        result = esp_ota_set_boot_partition(target);
        if (result != ESP_OK)
            ota_error(trace, "esp_ota_set_boot_partition", result);
    }
done:
    if (ota_open)
    {
        esp_err_t abort_error = esp_ota_abort(ota);
        if (abort_error != ESP_OK)
            app_events_add(APP_EVENT_WARNING, "OTA megszakítás: esp_ota_abort kód=%d", abort_error);
    }
    mbedtls_sha256_free(&hash);
    free(buffer);
    close_download(client);
    return result;
}

/* Külön hívható lépés, hogy a teljes letöltés/aktiválás hibautai tesztelhetők legyenek. */
static void check_release(void)
{
    ota_trace_t trace = {.stage = "előkészítés"};
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running || (esp_ota_get_state_partition(running, &state) == ESP_OK &&
                     state == ESP_OTA_IMG_PENDING_VERIFY))
    {
        app_events_add(APP_EVENT_WARNING, "OTA kihagyva: nincs futó partíció vagy még nem véglegesített firmware.");
        return;
    }
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target)
    {
        ota_error(&trace, "nincs OTA célpartíció", ESP_ERR_NOT_FOUND);
        return;
    }
    app_events_add(APP_EVENT_INFO, "Automatikus frissítés: GitHub-release ellenőrzése.");
    ota_stage(&trace, "release");
    char *json = download_text(APP_OTA_RELEASE_API, 65536, &trace);
    if (!json)
        goto failed;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json, &end, true);
    app_ota_release_t release;
    bool available = root && app_ota_release_parse(
                                 root, esp_app_get_description()->version, target->size, &release);
    bool valid_json = root != NULL;
    if (root)
    {
        const cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
        const char *candidate = cJSON_IsString(tag) ? tag->valuestring : "(hiányzik)";
        app_events_add(APP_EVENT_INFO, "OTA verzió: futó=%.31s; GitHub=%.31s",
                       esp_app_get_description()->version, candidate);
        if (!available)
            app_events_add(APP_EVENT_INFO,
                           "OTA release nem telepíthető: verzió/stabilitás/asset/URL/méret; OTA-cél=%u B",
                           (unsigned)target->size);
    }
    cJSON_Delete(root);
    free(json);
    if (!valid_json)
    {
        ota_error(&trace, "érvénytelen release JSON", ESP_FAIL);
        goto failed;
    }
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
    app_events_add(APP_EVENT_INFO, "OTA kiadás: %s; firmware=%u B; célpartíció=%u B",
                   release.tag, (unsigned)release.size, (unsigned)target->size);
    ota_stage(&trace, "SHA256-lista");
    char *checksums = download_text(release.checksums_url, 16384, &trace);
    uint8_t expected[32];
    bool checksum_ok = checksums && app_ota_checksum_parse(checksums, release.filename, expected);
    bool had_checksums = checksums != NULL;
    free(checksums);
    if (!checksum_ok)
    {
        if (had_checksums && !trace.reason)
            ota_error(&trace, "hiányzó/hibás SHA256 bejegyzés", ESP_FAIL);
        goto failed;
    }
    app_events_add(APP_EVENT_INFO, "Automatikus frissítés letöltése: %s", release.tag);
    ota_stage(&trace, "firmware");
    if (install_release(&release, target, expected, &trace) != ESP_OK)
        goto failed;
    app_events_add(APP_EVENT_INFO, "Automatikus frissítés kész: %s; újraindítás.", release.tag);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return;
failed:
    app_events_add(APP_EVENT_WARNING, "OTA sikertelen [%s]: %s; kód=%d. Firmware megmarad.",
                   trace.stage, trace.reason ? trace.reason : "ismeretlen hiba", trace.code);
    app_events_add(APP_EVENT_INFO, "Automatikus frissítés: új próba a következő napváltáskor.");
}

/* Kézi kérésnél a HTTP-task nem végez hálózati vagy flash-műveletet.
 * A foglalást még a task létrehozása előtt megszerezzük: dupla kattintás,
 * éjféli ellenőrzés és kézi feltöltés sem indulhat egymásra. */
static void manual_auto_update_task(void *arg)
{
    (void)arg;
    check_release();
    app_ota_end_operation();
    vTaskDelete(NULL);
}

esp_err_t app_ota_request_auto_update(void)
{
    if (!app_wifi_is_connected() || !app_ota_try_begin_operation())
        return ESP_ERR_INVALID_STATE;
    app_events_add(APP_EVENT_INFO, "Automatikus frissítés kézi ellenőrzése kérve a weboldalról.");
    if (xTaskCreate(manual_auto_update_task, "manual_auto_ota", 8192, NULL, 3, NULL) != pdPASS)
    {
        ota_memory("kézi indítás");
        app_events_add(APP_EVENT_WARNING, "OTA kézi indítás: nincs memória a háttérfeladathoz.");
        app_ota_end_operation();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void auto_update_task(void *arg);

static void day_changed(void)
{
    if (s_auto_busy)
        return;
    s_auto_busy = true;
    if (xTaskCreate(auto_update_task, "auto_ota", 8192, NULL, 3, NULL) != pdPASS)
    {
        s_auto_busy = false;
        ota_memory("napváltás");
        app_events_add(APP_EVENT_WARNING,
                       "Automatikus frissítés kihagyva: nincs memória a feladathoz. "
                       "Új próba a következő napváltáskor.");
    }
}

/*
 * Napváltáskor induló, egyszer lefutó feladat. Korábban állandóan élt, és
 * napi egy ellenőrzésért 8 KiB belső RAM-ot kötött le; a kijelzőnek, az SD
 * DMA-nak és a TLS-nek viszont pont ez a szűk keret hiányzott.
 */
static void auto_update_task(void *arg)
{
    (void)arg;
    /* A napváltást átfedő, megengedett mintaköz végét megvárjuk.
     * Ezalatt a statisztika, a BLE és a kijelző zavartalanul fut. */
    vTaskDelay(pdMS_TO_TICKS((s_settle_seconds + 1) * 1000U));
    if (!app_wifi_is_connected())
        app_events_add(
            APP_EVENT_WARNING,
            "Automatikus frissítés kihagyva: nincs Wi-Fi. Új próba a következő napváltáskor.");
    else if (!app_ota_try_begin_operation())
        app_events_add(APP_EVENT_WARNING,
                       "Automatikus frissítés kihagyva: másik OTA/újraindítás folyamatban.");
    else
    {
        check_release();
        app_ota_end_operation();
    }
    s_auto_busy = false;
    vTaskDelete(NULL);
}

esp_err_t app_ota_start_auto_update(const app_config_t *cfg)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;
    if (!cfg->ota.auto_update || s_auto_enabled)
        return ESP_OK;
    s_settle_seconds = cfg->statistics.max_integration_gap_seconds;
    s_auto_enabled = true;
    app_stats_set_day_change_callback(day_changed);
    return ESP_OK;
}
