/*
 * OTA firmware rollback-életciklus.
 *
 * Hálózati módban a Wi-Fi IP-címe és a sikeres időszinkron,
 * párosított Slave-nél a Mastertől átvett idő igazolja az indítást.
 */
#include "app_ota.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_events.h"
#include "app_state.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "ota_validation";
static bool s_started;

/* A visszaállás oka túléli az újraindítást: az alkalmazás RAM-naplója nem. */
#define OTA_RESULT_NAMESPACE "ota_result"
#define OTA_RESULT_KEY "reason"

static void save_rollback_reason(const char *reason)
{
    nvs_handle_t handle;
    if (nvs_open(OTA_RESULT_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return;
    if (nvs_set_str(handle, OTA_RESULT_KEY, reason) == ESP_OK)
        nvs_commit(handle);
    nvs_close(handle);
}

void app_ota_report_boot_state(void)
{
    nvs_handle_t handle;
    char reason[224] = {0};
    if (nvs_open(OTA_RESULT_NAMESPACE, NVS_READONLY, &handle) == ESP_OK)
    {
        size_t length = sizeof(reason);
        if (nvs_get_str(handle, OTA_RESULT_KEY, reason, &length) != ESP_OK)
            reason[0] = 0;
        nvs_close(handle);
    }
    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    esp_app_desc_t description;
    if (invalid && esp_ota_get_partition_description(invalid, &description) == ESP_OK)
        app_events_add(APP_EVENT_WARNING,
                       "Az előző OTA (%.31s) nem véglegesült; a rendszer visszaállt: %.31s.",
                       description.version, esp_app_get_description()->version);
    if (reason[0])
    {
        app_events_add(APP_EVENT_WARNING, "Az OTA visszaállás oka: %.80s", reason);
        if (nvs_open(OTA_RESULT_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
        {
            if (nvs_erase_key(handle, OTA_RESULT_KEY) == ESP_OK)
                nvs_commit(handle);
            nvs_close(handle);
        }
    }
}

/* A függőben lévő OTA image érvényesítési feltételeit figyelő háttértask. */
static void validation_task(void *arg)
{
    (void)arg;

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    /*
     * Normál bootnál nincs mit validálni, ezért a task azonnal megszűnik.
     */
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY)
    {
        vTaskDelete(NULL);
        return;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(300000);
    /* Slave módban a Master is frissülhet és újraindulhat; amíg a
     * masterkapcsolat hiányzik, türelmesebben várunk, hogy a visszaállás ne a
     * Master újraindulása miatt történjen. */
    const TickType_t waiting_timeout = pdMS_TO_TICKS(1200000);

    for (;;)
    {
        app_runtime_state_t runtime;
        app_state_get_runtime(&runtime);
        TickType_t limit = runtime.data_recept_mode == APP_DATA_SLAVE &&
                           !runtime.slave_connected ? waiting_timeout : timeout;
        if ((xTaskGetTickCount() - start) >= limit)
            break;

        if (app_state_ota_ready(&runtime))
        {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK)
            {
                app_events_add(APP_EVENT_INFO,
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
     * A hiányzó feltételeket NVS-be is kiírjuk, mert a RAM-napló most elvész.
     */
    app_runtime_state_t runtime;
    app_state_get_runtime(&runtime);
    const char *reason = "Az OTA-kép véglegesítése sikertelen.";
    if (runtime.data_recept_mode != APP_DATA_SLAVE && !runtime.wifi_connected)
        reason = "Az OTA-ellenőrzés határidejekor nincs Wi-Fi IP-cím.";
    else if (!runtime.time_synced)
        reason = "Az OTA-ellenőrzés határidejéig nem történt sikeres időszinkron.";
    save_rollback_reason(reason);
    app_events_add(APP_EVENT_ERROR, "%s", reason);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_ota_mark_app_invalid_rollback_and_reboot();
    vTaskDelete(NULL);
}

/* Az OTA validációs task egyszeri indítása. */
esp_err_t app_ota_start_validation_task(void)
{
    if (s_started)
        return ESP_OK;
    if (xTaskCreate(validation_task, "validation_task", 4096, NULL, 7, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    s_started = true;
    return ESP_OK;
}
