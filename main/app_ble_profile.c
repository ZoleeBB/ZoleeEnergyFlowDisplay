/* Az NVS és az újraindítás külön taskon fut, nem a BLE/LVGL eseménysorban. */
#include "app_ble_profile.h"
#include "app_ota.h"
#include "app_stats.h"
#include "app_events.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "esp_system.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_mutex;
static uint8_t s_profile[APP_BLE_PROFILE_SIZE];
static uint8_t s_applied_profile[APP_BLE_PROFILE_SIZE];
static bool s_have_profile;
static atomic_bool s_ready, s_boot_complete;
static char s_pin[7];
static bool s_pin_changed;

bool app_ble_profile_pin_changed(void)
{
    return s_pin_changed;
}

esp_err_t app_ble_profile_confirm_pin(void)
{
    if (!s_pin_changed)
        return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_profile", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    err = nvs_set_str(handle, "pin", s_pin);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t save_profile(const uint8_t *data)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_profile", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    err = nvs_set_blob(handle, "slave", data, APP_BLE_PROFILE_SIZE);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void profile_task(void *arg)
{
    (void)arg;
    uint8_t data[APP_BLE_PROFILE_SIZE];
    bool restart = false;
    for (;;)
    {
        if (xQueueReceive(s_queue, data, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            bool changed = !s_have_profile || memcmp(data, s_profile, sizeof(data));
            xSemaphoreGive(s_mutex);
            if (!changed || save_profile(data) == ESP_OK)
            {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                memcpy(s_profile, data, sizeof(data));
                s_have_profile = true;
                xSemaphoreGive(s_mutex);
                atomic_store(&s_ready, true);
                if (changed)
                {
                    restart = atomic_load(&s_boot_complete);
                    app_events_add(APP_EVENT_INFO,
                                   "Master Wi-Fi/automatikus frissítés profilja átvéve.");
                }
            }
            else
            {
                app_events_add(APP_EVENT_ERROR, "A védett masterprofil NVS-mentése sikertelen.");
            }
            memset(data, 0, sizeof(data));
        }
        if (atomic_load(&s_boot_complete))
        {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            restart = restart ||
                      (s_have_profile && memcmp(s_applied_profile, s_profile, sizeof(s_profile)));
            xSemaphoreGive(s_mutex);
        }
        if (restart && app_ota_try_begin_operation())
        {
            if (app_stats_save_now() == ESP_OK)
            {
                app_events_add(APP_EVENT_INFO, "Masterbeállítás változott; mentés és újraindítás.");
                vTaskDelay(pdMS_TO_TICKS(300));
                esp_restart();
            }
            app_ota_end_operation();
        }
    }
}

esp_err_t app_ble_profile_init(app_config_t *cfg)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;
    if (!cfg->data_seed_enabled && cfg->data_recept_mode != APP_DATA_SLAVE)
        return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_profile", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    if (cfg->data_seed_enabled)
    {
        char previous[7] = {0};
        size_t length = sizeof(previous);
        if (nvs_get_str(handle, "pin", previous, &length) != ESP_OK)
            previous[0] = 0;
        if (!cfg->data_seed_pin[0])
        {
            if (strlen(previous) == 6 && strspn(previous, "0123456789") == 6)
                memcpy(cfg->data_seed_pin, previous, sizeof(previous));
            else
            {
                uint32_t number;
                do
                {
                    number = esp_random();
                } while (number >= UINT32_MAX - UINT32_MAX % 1000000U);
                snprintf(cfg->data_seed_pin,
                         sizeof(cfg->data_seed_pin),
                         "%06lu",
                         (unsigned long)(number % 1000000U));
            }
        }
        memcpy(s_pin, cfg->data_seed_pin, sizeof(s_pin));
        s_pin_changed = strcmp(previous, s_pin) != 0;
    }
    if (cfg->data_recept_mode == APP_DATA_SLAVE)
    {
        size_t length = sizeof(s_profile);
        if (nvs_get_blob(handle, "slave", s_profile, &length) == ESP_OK)
            s_have_profile = app_ble_profile_decode(s_profile, length, cfg);
    }
    nvs_close(handle);
    if (cfg->data_recept_mode != APP_DATA_SLAVE)
        return ESP_OK;
    s_mutex = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(1, APP_BLE_PROFILE_SIZE);
    if (!s_mutex || !s_queue)
        return ESP_ERR_NO_MEM;
    return xTaskCreate(profile_task, "ble_profile", 4096, NULL, 3, NULL) == pdPASS ? ESP_OK
                                                                                   : ESP_ERR_NO_MEM;
}

bool app_ble_profile_submit(const uint8_t data[APP_BLE_PROFILE_SIZE])
{
    app_config_t cfg = {0};
    if (!s_queue || !app_ble_profile_decode(data, APP_BLE_PROFILE_SIZE, &cfg))
        return false;
    memset(&cfg, 0, sizeof(cfg));
    atomic_store(&s_ready, false);
    return xQueueOverwrite(s_queue, data) == pdPASS;
}

bool app_ble_profile_ready(void)
{
    return atomic_load(&s_ready);
}

void app_ble_profile_apply(app_config_t *cfg)
{
    if (!s_mutex || !cfg || cfg->data_recept_mode != APP_DATA_SLAVE)
        return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_have_profile)
    {
        (void)app_ble_profile_decode(s_profile, sizeof(s_profile), cfg);
        memcpy(s_applied_profile, s_profile, sizeof(s_profile));
    }
    xSemaphoreGive(s_mutex);
}

void app_ble_profile_boot_complete(void)
{
    atomic_store(&s_boot_complete, true);
}
