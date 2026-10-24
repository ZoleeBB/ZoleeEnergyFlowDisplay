/* A csomagfeltöltés a meglévő webes hitelesítést és explicit kérésfejlécet
 * használja. A Slave csak a BLE titkosított profilban kapott médiakulccsal olvas. */
#include "app_ringtones.h"
#include "app_storage.h"
#include "app_intercom_control.h"

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
    if (app_storage_lock() != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Az SD-kártya nem érhető el.");
    const char* path = APP_SD_MOUNT_POINT "/.ringtone-upload.zip";
    FILE* file = fopen(path, "wb");
    char buffer[1024];
    size_t left = req->content_len;
    bool ok = file != NULL;
    while (ok && left) {
        int count = httpd_req_recv(req, buffer, left > sizeof(buffer) ? sizeof(buffer) : left);
        ok = count > 0 && fwrite(buffer, 1, count, file) == (size_t)count;
        if (ok)
            left -= (size_t)count;
    }
    if (file && fclose(file))
        ok = false;
    app_storage_unlock();
    char error[160] = "A feltöltés megszakadt; a korábbi csengőhangok megmaradtak.";
    esp_err_t result = ok ? app_ringtones_install(path, error) : ESP_FAIL;
    if (app_storage_lock() == ESP_OK) {
        remove(path);
        app_storage_unlock();
    }
    if (result != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
    return httpd_resp_sendstr(
        req, "Csengőhangcsomag telepítve. Válassz hangot, majd mentsd a kaputelefon beállításait.");
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
    size_t size;
    uint8_t* data = app_ringtones_load(name, &size);
    if (!data)
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "A csengőhang nem található.");
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send(req, (const char*)data, size);
    free(data);
    return result;
}
