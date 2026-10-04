#include "app_storage.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_state.h"
#include "app_events.h"

static const char *TAG="storage";
static volatile bool s_ok;
static bool s_recovery_task_started;

esp_err_t app_storage_boot_mount(void){
    esp_err_t err=bsp_sdcard_mount(); s_ok=err==ESP_OK; app_state_set_sd_ok(s_ok);
    if(s_ok){ESP_LOGI(TAG,"SD mounted at %s",APP_SD_MOUNT_POINT);app_events_add(APP_EVENT_INFO,"SD-kártya csatlakoztatva.");}
    else{ESP_LOGE(TAG,"SD mount failed: %s",esp_err_to_name(err));app_events_add(APP_EVENT_ERROR,"SD-kártya csatlakoztatási hiba: %s",esp_err_to_name(err));}
    return err;
}
void app_storage_mark_fault(const char *reason){ if(s_ok){s_ok=false;app_state_set_sd_ok(false);app_events_add(APP_EVENT_ERROR,"SD-kártya hiba: %s",reason?reason:"ismeretlen");}}
bool app_storage_is_ok(void){return s_ok;}
static void recovery_task(void *arg){(void)arg;while(true){if(!s_ok){ESP_LOGW(TAG,"Trying SD recovery...");(void)bsp_sdcard_unmount();vTaskDelay(pdMS_TO_TICKS(150));esp_err_t err=bsp_sdcard_mount();if(err==ESP_OK){s_ok=true;app_state_set_sd_ok(true);app_events_add(APP_EVENT_INFO,"SD-kártya kapcsolat helyreállt.");ESP_LOGI(TAG,"SD recovered");}}vTaskDelay(pdMS_TO_TICKS(60000));}}
void app_storage_start_recovery_task(void){if(s_recovery_task_started)return;s_recovery_task_started=true;xTaskCreate(recovery_task,"sd_recovery",4096,NULL,4,NULL);}
