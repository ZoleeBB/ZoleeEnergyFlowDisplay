#include "app_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_events.h"
#include "app_state.h"
#include "app_wifi.h"

static const char *TAG="time";
static app_time_config_t s_cfg;
static bool s_initialized,s_background_started;

static void build_fixed_tz(int off,char *out,size_t n){int sign=off>=0?-1:1,a=off>=0?off:-off,h=a/60,m=a%60;if(m==0)snprintf(out,n,"UTC%c%d",sign<0?'-':'+',h);else snprintf(out,n,"UTC%c%d:%02d",sign<0?'-':'+',h,m);}
static void apply_timezone(void){char tz[96];if(!s_cfg.auto_dst)build_fixed_tz(s_cfg.utc_offset_minutes,tz,sizeof(tz));else if(strcmp(s_cfg.timezone,"Europe/Budapest")==0)snprintf(tz,sizeof(tz),"CET-1CEST,M3.5.0/2,M10.5.0/3");else snprintf(tz,sizeof(tz),"%s",s_cfg.timezone);setenv("TZ",tz,1);tzset();ESP_LOGI(TAG,"Timezone applied: %s",tz);}
esp_err_t app_time_init(const app_config_t *cfg){if(!cfg)return ESP_ERR_INVALID_ARG;if(s_initialized)return ESP_ERR_INVALID_STATE;s_cfg=cfg->time;apply_timezone();esp_sntp_config_t c=ESP_NETIF_SNTP_DEFAULT_CONFIG(s_cfg.ntp_server);esp_err_t e=esp_netif_sntp_init(&c);if(e==ESP_OK)s_initialized=true;return e;}
esp_err_t app_time_sync_once(void){if(!s_initialized||!app_wifi_is_connected())return ESP_ERR_INVALID_STATE;esp_err_t e=esp_netif_sntp_start();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)ESP_LOGW(TAG,"SNTP start failed: %s",esp_err_to_name(e));e=esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));if(e==ESP_OK)app_state_set_time_synced(true);return e;}
static void task(void *arg){(void)arg;while(true){vTaskDelay(pdMS_TO_TICKS(s_cfg.resync_hours*60U*60U*1000U));while(true){if(!app_wifi_is_connected()){vTaskDelay(pdMS_TO_TICKS(s_cfg.background_retry_minutes*60U*1000U));continue;}esp_err_t e=app_time_sync_once();if(e==ESP_OK){app_events_add(APP_EVENT_INFO,"NTP időszinkron sikeres.");break;}app_events_add(APP_EVENT_WARNING,"NTP újraszinkron sikertelen; belső óra tovább fut.");vTaskDelay(pdMS_TO_TICKS(s_cfg.background_retry_minutes*60U*1000U));}}}
void app_time_start_background_task(void){if(s_background_started)return;s_background_started=true;xTaskCreate(task,"time_sync",4096,NULL,4,NULL);}
