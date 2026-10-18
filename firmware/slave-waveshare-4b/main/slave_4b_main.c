/* Dedikált 4B Slave: nincs SD, konfigurálható adatforrás vagy master indítás. */
#include "slave_4b_ui.h"
#include "app_boot_config.h"
#include "app_ble_profile.h"
#include "app_events.h"
#include "app_ingest.h"
#include "app_ota.h"
#include "app_stats.h"
#include "app_storage.h"
#include "app_time.h"
#include "app_wifi.h"
#include "bsp/esp-bsp.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

static app_config_t config;
static int last_brightness = -1;

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    static int64_t previous;
    app_energy_sample_t sample = {0};
    app_runtime_state_t runtime;
    app_state_get_runtime(&runtime);
    int64_t now = esp_timer_get_time();
    bool available = app_state_get_sample(&sample) && runtime.slave_connected &&
                     !app_state_data_timed_out(&runtime, now, config.mqtt.data_timeout_seconds);
    char clock[8] = "--:--";
    if (runtime.time_synced)
    {
        time_t epoch = time(NULL);
        struct tm local;
        localtime_r(&epoch, &local);
        strftime(clock, sizeof(clock), "%H:%M", &local);
    }
    float elapsed = previous ? (float)(now - previous) / 1000000.f : 0;
    previous = now;
    slave_4b_ui_update(&sample, available, &runtime, clock, elapsed);
    app_ble_pairing_status_t pairing;
    app_ble_pairing_status(&pairing);
    slave_4b_ui_pairing(&pairing);
    int brightness = sample.night_mode ? 25 : 70;
    if (brightness != last_brightness)
    {
        bsp_display_brightness_set(brightness);
        last_brightness = brightness;
    }
}

static void fatal(const char *detail)
{
    ESP_LOGE("slave_4b", "%s", detail);
    if (bsp_display_lock(5000))
    {
        slave_4b_ui_message(detail);
        bsp_display_unlock();
    }
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

void app_main(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
    app_state_init();
    app_events_init();
    ESP_ERROR_CHECK(app_ota_start_validation_task());
    bsp_display_cfg_t display = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL
    };
    display.lv_adapter_cfg.task_stack_size = 32768;
    display.lv_adapter_cfg.stack_in_psram = true;
    ESP_ERROR_CHECK(bsp_display_start_with_config(&display) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(bsp_display_lock(5000) ? ESP_OK : ESP_ERR_TIMEOUT);
    slave_4b_ui_create(lv_screen_active());
    bsp_display_unlock();
    bsp_display_backlight_on();

    char explanation[256] = {0};
    if (app_boot_config_fallback(&config, explanation, sizeof(explanation)) != ESP_OK)
        fatal(explanation[0] ? explanation : "Belső konfigurációs hiba");
    /* A hardver szerepe fordításkor rögzített, a tárolt profil nem írhatja felül. */
    config.data_recept_mode = APP_DATA_SLAVE;
    config.data_seed_enabled = false;
    config.cardless_slave = true;
    app_storage_set_cardless();
    ESP_ERROR_CHECK(app_ble_profile_init(&config));
    app_state_set_data_mode(APP_DATA_SLAVE);
    app_state_set_config_ok(true);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(app_time_init(&config));
    ESP_ERROR_CHECK(bsp_display_lock(5000) ? ESP_OK : ESP_ERR_TIMEOUT);
    ESP_ERROR_CHECK(lv_timer_create(refresh, 33, NULL) ? ESP_OK : ESP_ERR_NO_MEM);
    bsp_display_unlock();
    ESP_ERROR_CHECK(app_ble_seed_start(&config));

    app_runtime_state_t runtime;
    do
    {
        vTaskDelay(pdMS_TO_TICKS(250));
        app_state_get_runtime(&runtime);
    } while (!runtime.time_synced);
    app_ble_profile_apply(&config);
    if (config.wifi.ssid[0] && app_wifi_start(&config) != ESP_OK)
        app_events_add(APP_EVENT_WARNING, "Opcionális Slave Wi-Fi nem indult el.");
    /* A fő kijelző sorrendje: a RAM-cache dátumát már a master órája adja.
     * Addig a közös BLE kliens megőrzi és újrapróbálja az első mintát. */
    ESP_ERROR_CHECK(app_stats_init(&config));
    ESP_ERROR_CHECK(app_ingest_init(APP_DATA_SLAVE));
    ESP_ERROR_CHECK(app_stats_start_task());
    ESP_ERROR_CHECK(app_time_start_background_task());
    ESP_ERROR_CHECK(app_ota_start_auto_update(&config));
    app_ble_profile_boot_complete();
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(60000));
}
