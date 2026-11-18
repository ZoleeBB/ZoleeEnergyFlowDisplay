/* A webkérés csak elindítja vagy olvassa a feladatot. A hálózati
 * eszközök által visszaadott szöveget a cJSON kódolja a válasznál. */
static void inverter_discovery_json_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t inverter_discovery_error(httpd_req_t *req, esp_err_t error)
{
    if (error == ESP_ERR_NOT_FOUND)
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "A keresés már nem érhető el. Indíts új keresést.");
    if (error == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "A keresés most nem indítható. Ellenőrizd a Wi-Fi-t és a folyamatban lévő keresést.");
    }
    if (error == ESP_ERR_NO_MEM)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Nincs elegendő memória a kereséshez.");
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Érvénytelen keresési beállítás vagy nem kereshető inverterprofil.");
}

static esp_err_t inverter_discovery_start_post(httpd_req_t *req)
{
    if (!authorize(req)) return ESP_OK;
    cJSON *form;
    if (!inverter_web_form(req, 384, &form)) return ESP_OK;
    const char *const keys[] = {"profile", "port", "unit", "device"};
    const cJSON *profile = cJSON_GetObjectItemCaseSensitive(form, "profile");
    const cJSON *port = cJSON_GetObjectItemCaseSensitive(form, "port");
    const cJSON *unit = cJSON_GetObjectItemCaseSensitive(form, "unit");
    const cJSON *device = cJSON_GetObjectItemCaseSensitive(form, "device");
    uint32_t job = 0;
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (slave_fields(form, keys, device ? 4 : 3) && cJSON_IsString(profile) &&
        slave_integer(port, 1, UINT16_MAX) && slave_integer(unit, 0, UINT8_MAX) &&
        (!device || cJSON_IsString(device))) {
        result = app_inverter_discovery_start(profile->valuestring, (uint16_t)port->valueint,
                                              (uint8_t)unit->valueint, device ? device->valuestring : "", &job);
    }
    cJSON_Delete(form);
    if (result != ESP_OK) return inverter_discovery_error(req, result);
    char response[32];
    snprintf(response, sizeof(response), "{\"job\":%lu}", (unsigned long)job);
    inverter_discovery_json_headers(req);
    return httpd_resp_sendstr(req, response);
}

static esp_err_t inverter_discovery_cancel_post(httpd_req_t *req)
{
    if (!authorize(req)) return ESP_OK;
    cJSON *form;
    if (!inverter_web_form(req, 64, &form)) return ESP_OK;
    const char *const keys[] = {"job"};
    const cJSON *job = cJSON_GetObjectItemCaseSensitive(form, "job");
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (slave_fields(form, keys, 1) && slave_integer(job, 1, UINT32_MAX))
        result = app_inverter_discovery_cancel((uint32_t)job->valuedouble);
    cJSON_Delete(form);
    if (result != ESP_OK) return inverter_discovery_error(req, result);
    inverter_discovery_json_headers(req);
    return httpd_resp_sendstr(req, "{\"cancelled\":true}");
}

static uint32_t inverter_discovery_query_job(httpd_req_t *req)
{
    char query[32], digits[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "job", digits, sizeof(digits)) != ESP_OK) return 0;
    uint32_t number = 0;
    for (const char *s = digits; *s; ++s) {
        if (*s < '0' || *s > '9') return 0;
        uint32_t digit = (uint32_t)(*s - '0');
        if (number > (UINT32_MAX - digit) / 10) return 0;
        number = number * 10 + digit;
    }
    return number;
}

static esp_err_t inverter_discovery_get(httpd_req_t *req)
{
    if (!authorize(req)) return ESP_OK;
    uint32_t job = inverter_discovery_query_job(req);
    if (!job) return inverter_discovery_error(req, ESP_ERR_INVALID_ARG);
    app_inverter_discovery_status_t *s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s) return inverter_discovery_error(req, ESP_ERR_NO_MEM);
    esp_err_t result = app_inverter_discovery_snapshot(job, s);
    if (result != ESP_OK) {
        free(s);
        return inverter_discovery_error(req, result);
    }
    cJSON *json = cJSON_CreateObject();
    cJSON *rows = cJSON_AddArrayToObject(json, "results");
    const char *state = "failed";
    switch (s->state) {
        case APP_DISCOVERY_RUNNING: state = "running"; break;
        case APP_DISCOVERY_DONE: state = "done"; break;
        case APP_DISCOVERY_CANCELLED: state = "cancelled"; break;
        default: break;
    }
    bool ok = rows && cJSON_AddNumberToObject(json, "job", job) &&
              cJSON_AddStringToObject(json, "profile", s->profile) && cJSON_AddStringToObject(json, "state", state) &&
              cJSON_AddNumberToObject(json, "checked", s->checked) && cJSON_AddNumberToObject(json, "total", s->total) &&
              cJSON_AddBoolToObject(json, "limited", s->limited);
    for (unsigned i = 0; ok && i < s->count; ++i) {
        cJSON *row = cJSON_CreateObject();
        if (!row || !cJSON_AddItemToArray(rows, row)) {
            cJSON_Delete(row);
            ok = false;
            break;
        }
        ok = cJSON_AddStringToObject(row, "host", s->results[i].host) &&
             cJSON_AddStringToObject(row, "model", s->results[i].model) &&
             cJSON_AddStringToObject(row, "serial", s->results[i].serial) &&
             cJSON_AddStringToObject(row, "device", s->results[i].device) &&
             cJSON_AddNumberToObject(row, "port", s->results[i].port) &&
             cJSON_AddNumberToObject(row, "unit", s->results[i].unit) &&
             cJSON_AddBoolToObject(row, "verified", s->results[i].verified);
    }
    free(s);
    char *body = ok ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!body) return inverter_discovery_error(req, ESP_ERR_NO_MEM);
    inverter_discovery_json_headers(req);
    result = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return result;
}
