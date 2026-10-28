/*
 * Gyári SD-témák frissítése a GitHubról.
 *
 * Ellenőrzés: a futó firmware tagjéhez tartozó templates/ könyvtár listája
 * (GitHub contents API) és témánként a template.json „version” mezője.
 * Telepítés: a template.json és az általa hivatkozott PNG-k letöltése az SD
 * download stage-ébe, majd a kézi ZIP-telepítéssel azonos validálás és atomi
 * csere. Hiba esetén a régi téma változatlan marad.
 *
 * Teljesítmény: csak kérésre fut, saját alacsony prioritású feladatban; a nagy
 * PNG-k PSRAM-pufferbe töltődnek, és egyetlen SD-írással kerülnek a stage-be.
 * A kijelző témaváltását nem lassítja (az SD-zárat csak a rövid írások idejére
 * tartja), és az OTA-val közös kizárást használja.
 */
#include "app_template_update.h"
#include "app_template_remote.h"
#include "app_templates.h"
#include "app_events.h"
#include "app_ota.h"
#include "app_storage.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define JSON_LIMIT (64U * 1024U)
#define LISTING_LIMIT (48U * 1024U)

typedef struct
{
    char id[33], name[81];
    unsigned installed, available;
    char status[48];
} item_t;

static SemaphoreHandle_t s_lock;
static app_template_update_state_t s_state;
static char s_tag[32], s_message[160], s_target[33];
static item_t s_items[APP_TEMPLATE_REMOTE_MAX_IDS];
static unsigned s_count;
static const char *TAG = "tpl_update";

/*
 * A legutóbbi letöltés hibájának rövid, felhasználónak is megmutatható oka.
 * Enélkül a webes felületen csak annyi látszott, hogy „a GitHub nem érhető el”,
 * és nem derült ki, hogy hálózati hiba, HTTP-állapot vagy memória okozta-e.
 */
static char s_reason[96];
/* A legutóbb sikeresen feloldott kiadás, hogy egy átmeneti API-hiba ne tiltsa le a frissítést. */
static char s_cached_tag[32];
static void reason_set(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(s_reason, sizeof(s_reason), format, args);
    va_end(args);
}

static bool ensure_lock(void)
{
    if (!s_lock)
        s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL;
}

static void set_state(app_template_update_state_t state, const char *message)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state = state;
    snprintf(s_message, sizeof(s_message), "%s", message ? message : "");
    xSemaphoreGive(s_lock);
}

typedef struct
{
    char location[512];
    bool too_long;
} redirect_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    redirect_t *redirect = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        !strcasecmp(event->header_key, "Location"))
    {
        if (!event->header_value || strlen(event->header_value) >= sizeof(redirect->location))
        {
            redirect->too_long = true;
            return ESP_FAIL;
        }
        snprintf(redirect->location, sizeof(redirect->location), "%s", event->header_value);
    }
    return ESP_OK;
}

/* HTTPS GET PSRAM-pufferbe, legfeljebb 3 engedélyezett átirányítással. */
static uint8_t *download(const char *url, size_t limit, size_t *size)
{
    char current[512];
    snprintf(current, sizeof(current), "%s", url);
    redirect_t *redirect = calloc(1, sizeof(*redirect));
    if (!redirect)
        return NULL;
    uint8_t *data = NULL;
    reason_set("ismeretlen hiba");
    for (unsigned attempt = 0; attempt < 4 && !data; attempt++)
    {
        if (!app_template_remote_url_allowed(current))
        {
            reason_set("nem engedélyezett cím");
            break;
        }
        redirect->location[0] = 0;
        esp_http_client_config_t config = {
            .url = current,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 15000,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .disable_auto_redirect = true,
            .event_handler = http_event,
            .user_data = redirect,
            .user_agent = "ZoleeEnergyFlowDisplay-templates",
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client)
        {
            reason_set("HTTP kliens nem indult");
            break;
        }
        if (!strncmp(current, "https://api.github.com/", 23))
        {
            esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
            esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
        }
        bool retry = false;
        esp_err_t opened = esp_http_client_open(client, 0);
        int64_t headers = opened == ESP_OK ? esp_http_client_fetch_headers(client) : -1;
        if (opened != ESP_OK)
            reason_set("kapcsolódás: %s", esp_err_to_name(opened));
        else if (headers < 0)
            reason_set("nincs válaszfejléc");
        if (opened == ESP_OK && headers >= 0)
        {
            int status = esp_http_client_get_status_code(client);
            if (status == 200)
            {
                int64_t expected = esp_http_client_get_content_length(client);
                size_t capacity = expected > 0 && (uint64_t)expected <= limit ? (size_t)expected : limit;
                /*
                 * A puffer PSRAM-ból a legjobb, de a kijelző témái is ott élnek:
                 * ha ott nincs hely, a belső RAM-ból is elfogadjuk, különben a
                 * teljes ellenőrzés elhasal („a GitHub nem érhető el”).
                 */
                uint8_t *buffer = heap_caps_malloc(capacity + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!buffer)
                    buffer = malloc(capacity + 1);
                size_t used = 0;
                bool ok = buffer && (expected <= 0 || (uint64_t)expected <= limit);
                if (!buffer)
                    reason_set("nincs elég memória (%u B)", (unsigned)(capacity + 1));
                else if (!ok)
                    reason_set("túl nagy válasz (%lld B)", (long long)expected);
                int64_t deadline = esp_timer_get_time() + 60000000LL;
                while (ok && esp_timer_get_time() < deadline)
                {
                    if (used == capacity)
                    {
                        /* Méret nélküli válasznál a korlátig nő a puffer. */
                        if (expected > 0 || capacity >= limit)
                            break;
                        size_t grown = capacity * 2 > limit ? limit : capacity * 2;
                        uint8_t *next = realloc(buffer, grown + 1);
                        if (!next)
                        {
                            ok = false;
                            break;
                        }
                        buffer = next;
                        capacity = grown;
                    }
                    int count = esp_http_client_read(client, (char *)buffer + used, (int)(capacity - used));
                    if (count < 0)
                        ok = false;
                    if (count <= 0)
                        break;
                    used += (size_t)count;
                }
                ok = ok && esp_http_client_is_complete_data_received(client) &&
                     (expected <= 0 || used == (size_t)expected);
                if (ok)
                {
                    buffer[used] = 0;
                    data = buffer;
                    *size = used;
                    reason_set("rendben");
                }
                else
                {
                    if (buffer)
                        reason_set("hiányos válasz (%u B)", (unsigned)used);
                    free(buffer);
                }
            }
            else if ((status == 301 || status == 302 || status == 307 || status == 308) &&
                     redirect->location[0])
            {
                snprintf(current, sizeof(current), "%s", redirect->location);
                retry = true;
            }
            else
            {
                reason_set(status == 403 || status == 429 ? "HTTP %d (a GitHub korlátozza a kéréseket)"
                                                          : "HTTP %d",
                           status);
                ESP_LOGW(TAG, "HTTP %d", status);
            }
        }
        esp_http_client_cleanup(client);
        if (!retry)
            break;
    }
    free(redirect);
    return data;
}

static cJSON *download_json(const char *url, size_t limit)
{
    size_t size = 0;
    uint8_t *text = download(url, limit, &size);
    cJSON *json = text ? cJSON_ParseWithLength((const char *)text, size) : NULL;
    if (text && !json)
        reason_set("a válasz nem JSON (%u B)", (unsigned)size);
    free(text);
    return json;
}

/*
 * Melyik kiadás témáit keressük. Kiadott firmware-nél a saját tagjét, hálózat
 * nélkül is. Fejlesztői (előzetes jelölésű) buildnél a GitHub legutóbbi stabil
 * kiadását; ennek hibáját a felhasználónak is megmutatjuk, mert eddig csak
 * annyi látszott, hogy „a GitHub nem érhető el”.
 */
static bool select_tag(char tag[32], char detail[160])
{
    const esp_app_desc_t *app = esp_app_get_description();
    const char *running = app && app->version[0] ? app->version : "ismeretlen";
    if (app_template_remote_tag(app ? app->version : NULL, NULL, tag))
    {
        app_events_add(APP_EVENT_INFO, "Témafrissítés: a futó kiadás témái (%s).", tag);
        return true;
    }
    app_events_add(APP_EVENT_INFO, "Témafrissítés: fejlesztői firmware (%s), legutóbbi kiadás lekérése.",
                   running);
    bool ok = false;
    /* Egy ismétlés: a Wi-Fi/DNS közvetlenül indulás vagy témaváltás után gyakran
     * csak másodpercekkel később válaszol. */
    for (unsigned attempt = 0; attempt < 2 && !ok; attempt++)
    {
        if (attempt)
            vTaskDelay(pdMS_TO_TICKS(1500));
        cJSON *release = download_json(APP_TEMPLATE_REMOTE_LATEST, JSON_LIMIT);
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(release, "tag_name");
        ok = cJSON_IsString(name) && app_template_remote_tag(NULL, name->valuestring, tag);
        if (!ok)
        {
            const char *why = !release ? s_reason
                              : !cJSON_IsString(name)
                                  ? "nincs tag_name a válaszban"
                                  : "a legutóbbi kiadás nem stabil vMAJOR.MINOR.PATCH";
            snprintf(detail, 160, "A legutóbbi kiadás nem kérdezhető le: %.64s (firmware: %.31s).",
                     why, running);
        }
        cJSON_Delete(release);
    }
    if (ok)
    {
        snprintf(s_cached_tag, sizeof(s_cached_tag), "%s", tag);
        app_events_add(APP_EVENT_INFO, "Témafrissítés: legutóbbi kiadás %s.", tag);
    }
    else if (s_cached_tag[0])
    {
        /* Átmeneti hiba (pl. a GitHub kéréskorlátja) ne vegye el a már ismert kiadást. */
        snprintf(tag, 32, "%s", s_cached_tag);
        app_events_add(APP_EVENT_WARNING, "Témafrissítés: %s Korábbi kiadás: %s.", detail,
                       s_cached_tag);
        detail[0] = 0;
        ok = true;
    }
    return ok;
}

static void check_task(void *arg)
{
    (void)arg;
    char tag[32], detail[160] = "";
    if (!select_tag(tag, detail))
    {
        if (!detail[0])
            snprintf(detail, sizeof(detail), "A GitHub nem érhető el, vagy nincs stabil kiadás.");
        app_events_add(APP_EVENT_WARNING, "Témafrissítés sikertelen: %s", detail);
        set_state(APP_TEMPLATE_UPDATE_ERROR, detail);
        vTaskDelete(NULL);
        return;
    }
    char url[256];
    snprintf(url, sizeof(url), APP_TEMPLATE_REMOTE_CONTENTS "%s", tag);
    cJSON *listing = download_json(url, LISTING_LIMIT);
    char (*ids)[33] = calloc(APP_TEMPLATE_REMOTE_MAX_IDS, 33);
    app_template_info_t *installed = calloc(APP_TEMPLATE_MAX, sizeof(*installed));
    item_t *items = calloc(APP_TEMPLATE_REMOTE_MAX_IDS, sizeof(*items));
    unsigned count = ids ? app_template_remote_ids(listing, ids, APP_TEMPLATE_REMOTE_MAX_IDS) : 0;
    cJSON_Delete(listing);
    if (!ids || !installed || !items || !count)
    {
        char message[160];
        snprintf(message, sizeof(message), "A témalista nem tölthető le (%.31s): %.90s.", tag,
                 ids && installed && items ? s_reason : "nincs elég memória");
        app_events_add(APP_EVENT_WARNING, "Témafrissítés sikertelen: %s", message);
        set_state(APP_TEMPLATE_UPDATE_ERROR, message);
        free(ids);
        free(installed);
        free(items);
        vTaskDelete(NULL);
        return;
    }
    unsigned local = app_templates_list(installed, APP_TEMPLATE_MAX, NULL);
    unsigned found = 0;
    for (unsigned i = 0; i < count; i++)
    {
        if (!app_template_remote_url(url, sizeof(url), tag, ids[i], "template.json"))
            continue;
        cJSON *document = download_json(url, JSON_LIMIT);
        if (!document)
            app_events_add(APP_EVENT_WARNING, "Témafrissítés: %s leírója nem tölthető le: %s",
                           ids[i], s_reason);
        const cJSON *version = cJSON_GetObjectItemCaseSensitive(document, "version");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(document, "name");
        if (cJSON_IsNumber(version) && version->valuedouble >= 1 && version->valuedouble < 100000)
        {
            item_t *item = &items[found++];
            snprintf(item->id, sizeof(item->id), "%s", ids[i]);
            snprintf(item->name, sizeof(item->name), "%s", cJSON_IsString(name) ? name->valuestring : ids[i]);
            item->available = (unsigned)version->valuedouble;
            for (unsigned j = 0; j < local; j++)
                if (!strcmp(installed[j].id, ids[i]))
                    item->installed = installed[j].version ? installed[j].version : 1;
        }
        cJSON_Delete(document);
    }
    unsigned updates = 0;
    for (unsigned i = 0; i < found; i++)
        if (items[i].installed && items[i].available > items[i].installed)
            updates++;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_items, items, sizeof(s_items));
    s_count = found;
    snprintf(s_tag, sizeof(s_tag), "%s", tag);
    s_state = APP_TEMPLATE_UPDATE_READY;
    if (updates)
        snprintf(s_message, sizeof(s_message), "%u téma frissíthető (%s).", updates, tag);
    else if (found < count)
        snprintf(s_message, sizeof(s_message),
                 "Minden telepített téma naprakész (%s); %u téma leírója nem volt letölthető.",
                 tag, count - found);
    else
        snprintf(s_message, sizeof(s_message), "Minden telepített téma naprakész (%s).", tag);
    xSemaphoreGive(s_lock);
    free(ids);
    free(installed);
    free(items);
    vTaskDelete(NULL);
}

/* Egy fájl a stage-be: a teljes tartalom egyetlen írással, rövid SD-zár alatt. */
static bool stage_file(const char *name, const uint8_t *data, size_t size)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", app_templates_download_stage(), name);
    if (app_storage_lock() != ESP_OK)
        return false;
    FILE *file = fopen(path, "wb");
    bool ok = file && fwrite(data, 1, size, file) == size;
    if (file && fclose(file) != 0)
        ok = false;
    app_storage_unlock();
    return ok;
}

static bool install_one(const char *tag, const char *id, char error[160])
{
    char url[256];
    size_t size = 0;
    app_templates_download_reset();
    if (!app_template_remote_url(url, sizeof(url), tag, id, "template.json"))
    {
        snprintf(error, 160, "%s", "Érvénytelen téma-azonosító");
        return false;
    }
    uint8_t *json = download(url, JSON_LIMIT, &size);
    cJSON *document = json ? cJSON_ParseWithLength((const char *)json, size) : NULL;
    char (*files)[65] = calloc(APP_TEMPLATE_REMOTE_MAX_FILES, 65);
    unsigned count = 0;
    bool ok = document && files &&
              app_template_remote_files(document, files, APP_TEMPLATE_REMOTE_MAX_FILES, &count) &&
              stage_file("template.json", json, size);
    cJSON_Delete(document);
    free(json);
    if (!ok)
        snprintf(error, 160, "A template.json nem tölthető le vagy hibás (%.90s)", s_reason);
    for (unsigned i = 0; ok && i < count; i++)
    {
        uint8_t *data = NULL;
        if (app_template_remote_url(url, sizeof(url), tag, id, files[i]))
            data = download(url, APP_TEMPLATE_PNG_MAX, &size);
        bool fetched = data != NULL;
        ok = fetched && stage_file(files[i], data, size);
        free(data);
        if (!ok)
            snprintf(error, 160, "%.64s: %.80s", files[i], fetched ? "SD-írási hiba" : s_reason);
    }
    free(files);
    if (ok && app_templates_install_downloaded(error) != ESP_OK)
        ok = false;
    if (!ok)
        app_templates_download_reset();
    return ok;
}

static void install_task(void *arg)
{
    (void)arg;
    char tag[32], target[33];
    item_t *items = calloc(APP_TEMPLATE_REMOTE_MAX_IDS, sizeof(*items));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(tag, sizeof(tag), "%s", s_tag);
    snprintf(target, sizeof(target), "%s", s_target);
    unsigned count = s_count;
    if (items)
        memcpy(items, s_items, sizeof(s_items));
    xSemaphoreGive(s_lock);
    unsigned done = 0, failed = 0;
    char error[160] = {0};
    for (unsigned i = 0; items && i < count; i++)
    {
        bool wanted = !strcmp(target, "all")
                          ? items[i].installed && items[i].available > items[i].installed
                          : !strcmp(target, items[i].id);
        if (!wanted)
            continue;
        char progress[160];
        snprintf(progress, sizeof(progress), "Letöltés és telepítés: %s…", items[i].name);
        set_state(APP_TEMPLATE_UPDATE_INSTALLING, progress);
        bool ok = install_one(tag, items[i].id, error);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (unsigned j = 0; j < s_count; j++)
            if (!strcmp(s_items[j].id, items[i].id))
            {
                if (ok)
                    s_items[j].installed = s_items[j].available;
                snprintf(s_items[j].status, sizeof(s_items[j].status), "%s", ok ? "Telepítve" : "Hiba");
            }
        xSemaphoreGive(s_lock);
        if (ok)
        {
            done++;
            app_events_add(APP_EVENT_INFO, "Téma frissítve a GitHubról: %s (%s).", items[i].id, tag);
        }
        else
        {
            failed++;
            app_events_add(APP_EVENT_WARNING, "Témafrissítés sikertelen: %s – %s", items[i].id, error);
        }
    }
    free(items);
    char message[160];
    if (failed)
        snprintf(message, sizeof(message), "%u téma frissítve, %u sikertelen. Utolsó hiba: %.80s",
                 done, failed, error);
    else if (done)
        snprintf(message, sizeof(message), "%u téma frissítve. A kijelző az új változatot azonnal használja.", done);
    else
        snprintf(message, sizeof(message), "%s", "Nem volt telepítendő téma.");
    set_state(failed ? APP_TEMPLATE_UPDATE_ERROR : APP_TEMPLATE_UPDATE_READY, message);
    app_ota_end_operation();
    vTaskDelete(NULL);
}

static bool busy(void)
{
    return s_state == APP_TEMPLATE_UPDATE_CHECKING || s_state == APP_TEMPLATE_UPDATE_INSTALLING;
}

esp_err_t app_template_update_check(void)
{
    if (!ensure_lock())
        return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (busy())
    {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_state = APP_TEMPLATE_UPDATE_CHECKING;
    snprintf(s_message, sizeof(s_message), "%s", "Témák ellenőrzése a GitHubon…");
    xSemaphoreGive(s_lock);
    if (xTaskCreate(check_task, "tpl_check", 8192, NULL, 3, NULL) != pdPASS)
    {
        set_state(APP_TEMPLATE_UPDATE_ERROR, "Nincs elegendő memória az ellenőrzéshez.");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_template_update_install(const char *id)
{
    if (!ensure_lock() || !id || (strcmp(id, "all") && !app_templates_safe_id(id)))
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ready = s_state == APP_TEMPLATE_UPDATE_READY || s_state == APP_TEMPLATE_UPDATE_ERROR;
    bool known = !strcmp(id, "all");
    for (unsigned i = 0; i < s_count; i++)
        if (!strcmp(s_items[i].id, id))
            known = true;
    if (!ready || !s_count || !known || !s_tag[0])
    {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_lock);
    /* Az OTA-val és a beállításmentéssel közös kizárás: egyszerre egy SD/flash-művelet. */
    if (!app_ota_try_begin_operation())
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_target, sizeof(s_target), "%s", id);
    s_state = APP_TEMPLATE_UPDATE_INSTALLING;
    snprintf(s_message, sizeof(s_message), "%s", "Telepítés indítása…");
    xSemaphoreGive(s_lock);
    if (xTaskCreate(install_task, "tpl_install", 8192, NULL, 3, NULL) != pdPASS)
    {
        app_ota_end_operation();
        set_state(APP_TEMPLATE_UPDATE_ERROR, "Nincs elegendő memória a telepítéshez.");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

cJSON *app_template_update_status(void)
{
    if (!ensure_lock())
        return NULL;
    cJSON *root = cJSON_CreateObject(), *items = cJSON_CreateArray();
    if (!root || !items)
    {
        cJSON_Delete(root);
        cJSON_Delete(items);
        return NULL;
    }
    static const char *const states[] = {"idle", "checking", "ready", "installing", "error"};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddStringToObject(root, "state", states[s_state]);
    cJSON_AddStringToObject(root, "tag", s_tag);
    cJSON_AddStringToObject(root, "message", s_message);
    for (unsigned i = 0; i < s_count; i++)
    {
        cJSON *item = cJSON_CreateObject();
        if (!item)
            break;
        cJSON_AddStringToObject(item, "id", s_items[i].id);
        cJSON_AddStringToObject(item, "name", s_items[i].name);
        cJSON_AddNumberToObject(item, "installed", s_items[i].installed);
        cJSON_AddNumberToObject(item, "available", s_items[i].available);
        cJSON_AddBoolToObject(item, "update", s_items[i].installed && s_items[i].available > s_items[i].installed);
        cJSON_AddStringToObject(item, "status", s_items[i].status);
        cJSON_AddItemToArray(items, item);
    }
    xSemaphoreGive(s_lock);
    cJSON_AddItemToObject(root, "items", items);
    return root;
}
