/*
 * OTA firmware rollback-életciklus.
 *
 * Új OTA image csak akkor válik véglegessé, ha 3 percen belül teljesül:
 * SD + config + Wi-Fi + NTP + MQTT broker kapcsolat.
 * Érvényes MQTT payload nem feltétel.
 */
#include "app_ota.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_events.h"
#include "app_state.h"

static const char *TAG = "ota_validation";
static bool s_started;

/* A függőben lévő OTA image érvényesítési feltételeit figyelő háttértask. */
static void validation_task(void *arg)
{
    (void)arg;

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    /*
     * Normál bootnál nincs mit validálni, ezért a task azonnal megszűnik.
     */
    if (!running ||
        esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        vTaskDelete(NULL);
        return;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(180000);

    while ((xTaskGetTickCount() - start) < timeout) {
        app_runtime_state_t runtime;
        app_state_get_runtime(&runtime);

        if (runtime.sd_ok &&
            runtime.config_ok &&
            runtime.wifi_connected &&
            runtime.time_synced &&
            runtime.mqtt_connected) {

            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                app_events_add(
                    APP_EVENT_INFO,
                    "OTA firmware ellenőrzése sikeres; verzió véglegesítve.");
                ESP_LOGI(TAG, "OTA image marked valid");
                vTaskDelete(NULL);
                return;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /*
     * A feltételek nem teljesültek időben:
     * az ESP-IDF érvénytelennek jelöli az image-et és visszaindítja az előzőt.
     */
    app_events_add(
        APP_EVENT_ERROR,
        "OTA ellenőrzési időtúllépés; visszaállás az előző firmware-re.");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_ota_mark_app_invalid_rollback_and_reboot();
    vTaskDelete(NULL);
}

/* Az OTA validációs task egyszeri indítása. */
void app_ota_start_validation_task(void)
{
    if (s_started) {
        return;
    }

    s_started = true;
    xTaskCreate(
        validation_task,
        "ota_validate",
        4096,
        NULL,
        7,
        NULL);
}
