/*
 * SD-témák frissítése a GitHubról:
 * GET  /api/templates/updates          – állapot és lista
 * POST /api/templates/updates/check    – ellenőrzés indítása
 * POST /api/templates/updates/install  – törzs: téma-azonosító vagy „all”
 * A POST a sablonkezelő meglévő X-Zolee-Template fejlécét kéri.
 */
#include "app_template_update.h"

static esp_err_t template_updates_reply(httpd_req_t *req, int status, const char *text)
{
    cJSON *root = app_template_update_status();
    if (!root)
        return ESP_ERR_NO_MEM;
    if (text)
        cJSON_AddStringToObject(root, "error", text);
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
    esp_err_t result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return result;
}

static esp_err_t template_updates_get(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    return template_updates_reply(req, 200, NULL);
}

static esp_err_t template_updates_post(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    char header[8] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Zolee-Template", header, sizeof(header)) != ESP_OK ||
        strcmp(header, "1"))
        return template_updates_reply(req, 400, "Hiányzó kérésazonosító.");
    if (!app_wifi_is_connected())
        return template_updates_reply(req, 409, "Nincs internetkapcsolat (Wi-Fi).");
    if (!app_storage_is_ok() || s_config.cardless_slave)
        return template_updates_reply(req, 409, "Témafrissítéshez működő SD-kártya kell.");
    if (!strcmp(req->uri, "/api/templates/updates/check"))
    {
        esp_err_t result = app_template_update_check();
        return template_updates_reply(req, result == ESP_OK ? 200 : 409,
                                      result == ESP_OK ? NULL : "Egy ellenőrzés vagy telepítés már folyamatban van.");
    }
    char id[40] = {0};
    if (req->content_len < 1 || req->content_len >= sizeof(id) ||
        httpd_req_recv(req, id, req->content_len) != (int)req->content_len)
        return template_updates_reply(req, 400, "Érvénytelen téma-azonosító.");
    esp_err_t result = app_template_update_install(id);
    return template_updates_reply(req, result == ESP_OK ? 200 : result == ESP_ERR_INVALID_ARG ? 400 : 409,
                                  result == ESP_OK ? NULL : "Előbb futtasd az ellenőrzést, és várd meg a folyamatban lévő műveletet.");
}

static esp_err_t template_updates_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/api/templates/updates", .method = HTTP_GET, .handler = template_updates_get},
        {.uri = "/api/templates/updates/check", .method = HTTP_POST, .handler = template_updates_post},
        {.uri = "/api/templates/updates/install", .method = HTTP_POST, .handler = template_updates_post},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
    {
        esp_err_t result = httpd_register_uri_handler(server, &routes[i]);
        if (result != ESP_OK)
            return result;
    }
    return ESP_OK;
}
