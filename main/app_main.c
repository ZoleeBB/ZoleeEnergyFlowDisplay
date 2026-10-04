#include <stdio.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "app_events.h"
#include "app_mqtt.h"
#include "app_ota.h"
#include "app_state.h"
#include "app_stats.h"
#include "app_storage.h"
#include "app_time.h"
#include "app_ui.h"
#include "app_web.h"
#include "app_wifi.h"

#define CONFIG_PATH APP_SD_MOUNT_POINT "/config.json"
static const char *TAG="app";
static app_config_t s_config;

static void fatal_forever(const char *title,const char *detail){ESP_LOGE(TAG,"%s: %s",title,detail?detail:"");app_ui_show_fatal(title,detail);while(true)vTaskDelay(pdMS_TO_TICKS(1000));}
static void init_nvs(void){esp_err_t e=nvs_flash_init();if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());e=nvs_flash_init();}ESP_ERROR_CHECK(e);}

void app_main(void){
    init_nvs();app_state_init();app_events_init();
    if(app_ui_init()!=ESP_OK){ESP_LOGE(TAG,"Display initialization failed");while(true)vTaskDelay(pdMS_TO_TICKS(1000));}
    app_ota_start_validation_task();

    app_ui_show_boot(APP_BOOT_SD,NULL);
    esp_err_t e=app_storage_boot_mount();
    if(e!=ESP_OK)fatal_forever("SD-kártya hiba","A rendszer nem tudja elérni az SD-kártyát. Ellenőrizd a kártyát, majd indítsd újra az eszközt.");

    app_ui_show_boot(APP_BOOT_CONFIG,NULL);
    char err[256];e=app_config_load_file(CONFIG_PATH,&s_config,err,sizeof(err));
    if(e!=ESP_OK)fatal_forever("Konfigurációs hiba",err);
    app_state_set_config_ok(true);app_ui_set_config(&s_config);app_events_add(APP_EVENT_INFO,"Konfiguráció betöltve.");

    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    app_ui_show_boot(APP_BOOT_WIFI,NULL);
    e=app_wifi_start(&s_config);if(e!=ESP_OK)fatal_forever("Wi-Fi inicializálási hiba",esp_err_to_name(e));
    while(!app_wifi_wait_connected(pdMS_TO_TICKS(1000))){}

    app_ui_show_boot(APP_BOOT_NTP,NULL);
    e=app_time_init(&s_config);if(e!=ESP_OK)fatal_forever("NTP inicializálási hiba",esp_err_to_name(e));
    while((e=app_time_sync_once())!=ESP_OK){
        char msg[160];snprintf(msg,sizeof(msg),"Az időszinkron nem sikerült (%s). Újrapróbálkozás %lu másodperc múlva.",esp_err_to_name(e),(unsigned long)s_config.time.startup_retry_seconds);
        app_ui_show_boot(APP_BOOT_NTP,msg);vTaskDelay(pdMS_TO_TICKS(s_config.time.startup_retry_seconds*1000U));
    }
    app_events_add(APP_EVENT_INFO,"NTP időszinkron sikeres.");

    e=app_stats_init(&s_config);
    if(e!=ESP_OK)fatal_forever("SD-kártya hiba","A statisztikai tárhely nem inicializálható. Ellenőrizd az SD-kártyát, majd indítsd újra az eszközt.");
    app_storage_start_recovery_task();app_stats_start_task();app_time_start_background_task();

    e=app_web_start(&s_config);if(e!=ESP_OK){app_events_add(APP_EVENT_ERROR,"Web/OTA felület nem indult el: %s",esp_err_to_name(e));ESP_LOGE(TAG,"Web server start failed: %s",esp_err_to_name(e));}

    app_ui_show_boot(APP_BOOT_MQTT,NULL);
    e=app_mqtt_start(&s_config);if(e!=ESP_OK)fatal_forever("MQTT inicializálási hiba",esp_err_to_name(e));
    while(!app_mqtt_wait_connected(pdMS_TO_TICKS(1000))){
        app_runtime_state_t rt;app_state_get_runtime(&rt);
        if(!rt.wifi_connected)app_ui_show_boot(APP_BOOT_WIFI,"A Wi-Fi kapcsolat helyreállítására várunk…");else app_ui_show_boot(APP_BOOT_MQTT,"Kapcsolódás az MQTT brokerhez…");
    }

    app_ui_show_boot(APP_BOOT_WAIT_DATA,NULL);
    TickType_t start=xTaskGetTickCount();
    while(true){app_runtime_state_t rt;app_state_get_runtime(&rt);if(rt.has_valid_sample)break;if((xTaskGetTickCount()-start)>=pdMS_TO_TICKS(s_config.mqtt.data_timeout_seconds*1000U))break;vTaskDelay(pdMS_TO_TICKS(200));}
    app_ui_start_runtime();app_events_add(APP_EVENT_INFO,"Rendszerindítás befejeződött.");
    while(true)vTaskDelay(pdMS_TO_TICKS(60000));
}
