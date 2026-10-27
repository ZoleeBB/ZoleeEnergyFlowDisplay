/*
 * Beállítások mentése és visszaállítása a webfelületen.
 * GET  /backup                    – kezelőoldal
 * GET  /api/backup                – letölthető JSON (nyílt jelszavak nélkül)
 * POST /api/backup/validate       – feltöltött mentés ellenőrzése írás nélkül
 * POST /api/backup/restore        – visszaállítás az SD-re, majd újraindítás
 * GET  /api/backup/flash          – belső tartalék állapota
 * POST /api/backup/flash          – belső tartalék frissítése most
 * POST /api/backup/flash/restore  – visszaállítás a belső tartalékból
 * Minden POST a beállításoknál használt X-Zolee-Settings fejlécet kéri.
 */
#include "app_backup.h"
#include "app_backup_page.h"

static esp_err_t backup_json(httpd_req_t *req, int status, bool ok, const char *message,
                             unsigned files, bool restart)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddStringToObject(root, "message", message ? message : "");
    cJSON_AddNumberToObject(root, "files", files);
    cJSON_AddBoolToObject(root, "restart", restart);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body)
        return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (status == 400)
        httpd_resp_set_status(req, "400 Bad Request");
    else if (status == 409)
        httpd_resp_set_status(req, "409 Conflict");
    else if (status >= 500)
        httpd_resp_set_status(req, "500 Internal Server Error");
    esp_err_t result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return result;
}

static bool backup_header_ok(httpd_req_t *req)
{
    char header[8] = {0};
    return httpd_req_get_hdr_value_str(req, "X-Zolee-Settings", header, sizeof(header)) == ESP_OK &&
           !strcmp(header, "1");
}

static bool backup_sd_available(void)
{
    return !s_config.cardless_slave && app_storage_is_ok();
}

static esp_err_t backup_page(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return app_web_send_page(req, app_backup_page, true);
}

static esp_err_t backup_download(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    if (!backup_sd_available() || app_storage_lock() != ESP_OK)
        return backup_json(req, 409, false, "Az SD-kártya nem érhető el; mentés csak SD-s eszközön készíthető.", 0, false);
    char error[APP_BACKUP_ERROR_SIZE] = {0};
    const esp_app_desc_t *app = esp_app_get_description();
    char *json = app_backup_export(APP_SD_MOUNT_POINT, false, app ? app->version : "", error);
    app_storage_unlock();
    if (!json)
        return backup_json(req, 500, false, error, 0, false);
    char disposition[96] = "attachment; filename=\"zolee-beallitasok.json\"";
    time_t now = time(NULL);
    struct tm local;
    if (now > 1577836800 && localtime_r(&now, &local))
        strftime(disposition, sizeof(disposition),
                 "attachment; filename=\"zolee-beallitasok-%Y%m%d-%H%M.json\"", &local);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    esp_err_t result = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return result;
}

/* A feltöltött mentés PSRAM-ba kerül; a 256 KiB-os korlát a web veremét nem terheli. */
static char *backup_receive(httpd_req_t *req, size_t *length, char error[APP_BACKUP_ERROR_SIZE])
{
    if (req->content_len < 2 || req->content_len > APP_BACKUP_MAX)
    {
        snprintf(error, APP_BACKUP_ERROR_SIZE, "%s", "A mentés mérete érvénytelen (legfeljebb 256 KiB).");
        return NULL;
    }
    size_t total = req->content_len, received = 0;
    char *body = heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body)
        body = malloc(total + 1);
    if (!body)
    {
        snprintf(error, APP_BACKUP_ERROR_SIZE, "%s", "Nincs elegendő memória.");
        return NULL;
    }
    int64_t deadline = esp_timer_get_time() + 30000000LL;
    while (received < total)
    {
        int n = httpd_req_recv(req, body + received, total - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && esp_timer_get_time() < deadline)
            continue;
        if (n <= 0)
        {
            free(body);
            snprintf(error, APP_BACKUP_ERROR_SIZE, "%s", "A feltöltés megszakadt.");
            return NULL;
        }
        received += (size_t)n;
    }
    body[received] = 0;
    *length = received;
    return body;
}

static esp_err_t backup_schedule_restart(httpd_req_t *req, unsigned files, const char *message)
{
    if (app_stats_save_now() != ESP_OK)
        return backup_json(req, 500, false,
                           "A beállítások visszaállítva, de a statisztika mentése sikertelen. Indítsd újra kézzel.",
                           files, false);
    if (xTaskCreate(reboot_task, "backup_restart", 2048, NULL, 8, NULL) != pdPASS)
        return backup_json(req, 500, false, "A beállítások visszaállítva, de az újraindítás nem ütemezhető.",
                           files, false);
    s_restart_pending = true;
    app_events_add(APP_EVENT_INFO, "Beállítások visszaállítva (%u fájl); újraindítás.", files);
    return backup_json(req, 200, true, message, files, true);
}

static esp_err_t backup_upload_locked(httpd_req_t *req)
{
    if (!backup_header_ok(req))
        return backup_json(req, 400, false, "Hiányzik a beállításkezelő kérésazonosítója.", 0, false);
    bool restore = !strcmp(req->uri, "/api/backup/restore");
    char error[APP_BACKUP_ERROR_SIZE] = {0};
    size_t length = 0;
    char *body = backup_receive(req, &length, error);
    if (!body)
        return backup_json(req, 400, false, error, 0, false);
    if (!backup_sd_available() || app_storage_lock() != ESP_OK)
    {
        free(body);
        return backup_json(req, 409, false, "Az SD-kártya nem érhető el.", 0, false);
    }
    unsigned files = 0;
    esp_err_t result = restore ? app_backup_restore(APP_SD_MOUNT_POINT, body, length, &files, error)
                               : app_backup_validate(APP_SD_MOUNT_POINT, body, length, &files, error);
    app_storage_unlock();
    free(body);
    if (result != ESP_OK)
        return backup_json(req, result == ESP_FAIL ? 500 : 400, false, error, 0, false);
    if (!restore)
        return backup_json(req, 200, true, "A mentés érvényes, visszaállítható.", files, false);
    app_backup_flash_request();
    return backup_schedule_restart(req, files, "Sikeres visszaállítás, az eszköz újraindul.");
}

static esp_err_t backup_upload(httpd_req_t *req)
{
    return exclusive_request(req, backup_upload_locked);
}

static esp_err_t backup_flash_get(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    app_backup_flash_info_t info;
    esp_err_t result = app_backup_flash_info(&info);
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(root, "supported", result == ESP_OK);
    cJSON_AddBoolToObject(root, "sd", backup_sd_available());
    cJSON_AddBoolToObject(root, "available", result == ESP_OK && info.available);
    cJSON_AddNumberToObject(root, "sequence", info.sequence);
    cJSON_AddNumberToObject(root, "bytes", info.length);
    cJSON_AddNumberToObject(root, "epoch", (double)info.epoch_s);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body)
        return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return result;
}

static esp_err_t backup_flash_post_locked(httpd_req_t *req)
{
    if (!backup_header_ok(req))
        return backup_json(req, 400, false, "Hiányzik a beállításkezelő kérésazonosítója.", 0, false);
    if (!backup_sd_available())
        return backup_json(req, 409, false, "Az SD-kártya nem érhető el.", 0, false);
    char error[APP_BACKUP_ERROR_SIZE] = {0};
    unsigned files = 0;
    if (!strcmp(req->uri, "/api/backup/flash/restore"))
    {
        esp_err_t result = app_backup_flash_restore_sd(&files, error);
        if (result != ESP_OK)
            return backup_json(req, result == ESP_FAIL ? 500 : 400, false, error, 0, false);
        return backup_schedule_restart(req, files, "Visszaállítva a belső tartalékból, az eszköz újraindul.");
    }
    if (app_storage_lock() != ESP_OK)
        return backup_json(req, 409, false, "Az SD-kártya nem érhető el.", 0, false);
    const esp_app_desc_t *app = esp_app_get_description();
    char *json = app_backup_export(APP_SD_MOUNT_POINT, true, app ? app->version : "", error);
    app_storage_unlock();
    if (!json)
        return backup_json(req, 500, false, error, 0, false);
    bool written = false;
    esp_err_t result = app_backup_flash_save(json, strlen(json), &written);
    free(json);
    if (result != ESP_OK)
        return backup_json(req, 500, false, "A belső tartalék írása sikertelen.", 0, false);
    return backup_json(req, 200, true, written ? "A belső tartalék frissítve." :
                       "A belső tartalék már naprakész volt.", 0, false);
}

static esp_err_t backup_flash_post(httpd_req_t *req)
{
    return exclusive_request(req, backup_flash_post_locked);
}

static esp_err_t backup_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/backup", .method = HTTP_GET, .handler = backup_page},
        {.uri = "/api/backup", .method = HTTP_GET, .handler = backup_download},
        {.uri = "/api/backup/validate", .method = HTTP_POST, .handler = backup_upload},
        {.uri = "/api/backup/restore", .method = HTTP_POST, .handler = backup_upload},
        {.uri = "/api/backup/flash", .method = HTTP_GET, .handler = backup_flash_get},
        {.uri = "/api/backup/flash", .method = HTTP_POST, .handler = backup_flash_post},
        {.uri = "/api/backup/flash/restore", .method = HTTP_POST, .handler = backup_flash_post},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
    {
        esp_err_t result = httpd_register_uri_handler(server, &routes[i]);
        if (result != ESP_OK)
            return result;
    }
    return ESP_OK;
}
