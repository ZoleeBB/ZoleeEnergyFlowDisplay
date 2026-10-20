/* A böngészős tesztindítás ugyanazt az OTA-folyamatot használja, mint az éjféli.
 * A saját kérésfejléc a többi beállító végponthoz hasonlóan kötelező. */
static esp_err_t ota_auto_post(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char header[2];
    if (httpd_req_get_hdr_value_str(req, "X-Zolee-OTA", header, sizeof(header)) != ESP_OK ||
        strcmp(header, "1"))
    {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, s_hu ? "Hiányzó OTA kérésfejléc." : "Missing OTA request header.");
    }
    if (req->content_len)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, s_hu ? "Ez a kérés nem fogad firmware-fájlt." : "This request does not accept a firmware file.");
    }
    if (!app_wifi_is_connected())
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, s_hu ? "Nincs Wi-Fi-kapcsolat." : "Wi-Fi is disconnected.");
    }
    esp_err_t result = app_ota_request_auto_update();
    if (result == ESP_ERR_INVALID_STATE)
    {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, s_hu ? "Frissítés vagy újraindítás már folyamatban van." : "An update or restart is already in progress.");
    }
    if (result != ESP_OK)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, s_hu ? "A frissítés nem indítható; nézd meg a rendszernaplót." : "Cannot start the update; check the system log.");
    }
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, s_hu
        ? "Az ellenőrzés elindult a háttérben. Az eredmény a rendszernaplóban látható; sikeres frissítéskor az eszköz újraindul."
        : "The check started in the background. See the system log for the result; a successful update restarts the device.");
}
