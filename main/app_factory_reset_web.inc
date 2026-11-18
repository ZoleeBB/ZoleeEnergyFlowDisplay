/* Az OTA-val és a beállításmentéssel közös kizárást az exclusive_request biztosítja. */
#include "app_boot_config.h"
#include "app_factory_reset.h"
#include "app_http.h"

static void factory_reset_reboot_task(void *arg)
{
    /* Ezt a feladatot kizárólag a saját kéréskezelője ébresztheti. */
    while (!ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
        /* Csak a tartós mentés utáni jelzés indít újra. */
    }
    reboot_task(arg);
}

static esp_err_t factory_reset_schedule(TaskHandle_t *worker)
{
    /* A tartós szándék rögzítése után már egy tápvesztés is elindítja a törlést.
     * Ezért előbb foglaljuk le az újraindító feladatot, amely addig nem futhat tovább. */
    if (xTaskCreate(factory_reset_reboot_task, "factory_reboot", 2048, NULL, 8, worker) != pdPASS)
        return ESP_ERR_NO_MEM;

    esp_err_t result = app_factory_reset_request(app_storage_is_ok() || !app_boot_config_internal());
    if (result != ESP_OK)
        vTaskDelete(*worker);
    return result;
}

static const char *factory_reset_skip_space(const char *at, const char *end)
{
    while (at != end && memchr(" \t\r\n", (unsigned char)*at, 4))
        ++at;
    return at;
}

static bool factory_reset_confirmed(const char *body, size_t length)
{
    /* A parancs egyetlen literális mező. Csak a JSON négy megengedett
     * szóközét fogadjuk el a tokenek között, heap- és rekurzióigény nélkül. */
    static const char *const tokens[] = {"{", "\"confirm\"", ":", "true", "}"};
    const char *end = body + length;
    for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
        body = factory_reset_skip_space(body, end);
        size_t size = strlen(tokens[i]);
        if (size > (size_t)(end - body) || memcmp(body, tokens[i], size))
            return false;
        body += size;
    }
    return factory_reset_skip_space(body, end) == end;
}

static esp_err_t factory_reset_reject(httpd_req_t *req, const char *status, const char *hu, const char *en)
{
    httpd_resp_set_status(req, status);
    /* Ha a kérésből maradt adat, ne tarthassa nyitva a webes kiszolgálót. */
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, s_hu ? hu : en);
    return ESP_FAIL;
}

static esp_err_t factory_reset_post_locked(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (req->method != HTTP_POST) {
        httpd_resp_set_hdr(req, "Allow", "POST");
        return factory_reset_reject(req, "405 Method Not Allowed", "POST kérés szükséges.", "POST required.");
    }
    char header[2];
    if (httpd_req_get_hdr_value_str(req, "X-Zolee-Settings", header, sizeof(header)) != ESP_OK ||
        strcmp(header, "1"))
        return factory_reset_reject(req, "403 Forbidden", "Hiányzó beállítási kérésfejléc.",
                                    "Missing settings request header.");
    if (!app_http_is_json_request(req))
        return factory_reset_reject(req, "415 Unsupported Media Type", "JSON kérés szükséges.",
                                    "Expected an application/json request.");

    char body[128];
    if (req->content_len > sizeof(body))
        return factory_reset_reject(req, "413 Payload Too Large", "A megerősítés túl hosszú.",
                                    "Confirmation is too long.");
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + 5000000;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (esp_timer_get_time() >= deadline)
            return factory_reset_reject(req, "408 Request Timeout", "A kérés fogadási ideje lejárt.",
                                        "Request timed out.");
        if (count == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (count <= 0)
            return factory_reset_reject(req, "400 Bad Request", "A megerősítés fogadása megszakadt.",
                                        "Confirmation was interrupted.");
        received += (size_t)count;
    }
    if (!factory_reset_confirmed(body, received))
        return factory_reset_reject(req, "400 Bad Request", "Hiányzó vagy hibás megerősítés.",
                                    "Missing or invalid confirmation.");

    TaskHandle_t worker;
    esp_err_t result = factory_reset_schedule(&worker);
    if (result == ESP_ERR_INVALID_STATE)
        return factory_reset_reject(req, "409 Conflict", "Ellenőrizd, hogy az eredeti SD-kártya van-e a kijelzőben. Ezután próbáld újra.",
                                    "Check that the display's original SD card is inserted, then try again.");
    if (result != ESP_OK)
        return factory_reset_reject(req, "503 Service Unavailable", "A visszaállítás nem indítható. Próbáld újra.",
                                    "Could not start the reset. Please try again.");

    s_restart_pending = true;
    httpd_resp_set_status(req, "202 Accepted");
    result = httpd_resp_sendstr(req, s_hu ? "Gyári visszaállítás elfogadva. A kijelző újraindul."
                                          : "Factory reset accepted. The display is restarting.");
    /* A tartósan elfogadott kérés megszakadt böngészőkapcsolatnál is végrehajtandó. */
    xTaskNotifyGive(worker);
    return result;
}
