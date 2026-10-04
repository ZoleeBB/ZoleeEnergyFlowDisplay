#include "app_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_events.h"
#include "app_state.h"

static const char *TAG="ota_validation";
static bool s_started;
static void task(void *arg){
    (void)arg;
    const esp_partition_t *running=esp_ota_get_running_partition();esp_ota_img_states_t state;
    if(!running||esp_ota_get_state_partition(running,&state)!=ESP_OK||state!=ESP_OTA_IMG_PENDING_VERIFY){vTaskDelete(NULL);return;}
    const TickType_t start=xTaskGetTickCount(),timeout=pdMS_TO_TICKS(180000);
    while((xTaskGetTickCount()-start)<timeout){
        app_runtime_state_t rt;app_state_get_runtime(&rt);
        if(rt.sd_ok&&rt.config_ok&&rt.wifi_connected&&rt.time_synced&&rt.mqtt_connected){
            esp_err_t e=esp_ota_mark_app_valid_cancel_rollback();
            if(e==ESP_OK){app_events_add(APP_EVENT_INFO,"OTA firmware ellenőrzése sikeres; verzió véglegesítve.");ESP_LOGI(TAG,"OTA image marked valid");vTaskDelete(NULL);return;}
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    app_events_add(APP_EVENT_ERROR,"OTA ellenőrzési időtúllépés; visszaállás az előző firmware-re.");
    vTaskDelay(pdMS_TO_TICKS(1000));esp_ota_mark_app_invalid_rollback_and_reboot();vTaskDelete(NULL);
}
void app_ota_start_validation_task(void){if(s_started)return;s_started=true;xTaskCreate(task,"ota_validate",4096,NULL,7,NULL);}
