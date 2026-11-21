/* A valódi OTA-validáló feladatot futtatjuk virtuális RTOS-órával. Sem az
 * energiaforrás, sem az első témakép nem szükséges a frissen írt kép elfogadásához. */
#include "app_events.h"
#include "app_ota.h"
#include "app_state.h"
#include "esp_ota_ops.h"
#include "freertos/task.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const esp_partition_t partition = {.size = 8388608};
static esp_ota_img_states_t image_state;
static app_runtime_state_t runtime;
static TickType_t ticks, time_arrives;
static unsigned scheduled, deleted, validated, rolled_back;
static char reason[224];
static bool committed, start_failed, run_on_wait, rollback_unavailable;
static void (*worker)(void *);

int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle)
{
    (void)name; (void)stack; (void)priority; (void)handle;
    assert(fn && !arg);
    if (start_failed) return 0;
    worker = fn;
    ++scheduled;
    return pdPASS;
}

TickType_t xTaskGetTickCount(void) { return ticks; }

void vTaskDelay(TickType_t wait)
{
    assert(wait && !deleted && ticks < pdMS_TO_TICKS(1300000));
    ticks += wait;
    if (run_on_wait) {
        run_on_wait = false;
        worker(NULL);
    }
    if (time_arrives && ticks >= time_arrives) runtime.time_synced = true;
}

void vTaskDelete(TaskHandle_t task) { assert(!task); ++deleted; }
void app_state_get_runtime(app_runtime_state_t *out) { *out = runtime; }
void app_events_add(app_event_level_t level, const char *fmt, ...) { (void)level; (void)fmt; }

const esp_partition_t *esp_ota_get_running_partition(void) { return &partition; }
const esp_partition_t *esp_ota_get_last_invalid_partition(void) { return NULL; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t *running, esp_ota_img_states_t *state)
{
    assert(running == &partition);
    *state = image_state;
    return ESP_OK;
}
const esp_app_desc_t *esp_app_get_description(void) { return NULL; }
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *d)
{
    (void)p; (void)d;
    return ESP_ERR_NOT_FOUND;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { ++validated; return ESP_OK; }
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void)
{
    assert(committed && reason[0]);
    ++rolled_back;
    return rollback_unavailable ? ESP_FAIL : ESP_OK;
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(name, "ota_result") && mode == NVS_READWRITE);
    *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{
    assert(h == 1 && !strcmp(key, "reason") && value && strlen(value) < sizeof(reason));
    strcpy(reason, value);
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *value, size_t *len)
{
    (void)h; (void)key; (void)value; (void)len;
    return ESP_ERR_NOT_FOUND;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    assert(h == 1 && !strcmp(key, "reason"));
    reason[0] = 0;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { assert(h == 1); committed = true; return ESP_OK; }
void nvs_close(nvs_handle_t h) { assert(h == 1); }

static void boot(app_data_mode_t mode)
{
    image_state = ESP_OTA_IMG_PENDING_VERIFY;
    runtime = (app_runtime_state_t){.data_recept_mode = mode};
    ticks = time_arrives = deleted = validated = rolled_back = 0;
    committed = false;
    reason[0] = 0;
}

static void expect_timeout(TickType_t deadline, const char *cause)
{
    worker(NULL);
    assert(deleted == 1 && !validated && rolled_back == 1 && committed);
    /* A naplóbejegyzés után a feladat adhat még egy másodpercet a kiírásra. */
    assert(ticks >= deadline && ticks <= deadline + pdMS_TO_TICKS(1000));
    assert(strstr(reason, cause));
}

int main(void)
{
    /* A két hidegindításnak külön statikus OTA-állapota van. */
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        image_state = ESP_OTA_IMG_VALID;
        assert(app_ota_start_validation_task() == ESP_OK && !scheduled);
        app_ota_wait_for_validation();
        assert(!ticks);
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));

    boot(APP_DATA_MQTT);
    start_failed = true;
    assert(app_ota_start_validation_task() == ESP_ERR_NO_MEM && !scheduled);
    start_failed = false;
    assert(app_ota_start_validation_task() == ESP_OK && worker);
    assert(app_ota_start_validation_task() == ESP_OK && scheduled == 1);
    /* Ha a felhasználó 5 perc után állítja be a Wi-Fi-t, és nincs használható
     * régi kép, az új időszinkron még feloldja a konfigurálást. */
    runtime.wifi_connected = true;
    rollback_unavailable = true;
    time_arrives = pdMS_TO_TICKS(303000);
    run_on_wait = true;
    app_ota_wait_for_validation();
    assert(ticks >= time_arrives && validated == 1 && rolled_back == 1 && deleted == 1 && !reason[0]);
    rollback_unavailable = false;
    app_ota_wait_for_validation();

    const app_data_mode_t master[] = {APP_DATA_MQTT, APP_DATA_HTTP, APP_DATA_LOXONE, APP_DATA_INVERTER};
    for (size_t i = 0; i < sizeof(master)/sizeof(master[0]); ++i) {
        boot(master[i]);
        runtime.wifi_connected = true;
        time_arrives = pdMS_TO_TICKS(4000);
        worker(NULL);
        assert(deleted == 1 && validated == 1 && !rolled_back && ticks == time_arrives);
        assert(!runtime.has_valid_sample && !committed);
    }

    /* A már párosított Slave Wi-Fi nélkül, a Mastertől kapott idővel érvényes. */
    boot(APP_DATA_SLAVE);
    runtime.config_ok = runtime.ble_ready = runtime.slave_connected = true;
    time_arrives = pdMS_TO_TICKS(2000);
    worker(NULL);
    assert(deleted == 1 && validated == 1 && !rolled_back && ticks == time_arrives);

    boot(APP_DATA_MQTT);
    runtime.time_synced = true;
    expect_timeout(pdMS_TO_TICKS(300000), "IP");
    boot(APP_DATA_INVERTER);
    runtime.wifi_connected = true;
    expect_timeout(pdMS_TO_TICKS(300000), "időszinkron");
    boot(APP_DATA_SLAVE);
    runtime.config_ok = true;
    expect_timeout(pdMS_TO_TICKS(1200000), "időszinkron");
    puts("PASS: actual pending OTA task, Wi-Fi/time before application data, BLE Slave, persisted rollback causes");
    return 0;
}
