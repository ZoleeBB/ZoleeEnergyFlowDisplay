/* A valódi NVS/profil-worker futtatása tartós tárral és hibainjektálással. */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../main/app_ble_profile.c"

static char stored_pin[7];
static uint8_t stored_profile[APP_BLE_PROFILE_SIZE], queued[APP_BLE_PROFILE_SIZE];
static bool have_stored, have_queued, fail_commit, fail_stats, busy;
static unsigned writes, saves, reboots;
static jmp_buf task_return;

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{
    assert(!strcmp(name, "ble_profile") && mode == NVS_READWRITE);
    *h = 1;
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len)
{
    (void)h;
    assert(!strcmp(key, "pin"));
    if (!stored_pin[0])
        return ESP_ERR_NOT_FOUND;
    assert(*len >= 7);
    memcpy(out, stored_pin, 7);
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{
    (void)h;
    assert(!strcmp(key, "pin"));
    memcpy(stored_pin, value, 7);
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    (void)h;
    assert(!strcmp(key, "slave"));
    if (!have_stored)
        return ESP_ERR_NOT_FOUND;
    assert(*len >= sizeof(stored_profile));
    memcpy(out, stored_profile, sizeof(stored_profile));
    *len = sizeof(stored_profile);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len)
{
    (void)h;
    assert(!strcmp(key, "slave") && len == sizeof(stored_profile));
    writes++;
    if (!fail_commit)
    {
        memcpy(stored_profile, data, len);
        have_stored = true;
    }
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    return fail_commit ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t h)
{
    (void)h;
}
uint32_t esp_random(void)
{
    return 987654321;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (void *)1;
}
int xSemaphoreTake(SemaphoreHandle_t h, unsigned wait)
{
    (void)h;
    (void)wait;
    return pdPASS;
}
int xSemaphoreGive(SemaphoreHandle_t h)
{
    (void)h;
    return pdPASS;
}
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{
    assert(count == 1 && size == APP_BLE_PROFILE_SIZE);
    return (void *)1;
}
int xQueueOverwrite(QueueHandle_t q, const void *data)
{
    (void)q;
    memcpy(queued, data, sizeof(queued));
    have_queued = true;
    return pdPASS;
}
int xQueueReceive(QueueHandle_t q, void *data, TickType_t timeout)
{
    (void)q;
    (void)timeout;
    if (!have_queued)
        longjmp(task_return, 1);
    memcpy(data, queued, sizeof(queued));
    have_queued = false;
    return pdTRUE;
}
int xTaskCreate(
    void (*f)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{
    (void)f;
    (void)name;
    (void)stack;
    (void)arg;
    (void)priority;
    (void)handle;
    return pdPASS;
}
void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
}
bool app_ota_try_begin_operation(void)
{
    return !busy;
}
void app_ota_end_operation(void)
{
}
esp_err_t app_stats_save_now(void)
{
    saves++;
    return fail_stats ? ESP_FAIL : ESP_OK;
}
void esp_restart(void)
{
    assert(saves > 0 && !fail_stats);
    reboots++;
    longjmp(task_return, 2);
}
void app_events_add(app_event_level_t level, const char *format, ...)
{
    (void)level;
    (void)format;
}
static void step(void)
{
    if (setjmp(task_return) == 0)
        profile_task(NULL);
}

int main(void)
{
    app_config_t master = {.data_seed_enabled = true};
    assert(app_ble_profile_init(&master) == ESP_OK);
    assert(strlen(master.data_seed_pin) == 6 && app_ble_profile_pin_changed());
    assert(app_ble_profile_confirm_pin() == ESP_OK);
    char pin[7];
    memcpy(pin, master.data_seed_pin, 7);
    master.data_seed_pin[0] = 0;
    assert(app_ble_profile_init(&master) == ESP_OK);
    assert(!strcmp(master.data_seed_pin, pin) && !app_ble_profile_pin_changed());
    strcpy(master.data_seed_pin, "000123");
    assert(app_ble_profile_init(&master) == ESP_OK && app_ble_profile_pin_changed());
    assert(app_ble_profile_confirm_pin() == ESP_OK && !strcmp(stored_pin, "000123"));
    app_config_t slave = {.data_recept_mode = APP_DATA_SLAVE};
    assert(app_ble_profile_init(&slave) == ESP_OK && !slave.wifi.ssid[0]);
    strcpy(master.wifi.ssid, "test-wifi");
    strcpy(master.wifi.password, "test-secret");
    master.ota.auto_update = true;
    uint8_t data[APP_BLE_PROFILE_SIZE];
    app_ble_profile_encode(&master, data);
    assert(app_ble_profile_submit(data) && !app_ble_profile_ready());
    step();
    assert(app_ble_profile_ready() && writes == 1 && !saves && !reboots);
    app_ble_profile_apply(&slave);
    assert(!strcmp(slave.wifi.ssid, "test-wifi") && slave.ota.auto_update);
    app_ble_profile_boot_complete();
    assert(app_ble_profile_submit(data));
    step();
    assert(writes == 1 && !reboots); /* Heartbeat nem ír NVS-t. */
    master.ota.auto_update = false;
    app_ble_profile_encode(&master, data);
    busy = true;
    assert(app_ble_profile_submit(data));
    step();
    assert(writes == 2 && !saves && !reboots);
    busy = false;
    fail_stats = true;
    assert(app_ble_profile_submit(data));
    step();
    assert(saves == 1 && !reboots);
    fail_stats = false;
    assert(app_ble_profile_submit(data));
    step();
    assert(reboots == 1 && saves == 2);
    assert(app_ble_profile_init(&slave) == ESP_OK &&
           !slave.ota.auto_update); /* Következő boot örökli. */
    app_ble_profile_apply(&slave);
    unsigned previous = writes;
    fail_commit = true;
    strcpy(master.wifi.ssid, "changed");
    app_ble_profile_encode(&master, data);
    assert(app_ble_profile_submit(data));
    step();
    assert(writes == previous + 1 && !app_ble_profile_ready());
    app_ble_profile_apply(&slave);
    assert(!strcmp(slave.wifi.ssid, "test-wifi"));
    puts("PASS: persistent auto PIN, PIN change, NVS profile, no repeated writes, boot inheritance "
         "and OTA/SD-safe runtime restart");
}
