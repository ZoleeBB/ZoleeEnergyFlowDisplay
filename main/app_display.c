/* A Waveshare BSP 2.0.3 a MIPI rajzpuffert belső RAM-ra rögzíti.
 * Az 50 soros RGB565 puffer ezen a panelen 80000 bájt: ez nem fér el
 * a kis belső heapben. A regisztrációt PSRAM-ra irányítjuk, a managed
 * komponensek módosítása nélkül. A többi beállítás változatlan másolat.
 */
#include "esp_lv_adapter_display.h"
#include "esp_log.h"

lv_display_t *__real_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config);

lv_display_t *__wrap_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config)
{
    if (!config || config->profile.interface != ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI) {
        return __real_esp_lv_adapter_register_display(config);
    }
    esp_lv_adapter_display_config_t psram_config = *config;
    psram_config.profile.use_psram = true;
    ESP_LOGI("app_display", "MIPI LVGL draw buffers use PSRAM (rows=%u)",
             (unsigned)psram_config.profile.buffer_height);
    return __real_esp_lv_adapter_register_display(&psram_config);
}
