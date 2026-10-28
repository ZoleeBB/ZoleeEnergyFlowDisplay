/*
 * Rendszerdiagnosztikai pillanatkép készítése a kijelző és a webfelület számára.
 *
 * Az erőforrás-mérés külön kezeli a szimulátort és a fizikai ESP32-P4 panelt.
 * A CPU-terhelést két mintavétel közötti üresjárati időből számítjuk.
 */
#include "app_diagnostics.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

#ifndef APP_TEMPLATE_DESKTOP
#include "app_wifi.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "app_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#endif

#ifndef APP_TEMPLATE_DESKTOP
/* A mintavételt atomikus jelző, a legutóbbi eredményt rövid kritikus szakasz védi. */
static atomic_flag s_cpu_busy = ATOMIC_FLAG_INIT;
static portMUX_TYPE s_cpu_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_cpu_time;
static uint64_t s_idle[2];
static unsigned s_cpu[2];
static bool s_cpu_valid;

/* A magonkénti értékeket legfeljebb két másodpercenként frissítjük. */
static void cpu_capture(app_diagnostics_t *out, int64_t now)
{
    if (!atomic_flag_test_and_set(&s_cpu_busy))
    {
        if (!s_cpu_time || now - s_cpu_time >= 2000000LL)
        {
            unsigned load[2] = {0};
            uint64_t idle[2];

            for (int core = 0; core < 2; core++)
            {
                TaskStatus_t info;
                vTaskGetInfo(xTaskGetIdleTaskHandleForCore(core), &info, pdFALSE, eInvalid);

                idle[core] = info.ulRunTimeCounter;
                if (s_cpu_time)
                {
                    uint64_t elapsed = now - s_cpu_time, delta = idle[core] - s_idle[core];
                    load[core] = app_diagnostics_cpu_percent(delta, elapsed);
                }
            }

            portENTER_CRITICAL(&s_cpu_lock);
            for (int core = 0; core < 2; core++)
            {
                s_idle[core] = idle[core];
                s_cpu[core] = load[core];
            }
            s_cpu_valid = s_cpu_time != 0;
            s_cpu_time = now;
            portEXIT_CRITICAL(&s_cpu_lock);
        }
        atomic_flag_clear(&s_cpu_busy);
    }

    portENTER_CRITICAL(&s_cpu_lock);
    out->cpu_valid = s_cpu_valid;
    memcpy(out->cpu_percent, s_cpu, sizeof(s_cpu));
    portEXIT_CRITICAL(&s_cpu_lock);
}
#endif

/* A teljes pillanatképet nullázott struktúrába gyűjtjük össze. */
void app_diagnostics_capture(app_diagnostics_t *out)
{
    memset(out, 0, sizeof(*out));
    app_state_get_runtime(&out->runtime);

    int64_t now = esp_timer_get_time();
    out->uptime_seconds = now / 1000000;
    out->packet_age_seconds = out->runtime.has_valid_sample
                                ? (now - out->runtime.last_valid_packet_monotonic_us) / 1000000
                                : -1;
    snprintf(out->ip, sizeof(out->ip), "-");

#ifdef APP_TEMPLATE_DESKTOP
    snprintf(out->firmware, sizeof(out->firmware), "Desktop simulator");
#else
    snprintf(out->firmware, sizeof(out->firmware), "%.32s", esp_app_get_description()->version);

    cpu_capture(out, now);

    out->sd_space_valid = app_storage_space(&out->sd_total_bytes, &out->sd_free_bytes);
    out->total_internal_kib =
        heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024;
    out->total_psram_kib = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024;
    out->free_psram_kib = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024;
    out->free_heap_kib = esp_get_free_heap_size() / 1024;
    out->free_internal_kib = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024;
    out->largest_dma_block_bytes =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    out->min_free_internal_kib =
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024;

    if (out->runtime.wifi_connected)
    {
        (void)app_wifi_get_ip(out->ip, sizeof(out->ip));
        (void)app_wifi_get_rssi(&out->rssi);
    }
#endif
}
