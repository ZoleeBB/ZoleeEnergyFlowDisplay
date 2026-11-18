/* A 10,1″ kijelző valódi app_main belépési pontja és app_state állapota fut.
 * Csak a hardver, a külső szolgáltatások és a fázisokat megjelenítő UI helyettesített. */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "esp_err.h"
#include "freertos/semphr.h"

#define ESP_ERROR_CHECK(expr) assert((expr) == ESP_OK)
#define MALLOC_CAP_DMA 8
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
const char *esp_err_to_name(esp_err_t error);
void vTaskPrioritySet(void *task, unsigned priority);

/* A teszt nem érinti a gazdarendszer /sdcard könyvtárát. */
static int fixture_access(const char *path, int mode);
static int fixture_rename(const char *from, const char *to);
#define access fixture_access
#define rename fixture_rename
#include "../main/app_main.c"
#undef access
#undef rename

static app_data_mode_t mode;
static bool setup;
static int wifi_starts, wifi_waits, ntp_attempts, web_starts, mqtt_starts;
static int audio_starts, loxone_starts, intercom_preparations, inverter_ticks, ingest_starts;
static bool runtime_started, ble_started, ble_profile_applied, setup_waiting_for_time, setup_web;
static app_boot_stage_t last_stage;
static int64_t elapsed_ms, runtime_start_ms;
static jmp_buf boot_finished;

static app_runtime_state_t runtime(void)
{
    app_runtime_state_t state;
    app_state_get_runtime(&state);
    return state;
}

static bool network_input(void)
{
    return !setup && (mode == APP_DATA_MQTT || mode == APP_DATA_HTTP || mode == APP_DATA_LOXONE);
}

static void fixture_config(app_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->data_recept_mode = mode;
    config->display.rotation_degrees = 270;
    config->time.startup_retry_seconds = 1;
    if (mode != APP_DATA_SLAVE) strcpy(config->wifi.ssid, "boot-test");
    strcpy(config->mqtt.broker, "test-broker");
}

esp_err_t app_config_load_file(const char *path, app_config_t *cfg, char *error, size_t len)
{
    (void)path; (void)error; (void)len;
    if (setup) return ESP_ERR_NOT_FOUND;
    fixture_config(cfg);
    return ESP_OK;
}

esp_err_t app_wifi_start(const app_config_t *cfg)
{
    assert(cfg->data_recept_mode == mode && !setup && mode != APP_DATA_SLAVE);
    assert(last_stage == APP_BOOT_WIFI);
    ++wifi_starts;
    return ESP_OK; /* A STA feladata indul, a DHCP még nincs kész. */
}

bool app_wifi_wait_connected(TickType_t timeout)
{
    assert(wifi_starts == 1 && timeout > 0);
    if (++wifi_waits >= 3) app_state_set_wifi_connected(true);
    return runtime().wifi_connected;
}

void app_ui_show_boot(app_boot_stage_t stage, const char *detail)
{
    (void)detail;
    if (stage == APP_BOOT_NTP && mode != APP_DATA_SLAVE) assert(runtime().wifi_connected);
    last_stage = stage;
}

esp_err_t app_time_init(const app_config_t *cfg)
{
    assert(cfg->data_recept_mode == mode);
    if (mode != APP_DATA_SLAVE) assert(runtime().wifi_connected);
    return ESP_OK;
}

esp_err_t app_time_sync_once(void)
{
    assert(mode != APP_DATA_SLAVE);
    if (setup) assert(setup_waiting_for_time);
    else assert(last_stage == APP_BOOT_NTP);
    assert(runtime().wifi_connected);
    if (++ntp_attempts == 1) return ESP_FAIL;
    app_state_set_time_synced(true);
    return ESP_OK;
}

esp_err_t app_setup_wifi_provision(app_config_t *config)
{
    assert(setup && config->data_recept_mode == mode);
    app_state_set_wifi_connected(true);
    return ESP_OK;
}

void app_setup_wifi_set_phase(app_setup_phase_t phase)
{
    assert(setup && runtime().wifi_connected);
    if (phase == APP_SETUP_WAIT_TIME) {
        assert(!runtime().time_synced);
        setup_waiting_for_time = true;
    } else {
        assert(phase == APP_SETUP_WEB && runtime().time_synced && web_starts == 2);
        setup_web = true;
        longjmp(boot_finished, 1);
    }
}

esp_err_t app_ui_start_runtime(void)
{
    app_runtime_state_t state = runtime();
    assert(!setup && !runtime_started && state.time_synced);
    assert(!state.has_valid_sample && !state.mqtt_connected && !state.loxone_connected);
    if (network_input() || mode == APP_DATA_SLAVE) assert(last_stage == APP_BOOT_MQTT);
    if (mode == APP_DATA_INVERTER) assert(last_stage == APP_BOOT_NTP);
    runtime_started = true;
    runtime_start_ms = elapsed_ms;
    return ESP_OK;
}

esp_err_t app_web_start(const app_config_t *cfg)
{
    assert(cfg->data_recept_mode == mode && runtime().time_synced);
    /* Hibás első szerverindítás: a kezelőfelület mérés nélkül is újrapróbálja. */
    return ++web_starts == 1 ? ESP_FAIL : ESP_OK;
}

esp_err_t app_mqtt_start(const app_config_t *cfg)
{
    assert(cfg->data_recept_mode == APP_DATA_MQTT && !setup);
    return ++mqtt_starts == 1 ? ESP_FAIL : ESP_OK;
}

esp_err_t app_ingest_init(app_data_mode_t input)
{
    assert(input == mode && !setup && runtime().time_synced);
    ++ingest_starts;
    return ESP_OK;
}

esp_err_t app_loxone_start(uint32_t wake_time)
{
    (void)wake_time;
    assert(mode == APP_DATA_LOXONE && !setup);
    ++loxone_starts;
    return ESP_OK;
}

esp_err_t app_loxone_audio_init(void) { ++audio_starts; return ESP_OK; }
esp_err_t app_intercom_prepare_video(void) { ++intercom_preparations; return ESP_OK; }
void app_inverter_tick(void) { assert(mode == APP_DATA_INVERTER); ++inverter_ticks; }
esp_err_t app_ble_seed_start(const app_config_t *cfg)
{
    assert(!setup && cfg->data_recept_mode == mode);
    ble_started = true;
    return ESP_OK;
}

void app_ble_profile_apply(app_config_t *cfg)
{
    assert(cfg->data_recept_mode == APP_DATA_SLAVE && runtime().time_synced);
    ble_profile_applied = true;
}

void vTaskDelay(TickType_t ticks)
{
    elapsed_ms += ticks;
    if (mode == APP_DATA_SLAVE && ble_started && !runtime_started && elapsed_ms >= 750)
        app_state_set_time_synced(true); /* A hitelesített Master óracsomagja. */
    if (runtime_started && elapsed_ms - runtime_start_ms >= 22000) longjmp(boot_finished, 1);
    assert(elapsed_ms < 90000);
}

int64_t esp_timer_get_time(void) { return elapsed_ms * 1000; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "mqtt")) mode = APP_DATA_MQTT;
    else if (!strcmp(argv[1], "http")) mode = APP_DATA_HTTP;
    else if (!strcmp(argv[1], "loxone")) mode = APP_DATA_LOXONE;
    else if (!strcmp(argv[1], "inverter")) mode = APP_DATA_INVERTER;
    else if (!strcmp(argv[1], "slave")) mode = APP_DATA_SLAVE;
    else { assert(!strcmp(argv[1], "setup")); setup = true; mode = APP_DATA_MQTT; }
    if (setjmp(boot_finished) == 0) app_main();

    assert(ntp_attempts == (mode == APP_DATA_SLAVE ? 0 : 2));
    assert(wifi_starts == (!setup && mode != APP_DATA_SLAVE));
    assert(setup_web == setup);
    assert(runtime_started == !setup && ble_started == !setup);
    assert(ble_profile_applied == (mode == APP_DATA_SLAVE));
    assert(web_starts == (mode == APP_DATA_SLAVE ? 0 : 2));
    assert(mqtt_starts == (mode == APP_DATA_MQTT && !setup ? 2 : 0));
    assert((inverter_ticks > 0) == (mode == APP_DATA_INVERTER));
    assert(loxone_starts == (mode == APP_DATA_LOXONE) && audio_starts == loxone_starts);
    assert(intercom_preparations == loxone_starts && ingest_starts == !setup);
    printf("10.1 startup: %s OK\n", argv[1]);
    return 0;
}

/* Változatlanul sikeres tesztkörnyezet az adott forgatókönyvet nem érintő
 * szolgáltatásoknak. Váratlan, végzetes hiba azonnal megszakítja a tesztet. */
static int fixture_access(const char *path, int flag)
{ (void)flag; return !setup && !strcmp(path, CONFIG_PATH) ? 0 : -1; }
static int fixture_rename(const char *from, const char *to)
{ (void)from; (void)to; abort(); }
void app_ui_show_fatal(const char *title, const char *detail)
{ fprintf(stderr, "%s: %s\n", title, detail); abort(); }
void esp_restart(void) { abort(); }
void app_config_set_defaults(app_config_t *cfg) { fixture_config(cfg); }
bool app_boot_config_setup_pending(void) { return setup; }
esp_err_t app_boot_config_restore(app_config_t *cfg, bool slave, char *error, size_t len)
{ (void)cfg; (void)slave; (void)error; (void)len; return ESP_ERR_NOT_FOUND; }
esp_err_t app_backup_flash_boot_config(app_config_t *cfg, char *error, size_t len)
{ (void)cfg; (void)error; (void)len; return ESP_ERR_NOT_FOUND; }
esp_err_t app_backup_flash_restore_sd(unsigned *files, char error[APP_BACKUP_ERROR_SIZE])
{ (void)files; (void)error; return ESP_ERR_NOT_FOUND; }
void app_events_add(app_event_level_t level, const char *fmt, ...) { (void)level; (void)fmt; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test"; }
size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 65536; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { return heap_caps_get_free_size(caps); }
void vTaskPrioritySet(void *task, unsigned priority) { (void)task; (void)priority; }
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; return &mutex; }
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{ (void)timeout; return pthread_mutex_lock(mutex) == 0; }
int xSemaphoreGive(SemaphoreHandle_t mutex) { return pthread_mutex_unlock(mutex) == 0; }
esp_err_t nvs_open(const char *name, int access_mode, nvs_handle_t *handle)
{ (void)name; (void)access_mode; (void)handle; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_get_u16(nvs_handle_t h, const char *key, uint16_t *v)
{ (void)h; (void)key; (void)v; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_u16(nvs_handle_t h, const char *key, uint16_t v)
{ (void)h; (void)key; (void)v; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
void app_boot_config_use_storage(bool internal) { (void)internal; }
esp_err_t app_boot_config_cache_file(const char *path) { (void)path; return ESP_OK; }
esp_err_t app_ble_profile_init(app_config_t *cfg) { (void)cfg; return ESP_OK; }
esp_err_t app_ui_init(uint16_t rotation) { assert(rotation == 270); return ESP_OK; }
void app_ui_set_config(const app_config_t *cfg) { assert(cfg->data_recept_mode == mode); }
void app_events_init(void) {}
void app_ota_report_boot_state(void) {}
void app_ble_profile_boot_complete(void) {}
void app_loxone_audio_run(void) {}
void app_boot_config_use_trusted_backup(void) {}
void app_storage_set_cardless(void) {}
#define OK_SERVICE(name) esp_err_t name(void) { return ESP_OK; }
OK_SERVICE(nvs_flash_init)
OK_SERVICE(nvs_flash_erase)
OK_SERVICE(esp_netif_init)
OK_SERVICE(esp_event_loop_create_default)
OK_SERVICE(app_ota_start_validation_task)
OK_SERVICE(app_storage_boot_mount)
OK_SERVICE(app_templates_init)
OK_SERVICE(app_storage_start_recovery_task)
OK_SERVICE(app_stats_start_task)
OK_SERVICE(app_time_start_background_task)
OK_SERVICE(app_backup_flash_start)
OK_SERVICE(app_templates_raw_start)
#define OK_CONFIG(name) esp_err_t name(const app_config_t *cfg) { (void)cfg; return ESP_OK; }
OK_CONFIG(app_boot_config_remember)
OK_CONFIG(app_boot_config_save_setup_wifi)
OK_CONFIG(app_ota_start_auto_update)
OK_CONFIG(app_stats_init)
