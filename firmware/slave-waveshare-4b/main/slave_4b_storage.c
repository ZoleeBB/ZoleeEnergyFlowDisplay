/* Ez a célprogram sosem csatol SD-t: profil és párosítás NVS-ben marad. */
#include "app_storage.h"
#include "app_state.h"

esp_err_t app_storage_boot_mount(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void app_storage_mark_fault(const char *reason)
{
    (void)reason;
}

bool app_storage_is_ok(void)
{
    return false;
}

bool app_storage_is_cardless(void)
{
    return true;
}

void app_storage_set_cardless(void)
{
    app_state_set_cardless(true);
}

esp_err_t app_storage_start_recovery_task(void)
{
    return ESP_OK;
}

esp_err_t app_storage_lock(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool app_storage_try_lock(void)
{
    return false;
}

void app_storage_unlock(void)
{
}

bool app_storage_space(uint64_t *total, uint64_t *free_bytes)
{
    if (total)
        *total = 0;
    if (free_bytes)
        *free_bytes = 0;
    return false;
}
