/*
 * Helyi HTTP információs és OTA webfelület.
 *
 * Fő végpontok:
 *   GET  /        - eszközállapot és eseménynapló
 *   GET  /update  - firmware feltöltő oldal
 *   POST /update  - bináris OTA image feltöltése
 *   POST /reboot  - eszköz újraindítása
 *   POST /api/factory-reset - megerősített helyi alaphelyzetbe állítás
 *
 * Ha az OTA felhasználónév és jelszó ki van töltve, minden végpont ugyanazt
 * a HTTP Basic Auth hitelesítést használja.
 */
#include "app_web.h"
#include "app_support_web.h"
#include "app_home_actions.h"
#include "app_web_style.h"
#include "app_http.h"
#include "app_loxone.h"
#include "app_templates.h"
#include "app_diagnostics.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mdns.h"

#include "app_event_log_view.h"
#include "app_event_store.h"
#include "app_events.h"
#include "app_state.h"
#include "app_storage.h"
#include "app_stats.h"
#include "app_wifi.h"
#include "app_ota.h"
#include "app_slave_presence_service.h"
#include "app_ble_seed.h"

static const char *TAG = "web";

static app_ota_config_t s_auth_cfg;
static app_config_t s_config;
/* A weben mentett új üzemmód végpontjai csak a következő indításkor léteznek. */
static app_data_mode_t s_web_mode;
static httpd_handle_t s_server;
static bool s_restart_pending;
static char s_expected_auth[256];
static char s_project_title[APP_PROJECT_TITLE_MAX];
static bool s_hu = true;

static esp_err_t app_web_send_page(httpd_req_t *req, const char *page, bool hu)
{
    return app_web_send_template(req, page, hu, s_web_mode);
}

static const char *web_mode_name(app_data_mode_t mode)
{
    switch (mode) {
        case APP_DATA_INVERTER: return s_hu ? "Közvetlen inverter" : "Direct inverter";
        case APP_DATA_LOXONE: return "Loxone";
        case APP_DATA_SLAVE: return "Slave (BLE)";
        case APP_DATA_HTTP: return "HTTP";
        default: return "MQTT";
    }
}

/* Az ESP reset okának rövid, emberileg olvasható szövege. */
static const char *reset_text(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:
            return (s_hu?"Bekapcsolás":"Power-on");
        case ESP_RST_EXT:
            return (s_hu?"Külső reset":"External reset");
        case ESP_RST_SW:
            return (s_hu?"Szoftveres újraindítás":"Software restart");
        case ESP_RST_PANIC:
            return (s_hu?"Programhiba":"Panic");
        case ESP_RST_INT_WDT:
            return (s_hu?"Megszakítási watchdog":"Interrupt watchdog");
        case ESP_RST_TASK_WDT:
            return (s_hu?"Feladat-watchdog":"Task watchdog");
        case ESP_RST_WDT:
            return (s_hu?"Watchdog":"Watchdog");
        case ESP_RST_DEEPSLEEP:
            return (s_hu?"Ébredés mélyalvásból":"Deep sleep wake");
        case ESP_RST_BROWNOUT:
            return (s_hu?"Tápfeszültség-esés":"Brownout");
        default:
            return (s_hu?"Ismeretlen":"Unknown");
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
        /*
         * Az encoded_len ismert, ezért snprintf helyett pontos méretű
         * másolást használunk; így a fordító sem jelezhet téves truncationt.
         */
        if (encoded_len + 7 <= sizeof(s_expected_auth)) {
            memcpy(s_expected_auth, "Basic ", 6);
            memcpy(s_expected_auth + 6, encoded, encoded_len);
            s_expected_auth[6 + encoded_len] = 0;
        }
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
        httpd_resp_sendstr(req, (s_hu?"Hitelesítés szükséges":"Authentication required"));
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
#include "app_battery_web.inc"

/* Az események kétnyelvű pillanatképe csak a táblázat küldéséig él.
 * A több mint 5 KiB-os tömb nem foglalja a HTTP-task belső vermét. */
static esp_err_t send_recent_events(httpd_req_t *req)
{
    app_event_entry_t *events = heap_caps_malloc(APP_EVENT_LOG_CAPACITY * sizeof(*events),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!events) {
        return httpd_resp_send_chunk(req, s_hu ? "<p class='muted'>Az eseménylista nem tölthető be.</p>"
                                               : "<p class='muted'>The event list could not be loaded.</p>",
                                     HTTPD_RESP_USE_STRLEN);
    }
    size_t count = app_events_snapshot(events, APP_EVENT_LOG_CAPACITY);
    if (!count) {
        free(events);
        return httpd_resp_send_chunk(req, s_hu ? "<p class='muted'>Még nincs esemény.</p>"
                                               : "<p class='muted'>No events yet.</p>",
                                     HTTPD_RESP_USE_STRLEN);
    }

    esp_err_t result = httpd_resp_send_chunk(req, "<div class='events-box'><table class='events'>",
                                             HTTPD_RESP_USE_STRLEN);
    /* A legfrissebb esemény elöl, ahogy a kijelző diagnosztikai nézetében. */
    while (count && result == ESP_OK) {
        const app_event_entry_t *entry = &events[--count];
        char when[32] = "-";
        if (entry->epoch_s > 0) {
            time_t epoch = (time_t)entry->epoch_s;
            struct tm local;
            localtime_r(&epoch, &local);
            strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
        }
        const char *css = entry->level == APP_EVENT_ERROR ? "error" :
                          entry->level == APP_EVENT_WARNING ? "warning" : "";
        char safe_event[APP_EVENT_TEXT_MAX * 6];
        html_escape(s_hu ? entry->text : entry->text_en, safe_event, sizeof(safe_event));
        result = sendf(req, "<tr><td class='muted'>%s</td><td class='%s'>%s</td><td>",
                       when, css, event_level_text(entry->level));
        if (result == ESP_OK) result = httpd_resp_send_chunk(req, safe_event, strlen(safe_event));
        if (result == ESP_OK) result = httpd_resp_send_chunk(req, "</td></tr>", HTTPD_RESP_USE_STRLEN);
    }
    free(events);
    if (result != ESP_OK) return result;
    return httpd_resp_send_chunk(req, "</table></div>", HTTPD_RESP_USE_STRLEN);
}

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

    httpd_resp_set_type(
        req,
        "text/html; charset=utf-8");

    /* Az oldal alapszerkezete és egyszerű, külső erőforrás nélküli stílusa. */
    httpd_resp_send_chunk(
        req,
        "<!doctype html><html lang='hu'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Zolee Energy Flow Display</title><style>" APP_WEB_CSS
        ".events td:first-child{white-space:nowrap;width:1%}.events td:nth-child(2){font-weight:600;width:1%}"
        /* Fix magasságú, görgethető lista: a kártya nem nyúlik el, a korábbi
         * sorok mégis visszanézhetők. */
        ".events-box{max-height:320px;overflow-y:auto;overscroll-behavior:contain}"
        ".events-box table{margin:0}"
        ".log-links{margin:10px 0 0;display:flex;gap:10px;flex-wrap:wrap}"
        "button.restart{background:#b34a09;color:#fff}"
        "@media(prefers-color-scheme:dark){button.restart{background:#ff9e4f;color:#211205}"
        "body[data-page=home] button.danger{color:#27100e}}"
        "#device-dialog{width:min(32rem,calc(100% - 24px));max-height:calc(100% - 24px);"
        "padding:20px;border:1px solid var(--line);border-radius:16px;background:var(--card);"
        "color:var(--text);box-shadow:var(--shadow);overflow:auto;overflow-wrap:anywhere}"
        "#device-dialog::backdrop{background:#091219b8}#device-dialog p:empty{margin:0}"
        "#device-countdown{font-weight:600}"
        "@media(max-width:480px){.device-actions>*,#device-buttons>button{width:100%}}"
        "</style></head><body data-page='home'>",
        HTTPD_RESP_USE_STRLEN);
    const char *nav = s_hu ? APP_WEB_NAV_HU : APP_WEB_NAV_EN;
    if (app_web_send_nav(req, nav, strlen(nav), s_hu, s_web_mode) != ESP_OK)
        return ESP_FAIL;

    if (app_web_send_support(req, s_hu) != ESP_OK) {
        return ESP_FAIL;
    }

    char safe_title[APP_PROJECT_TITLE_MAX * 6];
    html_escape(
        s_project_title[0]
            ? s_project_title
            : "Zolee Energy Flow Display",
        safe_title,
        sizeof(safe_title));

    sendf(req, "<h1>%s</h1>", safe_title);
    /* A navigáció a felső menüben van; itt csak az állapot és a gyakori műveletek. */
    httpd_resp_send_chunk(req, s_hu ?
        "<p class='lead'>Eszközállapot, legutóbbi események és gyors műveletek.</p>"
        "<div class='actions device-actions'><a class='button secondary' href='/settings'>Beállítások</a>"
        "<a class='button secondary' href='/backup'>Mentés és visszaállítás</a>"
        "<button class='restart' type='button' data-device-action='restart'>Eszköz újraindítása</button>"
        "<button class='danger' type='button' data-device-action='factory'>Alaphelyzetbe állítás</button></div>" :
        "<p class='lead'>Device status, recent events and quick actions.</p>"
        "<div class='actions device-actions'><a class='button secondary' href='/settings'>Settings</a>"
        "<a class='button secondary' href='/backup'>Backup and restore</a>"
        "<button class='restart' type='button' data-device-action='restart'>Restart device</button>"
        "<button class='danger' type='button' data-device-action='factory'>Factory reset</button></div>",
        HTTPD_RESP_USE_STRLEN);

    /* Eszközinformációs kártya. */
    httpd_resp_send_chunk(
        req,
        s_hu
            ? "<div class='card'><h2>Eszközinformáció</h2><div class='kv'>"
            : "<div class='card'><h2>Device information</h2><div class='kv'>",
        HTTPD_RESP_USE_STRLEN);

    sendf(
        req,
        "<div><span class='k'>Firmware</span><div class='v'>%s</div></div>",
        description ? description->version : "-");

    sendf(
        req,
        s_hu
            ? "<div><span class='k'>Üzemidő</span><div class='v'>%d nap %02d:%02d:%02d</div></div>"
            : "<div><span class='k'>Uptime</span><div class='v'>%d days %02d:%02d:%02d</div></div>",
        days,
        hours,
        minutes,
        seconds);

    sendf(req, "<div><span class='k'>IP</span><div class='v'>%s</div></div>", ip);

    sendf(
        req,
        "<div><span class='k'>Wi-Fi RSSI</span><div class='v'>%d dBm</div></div>",
        runtime.wifi_connected ? rssi : 0);

    const char *mode = web_mode_name(runtime.data_recept_mode);
    sendf(req, "<div><span class='k'>%s</span><div class='v'>%s</div></div>", s_hu ? "Adatfogadási mód" : "Data reception mode", mode);
    sendf(req, "<div><span class='k'>%s %s</span><div class='v'>%s</div></div>", mode,
        s_hu ? "adatfogadó" : "receiver",
        app_state_data_ready(&runtime) ? (s_hu ? "Készen áll" : "Ready") : (s_hu ? "Nem elérhető" : "Unavailable"));
    sendf(req, "<div><span class='k'>%s %s</span><div class='v'>%s</div></div>", s_hu ? "Utolsó érvényes csomag:" : "Last valid packet:", mode, packet_time);

    sendf(
        req,
        s_hu
            ? "<div><span class='k'>SD-kártya</span><div class='v'>%s</div></div>"
            : "<div><span class='k'>SD card</span><div class='v'>%s</div></div>",
        app_storage_is_ok()
            ? (s_hu ? "Rendben" : "OK")
            : (s_hu ? "Hiba" : "Error"));

    sendf(
        req,
        s_hu
            ? "<div><span class='k'>Reset oka</span><div class='v'>%s</div></div>"
            : "<div><span class='k'>Reset reason</span><div class='v'>%s</div></div>",
        reset_text(esp_reset_reason()));

    httpd_resp_send_chunk(
        req,
        "</div></div>",
        HTTPD_RESP_USE_STRLEN);

    /* Legutóbbi RAM események táblázata. */
    httpd_resp_send_chunk(
        req,
        s_hu
            ? "<div class='card'><h2>Legutóbbi események</h2>"
            : "<div class='card'><h2>Recent events</h2>",
        HTTPD_RESP_USE_STRLEN);

    if (send_recent_events(req) != ESP_OK) return ESP_FAIL;

    /* A teljes napló az SD-ről: a mai és az előző napi fájl külön nyitható,
     * így újraindítás után is visszakereshető, mi történt. */
    httpd_resp_send_chunk(
        req,
        s_hu
            ? "<p class='log-links'>"
              "<a class='button secondary' href='/api/events/log' target='_blank' rel='noopener'>Mai napló</a>"
              "<a class='button secondary' href='/api/events/log?prev=1' target='_blank' rel='noopener'>Előző napi napló</a>"
              "</p>"
            : "<p class='log-links'>"
              "<a class='button secondary' href='/api/events/log' target='_blank' rel='noopener'>Today's log</a>"
              "<a class='button secondary' href='/api/events/log?prev=1' target='_blank' rel='noopener'>Previous day's log</a>"
              "</p>",
        HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(
        req,
        "</div>",
        HTTPD_RESP_USE_STRLEN);

    battery_section(req);

    /* A műveletgombok már az oldal tetején, a Beállítások mellett szerepelnek. */

    sendf(req,"<script>document.documentElement.lang='%s';</script>",s_hu?"hu":"en");
    if (httpd_resp_send_chunk(req, app_home_actions, sizeof(app_home_actions) - 1) != ESP_OK) {
        return ESP_FAIL;
    }

    /* A chunked HTTP választ nulla hosszú chunk zárja le. */
    if (httpd_resp_send_chunk(
            req,
            "</body></html>",
            HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}

#include "app_ota_web.inc"

/* A böngészős OTA feltöltőoldal kiszolgálása, a beállított nyelven. */
static esp_err_t update_get(httpd_req_t *req)
{
    if(!authorize(req))return ESP_OK;
    static const char page_hu[] =
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Firmware frissítés</title><style>" APP_WEB_CSS "progress{margin-top:15px}#s{font-weight:600}#pick{display:flex;flex-wrap:wrap;gap:10px;align-items:flex-start}#pick input{flex:1 1 260px}</style></head><body data-page='update'>" APP_WEB_NAV_HU "<!--APP_SUPPORT-->"
        "<h1>Firmware frissítés</h1><p class='lead'>Új firmware a GitHubról automatikusan vagy kézzel, .bin fájlból. A frissítés után az eszköz újraindul; az SD-s témákat a Sablonok oldalon frissítheted.</p>"
        "<section><h2>Automatikus frissítés</h2><p>A legújabb stabil GitHub-kiadás ellenőrzése és telepítése. <a href='/'>Rendszernapló megnyitása</a></p><button id='auto' onclick='autoNow()'>Automatikus frissítés most</button></section>"
        "<section><h2>Kézi frissítés</h2>"
        "<div id='pick'><input id='f' type='file' aria-label='Firmware-fájl (.bin)' accept='.bin'><button id='btn' onclick='go()'>Frissítés indítása</button></div>"
        "<progress id='p' value='0' max='100' hidden></progress><p role='status' aria-live='polite' id='s'>Válassz egy .bin fájlt.</p></section>"
        "<script>"
        "async function autoNow(){const b=document.getElementById('auto'),u=document.getElementById('btn'),f=document.getElementById('f'),s=document.getElementById('s');b.disabled=u.disabled=f.disabled=true;s.textContent=\"GitHub-ellenőrzés indítása…\";try{const r=await fetch('/api/ota/auto',{method:'POST',headers:{'X-Zolee-OTA':'1'},cache:'no-store'});s.textContent=await r.text();}catch(e){s.textContent=\"Kapcsolati hiba; ellenőrizd a rendszernaplót.\";}finally{b.disabled=u.disabled=f.disabled=false;}}"
        "function reconnectAfterOta(){"
        "const status=document.getElementById('s');let left=30;"
        "status.textContent='Firmware telepítve, az eszköz újraindul. Újracsatlakozás '+left+' s múlva.';"
        "const timer=setInterval(()=>{left--;"
        "if(left>0){status.textContent='Újraindítás, kapcsolódás '+left+' s múlva.';return;}"
        "clearInterval(timer);status.textContent='Kapcsolódás az újraindult eszközhöz…';"
        "const retry=async()=>{let timerId;"
        "try{const ac=new AbortController();timerId=setTimeout(()=>ac.abort(),2500);"
        "const response=await fetch('/?ready='+Date.now(),{cache:'no-store',signal:ac.signal});"
        "if(response.ok){location.replace('/?ota='+Date.now());return;}"
        "}catch(e){}finally{clearTimeout(timerId);}"
        "setTimeout(retry,2000);};retry();"
        "},1000);"
        "}"
        "function go(){const f=document.getElementById('f').files[0],s=document.getElementById('s'),p=document.getElementById('p'),pick=document.getElementById('pick');"
        "if(!f){s.textContent=\"Előbb válassz ki egy firmware-fájlt.\";return;}pick.hidden=true;document.getElementById('auto').disabled=true;p.hidden=false;s.textContent=\"Feltöltés indítása…\";"
        "const x=new XMLHttpRequest();x.open('POST','/update');x.setRequestHeader('Content-Type','application/octet-stream');"
        "x.upload.onprogress=e=>{if(e.lengthComputable){const n=Math.round(e.loaded/e.total*100);p.value=n;s.textContent=\"Firmware feltöltése: \"+n+'%';if(n===100)s.textContent=\"Feltöltve. Firmware ellenőrzése az eszközön…\";}};"
        "x.onload=()=>{if(x.status>=200&&x.status<300){p.value=100;reconnectAfterOta();}else{pick.hidden=false;document.getElementById('auto').disabled=false;p.hidden=true;s.textContent=\"Frissítési hiba: \"+x.responseText;}};"
        "x.onerror=()=>{pick.hidden=false;document.getElementById('auto').disabled=false;p.hidden=true;s.textContent=\"Kapcsolati hiba. Ellenőrizd, hogy elindult-e az új firmware.\";};x.send(f);}</script></body></html>";
    static const char page_en[] =
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Firmware update</title><style>" APP_WEB_CSS "progress{margin-top:15px}#s{font-weight:600}#pick{display:flex;flex-wrap:wrap;gap:10px;align-items:flex-start}#pick input{flex:1 1 260px}</style></head><body data-page='update'>" APP_WEB_NAV_EN "<!--APP_SUPPORT-->"
        "<h1>Firmware update</h1><p class='lead'>Install new firmware from GitHub automatically or manually from a .bin file. The device restarts afterwards; update SD themes on the Templates page.</p>"
        "<section><h2>Automatic update</h2><p>Check and install the latest stable GitHub release. <a href='/'>Open system log</a></p><button id='auto' onclick='autoNow()'>Check for automatic update now</button></section>"
        "<section><h2>Manual update</h2>"
        "<div id='pick'><input id='f' type='file' aria-label='Firmware file (.bin)' accept='.bin'><button id='btn' onclick='go()'>Start update</button></div>"
        "<progress id='p' value='0' max='100' hidden></progress><p role='status' aria-live='polite' id='s'>Select a .bin file.</p></section>"
        "<script>"
        "async function autoNow(){const b=document.getElementById('auto'),u=document.getElementById('btn'),f=document.getElementById('f'),s=document.getElementById('s');b.disabled=u.disabled=f.disabled=true;s.textContent=\"Starting GitHub check…\";try{const r=await fetch('/api/ota/auto',{method:'POST',headers:{'X-Zolee-OTA':'1'},cache:'no-store'});s.textContent=await r.text();}catch(e){s.textContent=\"Connection error; check the system log.\";}finally{b.disabled=u.disabled=f.disabled=false;}}"
        "function reconnectAfterOta(){"
        "const status=document.getElementById('s');let left=30;"
        "status.textContent='Firmware installed; restarting. Reconnecting in '+left+' s.';"
        "const timer=setInterval(()=>{left--;"
        "if(left>0){status.textContent='Restarting; reconnecting in '+left+' s.';return;}"
        "clearInterval(timer);status.textContent='Connecting to restarted device…';"
        "const retry=async()=>{let timerId;"
        "try{const ac=new AbortController();timerId=setTimeout(()=>ac.abort(),2500);"
        "const response=await fetch('/?ready='+Date.now(),{cache:'no-store',signal:ac.signal});"
        "if(response.ok){location.replace('/?ota='+Date.now());return;}"
        "}catch(e){}finally{clearTimeout(timerId);}"
        "setTimeout(retry,2000);};retry();"
        "},1000);"
        "}"
        "function go(){const f=document.getElementById('f').files[0],s=document.getElementById('s'),p=document.getElementById('p'),pick=document.getElementById('pick');"
        "if(!f){s.textContent=\"Select a firmware file first.\";return;}pick.hidden=true;document.getElementById('auto').disabled=true;p.hidden=false;s.textContent=\"Starting upload…\";"
        "const x=new XMLHttpRequest();x.open('POST','/update');x.setRequestHeader('Content-Type','application/octet-stream');"
        "x.upload.onprogress=e=>{if(e.lengthComputable){const n=Math.round(e.loaded/e.total*100);p.value=n;s.textContent=\"Firmware upload: \"+n+'%';if(n===100)s.textContent=\"Uploaded. Verifying firmware on device…\";}};"
        "x.onload=()=>{if(x.status>=200&&x.status<300){p.value=100;reconnectAfterOta();}else{pick.hidden=false;document.getElementById('auto').disabled=false;p.hidden=true;s.textContent=\"Firmware update failed: \"+x.responseText;}};"
        "x.onerror=()=>{pick.hidden=false;document.getElementById('auto').disabled=false;p.hidden=true;s.textContent=\"Connection lost. Check whether the new firmware started.\";};x.send(f);}</script></body></html>";
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return app_web_send_page(req, s_hu ? page_hu : page_en, s_hu);
}

/* Késleltetett reboot task, hogy a HTTP válasz még ki tudjon menni. */
static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
}

/* Kézi újraindítás HTTP kezelője. */
static esp_err_t reboot_post_locked(httpd_req_t *req)
{
    if (!authorize(req)) {
        return ESP_OK;
    }

    /* SD-hiba nem akadályozhatja az újraindítást: mentés nélkül is a napi
     * statisztika veszne el, amit hibás kártyán úgysem lehet kiírni, viszont
     * a hardveres reset marad az egyetlen kiút. A hiba naplóba kerül. */
    esp_err_t saved=app_stats_save_now();
    if(saved!=ESP_OK)
        app_events_add(APP_EVENT_WARNING,
            "Újraindítás előtt a statisztika mentése sikertelen (%s); az újraindítás folytatódik.",
            esp_err_to_name(saved));
    if(xTaskCreate(reboot_task,"reboot",2048,NULL,8,NULL)!=pdPASS){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
            s_hu?"Az újraindítás nem ütemezhető.":"Could not schedule restart.");
        return ESP_OK;
    }
    s_restart_pending = true;
    httpd_resp_sendstr(req,s_hu?"Újraindítás...":"Restarting...");

    return ESP_OK;
}

/*
 * Bináris OTA image fogadása.
 * A bejövő adat közvetlenül az inaktív OTA partícióra kerül, 4 kB-os blokkokban.
 */
static esp_err_t ota_post_locked(httpd_req_t *req)
{
    if (!authorize(req)) {
        return ESP_OK;
    }

    if (req->content_len <= 0) {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            (s_hu?"Üres firmware-fájl":"Empty image"));
        return ESP_OK;
    }

    const esp_partition_t *target =
        esp_ota_get_next_update_partition(NULL);

    if (!target ||
        (size_t)req->content_len > target->size) {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            (s_hu?"Túl nagy firmware-fájl":"Image too large"));
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
            (s_hu?"A frissítés nem indítható":"OTA begin failed"));
        return ESP_OK;
    }

    char *buffer = malloc(4096);
    if (!buffer) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            (s_hu?"Nincs elegendő memória":"Out of memory"));
        return ESP_OK;
    }

    int remaining = req->content_len;
    int64_t last_progress=esp_timer_get_time();

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
            if(esp_timer_get_time()-last_progress<30000000LL)continue;
            free(buffer);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,
                s_hu?"A feltöltés 30 másodperce nem halad.":"Upload stalled for 30 seconds.");
            return ESP_OK;
        }

        if (received <= 0) {
            free(buffer);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(
                req,
                HTTPD_500_INTERNAL_SERVER_ERROR,
                (s_hu?"A feltöltés megszakadt":"Upload interrupted"));
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
                (s_hu?"A firmware írása sikertelen":"Write failed"));
            return ESP_OK;
        }

        remaining -= received;
        last_progress=esp_timer_get_time();
    }

    free(buffer);

    /*
     * esp_ota_end ellenőrzi az image struktúráját.
     * Csak sikeres lezárás után állítjuk be következő boot partíciónak.
     */
    err = esp_ota_end(ota_handle);

    if (err == ESP_OK) {
        /* A már ellenőrzött firmware aktiválását SD-hiba nem akadályozhatja:
         * a statisztika hibás kártyára úgysem írható, a frissítés viszont
         * éppen ilyenkor kell. A sikertelen mentés naplóba kerül. */
        esp_err_t saved=app_stats_save_now();
        if(saved!=ESP_OK)
            app_events_add(APP_EVENT_WARNING,
                "OTA előtt a statisztika mentése sikertelen (%s); az aktiválás folytatódik.",
                esp_err_to_name(saved));
        err = esp_ota_set_boot_partition(target);
    }

    if (err != ESP_OK) {
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            (s_hu?"A firmware ellenőrzése sikertelen":"Validation failed"));
        return ESP_OK;
    }

    app_events_add(
        APP_EVENT_INFO,
        "Új OTA firmware feltöltve; újraindítás következik.");

    if(xTaskCreate(reboot_task,"ota_reboot",2048,NULL,8,NULL)!=pdPASS){
        httpd_resp_sendstr(req,s_hu?"Firmware elmentve. Indítsd újra az eszközt.":
            "Firmware saved. Please restart the device.");
        return ESP_OK;
    }
    s_restart_pending = true;
    httpd_resp_sendstr(req,s_hu?"Firmware feltöltve. Az eszköz újraindul.":
        "Firmware uploaded. The device will restart.");

    return ESP_OK;
}

/* Az egyetlen webtask és az automatikus OTA ugyanazt az atomi foglalást használja.
 * Sikeres aktiválás után a rebootig nem engedünk új flash-írást/konfigváltást. */
static esp_err_t exclusive_request(httpd_req_t *req, esp_err_t (*handler)(httpd_req_t *))
{
    if (!authorize(req)) return ESP_OK;
    if (!app_ota_try_begin_operation()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "OTA/frissítés vagy újraindítás folyamatban. Próbáld később.");
    }
    esp_err_t result = handler(req);
    if (!s_restart_pending && esp_ota_get_boot_partition() == esp_ota_get_running_partition())
        app_ota_end_operation();
    return result;
}

static esp_err_t ota_post(httpd_req_t *req)
{
    return exclusive_request(req, ota_post_locked);
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    return exclusive_request(req, reboot_post_locked);
}

#include "app_factory_reset_web.inc"

static esp_err_t factory_reset_post(httpd_req_t *req)
{
    return exclusive_request(req, factory_reset_post_locked);
}

static esp_err_t diagnostics_get(httpd_req_t *req){
    if(!authorize(req))return ESP_OK;
    app_diagnostics_t d;app_diagnostics_capture(&d);
    cJSON *o=cJSON_CreateObject();if(!o)return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(o,"wifi_connected",d.runtime.wifi_connected);
    cJSON_AddStringToObject(o,"ip",d.ip);cJSON_AddNumberToObject(o,"rssi_dbm",d.rssi);
    cJSON_AddStringToObject(o,"data_mode",web_mode_name(d.runtime.data_recept_mode));
    cJSON_AddBoolToObject(o,"ble_enabled",d.runtime.ble_enabled);
    if(d.runtime.ble_enabled){
        cJSON_AddStringToObject(o,"ble_role",d.runtime.ble_master?"Master":"Slave");
        cJSON_AddBoolToObject(o,"ble_ready",d.runtime.ble_ready);
        cJSON_AddBoolToObject(o,"ble_advertising",d.runtime.ble_advertising);
        cJSON_AddBoolToObject(o,"ble_scanning",d.runtime.ble_scanning);
        cJSON_AddStringToObject(o,"ble_address",d.runtime.ble_address);
        cJSON_AddStringToObject(o,"ble_peer",d.runtime.ble_peer);
        if(d.runtime.ble_master)cJSON_AddNumberToObject(o,"ble_slaves",d.runtime.ble_slaves);
        else{cJSON_AddBoolToObject(o,"slave_connected",d.runtime.slave_connected);cJSON_AddNumberToObject(o,"ble_rssi_dbm",d.runtime.ble_rssi);}
    }
    cJSON_AddBoolToObject(o,"data_ready",app_state_data_ready(&d.runtime));
    cJSON_AddBoolToObject(o,"mqtt_connected",d.runtime.mqtt_connected);
    cJSON_AddBoolToObject(o,"sd_ok",d.runtime.sd_ok);cJSON_AddBoolToObject(o,"time_synced",d.runtime.time_synced);
    cJSON_AddBoolToObject(o,"cardless_slave",d.runtime.cardless_slave);
    if (d.runtime.cardless_slave) cJSON_AddBoolToObject(o,"remote_stats_valid",d.runtime.remote_stats_valid);
    if(d.packet_age_seconds<0)cJSON_AddNullToObject(o,"packet_age_seconds");else cJSON_AddNumberToObject(o,"packet_age_seconds",d.packet_age_seconds);
    cJSON_AddStringToObject(o,"firmware",d.firmware);cJSON_AddNumberToObject(o,"uptime_seconds",d.uptime_seconds);
    if(d.cpu_valid){cJSON_AddNumberToObject(o,"cpu0_percent",d.cpu_percent[0]);cJSON_AddNumberToObject(o,"cpu1_percent",d.cpu_percent[1]);}
    else {cJSON_AddNullToObject(o,"cpu0_percent");cJSON_AddNullToObject(o,"cpu1_percent");}
    cJSON_AddNumberToObject(o,"total_internal_kib",d.total_internal_kib);
    cJSON_AddNumberToObject(o,"total_psram_kib",d.total_psram_kib);cJSON_AddNumberToObject(o,"free_psram_kib",d.free_psram_kib);
    if(d.sd_space_valid){cJSON_AddNumberToObject(o,"sd_total_bytes",d.sd_total_bytes);cJSON_AddNumberToObject(o,"sd_free_bytes",d.sd_free_bytes);
        cJSON_AddNumberToObject(o,"sd_used_percent",round((double)(d.sd_total_bytes-d.sd_free_bytes)*10000/d.sd_total_bytes)/100);}
    else{cJSON_AddNullToObject(o,"sd_total_bytes");cJSON_AddNullToObject(o,"sd_free_bytes");cJSON_AddNullToObject(o,"sd_used_percent");}
    cJSON_AddNumberToObject(o,"free_heap_kib",d.free_heap_kib);
    cJSON_AddNumberToObject(o,"free_internal_kib",d.free_internal_kib);
    cJSON_AddNumberToObject(o,"largest_dma_block_bytes",d.largest_dma_block_bytes);
    cJSON_AddNumberToObject(o,"min_free_internal_kib",d.min_free_internal_kib);
    char *json=cJSON_PrintUnformatted(o);cJSON_Delete(o);if(!json)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_sendstr(req,json);free(json);return result;
}
/*
 * A mentett eseménynapló kiszolgálása (mai vagy előző napi).
 *
 * A kártyát csak az olvasás idejére vesszük el, és nem blokkolva: ha más
 * dolgozik rajta, a kérés udvariasan visszautasít. A fájl végéről legfeljebb
 * 64 KiB-ot olvasunk egy PSRAM-pufferbe, majd a zár elengedése után küldjük el
 * - így egy lassú böngésző sem tartja a kártyát a kijelző elől.
 */
#define EVENT_LOG_TAIL (64U * 1024U)
static esp_err_t events_log_get(httpd_req_t *req)
{
    char query[32] = {0};
    bool previous = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
                    strstr(query, "prev=1") != NULL;
    const char *path = previous ? APP_EVENT_STORE_DIR "/events-prev.log"
                                : APP_EVENT_STORE_DIR "/events.log";
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (!app_storage_is_ok() || app_storage_is_cardless()) {
        return httpd_resp_sendstr(req, s_hu ? "Az SD-kártya nem érhető el.\n"
                                            : "The SD card is unavailable.\n");
    }
    if (!app_storage_try_lock()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, s_hu ? "A kártyán épp más művelet fut; próbáld újra.\n"
                                            : "The card is busy; please try again.\n");
    }
    char *text = NULL;
    size_t length = 0;
    FILE *file = fopen(path, "rb");
    if (file) {
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        long from = size > (long)EVENT_LOG_TAIL ? size - (long)EVENT_LOG_TAIL : 0;
        if (size > 0 && !fseek(file, from, SEEK_SET)) {
            size_t want = (size_t)(size - from);
            text = malloc(want + 1);
            if (text) {
                length = fread(text, 1, want, file);
                text[length] = 0;
            }
        }
        fclose(file);
    }
    app_storage_unlock();
    if (!text) {
        return httpd_resp_sendstr(req, s_hu ? "Ehhez a naphoz még nincs mentett napló.\n"
                                            : "No stored log for this day yet.\n");
    }
    /* A fájl közepére ugrottunk: a csonka első sort eldobjuk. */
    char *start = text;
    bool header = length && text[0] == '#';
    if (length && !header) {
        char *newline = strchr(text, '\n');
        if (newline) {
            start = newline + 1;
        }
    }
    /*
     * A fájl függelékként épül (ez a helyes és olcsó írási mód), a lista
     * viszont a legfrissebb sorral kezdődjön, mint a kijelzőn és az
     * információs oldalon. A megfordítás helyben, második puffer nélkül megy;
     * a napot jelölő fejléc marad legelöl.
     */
    if (header) {
        char *newline = strchr(start, '\n');
        if (newline) {
            app_event_log_reverse_lines(newline + 1);
        }
    } else {
        app_event_log_reverse_lines(start);
    }
    esp_err_t result = httpd_resp_sendstr(req, start);
    free(text);
    return result;
}

/*
 * Honnan fogy a szűkös belső RAM: feladatonkénti szabad verem és a belső
 * heap összesítője. A feladatvermek mindig belső RAM-ból foglalódnak, ezért
 * a mérés nélküli tippelgetés helyett ez mutatja meg a nagy fogyasztókat.
 */
static esp_err_t diagnostics_tasks_get(httpd_req_t *req){
    if(!authorize(req))return ESP_OK;
    UBaseType_t count=uxTaskGetNumberOfTasks();
    TaskStatus_t *status=count?calloc(count,sizeof(*status)):NULL;
    char *body=malloc(2048);
    if(!status||!body){free(status);free(body);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Nincs elég memória a jelentéshez.");
        return ESP_OK;}
    count=uxTaskGetSystemState(status,count,NULL);
    /* A soros konzolra a régiónkénti bontás is kimegy: a DMA-képes memória
     * csak bizonyos régiókban van, és a HTTP-jelentés csak összesít. */
    heap_caps_print_heap_info(MALLOC_CAP_DMA);
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    multi_heap_info_t internal,dma;
    heap_caps_get_info(&internal,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    heap_caps_get_info(&dma,MALLOC_CAP_DMA);
    size_t used=0;
    used+=(size_t)snprintf(body+used,2048-used,
        "belso: szabad=%u B; legkisebb=%u B; legnagyobb blokk=%u B; foglalt=%u B\n"
        "DMA:   szabad=%u B; legnagyobb blokk=%u B\n"
        "feladatok (%u) – szabad verem bajtban:\n",
        (unsigned)internal.total_free_bytes,(unsigned)internal.minimum_free_bytes,
        (unsigned)internal.largest_free_block,(unsigned)internal.total_allocated_bytes,
        (unsigned)dma.total_free_bytes,(unsigned)dma.largest_free_block,(unsigned)count);
    for(UBaseType_t i=0;i<count&&used<2000;i++)
        used+=(size_t)snprintf(body+used,2048-used,"  %-16s %6u\n",
            status[i].pcTaskName?status[i].pcTaskName:"?",
            (unsigned)status[i].usStackHighWaterMark);
    free(status);
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_sendstr(req,body);
    free(body);
    return result;
}

static esp_err_t diagnostics_page(httpd_req_t *req){
    if(!authorize(req))return ESP_OK;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return app_web_send_page(req,"<!doctype html><html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Diagnostics</title>"
        "<style>" APP_WEB_CSS "#values td:first-child{color:var(--muted);width:45%}</style><body data-page='diagnostics'>" APP_WEB_NAV_HU "<!--APP_SUPPORT-->"
        "<h1>Diagnosztika / Diagnostics</h1><p class='lead'>Élő rendszerértékek, 3 másodpercenként frissül.</p><section><table id='values'></table></section>"
        "<section><h2>Belső RAM és feladatvermek</h2><pre id='tasks' style='white-space:pre-wrap;overflow-x:auto'>Betöltés…</pre></section>"
        "<script>const names={cpu0_percent:'CPU0 (%)',cpu1_percent:'CPU1 (%)',total_internal_kib:'Belső RAM összesen (KiB)',total_psram_kib:'PSRAM összesen (KiB)',free_psram_kib:'PSRAM szabad (KiB)',sd_total_bytes:'SD összesen (bájt)',sd_free_bytes:'SD szabad (bájt)',sd_used_percent:'SD foglaltság (%)',wifi_connected:'Wi-Fi',ip:'IP',rssi_dbm:'RSSI (dBm)',data_mode:'Adatforrás / Input',data_ready:'Adatkapcsolat / Input ready',"
        "ble_enabled:'Bluetooth',ble_role:'BT szerep / Role',ble_ready:'BT kész / Ready',ble_advertising:'BT hirdetés / Advertising',ble_scanning:'Master keresés / Scanning',ble_address:'Saját BT cím / Local address',ble_peer:'BT partner / Peer',ble_slaves:'Kapcsolódó slave-ek / Connected slaves',slave_connected:'Master kapcsolat / Master connected',ble_rssi_dbm:'BT RSSI (dBm)',"
        "mqtt_connected:'MQTT',sd_ok:'SD',cardless_slave:'SD nélküli Slave / Cardless Slave',remote_stats_valid:'Friss master-statisztika / Current master stats',time_synced:'Időszinkron / Time sync',packet_age_seconds:'Csomag kora (s) / Packet age (s)',firmware:'Firmware',uptime_seconds:'Üzemidő (s) / Uptime (s)',free_heap_kib:'Szabad memória (KiB) / Free memory (KiB)',free_internal_kib:'Szabad belső RAM (KiB) / Free internal RAM (KiB)',largest_dma_block_bytes:'Legnagyobb DMA-blokk (bájt) / Largest DMA block (bytes)',min_free_internal_kib:'Legkisebb szabad belső RAM (KiB) / Minimum free internal RAM (KiB)'};"
        "fetch('/api/diagnostics/tasks',{cache:'no-store'}).then(r=>r.text()).then(t=>document.getElementById('tasks').textContent=t).catch(()=>document.getElementById('tasks').textContent='A jelentés nem olvasható.');"
        "async function refresh(){try{let r=await fetch('/api/diagnostics',{cache:'no-store'});if(!r.ok)throw Error(r.status);let data=await r.json(),table=document.getElementById('values');table.replaceChildren();"
        "for(let [k,v] of Object.entries(data)){let row=table.insertRow();row.insertCell().textContent=names[k]||k;let cell=row.insertCell();cell.textContent=v===null?'—':(k==='sd_used_percent'?Number(v).toFixed(2):v);if(k==='sd_used_percent'&&v!==null){let bar=document.createElement('progress');bar.max=100;bar.value=v;bar.style.width='100%';cell.append(bar);}}}catch(e){document.getElementById('values').textContent='Kapcsolati hiba / Connection failed';}}"
        "refresh();setInterval(refresh,3000);</script></html>", s_hu);
}

/* mDNS és HTTP szerver egyszeri inicializálása, majd a végpontok regisztrálása. */
#include "app_template_web.inc"
#include "app_settings_web.inc"
#include "app_loxone_web.inc"
#include "app_inverter_web.inc"
#include "app_backup_web.inc"
#include "app_template_update_web.inc"
esp_err_t app_web_start(const app_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_server) {
        return ESP_ERR_INVALID_STATE;
    }

    s_config = *cfg;
    s_web_mode = cfg->data_recept_mode;
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
    http_cfg.max_uri_handlers = 64;
    /* Minden nyitott kapcsolat lwIP-puffereket köt le a szűk, DMA-képes belső
     * RAM-ból; a böngésző hatot is nyit. Négy elég a felülethez, a legrégebbit
     * a szerver magától bontja, így marad keret az SD-nek és a TLS-nek. */
    http_cfg.max_open_sockets = 4;
    http_cfg.lru_purge_enable = true;
    http_cfg.stack_size = 12288;
    http_cfg.recv_wait_timeout = 5;

    err = httpd_start(&s_server, &http_cfg);
    if (err != ESP_OK) {
        return err;
    }

    /* A kezdőlap és a készülékműveletek végpontjai. */
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        /* A fekete beállítóképernyőn a csupasz helyi IP-cím látszik. */
        .handler = app_boot_config_setup_pending() ? settings_page : root_handler
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

    httpd_uri_t auto_update_uri = {
        .uri = "/api/ota/auto",
        .method = HTTP_POST,
        .handler = ota_auto_post
    };

    httpd_uri_t reboot_uri = {
        .uri = "/reboot",
        .method = HTTP_POST,
        .handler = reboot_post
    };

    const httpd_uri_t factory_reset_uri = {
        .uri = "/api/factory-reset",
        .method = HTTP_POST,
        .handler = factory_reset_post
    };

    if ((err = httpd_register_uri_handler(
            s_server,
            &root_uri)) != ESP_OK) {
        goto failed;
    }

    if ((err = httpd_register_uri_handler(
            s_server,
            &update_get_uri)) != ESP_OK) {
        goto failed;
    }

    if ((err = httpd_register_uri_handler(
            s_server,
            &update_post_uri)) != ESP_OK) {
        goto failed;
    }

    if ((err = httpd_register_uri_handler(
            s_server,
            &reboot_uri)) != ESP_OK) {
        goto failed;
    }

    if ((err = httpd_register_uri_handler(s_server, &auto_update_uri)) != ESP_OK) {
        goto failed;
    }
    if ((err = httpd_register_uri_handler(s_server, &factory_reset_uri)) != ESP_OK) {
        goto failed;
    }

    if ((err = app_http_register(s_server)) != ESP_OK) goto failed;
    if ((err = template_register(s_server)) != ESP_OK) goto failed;
    if ((err = settings_register(s_server)) != ESP_OK) goto failed;
    if ((err = loxone_register(s_server)) != ESP_OK) goto failed;
    if ((err = inverter_register(s_server)) != ESP_OK) goto failed;
    if ((err = backup_register(s_server)) != ESP_OK) goto failed;
    if ((err = template_updates_register(s_server)) != ESP_OK) goto failed;
    const httpd_uri_t diagnostic_routes[]={
        {.uri="/diagnostics",.method=HTTP_GET,.handler=diagnostics_page},
        {.uri="/api/diagnostics",.method=HTTP_GET,.handler=diagnostics_get},
        {.uri="/api/battery",.method=HTTP_GET,.handler=battery_get},
        {.uri="/api/battery/reset",.method=HTTP_POST,.handler=battery_reset_post},
        {.uri="/api/diagnostics/tasks",.method=HTTP_GET,.handler=diagnostics_tasks_get},
        {.uri="/api/events/log",.method=HTTP_GET,.handler=events_log_get}};
    for(unsigned i=0;i<6;i++)if((err=httpd_register_uri_handler(s_server,&diagnostic_routes[i]))!=ESP_OK)goto failed;
    app_state_set_http_ready(cfg->data_recept_mode != APP_DATA_MQTT);

    app_events_add(
        APP_EVENT_INFO,
        "Webes információs és OTA felület elindult.");

    return ESP_OK;
failed:
    httpd_stop(s_server);
    s_server = NULL;
    app_state_set_http_ready(false);
    return err;
}

