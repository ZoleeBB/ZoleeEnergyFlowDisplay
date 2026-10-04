/*
 * Helyi HTTP információs és OTA webfelület.
 *
 * Fő végpontok:
 *   GET  /        - eszközállapot és eseménynapló
 *   GET  /update  - firmware feltöltő oldal
 *   POST /update  - bináris OTA image feltöltése
 *   POST /reboot  - eszköz újraindítása
 *
 * Ha az OTA felhasználónév és jelszó ki van töltve, minden végpont ugyanazt
 * a HTTP Basic Auth hitelesítést használja.
 */
#include "app_web.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mdns.h"

#include "app_events.h"
#include "app_state.h"
#include "app_storage.h"
#include "app_wifi.h"

static const char *TAG = "web";

static app_ota_config_t s_auth_cfg;
static httpd_handle_t s_server;
static char s_expected_auth[256];
static char s_project_title[APP_PROJECT_TITLE_MAX];
static bool s_hu = true;

/* Az ESP reset okának rövid, emberileg olvasható szövege. */
static const char *reset_text(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:
            return "Power-on";
        case ESP_RST_EXT:
            return "External reset";
        case ESP_RST_SW:
            return "Software restart";
        case ESP_RST_PANIC:
            return "Panic";
        case ESP_RST_INT_WDT:
            return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "Task watchdog";
        case ESP_RST_WDT:
            return "Watchdog";
        case ESP_RST_DEEPSLEEP:
            return "Deep sleep wake";
        case ESP_RST_BROWNOUT:
            return "Brownout";
        default:
            return "Unknown";
    }
}

/* A Basic Auth csak akkor aktív, ha felhasználónév van konfigurálva. */
static bool auth_enabled(void)
{
    return s_auth_cfg.username[0] != 0;
}

/*
 * Az elvárt "Authorization: Basic ..." fejléc előállítása.
 * A konfiguráció betöltője garantálja, hogy user/jelszó vagy együtt van, vagy egyik sincs.
 */
static void build_auth(void)
{
    s_expected_auth[0] = 0;

    if (!auth_enabled()) {
        return;
    }

    char plain[
        APP_OTA_USER_MAX +
        APP_OTA_PASSWORD_MAX +
        2];

    snprintf(
        plain,
        sizeof(plain),
        "%s:%s",
        s_auth_cfg.username,
        s_auth_cfg.password);

    /*
     * A maximális konfigurálható user+jelszó Base64 alakja ~212 karakter,
     * ezért 256 bájtos puffer szükséges. A kisebb puffer hosszú hitelesítési
     * adatoknál csendben sikertelen kódolást és használhatatlan belépést okozna.
     */
    unsigned char encoded[256];
    size_t encoded_len = 0;

    if (mbedtls_base64_encode(
            encoded,
            sizeof(encoded) - 1,
            &encoded_len,
            (const unsigned char *)plain,
            strlen(plain)) == 0) {

        encoded[encoded_len] = 0;
        snprintf(
            s_expected_auth,
            sizeof(s_expected_auth),
            "Basic %s",
            encoded);
    }
}

/* Egy HTTP kérés hitelesítése. Sikertelenségnél itt küldjük vissza a 401 választ. */
static bool authorize(httpd_req_t *req)
{
    if (!auth_enabled()) {
        return true;
    }

    size_t len =
        httpd_req_get_hdr_value_len(req, "Authorization");

    char value[256];

    if (!len ||
        len >= sizeof(value) ||
        httpd_req_get_hdr_value_str(
            req,
            "Authorization",
            value,
            sizeof(value)) != ESP_OK ||
        strcmp(value, s_expected_auth) != 0) {

        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(
            req,
            "WWW-Authenticate",
            "Basic realm=\"Zolee Energy Flow Display\"");
        httpd_resp_sendstr(req, "Authentication required");
        return false;
    }

    return true;
}

/* RAM eseményszint lokalizált megnevezése. */
static const char *event_level_text(app_event_level_t level)
{
    switch (level) {
        case APP_EVENT_ERROR:
            return s_hu ? "Hiba" : "Error";

        case APP_EVENT_WARNING:
            return s_hu ? "Figyelmeztetés" : "Warning";

        default:
            return s_hu ? "Információ" : "Information";
    }
}

/*
 * Kis HTML-részletek formázott küldése chunked válaszban.
 * A fix puffer korlátozza egyetlen részlet maximális hosszát.
 */
static esp_err_t sendf(httpd_req_t *req, const char *fmt, ...)
{
    char buffer[768];

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len < 0) {
        return ESP_FAIL;
    }

    if (len >= (int)sizeof(buffer)) {
        len = (int)sizeof(buffer) - 1;
    }

    return httpd_resp_send_chunk(req, buffer, len);
}

/*
 * Dinamikus szöveg HTML escape-elése.
 * A project title és eseményszöveg így nem tud HTML-ként értelmeződni.
 */
static void html_escape(
    const char *src,
    char *dst,
    size_t dst_len)
{
    if (!dst || dst_len == 0) {
        return;
    }

    size_t write_pos = 0;

    for (size_t i = 0;
         src && src[i] && write_pos + 1 < dst_len;
         i++) {

        const char *replacement = NULL;

        switch (src[i]) {
            case '&':
                replacement = "&amp;";
                break;
            case '<':
                replacement = "&lt;";
                break;
            case '>':
                replacement = "&gt;";
                break;
            case '"':
                replacement = "&quot;";
                break;
            case '\'':
                replacement = "&#39;";
                break;
            default:
                break;
        }

        if (replacement) {
            size_t len = strlen(replacement);

            if (write_pos + len >= dst_len) {
                break;
            }

            memcpy(dst + write_pos, replacement, len);
            write_pos += len;
        } else {
            dst[write_pos++] = src[i];
        }
    }

    dst[write_pos] = 0;
}

/* A fő információs oldal összeállítása az aktuális runtime állapotból. */
static esp_err_t root_handler(httpd_req_t *req)
{
    if (!authorize(req)) {
        return ESP_OK;
    }

    /* Kapcsolati állapot és pillanatnyi hálózati információk. */
    app_runtime_state_t runtime;
    app_state_get_runtime(&runtime);

    int rssi = 0;
    char ip[32] = "-";

    if (runtime.wifi_connected) {
        (void)app_wifi_get_rssi(&rssi);
        (void)app_wifi_get_ip(ip, sizeof(ip));
    }

    /* Az utolsó érvényes MQTT csomag helyi időre formázott időpontja. */
    char packet_time[48] = "-";

    if (runtime.last_valid_packet_epoch_s > 0) {
        time_t packet_epoch =
            (time_t)runtime.last_valid_packet_epoch_s;

        struct tm local_time;
        localtime_r(&packet_epoch, &local_time);

        strftime(
            packet_time,
            sizeof(packet_time),
            "%Y-%m-%d %H:%M:%S",
            &local_time);
    }

    /* Üzemidő felbontása nap + óra:perc:másodperc formára. */
    int64_t uptime_seconds =
        esp_timer_get_time() / 1000000LL;

    int days = (int)(uptime_seconds / 86400);
    uptime_seconds %= 86400;

    int hours = (int)(uptime_seconds / 3600);
    uptime_seconds %= 3600;

    int minutes = (int)(uptime_seconds / 60);
    int seconds = (int)(uptime_seconds % 60);

    const esp_app_desc_t *description =
        esp_app_get_description();

    /* A RAM eseménynaplóból konzisztens snapshotot kérünk. */
    app_event_entry_t events[APP_EVENT_LOG_CAPACITY];
    size_t event_count =
        app_events_snapshot(
            events,
            APP_EVENT_LOG_CAPACITY);

    httpd_resp_set_type(
        req,
        "text/html; charset=utf-8");

    /* Az oldal alapszerkezete és egyszerű, külső erőforrás nélküli stílusa. */
    httpd_resp_send_chunk(
        req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{font-family:system-ui;margin:28px;max-width:900px;background:#f7f8fa;color:#17202a}"
        ".card{background:#fff;border-radius:14px;padding:18px 22px;margin:14px 0;box-shadow:0 2px 12px #0001}"
        "table{border-collapse:collapse;width:100%}"
        "td{padding:7px;border-bottom:1px solid #eee}"
        "button,a{padding:10px 15px;border:0;border-radius:9px;background:#176b45;color:white;text-decoration:none;cursor:pointer;margin-right:8px}"
        ".muted{color:#6e7c83}.error{color:#b42318}.warning{color:#b66b00}"
        "</style></head><body>",
        HTTPD_RESP_USE_STRLEN);

    char safe_title[APP_PROJECT_TITLE_MAX * 6];
    html_escape(
        s_project_title[0]
            ? s_project_title
            : "Zolee Energy Flow Display",
        safe_title,
        sizeof(safe_title));

    sendf(req, "<h1>%s</h1>", safe_title);

    /* Eszközinformációs kártya. */
    httpd_resp_send_chunk(
        req,
        s_hu
            ? "<div class='card'><h2>Eszközinformáció</h2><table>"
            : "<div class='card'><h2>Device information</h2><table>",
        HTTPD_RESP_USE_STRLEN);

    sendf(
        req,
        "<tr><td>Firmware</td><td>%s</td></tr>",
        description ? description->version : "-");

    sendf(
        req,
        s_hu
            ? "<tr><td>Üzemidő</td><td>%d nap %02d:%02d:%02d</td></tr>"
            : "<tr><td>Uptime</td><td>%d days %02d:%02d:%02d</td></tr>",
        days,
        hours,
        minutes,
        seconds);

    sendf(req, "<tr><td>IP</td><td>%s</td></tr>", ip);

    sendf(
        req,
        "<tr><td>Wi-Fi RSSI</td><td>%d dBm</td></tr>",
        runtime.wifi_connected ? rssi : 0);

    sendf(
        req,
        s_hu
            ? "<tr><td>MQTT kapcsolat</td><td>%s</td></tr>"
            : "<tr><td>MQTT connection</td><td>%s</td></tr>",
        runtime.mqtt_connected
            ? (s_hu ? "Kapcsolódva" : "Connected")
            : (s_hu ? "Nincs kapcsolat" : "Disconnected"));

    sendf(
        req,
        s_hu
            ? "<tr><td>Utolsó MQTT csomag</td><td>%s</td></tr>"
            : "<tr><td>Last MQTT packet</td><td>%s</td></tr>",
        packet_time);

    sendf(
        req,
        s_hu
            ? "<tr><td>SD-kártya</td><td>%s</td></tr>"
            : "<tr><td>SD card</td><td>%s</td></tr>",
        app_storage_is_ok()
            ? (s_hu ? "Rendben" : "OK")
            : (s_hu ? "Hiba" : "Error"));

    sendf(
        req,
        s_hu
            ? "<tr><td>Reset oka</td><td>%s</td></tr>"
            : "<tr><td>Reset reason</td><td>%s</td></tr>",
        reset_text(esp_reset_reason()));

    httpd_resp_send_chunk(
        req,
        "</table></div>",
        HTTPD_RESP_USE_STRLEN);

    /* Legutóbbi RAM események táblázata. */
    httpd_resp_send_chunk(
        req,
        s_hu
            ? "<div class='card'><h2>Legutóbbi események</h2>"
            : "<div class='card'><h2>Recent events</h2>",
        HTTPD_RESP_USE_STRLEN);

    if (event_count == 0) {
        httpd_resp_send_chunk(
            req,
            s_hu
                ? "<p class='muted'>Még nincs esemény.</p>"
                : "<p class='muted'>No events yet.</p>",
            HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_chunk(
            req,
            "<table>",
            HTTPD_RESP_USE_STRLEN);

        for (size_t i = 0; i < event_count; i++) {
            char when[32] = "-";

            if (events[i].epoch_s > 0) {
                time_t event_epoch =
                    (time_t)events[i].epoch_s;

                struct tm local_time;
                localtime_r(
                    &event_epoch,
                    &local_time);

                strftime(
                    when,
                    sizeof(when),
                    "%Y-%m-%d %H:%M:%S",
                    &local_time);
            }

            const char *css_class =
                events[i].level == APP_EVENT_ERROR
                    ? "error"
                    : events[i].level == APP_EVENT_WARNING
                        ? "warning"
                        : "";

            char safe_event[APP_EVENT_TEXT_MAX * 6];
            html_escape(
                events[i].text,
                safe_event,
                sizeof(safe_event));

            sendf(
                req,
                "<tr><td class='muted'>%s</td>"
                "<td class='%s'>%s</td><td>%s</td></tr>",
                when,
                css_class,
                event_level_text(events[i].level),
                safe_event);
        }

        httpd_resp_send_chunk(
            req,
            "</table>",
            HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send_chunk(
        req,
        "</div>",
        HTTPD_RESP_USE_STRLEN);

    /* OTA és újraindítási műveletek. */
    httpd_resp_send_chunk(
        req,
        s_hu
            ? "<div class='card'><a href='/update'>Firmware frissítés</a>"
              "<form method='post' action='/reboot' style='display:inline' "
              "onsubmit=\"return confirm('Biztosan újraindítod az eszközt?')\">"
              "<button type='submit'>Eszköz újraindítása</button></form></div>"
            : "<div class='card'><a href='/update'>Firmware update</a>"
              "<form method='post' action='/reboot' style='display:inline' "
              "onsubmit=\"return confirm('Restart the device?')\">"
              "<button type='submit'>Restart device</button></form></div>",
        HTTPD_RESP_USE_STRLEN);

    /* A chunked HTTP választ nulla hosszú chunk zárja le. */
    if (httpd_resp_send_chunk(
            req,
            "</body></html>",
            HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}

/* A böngészős OTA feltöltőoldal kiszolgálása, a beállított nyelven. */
static esp_err_t update_get(httpd_req_t *req)
{
    if (!authorize(req)) {
        return ESP_OK;
    }

    static const char page_hu[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "</head><body><h1>Firmware frissítés</h1>"
        "<input id='f' type='file' accept='.bin'>"
        "<button onclick='go()'>Frissítés</button><p id='s'></p>"
        "<script>async function go(){"
        "let f=document.getElementById('f').files[0];if(!f)return;"
        "let r=await fetch('/update',{method:'POST',"
        "headers:{'Content-Type':'application/octet-stream'},body:f});"
        "document.getElementById('s').textContent=await r.text();"
        "}</script></body></html>";

    static const char page_en[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "</head><body><h1>Firmware update</h1>"
        "<input id='f' type='file' accept='.bin'>"
        "<button onclick='go()'>Update</button><p id='s'></p>"
        "<script>async function go(){"
        "let f=document.getElementById('f').files[0];if(!f)return;"
        "let r=await fetch('/update',{method:'POST',"
        "headers:{'Content-Type':'application/octet-stream'},body:f});"
        "document.getElementById('s').textContent=await r.text();"
        "}</script></body></html>";

    httpd_resp_set_type(
        req,
        "text/html; charset=utf-8");

    return httpd_resp_send(
        req,
        s_hu ? page_hu : page_en,
        HTTPD_RESP_USE_STRLEN);
}

/* Késleltetett reboot task, hogy a HTTP válasz még ki tudjon menni. */
static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
}

/* Kézi újraindítás HTTP kezelője. */
static esp_err_t reboot_post(httpd_req_t *req)
{
    if (!authorize(req)) {
        return ESP_OK;
    }

    httpd_resp_sendstr(
        req,
        s_hu ? "Újraindítás..." : "Restarting...");

    xTaskCreate(
        reboot_task,
        "reboot",
        2048,
        NULL,
        8,
        NULL);

    return ESP_OK;
}

/*
 * Bináris OTA image fogadása.
 * A bejövő adat közvetlenül az inaktív OTA partícióra kerül, 4 kB-os blokkokban.
 */
static esp_err_t ota_post(httpd_req_t *req)
{
    if (!authorize(req)) {
        return ESP_OK;
    }

    if (req->content_len <= 0) {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Empty image");
        return ESP_OK;
    }

    const esp_partition_t *target =
        esp_ota_get_next_update_partition(NULL);

    if (!target ||
        (size_t)req->content_len > target->size) {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Image too large");
        return ESP_OK;
    }

    esp_ota_handle_t ota_handle = 0;

    esp_err_t err = esp_ota_begin(
        target,
        (size_t)req->content_len,
        &ota_handle);

    if (err != ESP_OK) {
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "OTA begin failed");
        return ESP_OK;
    }

    char *buffer = malloc(4096);
    if (!buffer) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Out of memory");
        return ESP_OK;
    }

    int remaining = req->content_len;

    while (remaining > 0) {
        int block_size =
            remaining > 4096 ? 4096 : remaining;

        int received =
            httpd_req_recv(
                req,
                buffer,
                block_size);

        /* A HTTP socket timeout átmeneti, ilyenkor ugyanazt a blokkot újra kérjük. */
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }

        if (received <= 0) {
            free(buffer);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(
                req,
                HTTPD_500_INTERNAL_SERVER_ERROR,
                "Upload interrupted");
            return ESP_OK;
        }

        err = esp_ota_write(
            ota_handle,
            buffer,
            (size_t)received);

        if (err != ESP_OK) {
            free(buffer);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(
                req,
                HTTPD_500_INTERNAL_SERVER_ERROR,
                "Write failed");
            return ESP_OK;
        }

        remaining -= received;
    }

    free(buffer);

    /*
     * esp_ota_end ellenőrzi az image struktúráját.
     * Csak sikeres lezárás után állítjuk be következő boot partíciónak.
     */
    err = esp_ota_end(ota_handle);

    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(target);
    }

    if (err != ESP_OK) {
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Validation failed");
        return ESP_OK;
    }

    app_events_add(
        APP_EVENT_INFO,
        "Új OTA firmware feltöltve; újraindítás következik.");

    httpd_resp_sendstr(
        req,
        s_hu
            ? "Firmware feltöltve. Az eszköz újraindul."
            : "Firmware uploaded. The device will restart.");

    xTaskCreate(
        reboot_task,
        "ota_reboot",
        2048,
        NULL,
        8,
        NULL);

    return ESP_OK;
}

/* mDNS és HTTP szerver egyszeri inicializálása, majd a végpontok regisztrálása. */
esp_err_t app_web_start(const app_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_server) {
        return ESP_ERR_INVALID_STATE;
    }

    s_auth_cfg = cfg->ota;
    snprintf(
        s_project_title,
        sizeof(s_project_title),
        "%s",
        cfg->project.title);

    s_hu = strcmp(cfg->project.language, "en") != 0;
    build_auth();

    /* mDNS nélkül az IP címen még működhet a weboldal, ezért ez nem végzetes hiba. */
    esp_err_t err = mdns_init();

    if (err == ESP_OK) {
        mdns_hostname_set(APP_DEVICE_HOSTNAME);
        mdns_instance_name_set(
            "Zolee Energy Flow Display");
        mdns_service_add(
            NULL,
            "_http",
            "_tcp",
            80,
            NULL,
            0);
    } else {
        ESP_LOGW(
            TAG,
            "mDNS init failed: %s",
            esp_err_to_name(err));
    }

    /* Az ESP-IDF beépített HTTP szerverének indítása. */
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 8;
    http_cfg.stack_size = 8192;

    err = httpd_start(&s_server, &http_cfg);
    if (err != ESP_OK) {
        return err;
    }

    /* A négy alkalmazásszintű végpont definíciója. */
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler
    };

    httpd_uri_t update_get_uri = {
        .uri = "/update",
        .method = HTTP_GET,
        .handler = update_get
    };

    httpd_uri_t update_post_uri = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = ota_post
    };

    httpd_uri_t reboot_uri = {
        .uri = "/reboot",
        .method = HTTP_POST,
        .handler = reboot_post
    };

    if ((err = httpd_register_uri_handler(
            s_server,
            &root_uri)) != ESP_OK) {
        return err;
    }

    if ((err = httpd_register_uri_handler(
            s_server,
            &update_get_uri)) != ESP_OK) {
        return err;
    }

    if ((err = httpd_register_uri_handler(
            s_server,
            &update_post_uri)) != ESP_OK) {
        return err;
    }

    if ((err = httpd_register_uri_handler(
            s_server,
            &reboot_uri)) != ESP_OK) {
        return err;
    }

    app_events_add(
        APP_EVENT_INFO,
        "Webes információs és OTA felület elindult.");

    return ESP_OK;
}
