/* A Waveshare BSP MIPI profilját PSRAM-ra irányítjuk, a managed
 * komponensek módosítása nélkül. A teljes képkockás mód a panel három
 * meglévő pufferét használja. A PPA forgatást külön letiltjuk.
 */
#include "esp_lv_adapter_display.h"
#include "esp_log.h"

lv_display_t *__real_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config);

/* Registered before the BSP starts its worker: startup lock failure must not
 * prevent us from observing where the first render/flush stops. */
static void startup_display_event(lv_event_t *event)
{
    static unsigned reported;
    unsigned bit;
    const char *message;
    switch (lv_event_get_code(event))
    {
        case LV_EVENT_RENDER_START:
            bit = 1;
            message = "First LVGL render started";
            break;
        case LV_EVENT_FLUSH_START:
            bit = 2;
            message = "First LCD flush started";
            break;
        case LV_EVENT_FLUSH_FINISH:
            bit = 4;
            message = "First LCD flush callback returned";
            break;
        case LV_EVENT_REFR_READY:
            bit = 8;
            message = "First adapter refresh completed";
            break;
        default:
            return;
    }
    if (!(reported & bit))
    {
        reported |= bit;
        ESP_LOGI("app_display", "%s", message);
    }
}

lv_display_t *__wrap_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config)
{
    if (!config || config->profile.interface != ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI)
    {
        return __real_esp_lv_adapter_register_display(config);
    }
    esp_lv_adapter_display_config_t psram_config = *config;
    psram_config.profile.use_psram = true;
    /* Adapter 0.6.4 must also honor this flag on its rotation path. */
    psram_config.profile.enable_ppa_accel = false;
    ESP_LOGI("app_display",
             "MIPI LVGL draw buffers use PSRAM (rows=%u)",
             (unsigned)psram_config.profile.buffer_height);
    ESP_LOGI("app_display", "MIPI rotation uses CPU fallback (PPA disabled)");
    ESP_LOGI("app_display", "Complete-frame rotation; partial framebuffer repair is not used");
    ESP_LOGI("app_display", "LVGL rendering is synchronous (OS_NONE, one draw unit)");
    lv_display_t *display = __real_esp_lv_adapter_register_display(&psram_config);
    if (display)
    {
        lv_display_add_event_cb(display, startup_display_event, LV_EVENT_ALL, NULL);
    }
    return display;
}
