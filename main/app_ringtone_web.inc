/* A csomagfeltöltés a meglévő webes hitelesítést és explicit kérésfejlécet
 * használja. A Slave csak a BLE titkosított profilban kapott médiakulccsal olvas. */
#include "app_ringtones.h"
#include "app_storage.h"
#include "app_intercom_control.h"
#include <stdatomic.h>
#include "app_web_upload.inc"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_err_t ringtone_upload(httpd_req_t* req)
{
    if (!authorize(req))
        return ESP_OK;
    char header[8];
    if (httpd_req_get_hdr_value_str(req, "X-Zolee-Loxone", header, sizeof(header)) != ESP_OK
        || strcmp(header, "1") || req->content_len < 22 || req->content_len > 8U * 1024U * 1024U)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Hibás ZIP-kérés (maximum 8 MiB).");
    app_intercom_snapshot_t state = { 0 };
    if (app_loxone_intercom_snapshot(&state) && (state.ringing || state.audio_active))
        return httpd_resp_send_err(
            req, HTTPD_400_BAD_REQUEST, "Várd meg a csengetés vagy beszélgetés végét.");
    char path[256], error[160] = "Csengőhang telepítése sikertelen.";
    snprintf(path, sizeof(path), "%s/.ringtone-upload.zip", app_templates_root());
    bool ok = receive_sd_zip(req, path, error);
    esp_err_t result = ok ? app_ringtones_install(path, error) : ESP_FAIL;
    if (app_storage_lock() == ESP_OK) {
        remove(path);
        app_storage_unlock();
    }
    if (result != ESP_OK)
        return httpd_resp_send_err(req, ok ? HTTPD_400_BAD_REQUEST : HTTPD_500_INTERNAL_SERVER_ERROR, error);
    return httpd_resp_sendstr(
        req, "Csengőhangcsomag telepítve. Válassz hangot, majd mentsd a kaputelefon beállításait.");
}

/* A böngésző csak lejátszási ütemben kérheti a WAV bájtjait. A szinkron
 * httpd_resp_send emiatt az egyetlen HTTP-kezelőt és az ikonokat is megakasztaná.
 * A leválasztott kérés belső veremmel dolgozik; SD-zár csak a beolvasás alatt él. */
typedef struct {
    httpd_req_t* request;
    char name[65];
} ringtone_response_t;
static atomic_uint s_ringtone_responses;

static void ringtone_response_worker(void* argument)
{
    ringtone_response_t* response = argument;
    size_t size = 0;
    uint8_t* data = app_ringtones_load(response->name, &size);
    if (data) {
        httpd_resp_set_type(response->request, "audio/wav");
        httpd_resp_set_hdr(response->request, "Cache-Control", "no-store");
        /* Az SD-zár itt már nincs fogva, a többi HTTP-kérés közben futhat. */
        httpd_resp_send(response->request, (const char*)data, size);
        free(data);
    } else {
        httpd_resp_send_err(response->request, HTTPD_404_NOT_FOUND, "A csengőhang nem tölthető be.");
    }
    httpd_req_async_handler_complete(response->request);
    free(response);
    atomic_fetch_sub(&s_ringtone_responses, 1);
    vTaskDelete(NULL);
}

static esp_err_t ringtone_get(httpd_req_t* req)
{
    char key[65];
    bool slave = httpd_req_get_hdr_value_str(req, "X-Zolee-Media", key, sizeof(key)) == ESP_OK
        && app_intercom_control_media_key(key);
    memset(key, 0, sizeof(key));
    if (!slave && !authorize(req))
        return ESP_OK;
    char query[96], name[65];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK
        || httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK || !app_ringtone_name(name))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Érvénytelen hangazonosító.");
    /* A lassú/leállított lejátszók nem indíthatnak korlátlan hangletöltést. */
    if (atomic_fetch_add(&s_ringtone_responses, 1) >= 4) {
        atomic_fetch_sub(&s_ringtone_responses, 1);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Túl sok párhuzamos hangletöltés.");
    }
    ringtone_response_t* response = calloc(1, sizeof(*response));
    if (!response) {
        atomic_fetch_sub(&s_ringtone_responses, 1);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Nincs memória a hangmintához.");
    }
    snprintf(response->name, sizeof(response->name), "%s", name);
    if (httpd_req_async_handler_begin(req, &response->request) != ESP_OK) {
        free(response);
        atomic_fetch_sub(&s_ringtone_responses, 1);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "A hangminta nem indítható.");
    }
    /* Sima xTaskCreate: kicsi belső RAM-os verem, SD/ROM-hívásokhoz is. */
    if (xTaskCreate(ringtone_response_worker, "ringtone_http", 4096, response, 3, NULL) != pdPASS) {
        httpd_resp_send_err(response->request, HTTPD_500_INTERNAL_SERVER_ERROR, "A hangminta feladata nem indítható.");
        httpd_req_async_handler_complete(response->request);
        free(response);
        atomic_fetch_sub(&s_ringtone_responses, 1);
    }
    return ESP_OK;
}
