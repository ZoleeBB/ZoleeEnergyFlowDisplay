/*
 * microSD életciklus és hibautáni helyreállítás.
 *
 * A fájlrendszer és a le-/felcsatolás ugyanazt a mutexet használja,
 * de az élő mérések ettől függetlenül tovább feldolgozhatók.
 */
#include "app_storage.h"
#include <stdatomic.h>
#include "bsp/esp-bsp.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "app_events.h"
#include "app_state.h"

static atomic_bool s_ok;
static bool s_recovery_task_started;
static SemaphoreHandle_t s_io_mutex;

static portMUX_TYPE s_space_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_total_bytes, s_free_bytes;

/* A legutóbb mért tárhelyadatok rövid, szálbiztos lekérése. */
bool app_storage_space(uint64_t *total, uint64_t *free_bytes)
{
    portENTER_CRITICAL(&s_space_lock);
    *total = s_total_bytes;
    *free_bytes = s_free_bytes;
    portEXIT_CRITICAL(&s_space_lock);

    return app_storage_is_ok() && *total > 0 && *free_bytes <= *total;
}

/* Csak a háttértaskból, az SD-zár alatt, legfeljebb percenként. */
static void update_space(void)
{
    uint64_t total = 0, available = 0;
    if (app_storage_is_ok() && esp_vfs_fat_info(APP_SD_MOUNT_POINT, &total, &available) != ESP_OK)
        total = available = 0;

    portENTER_CRITICAL(&s_space_lock);
    s_total_bytes = total;
    s_free_bytes = available;
    portEXIT_CRITICAL(&s_space_lock);
}

/* Az SD-fájlműveletek és az unmount/mount nem futhatnak egymás mellett. */
esp_err_t app_storage_lock(void)
{
    if (!s_io_mutex)
        return ESP_ERR_INVALID_STATE;
    return xSemaphoreTake(s_io_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_FAIL;
}

void app_storage_unlock(void)
{
    xSemaphoreGive(s_io_mutex);
}

bool app_storage_try_lock(void)
{
    return s_io_mutex && xSemaphoreTake(s_io_mutex, 0) == pdTRUE;
}

/* Induláskori csatolás, eredményközlés a közös állapot és a napló felé. */
esp_err_t app_storage_boot_mount(void)
{
    if (!s_io_mutex)
        s_io_mutex = xSemaphoreCreateMutex();
    if (!s_io_mutex)
        return ESP_ERR_NO_MEM;

    esp_err_t error = bsp_sdcard_mount();
    atomic_store(&s_ok, error == ESP_OK);
    app_state_set_sd_ok(error == ESP_OK);
    app_events_add(error == ESP_OK ? APP_EVENT_INFO : APP_EVENT_ERROR,
                   error == ESP_OK ? "SD-kártya csatlakoztatva."
                                   : "SD-kártya csatlakoztatási hiba.");
    return error;
}

/* Egy hibát csak egyszer naplózunk, amíg a kártya ismét elérhetővé nem válik. */
void app_storage_mark_fault(const char *reason)
{
    if (!atomic_exchange(&s_ok, false))
        return;
    app_state_set_sd_ok(false);
    app_events_add(APP_EVENT_ERROR, "SD-kártya hiba: %s", reason ? reason : "UNKNOWN");
}

bool app_storage_is_ok(void)
{
    return atomic_load(&s_ok);
}

/*
 * Öt másodpercenként CMD13 állapotolvasás: nincs próbaírás vagy FatFs-cache függés.
 * Hibánál legkorábban 60 másodperc múlva csatoljuk újra az SD-kártyát.
 */
static void recovery_task(void *arg)
{
    (void)arg;
    int64_t next_recovery = 0, next_space = 0;

    while (true)
    {
        if (app_storage_lock() == ESP_OK)
        {
            int64_t now = esp_timer_get_time();
            if (app_storage_is_ok())
            {
                if (!bsp_sdcard || sdmmc_get_status(bsp_sdcard) != ESP_OK)
                {
                    app_storage_mark_fault("SD_STATUS");
                    next_recovery = now + 60000000LL;
                }
            }
            else if (now >= next_recovery)
            {
                (void)bsp_sdcard_unmount();
                esp_err_t error = bsp_sdcard_mount();
                next_recovery = now + 60000000LL;

                if (error == ESP_OK && bsp_sdcard && sdmmc_get_status(bsp_sdcard) == ESP_OK)
                {
                    atomic_store(&s_ok, true);
                    app_state_set_sd_ok(true);
                    app_events_add(APP_EVENT_INFO, "SD-kártya kapcsolat helyreállt.");
                }
            }

            if (now >= next_space)
            {
                update_space();
                next_space = now + 60000000LL;
            }
            app_storage_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* Az ismételt indítási kérések nem hoznak létre párhuzamos helyreállító taskokat. */
esp_err_t app_storage_start_recovery_task(void)
{
    if (s_recovery_task_started)
        return ESP_OK;
    if (xTaskCreate(recovery_task, "sd_recovery", 4096, NULL, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;

    s_recovery_task_started = true;
    return ESP_OK;
}
