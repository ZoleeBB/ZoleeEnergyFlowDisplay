/* A végpontok csak az inverteres Masteren léteznek. Az összes tartós beállítás
 * azonos atomi mentéshez tartozik, a válasz titok helyett csak jelenlétjelzőt ad. */
#include "mbedtls/platform_util.h"
#include "app_inverter.h"
#include "app_inverter_page.h"
#include "app_inverter_registers.h"

static esp_err_t inverter_page(httpd_req_t *req)
{
    if (!authorize(req)) return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return app_web_send_page(req, app_inverter_page, true);
}

static esp_err_t inverter_get(httpd_req_t *req)
{
    if (!authorize(req)) return ESP_OK;
    cJSON *settings = app_inverter_settings_get();
    char *json = settings ? cJSON_PrintUnformatted(settings) : NULL;
    cJSON_Delete(settings);
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                         "Az inverterbeállítások nem olvashatók.");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, json);
    free(json);
    return result;
}

/* Egyszerre csak egy regiszter metaadata kerül a kupacra, a teljes SunSpec
 * táblának nincs szüksége tartós vagy egyszerre felépített JSON-fára. */
static esp_err_t inverter_register_row(httpd_req_t *req, const app_inverter_register_t *r)
{
    cJSON *row = cJSON_CreateObject();
    char *text = NULL;
    if (row && cJSON_AddStringToObject(row, "id", r->key) &&
        cJSON_AddStringToObject(row, "label", s_hu ? r->hu : r->en) &&
        cJSON_AddNumberToObject(row, "address", r->address) &&
        cJSON_AddNumberToObject(row, "words", r->words) &&
        cJSON_AddNumberToObject(row, "function", r->function) &&
        cJSON_AddStringToObject(row, "format", r->format) &&
        (r->base ? cJSON_AddStringToObject(row, "base", r->base) : cJSON_AddNullToObject(row, "base")))
        text = cJSON_PrintUnformatted(row);
    cJSON_Delete(row);
    if (!text) return ESP_ERR_NO_MEM;
    esp_err_t result = httpd_resp_send_chunk(req, text, strlen(text));
    cJSON_free(text);
    return result;
}

static esp_err_t inverter_registers_get(httpd_req_t *req)
{
    if (!authorize(req)) return ESP_OK;
    char query[80], profile[40];
    size_t count = 0;
    const app_inverter_register_t *rows = NULL;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "profile", profile, sizeof(profile)) == ESP_OK)
        rows = app_inverter_registers(profile, &count);
    if (!rows) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ismeretlen Modbus-profil.");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const char prefix[] = "{\"registers\":[";
    if (httpd_resp_send_chunk(req, prefix, sizeof(prefix) - 1) != ESP_OK) return ESP_FAIL;
    for (size_t i = 0; i < count; ++i) {
        if ((i && httpd_resp_send_chunk(req, ",", 1) != ESP_OK) ||
            inverter_register_row(req, &rows[i]) != ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_send_chunk(req, "]}", 2) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t inverter_post(httpd_req_t *req)
{
    if (!authorize(req)) return ESP_OK;
    char header[4];
    if (httpd_req_get_hdr_value_str(req, "X-Zolee-Inverter", header, sizeof(header)) != ESP_OK ||
        strcmp(header, "1") || req->content_len < 2 || req->content_len > 12288)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Érvénytelen inverterkérés.");
    char *body = heap_caps_calloc(1, req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Nincs elegendő memória.");
    size_t used = 0;
    while (used < req->content_len) {
        int count = httpd_req_recv(req, body + used, req->content_len - used);
        if (count <= 0 || (size_t)count > req->content_len - used) break;
        used += (size_t)count;
    }
    /* A korlátos JSON-olvasó ellenőrzi a mélységet, NUL-t és az objektum végét. */
    cJSON *form = used == req->content_len && slave_json_text(body, used)
        ? lx_home_document_parse(body, used) : NULL;
    mbedtls_platform_zeroize(body, req->content_len);
    free(body);
    esp_err_t result = cJSON_IsObject(form) ? app_inverter_settings_save(form) : ESP_ERR_INVALID_ARG;
    const char *const protected_keys[] = {"key", "secret"};
    for (unsigned i = 0; i < 2; ++i) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(form, protected_keys[i]);
        if (cJSON_IsString(value)) mbedtls_platform_zeroize(value->valuestring, strlen(value->valuestring));
    }
    cJSON_Delete(form);
    if (result == ESP_ERR_INVALID_ARG)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
            "Ellenőrizd az inverter típusát, az elérést, az azonosítókat, a regisztercímeket, a koordinátákat és az időprogramot.");
    if (result == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "A beállítás közben változott. Töltsd újra az oldalt mentés előtt.");
    }
    if (result != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "A beállítások tartós mentése sikertelen.");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}

static esp_err_t inverter_register(httpd_handle_t server)
{
    if (s_config.data_recept_mode != APP_DATA_INVERTER) return ESP_OK;
    const httpd_uri_t routes[] = {
        {.uri="/inverter", .method=HTTP_GET, .handler=inverter_page},
        {.uri="/api/inverter", .method=HTTP_GET, .handler=inverter_get},
        {.uri="/api/inverter", .method=HTTP_POST, .handler=inverter_post},
        {.uri="/api/inverter/registers", .method=HTTP_GET, .handler=inverter_registers_get},
        {.uri="/api/inverter/slaves", .method=HTTP_GET, .handler=slave_presence_get},
        {.uri="/api/inverter/slaves", .method=HTTP_POST, .handler=slave_presence_post}
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t result = httpd_register_uri_handler(server, &routes[i]);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}
