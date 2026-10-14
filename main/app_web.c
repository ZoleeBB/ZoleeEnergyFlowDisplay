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
#include "app_http.h"
#include "app_templates.h"
#include "app_diagnostics.h"

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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mdns.h"

#include "app_events.h"
#include "app_state.h"
#include "app_storage.h"
#include "app_stats.h"
#include "app_wifi.h"

static const char *TAG = "web";

static app_ota_config_t s_auth_cfg;
static app_config_t s_config;
static httpd_handle_t s_server;
static char s_expected_auth[256];
static char s_project_title[APP_PROJECT_TITLE_MAX];
static bool s_hu = true;

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
        "<!doctype html><html lang='hu'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{font-family:system-ui;margin:28px;max-width:900px;background:#f7f8fa;color:#17202a}"
        ".card{background:#fff;border-radius:14px;padding:18px 22px;margin:14px 0;box-shadow:0 2px 12px #0001}"
        "table{border-collapse:collapse;width:100%}"
        "td{padding:7px;border-bottom:1px solid #eee}"
        "button,a{padding:10px 15px;border:0;border-radius:9px;background:#176b45;color:white;text-decoration:none;cursor:pointer}"
        ".actions{display:flex;gap:10px;flex-wrap:wrap;margin:8px 0 22px}"
        ".actions>a,.actions button{display:flex;align-items:center;justify-content:center;width:205px;min-height:44px;box-sizing:border-box;margin:0;text-align:center;font:inherit}"
        ".actions .restart{background:#DE790E;color:white;font-weight:600}"
        ".actions .restart:hover{background:#B85E06}"
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
    /* A négy főművelet együtt, az oldal tetején, az eszközinformációk előtt. */
    httpd_resp_send_chunk(req,s_hu?
        "<nav class='actions' aria-label='Eszközműveletek'>"
        "<a href='/settings'>Beállítások</a>"
        "<a href='/templates'>Sablonkezelő</a>"
        "<a href='/update'>Firmware frissítés</a>"
        "<button class='restart' type='button' onclick='restartDevice()'>Eszköz újraindítása</button>"
        "</nav>":
        "<nav class='actions' aria-label='Device actions'>"
        "<a href='/settings'>Settings</a>"
        "<a href='/templates'>Templates</a>"
        "<a href='/update'>Firmware update</a>"
        "<button class='restart' type='button' onclick='restartDevice()'>Restart device</button>"
        "</nav>",HTTPD_RESP_USE_STRLEN);

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

    const char *mode = runtime.data_recept_mode == APP_DATA_HTTP ? "HTTP" : "MQTT";
    sendf(req, "<tr><td>%s</td><td>%s</td></tr>", s_hu ? "Adatfogadási mód" : "Data reception mode", mode);
    sendf(req, "<tr><td>%s %s</td><td>%s</td></tr>", mode,
        s_hu ? "adatfogadó" : "receiver",
        app_state_data_ready(&runtime) ? (s_hu ? "Készen áll" : "Ready") : (s_hu ? "Nem elérhető" : "Unavailable"));
    sendf(req, "<tr><td>%s %s</td><td>%s</td></tr>", s_hu ? "Utolsó érvényes csomag:" : "Last valid packet:", mode, packet_time);

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
                s_hu?events[i].text:events[i].text_en,
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

    /* A műveletgombok már az oldal tetején, a Beállítások mellett szerepelnek. */

    sendf(req,"<script>document.documentElement.lang='%s';</script>",s_hu?"hu":"en");
    httpd_resp_send_chunk(req,
        "<div id='reboot-mask' hidden style='position:fixed;inset:0;background:#000;color:#fff;"
        "z-index:999;display:none;align-items:center;justify-content:center;text-align:center'>"
        "<div><h1 id='reboot-title'></h1><p id='reboot-state'></p>"
        "<p id='reboot-countdown' style='font-size:28px'></p></div></div>"
        "<script>"
        "async function restartDevice(){"
        "const hu=document.documentElement.lang!=='en';"
        "if(!confirm(hu?'Biztosan újraindítod az eszközt?':'Restart the device?'))return;"
        "const m=document.getElementById('reboot-mask');m.style.display='flex';"
        "document.getElementById('reboot-title').textContent=hu?'Újraindítás folyamatban':'Restarting device';"
        "const status=document.getElementById('reboot-state');"
        "const counter=document.getElementById('reboot-countdown');"
        "status.textContent=hu?'Statisztika mentése és az eszköz újraindítása…':'Saving statistics and restarting…';"
        "try{const r=await fetch('/reboot',{method:'POST'});if(!r.ok)throw Error(await r.text());}"
        "catch(e){status.textContent=(hu?'Újraindítási hiba: ':'Restart error: ')+e.message;return;}"
        "let remaining=20,started=Date.now(),offline=false;"
        "const tick=async()=>{remaining=Math.max(0,20-Math.floor((Date.now()-started)/1000));"
        "counter.textContent=remaining>0?(hu?'Visszaszámlálás: ':'Countdown: ')+remaining+' s':'';"
        "if(remaining>0){setTimeout(tick,1000);return;}"
        "status.textContent=hu?'Kapcsolódás az újraindult eszközhöz…':'Reconnecting to device…';"
        "const probe=async()=>{try{const c=new AbortController();"
        "const t=setTimeout(()=>c.abort(),1800);"
        "const r=await fetch('/api/settings?ping='+Date.now(),{cache:'no-store',signal:c.signal});"
        "clearTimeout(t);if(r.ok){location.href='/';return;} }catch(e){}"
        "setTimeout(probe,2000);};probe();};tick();"
        "}"
        "</script>",HTTPD_RESP_USE_STRLEN);

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
    if(!authorize(req))return ESP_OK;
    static const char page_hu[] =
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font:16px system-ui;max-width:760px;margin:35px auto;padding:20px;background:#edf3f5;color:#14202a}"
        ".card{background:white;padding:22px;border-radius:16px}button{padding:12px 18px;border:0;border-radius:9px;background:#176b45;color:white;cursor:pointer}"
        "progress{width:100%;margin-top:15px}#s{font-weight:600}</style></head><body>"
        "<a href='/'>← Eszköz</a><h1>Firmware frissítés</h1><div class='card'>"
        "<div id='pick'><input id='f' type='file' accept='.bin'><button id='btn' onclick='go()'>Frissítés indítása</button></div>"
        "<progress id='p' value='0' max='100' hidden></progress><p role='status' aria-live='polite' id='s'>Válassz egy .bin fájlt.</p></div>"
        "<script>"
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
        "if(!f){s.textContent=\"Előbb válassz ki egy firmware-fájlt.\";return;}pick.hidden=true;p.hidden=false;s.textContent=\"Feltöltés indítása…\";"
        "const x=new XMLHttpRequest();x.open('POST','/update');x.setRequestHeader('Content-Type','application/octet-stream');"
        "x.upload.onprogress=e=>{if(e.lengthComputable){const n=Math.round(e.loaded/e.total*100);p.value=n;s.textContent=\"Firmware feltöltése: \"+n+'%';if(n===100)s.textContent=\"Feltöltve. Firmware ellenőrzése az eszközön…\";}};"
        "x.onload=()=>{if(x.status>=200&&x.status<300){p.value=100;reconnectAfterOta();}else{pick.hidden=false;p.hidden=true;s.textContent=\"Frissítési hiba: \"+x.responseText;}};"
        "x.onerror=()=>{pick.hidden=false;p.hidden=true;s.textContent=\"Kapcsolati hiba. Ellenőrizd, hogy elindult-e az új firmware.\";};x.send(f);}</script></body></html>";
    static const char page_en[] =
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font:16px system-ui;max-width:760px;margin:35px auto;padding:20px;background:#edf3f5;color:#14202a}"
        ".card{background:white;padding:22px;border-radius:16px}button{padding:12px 18px;border:0;border-radius:9px;background:#176b45;color:white;cursor:pointer}"
        "progress{width:100%;margin-top:15px}#s{font-weight:600}</style></head><body>"
        "<a href='/'>← Device</a><h1>Firmware update</h1><div class='card'>"
        "<div id='pick'><input id='f' type='file' accept='.bin'><button id='btn' onclick='go()'>Start update</button></div>"
        "<progress id='p' value='0' max='100' hidden></progress><p role='status' aria-live='polite' id='s'>Select a .bin file.</p></div>"
        "<script>"
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
        "if(!f){s.textContent=\"Select a firmware file first.\";return;}pick.hidden=true;p.hidden=false;s.textContent=\"Starting upload…\";"
        "const x=new XMLHttpRequest();x.open('POST','/update');x.setRequestHeader('Content-Type','application/octet-stream');"
        "x.upload.onprogress=e=>{if(e.lengthComputable){const n=Math.round(e.loaded/e.total*100);p.value=n;s.textContent=\"Firmware upload: \"+n+'%';if(n===100)s.textContent=\"Uploaded. Verifying firmware on device…\";}};"
        "x.onload=()=>{if(x.status>=200&&x.status<300){p.value=100;reconnectAfterOta();}else{pick.hidden=false;p.hidden=true;s.textContent=\"Firmware update failed: \"+x.responseText;}};"
        "x.onerror=()=>{pick.hidden=false;p.hidden=true;s.textContent=\"Connection lost. Check whether the new firmware started.\";};x.send(f);}</script></body></html>";
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,s_hu?page_hu:page_en,HTTPD_RESP_USE_STRLEN);
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

    esp_err_t saved=app_stats_save_now();
    if(saved!=ESP_OK){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
            "A statisztika mentése sikertelen. Az újraindítás elmaradt.");
        return ESP_OK;
    }
    if(xTaskCreate(reboot_task,"reboot",2048,NULL,8,NULL)!=pdPASS){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
            s_hu?"Az újraindítás nem ütemezhető.":"Could not schedule restart.");
        return ESP_OK;
    }
    httpd_resp_sendstr(req,s_hu?"Újraindítás...":"Restarting...");

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
        esp_err_t saved=app_stats_save_now();
        if(saved!=ESP_OK){
            app_events_add(APP_EVENT_ERROR,"OTA előtt a statisztika mentése sikertelen.");
            httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                "Az SD-statisztika mentése sikertelen; a firmware aktiválása elmaradt.");
            return ESP_OK;
        }
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
    httpd_resp_sendstr(req,s_hu?"Firmware feltöltve. Az eszköz újraindul.":
        "Firmware uploaded. The device will restart.");

    return ESP_OK;
}

static esp_err_t diagnostics_get(httpd_req_t *req){
    if(!authorize(req))return ESP_OK;
    app_diagnostics_t d;app_diagnostics_capture(&d);
    cJSON *o=cJSON_CreateObject();if(!o)return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(o,"wifi_connected",d.runtime.wifi_connected);
    cJSON_AddStringToObject(o,"ip",d.ip);cJSON_AddNumberToObject(o,"rssi_dbm",d.rssi);
    cJSON_AddStringToObject(o,"data_mode",d.runtime.data_recept_mode==APP_DATA_HTTP?"HTTP":"MQTT");
    cJSON_AddBoolToObject(o,"data_ready",app_state_data_ready(&d.runtime));
    cJSON_AddBoolToObject(o,"mqtt_connected",d.runtime.mqtt_connected);
    cJSON_AddBoolToObject(o,"sd_ok",d.runtime.sd_ok);cJSON_AddBoolToObject(o,"time_synced",d.runtime.time_synced);
    if(d.packet_age_seconds<0)cJSON_AddNullToObject(o,"packet_age_seconds");else cJSON_AddNumberToObject(o,"packet_age_seconds",d.packet_age_seconds);
    cJSON_AddStringToObject(o,"firmware",d.firmware);cJSON_AddNumberToObject(o,"uptime_seconds",d.uptime_seconds);
    cJSON_AddNumberToObject(o,"free_heap_kib",d.free_heap_kib);
    cJSON_AddNumberToObject(o,"free_internal_kib",d.free_internal_kib);
    cJSON_AddNumberToObject(o,"largest_dma_block_bytes",d.largest_dma_block_bytes);
    char *json=cJSON_PrintUnformatted(o);cJSON_Delete(o);if(!json)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_sendstr(req,json);free(json);return result;
}
static esp_err_t diagnostics_page(httpd_req_t *req){
    if(!authorize(req))return ESP_OK;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_sendstr(req,"<!doctype html><html><meta charset='utf-8'><title>Diagnostics</title>"
        "<style>body{font:18px system-ui;max-width:850px;margin:40px auto;background:#eef4f7}table{width:100%;background:white;border-radius:16px;padding:18px}td{padding:12px}</style>"
        "<a href='/'>← Eszköz / Device</a> · <a href='/templates'>Sablonok / Templates</a><h1>Diagnosztika / Diagnostics</h1><table id='values'></table>"
        "<script>const names={wifi_connected:'Wi-Fi',ip:'IP',rssi_dbm:'RSSI (dBm)',data_mode:'Adatforrás / Input',data_ready:'Adatkapcsolat / Input ready',"
        "mqtt_connected:'MQTT',sd_ok:'SD',time_synced:'NTP',packet_age_seconds:'Csomag kora (s) / Packet age (s)',firmware:'Firmware',uptime_seconds:'Üzemidő (s) / Uptime (s)',free_heap_kib:'Szabad memória (KiB) / Free memory (KiB)',free_internal_kib:'Szabad belső RAM (KiB) / Free internal RAM (KiB)',largest_dma_block_bytes:'Legnagyobb DMA-blokk (bájt) / Largest DMA block (bytes)'};"
        "async function refresh(){try{let r=await fetch('/api/diagnostics',{cache:'no-store'});if(!r.ok)throw Error(r.status);let data=await r.json(),table=document.getElementById('values');table.replaceChildren();"
        "for(let [k,v] of Object.entries(data)){let row=table.insertRow();row.insertCell().textContent=names[k]||k;row.insertCell().textContent=v===null?'—':v;}}catch(e){document.getElementById('values').textContent='Kapcsolati hiba / Connection failed';}}"
        "refresh();setInterval(refresh,3000);</script></html>");
}

/* mDNS és HTTP szerver egyszeri inicializálása, majd a végpontok regisztrálása. */
#include "app_template_web.inc"
#include "app_settings_web.inc"
esp_err_t app_web_start(const app_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_server) {
        return ESP_ERR_INVALID_STATE;
    }

    s_config = *cfg;
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
    http_cfg.max_uri_handlers = 24;
    http_cfg.stack_size = 12288;
    http_cfg.recv_wait_timeout = 5;

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

    if ((err = app_http_register(s_server)) != ESP_OK) goto failed;
    if ((err = template_register(s_server)) != ESP_OK) goto failed;
    if ((err = settings_register(s_server)) != ESP_OK) goto failed;
    const httpd_uri_t diagnostic_routes[]={
        {.uri="/diagnostics",.method=HTTP_GET,.handler=diagnostics_page},
        {.uri="/api/diagnostics",.method=HTTP_GET,.handler=diagnostics_get}};
    for(unsigned i=0;i<2;i++)if((err=httpd_register_uri_handler(s_server,&diagnostic_routes[i]))!=ESP_OK)goto failed;
    app_state_set_http_ready(cfg->data_recept_mode == APP_DATA_HTTP);

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

