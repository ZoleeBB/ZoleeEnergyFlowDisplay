#include "app_web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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

static const char *TAG="web";
static app_ota_config_t s_auth_cfg;
static httpd_handle_t s_server;
static char s_expected_auth[256];
static char s_project_title[APP_PROJECT_TITLE_MAX];
static bool s_hu=true;

static const char *reset_text(esp_reset_reason_t r){switch(r){case ESP_RST_POWERON:return"Power-on";case ESP_RST_EXT:return"External reset";case ESP_RST_SW:return"Software restart";case ESP_RST_PANIC:return"Panic";case ESP_RST_INT_WDT:return"Interrupt watchdog";case ESP_RST_TASK_WDT:return"Task watchdog";case ESP_RST_WDT:return"Watchdog";case ESP_RST_DEEPSLEEP:return"Deep sleep wake";case ESP_RST_BROWNOUT:return"Brownout";default:return"Unknown";}}
static bool auth_enabled(void){return s_auth_cfg.username[0]!=0;}
static void build_auth(void){s_expected_auth[0]=0;if(!auth_enabled())return;char plain[APP_OTA_USER_MAX+APP_OTA_PASSWORD_MAX+2];snprintf(plain,sizeof(plain),"%s:%s",s_auth_cfg.username,s_auth_cfg.password);unsigned char enc[192];size_t n=0;if(mbedtls_base64_encode(enc,sizeof(enc)-1,&n,(const unsigned char*)plain,strlen(plain))==0){enc[n]=0;snprintf(s_expected_auth,sizeof(s_expected_auth),"Basic %s",enc);}}
static bool authorize(httpd_req_t *req){if(!auth_enabled())return true;size_t len=httpd_req_get_hdr_value_len(req,"Authorization");char v[256];if(!len||len>=sizeof(v)||httpd_req_get_hdr_value_str(req,"Authorization",v,sizeof(v))!=ESP_OK||strcmp(v,s_expected_auth)){httpd_resp_set_status(req,"401 Unauthorized");httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Zolee Energy Flow Display\"");httpd_resp_sendstr(req,"Authentication required");return false;}return true;}

static const char *event_level_text(app_event_level_t level){
    switch(level){
        case APP_EVENT_ERROR:return s_hu?"Hiba":"Error";
        case APP_EVENT_WARNING:return s_hu?"Figyelmeztetés":"Warning";
        default:return s_hu?"Információ":"Information";
    }
}

static esp_err_t sendf(httpd_req_t *req,const char *fmt,...){
    char buffer[768];
    va_list ap;
    va_start(ap,fmt);
    int len=vsnprintf(buffer,sizeof(buffer),fmt,ap);
    va_end(ap);
    if(len<0)return ESP_FAIL;
    if(len>=(int)sizeof(buffer))len=(int)sizeof(buffer)-1;
    return httpd_resp_send_chunk(req,buffer,len);
}

static esp_err_t root_handler(httpd_req_t *req){
    if(!authorize(req))return ESP_OK;

    app_runtime_state_t rt;
    app_state_get_runtime(&rt);

    int rssi=0;
    char ip[32]="-";
    if(rt.wifi_connected){
        (void)app_wifi_get_rssi(&rssi);
        (void)app_wifi_get_ip(ip,sizeof(ip));
    }

    char packet[48]="-";
    if(rt.last_valid_packet_epoch_s>0){
        time_t t=(time_t)rt.last_valid_packet_epoch_s;
        struct tm tmv;
        localtime_r(&t,&tmv);
        strftime(packet,sizeof(packet),"%Y-%m-%d %H:%M:%S",&tmv);
    }

    int64_t up=esp_timer_get_time()/1000000LL;
    int days=(int)(up/86400);
    up%=86400;
    int hours=(int)(up/3600);
    up%=3600;
    int mins=(int)(up/60);
    int secs=(int)(up%60);

    const esp_app_desc_t *desc=esp_app_get_description();
    app_event_entry_t events[APP_EVENT_LOG_CAPACITY];
    size_t event_count=app_events_snapshot(events,APP_EVENT_LOG_CAPACITY);

    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_send_chunk(req,
        "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:system-ui;margin:28px;max-width:900px;background:#f7f8fa;color:#17202a}.card{background:#fff;border-radius:14px;padding:18px 22px;margin:14px 0;box-shadow:0 2px 12px #0001}table{border-collapse:collapse;width:100%}td{padding:7px;border-bottom:1px solid #eee}button,a{padding:10px 15px;border:0;border-radius:9px;background:#176b45;color:white;text-decoration:none;cursor:pointer;margin-right:8px}.muted{color:#6e7c83}.error{color:#b42318}.warning{color:#b66b00}</style></head><body>",
        HTTPD_RESP_USE_STRLEN);

    sendf(req,"<h1>%s</h1>",s_project_title[0]?s_project_title:"Zolee Energy Flow Display");
    httpd_resp_send_chunk(req,s_hu?"<div class='card'><h2>Eszközinformáció</h2><table>":"<div class='card'><h2>Device information</h2><table>",HTTPD_RESP_USE_STRLEN);
    sendf(req,s_hu?"<tr><td>Firmware</td><td>%s</td></tr>":"<tr><td>Firmware</td><td>%s</td></tr>",desc?desc->version:"-");
    sendf(req,s_hu?"<tr><td>Üzemidő</td><td>%d nap %02d:%02d:%02d</td></tr>":"<tr><td>Uptime</td><td>%d days %02d:%02d:%02d</td></tr>",days,hours,mins,secs);
    sendf(req,"<tr><td>IP</td><td>%s</td></tr>",ip);
    sendf(req,"<tr><td>Wi-Fi RSSI</td><td>%d dBm</td></tr>",rt.wifi_connected?rssi:0);
    sendf(req,s_hu?"<tr><td>MQTT kapcsolat</td><td>%s</td></tr>":"<tr><td>MQTT connection</td><td>%s</td></tr>",rt.mqtt_connected?(s_hu?"Kapcsolódva":"Connected"):(s_hu?"Nincs kapcsolat":"Disconnected"));
    sendf(req,s_hu?"<tr><td>Utolsó MQTT csomag</td><td>%s</td></tr>":"<tr><td>Last MQTT packet</td><td>%s</td></tr>",packet);
    sendf(req,s_hu?"<tr><td>SD-kártya</td><td>%s</td></tr>":"<tr><td>SD card</td><td>%s</td></tr>",app_storage_is_ok()?(s_hu?"Rendben":"OK"):(s_hu?"Hiba":"Error"));
    sendf(req,s_hu?"<tr><td>Reset oka</td><td>%s</td></tr>":"<tr><td>Reset reason</td><td>%s</td></tr>",reset_text(esp_reset_reason()));
    httpd_resp_send_chunk(req,"</table></div>",HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req,s_hu?"<div class='card'><h2>Legutóbbi események</h2>":"<div class='card'><h2>Recent events</h2>",HTTPD_RESP_USE_STRLEN);
    if(event_count==0){
        httpd_resp_send_chunk(req,s_hu?"<p class='muted'>Még nincs esemény.</p>":"<p class='muted'>No events yet.</p>",HTTPD_RESP_USE_STRLEN);
    }else{
        httpd_resp_send_chunk(req,"<table>",HTTPD_RESP_USE_STRLEN);
        for(size_t i=0;i<event_count;i++){
            char when[32]="-";
            if(events[i].epoch_s>0){
                time_t t=(time_t)events[i].epoch_s;
                struct tm tmv;
                localtime_r(&t,&tmv);
                strftime(when,sizeof(when),"%Y-%m-%d %H:%M:%S",&tmv);
            }
            const char *cls=events[i].level==APP_EVENT_ERROR?"error":events[i].level==APP_EVENT_WARNING?"warning":"";
            sendf(req,"<tr><td class='muted'>%s</td><td class='%s'>%s</td><td>%s</td></tr>",when,cls,event_level_text(events[i].level),events[i].text);
        }
        httpd_resp_send_chunk(req,"</table>",HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(req,"</div>",HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req,
        s_hu?
        "<div class='card'><a href='/update'>Firmware frissítés</a><form method='post' action='/reboot' style='display:inline' onsubmit=\"return confirm('Biztosan újraindítod az eszközt?')\"><button type='submit'>Eszköz újraindítása</button></form></div>":
        "<div class='card'><a href='/update'>Firmware update</a><form method='post' action='/reboot' style='display:inline' onsubmit=\"return confirm('Restart the device?')\"><button type='submit'>Restart device</button></form></div>",
        HTTPD_RESP_USE_STRLEN);

    return httpd_resp_send_chunk(req,"</body></html>",HTTPD_RESP_USE_STRLEN)==ESP_OK?
        httpd_resp_send_chunk(req,NULL,0):ESP_FAIL;
}
static esp_err_t update_get(httpd_req_t *req){if(!authorize(req))return ESP_OK;static const char page[]="<!doctype html><html><head><meta charset='utf-8'></head><body><h1>Firmware frissítés</h1><input id='f' type='file' accept='.bin'><button onclick='go()'>Frissítés</button><p id='s'></p><script>async function go(){let f=document.getElementById('f').files[0];if(!f)return;let r=await fetch('/update',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f});document.getElementById('s').textContent=await r.text();}</script></body></html>";httpd_resp_set_type(req,"text/html; charset=utf-8");return httpd_resp_send(req,page,HTTPD_RESP_USE_STRLEN);}
static void reboot_task(void *arg){(void)arg;vTaskDelay(pdMS_TO_TICKS(700));esp_restart();}
static esp_err_t reboot_post(httpd_req_t *req){if(!authorize(req))return ESP_OK;httpd_resp_sendstr(req,"Restarting");xTaskCreate(reboot_task,"reboot",2048,NULL,8,NULL);return ESP_OK;}
static esp_err_t ota_post(httpd_req_t *req){
    if (!authorize(req)) {
        return ESP_OK;
    }
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty image");
        return ESP_OK;
    }
    const esp_partition_t *target=esp_ota_get_next_update_partition(NULL);if(!target||(size_t)req->content_len>target->size){httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Image too large");return ESP_OK;}
    esp_ota_handle_t ota=0;esp_err_t e=esp_ota_begin(target,(size_t)req->content_len,&ota);if(e!=ESP_OK){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"OTA begin failed");return ESP_OK;}
    char *buf=malloc(4096);if(!buf){esp_ota_abort(ota);httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");return ESP_OK;}
    int left=req->content_len;while(left>0){int n=httpd_req_recv(req,buf,left>4096?4096:left);if(n==HTTPD_SOCK_ERR_TIMEOUT)continue;if(n<=0){free(buf);esp_ota_abort(ota);httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Upload interrupted");return ESP_OK;}e=esp_ota_write(ota,buf,(size_t)n);if(e!=ESP_OK){free(buf);esp_ota_abort(ota);httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Write failed");return ESP_OK;}left-=n;}free(buf);
    e=esp_ota_end(ota);if(e==ESP_OK)e=esp_ota_set_boot_partition(target);if(e!=ESP_OK){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Validation failed");return ESP_OK;}
    httpd_resp_sendstr(req,"Firmware feltöltve. Az eszköz újraindul.");xTaskCreate(reboot_task,"ota_reboot",2048,NULL,8,NULL);return ESP_OK;
}
esp_err_t app_web_start(const app_config_t *cfg){
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    s_auth_cfg = cfg->ota;
    snprintf(s_project_title,sizeof(s_project_title),"%s",cfg->project.title);
    s_hu=strcmp(cfg->project.language,"en")!=0;
    build_auth();
    esp_err_t e=mdns_init();if(e==ESP_OK){mdns_hostname_set(APP_DEVICE_HOSTNAME);mdns_instance_name_set("Zolee Energy Flow Display");mdns_service_add(NULL,"_http","_tcp",80,NULL,0);}else ESP_LOGW(TAG,"mDNS init failed: %s",esp_err_to_name(e));
    httpd_config_t hc=HTTPD_DEFAULT_CONFIG();hc.max_uri_handlers=8;hc.stack_size=8192;e=httpd_start(&s_server,&hc);if(e!=ESP_OK)return e;
    httpd_uri_t r={.uri="/",.method=HTTP_GET,.handler=root_handler};httpd_uri_t ug={.uri="/update",.method=HTTP_GET,.handler=update_get};httpd_uri_t up={.uri="/update",.method=HTTP_POST,.handler=ota_post};httpd_uri_t rb={.uri="/reboot",.method=HTTP_POST,.handler=reboot_post};
    if ((e = httpd_register_uri_handler(s_server, &r)) != ESP_OK) return e;
    if ((e = httpd_register_uri_handler(s_server, &ug)) != ESP_OK) return e;
    if ((e = httpd_register_uri_handler(s_server, &up)) != ESP_OK) return e;
    if ((e = httpd_register_uri_handler(s_server, &rb)) != ESP_OK) return e;
    app_events_add(APP_EVENT_INFO,"Webes információs és OTA felület elindult.");return ESP_OK;
}
