/* Dedikált 4B Slave: nincs SD, konfigurálható adatforrás vagy master indítás. */
#include <math.h>
#include "slave_4b_ui.h"
#include "slave_4b_intercom.h"
#include "app_intercom.h"
#include "app_intercom_audio.h"
#include "app_intercom_control.h"
#include "app_boot_config.h"
#include "app_ble_profile.h"
#include "app_slave_display_input.h"
#include "app_slave_presence_service.h"
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
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <stdatomic.h>

/* A kijelző tájolása a Master webes Slave-kártyájáról érkezik, és csak
 * induláskor alkalmazható (futó MIPI DSI mellett nincs átforgatás), ezért
 * NVS-be kerül, majd a Slave újraindul. Az érintést az adapter forgatja. */
#define SLAVE_ROTATION_NAMESPACE "slave_4b"
#define SLAVE_ROTATION_KEY "rotation"
#define SLAVE_ROTATION_DELAY_US INT64_C(2000000)

static app_config_t config;
static uint16_t rotation_degrees;
static int last_brightness = -1;
static bool last_runtime_wifi;
static app_slave_display_t display_state;
static app_slave_display_input_t input_gate;
static atomic_bool boot_complete;
/* A napi statisztika-pillanatkép (288 ötperces sáv) PSRAM-ban, csak látható
 * összesítőnél és legfeljebb másodpercenként frissül a 33 ms-os körben. */
static app_stats_snapshot_t *summary_snapshot;

static uint16_t rotation_load(void)
{
    nvs_handle_t handle;
    uint8_t quarters = 0;
    if (nvs_open(SLAVE_ROTATION_NAMESPACE, NVS_READONLY, &handle) == ESP_OK)
    {
        if (nvs_get_u8(handle, SLAVE_ROTATION_KEY, &quarters) != ESP_OK || quarters > 3)
            quarters = 0;
        nvs_close(handle);
    }
    return (uint16_t)(quarters * 90u);
}

static bool rotation_save(uint16_t degrees)
{
    nvs_handle_t handle;
    if (degrees % 90 || degrees > 270 ||
        nvs_open(SLAVE_ROTATION_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return false;
    esp_err_t error = nvs_set_u8(handle, SLAVE_ROTATION_KEY, (uint8_t)(degrees / 90));
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}

/* Rövid késleltetés után indul újra, hogy a Master webes visszaigazolása és a
 * BLE-nyugta még kimenjen. Kaputelefon-hívás vagy PIN-bevitel alatt vár. */
static void rotation_follow(int64_t now, bool busy)
{
    static int64_t pending_since;
    static bool failed;
    uint16_t desired = 0;
    if (failed || !app_slave_presence_service_rotation(&desired) ||
        desired == rotation_degrees)
    {
        pending_since = 0;
        return;
    }
    if (busy)
        return;
    if (!pending_since)
    {
        pending_since = now;
        return;
    }
    if (now - pending_since < SLAVE_ROTATION_DELAY_US)
        return;
    if (!rotation_save(desired))
    {
        failed = true;
        app_events_add(APP_EVENT_WARNING, "A kijelző tájolását nem sikerült elmenteni.");
        return;
    }
    app_events_add(APP_EVENT_INFO, "Új kijelzőtájolás érkezett a Mastertől; a Slave újraindul.");
    esp_restart();
}

/* A BLE a C6 társprocesszoron keresztül fut. Ha a host nem szinkronizál, vagy
 * a Masterrel sokáig nincs kapcsolat, a kijelző adat nélkül marad: ilyenkor az
 * eszköz magától újraindul. A próbálkozások között növekvő szünet van, hogy egy
 * tartósan kikapcsolt Master mellett ne induljon percenként újra. */
#define SLAVE_BLE_SYNC_LIMIT_US INT64_C(90000000)
#define SLAVE_BLE_LINK_LIMIT_US INT64_C(600000000)
#define SLAVE_BLE_BACKOFF_KEY "ble_restarts"

static void ble_watchdog(int64_t now, const app_runtime_state_t *runtime)
{
    static int64_t good_us;
    static bool restarting;
    static uint8_t attempts = UINT8_MAX;
    if (attempts == UINT8_MAX)
    {
        nvs_handle_t handle;
        attempts = 0;
        if (nvs_open(SLAVE_ROTATION_NAMESPACE, NVS_READONLY, &handle) == ESP_OK)
        {
            if (nvs_get_u8(handle, SLAVE_BLE_BACKOFF_KEY, &attempts) != ESP_OK)
                attempts = 0;
            nvs_close(handle);
        }
    }
    if (!good_us)
        good_us = now;
    if (runtime->slave_connected)
    {
        good_us = now;
        if (attempts)
        {
            nvs_handle_t handle;
            attempts = 0;
            if (nvs_open(SLAVE_ROTATION_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
            {
                if (nvs_erase_key(handle, SLAVE_BLE_BACKOFF_KEY) == ESP_OK)
                    nvs_commit(handle);
                nvs_close(handle);
            }
        }
        return;
    }
    if (restarting)
        return;
    /* Az első próbálkozás 10 perc után, a továbbiak 30, majd 120 percenként. */
    int64_t limit = !runtime->ble_ready ? SLAVE_BLE_SYNC_LIMIT_US :
                    attempts == 0 ? SLAVE_BLE_LINK_LIMIT_US :
                    attempts == 1 ? 3 * SLAVE_BLE_LINK_LIMIT_US :
                                    12 * SLAVE_BLE_LINK_LIMIT_US;
    if (now - good_us < limit || now < limit)
        return;
    restarting = true;
    nvs_handle_t handle;
    if (nvs_open(SLAVE_ROTATION_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
    {
        uint8_t next = attempts < 250 ? (uint8_t)(attempts + 1) : attempts;
        if (nvs_set_u8(handle, SLAVE_BLE_BACKOFF_KEY, next) == ESP_OK)
            nvs_commit(handle);
        nvs_close(handle);
    }
    app_events_add(APP_EVENT_WARNING, runtime->ble_ready ?
                   "Nincs masterkapcsolat %u perce; a Slave újraindul." :
                   "A BLE host nem szinkronizált %u perc alatt; a Slave újraindul.",
                   (unsigned)(limit / 60000000));
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

/* A diagnosztika naplója és az eseménymásolat PSRAM-ban él, és csak nyitott
 * képernyőnél készül el: a 33 ms-os kör és a belső RAM érintetlen marad. */
typedef struct
{
    app_event_entry_t entries[12];
    char text[1536];
} diagnostics_log_t;

static diagnostics_log_t *diagnostics_log;
static char network_address[16];
static uint32_t touch_count;

static void log_refresh(void)
{
    if (!diagnostics_log)
        diagnostics_log = heap_caps_calloc(1, sizeof(*diagnostics_log), MALLOC_CAP_SPIRAM);
    if (!diagnostics_log)
        return;
    size_t count = app_events_snapshot(diagnostics_log->entries,
                                       sizeof(diagnostics_log->entries) /
                                       sizeof(diagnostics_log->entries[0]));
    size_t position = 0;
    diagnostics_log->text[0] = 0;
    /* A legfrissebb elöl, ahogy a Master weboldalán is. */
    for (size_t i = count; i-- > 0 && position + 1 < sizeof(diagnostics_log->text);)
    {
        const app_event_entry_t *entry = &diagnostics_log->entries[i];
        char stamp[16] = "--:--:--";
        if (entry->epoch_s > 0)
        {
            time_t epoch = (time_t)entry->epoch_s;
            struct tm local;
            localtime_r(&epoch, &local);
            strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
        }
        int written = snprintf(diagnostics_log->text + position,
                               sizeof(diagnostics_log->text) - position, "%s  %s%s\n",
                               stamp, entry->level == APP_EVENT_ERROR ? "HIBA: " :
                               entry->level == APP_EVENT_WARNING ? "FIGYELEM: " : "",
                               entry->text);
        if (written <= 0)
            break;
        position += (size_t)written;
    }
}

/* A Slave-nek nincs SD-kártyája és saját beállítóoldala, ezért a nappali és
 * éjszakai fényerőt a Master profiljából veszi át – újraindítás nélkül. */
static void brightness_follow(int64_t now)
{
    static int64_t last;
    if (last && now - last < 1000000)
        return;
    last = now;
    app_ble_profile_apply(&config);
    if (display_state.profile.day_brightness == config.display.day_brightness &&
        display_state.profile.night_brightness == config.display.night_brightness)
        return;
    app_slave_display_profile_t profile = display_state.profile;
    profile.day_brightness = config.display.day_brightness;
    profile.night_brightness = config.display.night_brightness;
    if (!app_slave_display_profile(&display_state, &profile, now))
        return;
    app_events_add(APP_EVENT_INFO, "Fényerő a Mastertől: nappal %u%%, éjjel %u%%.",
                   (unsigned)profile.day_brightness, (unsigned)profile.night_brightness);
}

static void status_refresh(int64_t now, unsigned brightness)
{
    static int64_t last;
    if (last && now - last < 1000000)
        return;
    last = now;
    /* Csak nyitott diagnosztikánál dolgozunk: a 33 ms-os kör nem hív hálózatot. */
    if (!slave_4b_ui_diagnostics_visible())
        return;
    if (!last_runtime_wifi || app_wifi_get_ip(network_address, sizeof(network_address)) != ESP_OK)
        network_address[0] = 0;
    log_refresh();
    app_slave_presence_state_t presence = {0};
    bool known = app_slave_presence_service_state(&presence);
    int64_t remaining = display_state.awake_until_us - now;
    slave_4b_status_t status = {
        .firmware = esp_app_get_description()->version,
        .ip = network_address,
        .log = diagnostics_log ? diagnostics_log->text : NULL,
        .rotation_degrees = rotation_degrees,
        .uptime_seconds = (uint32_t)(now / 1000000),
        .night = display_state.night,
        .brightness = brightness,
        .blank_at_night = display_state.profile.blank_at_night,
        .presence_source = known && presence.source,
        .presence_fresh = known && presence.fresh,
        .presence_active = display_state.presence_active,
        .presence_age_seconds = known ? presence.age_seconds : 0,
        .timeout_seconds = display_state.profile.presence_timeout_seconds,
        .blank_in_seconds = display_state.presence_active || remaining <= 0 ?
                            -1 : (int32_t)(remaining / 1000000),
        .touch_count = touch_count,
    };
    slave_4b_ui_status(&status);
}

static void summary_refresh(int64_t now)
{
    static int64_t last;
    static bool was_visible;
    bool visible = slave_4b_ui_summary_visible();
    bool due = visible && (!was_visible || now - last >= 1000000);
    was_visible = visible;
    if (!due || !atomic_load(&boot_complete))
        return;
    if (!summary_snapshot)
        summary_snapshot = heap_caps_calloc(1, sizeof(*summary_snapshot), MALLOC_CAP_SPIRAM);
    if (!summary_snapshot)
        return;
    last = now;
    app_stats_get_snapshot(summary_snapshot);
    const app_stats_snapshot_t *st = summary_snapshot;
    const app_energy_totals_t *t = &st->totals;
    slave_4b_summary_t summary = {
        .available = st->remote_available,
        .stale = st->remote_stale,
        .production_kwh = t->production_kwh,
        .consumption_kwh = t->consumption_kwh,
        .import_kwh = t->grid_import_kwh,
        .export_kwh = t->grid_export_kwh,
        .charge_kwh = t->battery_charge_kwh,
        .discharge_kwh = t->battery_discharge_kwh,
        .money_valid = st->reference_valid,
    };
    app_stats_metrics_t metrics = app_stats_metrics(t, &st->reference_energy);
    summary.self_sufficiency_pct = metrics.self_sufficiency_pct;
    summary.self_consumption_pct = metrics.self_consumption_pct;
    summary.purchase_cost = metrics.purchase_cost;
    summary.export_revenue = metrics.export_revenue;
    summary.savings = metrics.savings;
    summary.net_cost = metrics.net_cost;
    snprintf(summary.currency, sizeof(summary.currency), "%s", st->reference_currency);
    slave_4b_ui_summary(&summary);
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    static int64_t previous;
    app_energy_sample_t sample = {0};
    app_runtime_state_t runtime;
    app_state_get_runtime(&runtime);
    int64_t now = esp_timer_get_time();
    bool available = app_state_get_sample(&sample) && runtime.slave_connected &&
                     !app_state_data_timed_out(&runtime, now, app_config_data_timeout(&config));
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
    app_runtime_state_t visible_runtime = runtime;
    if (now - runtime.slave_packet_monotonic_us >= 15000000LL)
        visible_runtime.slave_source_stale = true;
    slave_4b_ui_update(&sample, available, &visible_runtime, clock, elapsed);
    summary_refresh(now);
    app_ble_pairing_status_t pairing;
    app_ble_pairing_status(&pairing);
    slave_4b_ui_pairing(&pairing);
    /* Csak friss, csatlakoztatott minta változtathat napszakot. Kieséskor az
     * utolsó ismert napszak marad, nem ugrunk vissza nappali fényerőre. */
    if (available || (runtime.energy_flow_disabled && runtime.slave_connected && !visible_runtime.slave_source_stale))
        app_slave_display_night(&display_state, runtime.energy_flow_disabled ? runtime.standalone_night : sample.night_mode, now);
    bool intercom_visible = slave_4b_intercom_update();
    app_slave_display_keep_visible(&display_state,
                                   intercom_visible || !atomic_load(&boot_complete) || pairing.pin_required ||
                                   !input_gate.input, now);
    app_slave_presence_service_apply(&display_state, now);
    last_runtime_wifi = runtime.wifi_connected;
    rotation_follow(now, intercom_visible || pairing.pin_required);
    if (!pairing.pin_required && !intercom_visible)
        ble_watchdog(now, &runtime);
    if (atomic_load(&boot_complete))
        brightness_follow(now);
    /* A Master webes „frissítések keresése" gombja ide is elér: a Slave a saját
     * kiadását tölti le, a Wi-Fi és a foglaltság ellenőrzése a közös OTA-é. */
    if (app_slave_presence_service_take_ota_request() &&
        app_ota_request_auto_update() != ESP_OK)
        app_events_add(APP_EVENT_WARNING,
                       "A Master kérésére indított frissítésellenőrzés nem indult el "
                       "(nincs Wi-Fi, vagy másik művelet fut).");
    int brightness = (int)app_slave_display_brightness(&display_state, now);
    if (intercom_visible) {
        unsigned day, night;
        app_intercom_control_brightness(&day, &night);
        brightness = display_state.night ? (int)night : (int)day;
    }
    if (brightness != last_brightness)
    {
        /* A háttérvilágítás nullázása után a BSP-t külön is felkapcsoljuk, hogy
         * az ébresztés akkor is látható legyen, ha a 0% leállította a PWM-et. */
        if (last_brightness <= 0 && brightness > 0)
            bsp_display_backlight_on();
        bsp_display_brightness_set(brightness);
        if (last_brightness >= 0)
            app_events_add(APP_EVENT_INFO, brightness == 0 ?
                           "Kijelző elsötétült (éjszakai mód, nincs jelenlét)." :
                           "Kijelző felébredt (fényerő: %d%%).", brightness);
        last_brightness = brightness;
    }
    /* A diagnosztikán látszódjon, hogy az érintés és a jelenlét tényleg beért. */
    static bool was_touch, was_presence;
    if (display_state.pointer_down && !was_touch)
    {
        touch_count++;
        app_events_add(APP_EVENT_INFO, "Érintés érkezett (fényerő: %d%%).", brightness);
    }
    was_touch = display_state.pointer_down;
    if (display_state.presence_active != was_presence)
    {
        was_presence = display_state.presence_active;
        app_events_add(APP_EVENT_INFO, was_presence ? "Jelenlét: aktív." :
                       "Jelenlét megszűnt, %u mp múlva sötétítés.",
                       (unsigned)display_state.profile.presence_timeout_seconds);
    }
    status_refresh(now, (unsigned)brightness);
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
    app_ota_report_boot_state();
    ESP_ERROR_CHECK(app_ota_start_validation_task());
    rotation_degrees = rotation_load();
    /* Az érintés nem forog magától: a 10.1-es Masterrel azonos konvenció szerint
     * a negyedfordulatnál tengelycsere, a félfordulatnál kettős tükrözés kell. */
    bool quarter = rotation_degrees == 90 || rotation_degrees == 270;
    bsp_display_cfg_t display = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = rotation_degrees == 90 ? ESP_LV_ADAPTER_ROTATE_90 :
                    rotation_degrees == 180 ? ESP_LV_ADAPTER_ROTATE_180 :
                    rotation_degrees == 270 ? ESP_LV_ADAPTER_ROTATE_270 :
                                              ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {.swap_xy = quarter,
                        .mirror_x = rotation_degrees == 90 || rotation_degrees == 180,
                        .mirror_y = rotation_degrees == 180 || rotation_degrees == 270}
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
    /* Régi Masterrel és a célzott, védett profil megérkezéséig nincs sötétítés. */
    app_slave_display_profile_t display_profile = {
        .day_brightness = config.display.day_brightness,
        .night_brightness = config.display.night_brightness,
        .blank_at_night = false,
        .presence_timeout_seconds = 30,
    };
    ESP_ERROR_CHECK(app_slave_display_init(&display_state, &display_profile,
                                          esp_timer_get_time()) ? ESP_OK : ESP_ERR_INVALID_ARG);
    app_slave_display_keep_visible(&display_state, true, esp_timer_get_time());
    app_state_set_data_mode(APP_DATA_SLAVE);
    app_state_set_config_ok(true);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(app_time_init(&config));
    ESP_ERROR_CHECK(bsp_display_lock(5000) ? ESP_OK : ESP_ERR_TIMEOUT);
    for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input))
    {
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_display(input) == lv_display_get_default())
        {
            ESP_ERROR_CHECK(app_slave_display_input_bind(&input_gate, input, &display_state,
                                                         esp_timer_get_time) ? ESP_OK : ESP_FAIL);
            slave_4b_ui_bind_input(input);
            break;
        }
    }
    ESP_ERROR_CHECK(lv_timer_create(refresh, 33, NULL) ? ESP_OK : ESP_ERR_NO_MEM);
    bsp_display_unlock();
    (void)app_intercom_prepare_video();
    ESP_ERROR_CHECK(app_intercom_start());
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
    atomic_store(&boot_complete, true);
    for (;;) {
        app_intercom_audio_poll();
        slave_4b_intercom_prepare();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
