/*
 * Az alkalmazás fő belépési pontja.
 *
 * Feladata az alrendszerek meghatározott sorrendű inicializálása:
 * NVS -> kijelző -> SD -> konfiguráció -> Wi-Fi -> NTP -> statisztika
 * -> web/OTA -> MQTT -> normál futási mód.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "app_display_boot.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "app_factory_reset.h"
#include "app_ble_seed.h"
#include "app_backup.h"
#include "app_events.h"
#include "app_mqtt.h"
#include "app_inverter.h"
#include "app_setup_wifi.h"
#include "app_loxone.h"
#include "app_loxone_audio_service.h"
#include "app_intercom.h"
#include "app_ingest.h"
#include "app_ota.h"
#include "app_ble_profile.h"
#include "app_boot_config.h"
#include "app_state.h"
#include "app_stats.h"
#include "app_storage.h"
#include "app_time.h"
#include "app_ui.h"
#include "app_web.h"
#include "app_templates.h"
#include "app_wifi.h"

/* A konfiguráció fix helye a microSD gyökerében. */
#define CONFIG_PATH APP_SD_MOUNT_POINT "/config.json"
#define CONFIG_BACKUP_PATH APP_SD_MOUNT_POINT "/config.json.webbak"

static const char *TAG = "app";
static app_config_t s_config;
static bool s_retry_web, s_retry_mqtt, s_retry_ingest, s_retry_loxone;
static app_display_boot_state_t s_boot_orientation = {.rotation_degrees=270};
static uint16_t s_active_rotation = 270;

/* Az NVS mentés csak a tájolás korai indításához kell; a felhasználói
 * beállítás továbbra is a config.json-ban van. */
static esp_err_t orientation_read(app_display_boot_state_t *state)
{
    nvs_handle_t h;
    esp_err_t err=nvs_open("display_ui",NVS_READONLY,&h);
    if(err==ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if(err!=ESP_OK) return err;
    (void)nvs_get_u16(h,"rotation",&state->rotation_degrees);
    nvs_close(h);
    state->rotation_degrees=state->rotation_degrees==90?90:270;
    return ESP_OK;
}
static esp_err_t orientation_write(const app_display_boot_state_t *state)
{
    nvs_handle_t h;
    esp_err_t err=nvs_open("display_ui",NVS_READWRITE,&h);
    if(err!=ESP_OK) return err;
    err=nvs_set_u16(h,"rotation",state->rotation_degrees);
    if(err==ESP_OK) err=nvs_commit(h);
    nvs_close(h);
    return err;
}

/* A konfiguráció betöltése után használt egyszerű HU/EN szövegválasztó. */
static bool configured_hu(void)
{
    return strcmp(s_config.project.language, "en") != 0;
}

/* Lokalizált szöveg kiválasztása a már betöltött projektkonfiguráció alapján. */
static const char *cfg_tr(const char *hu_text, const char *en_text)
{
    return configured_hu() ? hu_text : en_text;
}

/*
 * Végzetes indulási hiba megjelenítése.
 * Ilyenkor szándékosan nem próbálunk továbbindulni; újraindítás szükséges.
 */
static void fatal_forever(const char *title, const char *detail)
{
    ESP_LOGE(TAG, "%s: %s", title, detail ? detail : "");
    app_ui_show_fatal(title, detail);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*
 * Az NVS inicializálása.
 * Verzióváltás vagy elfogyott lapok esetén az ESP-IDF ajánlása szerint
 * töröljük az NVS-t, majd egyszer újrainicializáljuk.
 */
static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);
}

/*
 * ESP-IDF alkalmazás belépési pont.
 * A normál dashboard csak az összes kötelező indulási feltétel után indul el.
 */
static void __attribute__((noinline)) app_boot(void)
{
    /* Alap rendszerállapot és RAM eseménynapló. */
    init_nvs();
    app_state_init();
    app_events_init();

    /*
     * Az OTA rollback ellenőrzést még a kijelző inicializálása előtt elindítjuk.
     * Így egy hibás új firmware akkor is automatikusan vissza tud állni,
     * ha már a kijelző indítása közben megakadna.
     */
    /* Indítási memóriahiba esetén újraindulunk; pending OTA-nál a bootloader rollbackel. */
    app_ota_report_boot_state();
    ESP_ERROR_CHECK(app_ota_start_validation_task());

    /* Az első UI-képkocka előtt kiválasztjuk az NVS-ben eltárolt tájolást. */
    app_reset_plan_t reset;
    esp_err_t reset_read = app_factory_reset_read(&reset);
    if (reset_read == ESP_OK && !reset.scope && orientation_read(&s_boot_orientation) != ESP_OK)
        app_events_add(APP_EVENT_WARNING,"Tájolási NVS nem olvasható; 270 fokkal indulunk.");
    s_active_rotation=app_display_boot_select(&s_boot_orientation);
    ESP_LOGI(TAG,"Display startup rotation: %u degrees",(unsigned)s_active_rotation);

    /* Eredeti indulás: a kijelző már az SD-kártya ellenőrzését is mutatja. */
    if (app_ui_init(s_active_rotation) != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed");
        /* Az OTA-rollback validáció tovább futhat. */
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    app_ui_show_boot(APP_BOOT_SD, NULL);
    esp_err_t err = app_storage_boot_mount();
    bool cardless = err != ESP_OK;
    /* Élő feladat a régi RAM-konfigurációt bármikor visszaírhatná a törlés
     * után. Ezért a tartós kérés feldolgozása a boot kizárólagos szakasza. */
    for (;;) {
        app_reset_stage_t stage = APP_RESET_JOURNAL;
        esp_err_t step = reset_read;
        if (step == ESP_OK && reset.scope)
            step = app_factory_reset_apply(&reset, cardless ? NULL : APP_SD_MOUNT_POINT, &stage);
        if (step == ESP_OK) {
            if (stage == APP_RESET_RESTART) esp_restart();
            break;
        }
        const char *reason = "A belső visszaállításjelző nem olvasható vagy nem menthető.";
        if (stage == APP_RESET_CARD) reason = reset.scope == APP_RESET_INTERNAL ?
            "A visszaállítás SD nélkül indult. Vedd ki az időközben behelyezett kártyát." :
            "Helyezd vissza a jóváhagyáskor használt SD-kártyát.";
        else if (stage == APP_RESET_RADIO) reason = "A Wi-Fi modul gyári visszaállítása sikertelen.";
        else if (stage == APP_RESET_STORAGE) reason = "A tárolt adatok törlése sikertelen.";
        char detail[200];
        snprintf(detail, sizeof(detail), "%s (%s) Újrapróbálás…", reason, esp_err_to_name(step));
        app_ui_show_boot(APP_BOOT_SD, detail);
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (stage == APP_RESET_CARD) cardless = app_factory_reset_remount() != ESP_OK;
        if (stage == APP_RESET_JOURNAL) reset_read = app_factory_reset_read(&reset);
    }
    app_ui_show_boot(APP_BOOT_CONFIG, NULL);
    char config_error[256] = {0};
    bool trusted_sd = false;
    if (!reset.scope && !cardless) {
        if (access(CONFIG_PATH, F_OK) != 0 && access(CONFIG_BACKUP_PATH, F_OK) == 0)
            (void)rename(CONFIG_BACKUP_PATH, CONFIG_PATH);
        if (access(CONFIG_PATH, F_OK) != 0) {
            unsigned restored = 0;
            char restore_error[APP_BACKUP_ERROR_SIZE] = {0};
            if (app_backup_flash_restore_sd(&restored, restore_error) == ESP_OK)
                app_events_add(APP_EVENT_INFO, "Korábbi SD-beállítások visszaállítva.");
        }
        trusted_sd = app_config_load_file(CONFIG_PATH, &s_config, config_error, sizeof(config_error)) == ESP_OK;
    }
    if (reset.scope) {
        app_boot_config_begin_setup(&s_config, cardless);
    } else if (trusted_sd) {
        (void)app_boot_config_cache_file(CONFIG_PATH);
        if (app_boot_config_remember(&s_config) != ESP_OK)
            app_events_add(APP_EVENT_WARNING, "A tartalék szerepjelölő nem menthető.");
    } else {
        err = app_boot_config_restore(&s_config, false, config_error, sizeof(config_error));
        if (err == ESP_ERR_NOT_FOUND &&
            app_backup_flash_boot_config(&s_config, config_error, sizeof(config_error)) == ESP_OK) {
            app_boot_config_use_trusted_backup();
            err = ESP_OK;
            app_events_add(APP_EVENT_WARNING, "Az SD helyett a belső beállításmentésből indulunk.");
        }
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
            fatal_forever("Belső konfiguráció hiba", esp_err_to_name(err));
        if (err != ESP_OK) app_config_set_defaults(&s_config);
        s_config.cardless_slave = cardless && s_config.data_recept_mode == APP_DATA_SLAVE;
        app_boot_config_use_storage(cardless);
    }
    /* A még üres, első beállításra váró kártyát is megjegyezzük: a webes
     * config-mentés és az első teljes indulás között is kivehetik. */
    if (!cardless && app_factory_reset_remember_sd() != ESP_OK)
        app_events_add(APP_EVENT_WARNING, "A beállításokat tároló SD azonosítója nem menthető.");
    if (s_config.cardless_slave) app_storage_set_cardless();
    const bool setup = app_boot_config_setup_pending();

    /* Az SD-n tárolt konfiguráció a hiteles forrás. Változás esetén az NVS
     * gyorsindítási másolat frissül, és a kijelző az új beállítással újraindul.
     * Nem hívjuk az LVGL futás közbeni rotation API-ját. */
    bool rotation_restart_pending = false;
    if(app_display_boot_sync(&s_boot_orientation,s_config.display.rotation_degrees)) {
        if(orientation_write(&s_boot_orientation)==ESP_OK) {
            ESP_LOGI(TAG,"New display orientation saved; controlled restart");
            rotation_restart_pending = app_ota_validation_pending();
            if (!rotation_restart_pending) {
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        } else {
            app_events_add(APP_EVENT_WARNING,"Tájolási NVS írása sikertelen; jelenlegi állás marad.");
        }
    }
    /* A JPEG kis belső DMA-foglalása megelőzi a Wi-Fi/BLE feladatokat.
     * Csak Loxone módban kell; hiba esetén az alkalmazás tovább fut, és a videó újrapróbálja. */
    if (s_config.data_recept_mode == APP_DATA_LOXONE) {
        /* A dekóder leírói DMA-képes belső RAM-ból foglalódnak, amiből kevés
         * van; a foglalás mértékét ezért megmérjük. */
        unsigned dma_before = (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA);
        esp_err_t prepared = app_intercom_prepare_video();
        app_events_add(APP_EVENT_INFO, "JPEG-dekóder DMA-foglalás: %u -> %u B", dma_before,
                       (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
        if (prepared != ESP_OK)
            app_events_add(APP_EVENT_WARNING, "A kaputelefon JPEG-dekóderének korai előkészítése nem sikerült.");
    }
    if (!setup && app_ble_profile_init(&s_config) != ESP_OK) {
        if (s_config.data_recept_mode == APP_DATA_SLAVE)
            fatal_forever("Bluetooth profilhiba", "Az időszinkronhoz szükséges párosítási profil nem olvasható.");
        s_config.data_seed_enabled = false;
        app_events_add(APP_EVENT_WARNING, "A BLE profil nem olvasható, az adatmegosztás szünetel.");
    }
    app_state_set_data_mode(s_config.data_recept_mode);
    app_state_set_energy_flow_disabled(s_config.energy_flow_disabled, false);
    app_state_set_config_ok(true);
    app_ui_set_config(&s_config);
    if(!setup && app_templates_init()!=ESP_OK)app_events_add(APP_EVENT_WARNING,"Sablonkezelő nem indult; alapnézet aktív.");
    app_events_add(APP_EVENT_INFO, "Konfiguráció betöltve.");

    /* Hálózati ESP-IDF infrastruktúra inicializálása. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const bool slave = s_config.data_recept_mode == APP_DATA_SLAVE;
    if (setup) {
        err = app_setup_wifi_provision(&s_config);
        if (err != ESP_OK) fatal_forever("Wi-Fi beállítási hiba", esp_err_to_name(err));
        while (app_boot_config_save_setup_wifi(&s_config) != ESP_OK) {
            app_events_add(APP_EVENT_WARNING, "A Wi-Fi profil belső mentését újrapróbáljuk.");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
        app_setup_wifi_set_phase(APP_SETUP_WAIT_TIME);
    } else if (!slave) {
        app_ui_show_boot(APP_BOOT_WIFI, NULL);
        err = app_wifi_start(&s_config);
        if (err != ESP_OK) fatal_forever("Wi-Fi indítási hiba", esp_err_to_name(err));
        while (!app_wifi_wait_connected(pdMS_TO_TICKS(1000))) {
            /* A Wi-Fi sor az első tényleges IP-címig marad aktív. */
        }
    }

    /*
     * Az NTP szinkron indulási előfeltétel, mert a napi statisztika
     * helyi dátumhoz és időhöz kötött.
     */
    if (!setup) app_ui_show_boot(APP_BOOT_NTP, NULL);
    err = app_time_init(&s_config);
    if (err != ESP_OK) {
        fatal_forever(
            cfg_tr("NTP inicializálási hiba", "NTP initialization error"),
            esp_err_to_name(err));
    }

    err = setup ? ESP_OK : app_ble_seed_start(&s_config);
    if (err != ESP_OK) {
        if (slave)
            fatal_forever("Bluetooth inicializálási hiba", esp_err_to_name(err));
        app_events_add(APP_EVENT_WARNING, "BLE adat seed indítási hiba: %s", esp_err_to_name(err));
    }
    if (slave) {
        app_ui_show_boot(APP_BOOT_NTP, cfg_tr("Master keresése és Bluetooth időszinkron...", "Searching for master and synchronizing Bluetooth time..."));
        app_runtime_state_t runtime;
        do {
            vTaskDelay(pdMS_TO_TICKS(250));
            app_state_get_runtime(&runtime);
        } while (!runtime.time_synced);
        /* Az óracsomag csak a hitelesített profil tartós átvétele után érkezhet. */
        app_ble_profile_apply(&s_config);
        if (s_config.wifi.ssid[0] && app_wifi_start(&s_config) != ESP_OK)
            app_events_add(APP_EVENT_WARNING, "Az átvett Slave Wi-Fi nem indult el.");
    }
    while (!slave && (err = app_time_sync_once()) != ESP_OK) {
        char message[160];
        snprintf(
            message,
            sizeof(message),
            cfg_tr(
                "Az időszinkron nem sikerült (%s). Újrapróbálkozás %lu másodperc múlva.",
                "Time synchronization failed (%s). Retrying in %lu seconds."),
            esp_err_to_name(err),
            (unsigned long)s_config.time.startup_retry_seconds);

        if (!setup) app_ui_show_boot(APP_BOOT_NTP, message);
        vTaskDelay(pdMS_TO_TICKS(
            s_config.time.startup_retry_seconds * 1000U));
    }

    app_events_add(APP_EVENT_INFO, slave ? "Bluetooth master időszinkron sikeres." : "NTP időszinkron sikeres.");

    /* Az új tájolás, a konfiguráló web és egy Slave-profil is kérhet újraindítást.
     * A próbakép ekkorra teljesítette az ellenőrzés összes feltételét. */
    app_ota_wait_for_validation();
    if (rotation_restart_pending) esp_restart();

    /* Első konfigurálás alatt csak a helyi admin fut. A böngészős mentés
     * és az újraindítás után indul el a kiválasztott adatforrás. */
    if (setup) {
        while (app_web_start(&s_config) != ESP_OK)
            vTaskDelay(pdMS_TO_TICKS(5000));
        app_setup_wifi_set_phase(APP_SETUP_WEB);
        for (;;) vTaskDelay(pdMS_TO_TICKS(60000));
    }

    if (s_config.data_recept_mode != APP_DATA_INVERTER)
        app_ui_show_boot(APP_BOOT_MQTT, NULL);

    /* A helyi Otthon-modell a legelső párhuzamos UI- vagy webes olvasás
     * előtt épül fel. A Miniserver elérését már a külön worker intézi. */
    if (s_config.data_recept_mode == APP_DATA_LOXONE) {
        if (app_loxone_audio_init() != ESP_OK)
            app_events_add(APP_EVENT_WARNING, "Az Audio előkészítése átmenetileg sikertelen.");
        s_retry_loxone = app_loxone_start(s_config.display.touch_wake_seconds) != ESP_OK;
        if (s_retry_loxone)
            app_events_add(APP_EVENT_ERROR, "A Loxone kliens előkészítése sikertelen.");
    }
    if (app_ui_start_runtime() != ESP_OK)
        fatal_forever("Kijelzőhiba", "A kijelző feladata nem indítható.");

    /* A mai és tegnapi statisztika betöltése / létrehozása. */
    if (app_stats_init(&s_config) != ESP_OK)
        app_events_add(APP_EVENT_WARNING, "A statisztikai tárhely nem érhető el; az élő adatok továbbra is megjelennek.");
    s_retry_ingest = app_ingest_init(s_config.data_recept_mode) != ESP_OK;
    if (s_retry_ingest)
        app_events_add(APP_EVENT_ERROR, "Az adatfogadó nem indult el.");
    if (app_storage_start_recovery_task() != ESP_OK)
        app_events_add(APP_EVENT_WARNING, "Az SD helyreállítási feladata nem indult el.");
    if (app_stats_start_task() != ESP_OK)
        app_events_add(APP_EVENT_WARNING, "A statisztika karbantartása nem indult el.");
    if (app_time_start_background_task() != ESP_OK)
        app_events_add(APP_EVENT_WARNING, "Az idő háttérszinkronja nem indult el.");
    if (app_ota_start_auto_update(&s_config) != ESP_OK)
        app_events_add(APP_EVENT_WARNING, "Az automatikus frissítés nem indult el.");
    s_retry_web = (!slave || s_config.wifi.ssid[0]) && app_web_start(&s_config) != ESP_OK;
    if (s_retry_web)
        app_events_add(APP_EVENT_ERROR, "A webfelület nem indult el.");
    s_retry_mqtt = s_config.data_recept_mode == APP_DATA_MQTT && s_config.mqtt.broker[0] &&
        app_mqtt_start(&s_config) != ESP_OK;
    if (s_retry_mqtt)
        app_events_add(APP_EVENT_ERROR, "Az MQTT kliens előkészítése sikertelen.");
    app_events_add(APP_EVENT_INFO, "Háttérszolgáltatások indítása befejeződött. Szabad DMA-memória: %u B (legnagyobb blokk %u B).",
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    /* Belső beállítás-tartalék: induláskor egyszer összeveti az SD-vel, és csak
     * eltérésnél ír. SD nélküli Slave-nél nincs mit menteni. */
    if (trusted_sd && app_backup_flash_start() != ESP_OK)
        app_events_add(APP_EVENT_WARNING, "A belső beállítás-tartalék feladata nem indult el.");
    /* Az előre dekódolt sablonképek a háttérben, lassú ütemben készülnek el. */
    if (!cardless && !s_config.energy_flow_disabled &&
        app_templates_raw_start() != ESP_OK)
        app_events_add(APP_EVENT_WARNING, "A sablonképek előkészítő feladata nem indult el.");
    if (slave) app_ble_profile_boot_complete();

}

void app_main(void)
{
    /* A boot helyi pufferei a visszatéréssel felszabadulnak. A noinline
     * határt és az itt megmaradó kis keretet a valódi ESP-build ellenőrzi. */
    app_boot();
    int64_t next_retry = esp_timer_get_time() + 10000000LL;
    for (;;) {
        if (s_config.data_recept_mode == APP_DATA_LOXONE && !s_retry_web && !s_retry_ingest && !s_retry_loxone) {
            /* A natív Audio-kapcsolat a már meglévő 10 KiB-os main vermen él. */
            vTaskPrioritySet(NULL, 3);
            app_loxone_audio_run();
        }
        if (s_config.data_recept_mode == APP_DATA_INVERTER) app_inverter_tick();
        if (esp_timer_get_time() >= next_retry) {
            if (s_retry_web) s_retry_web = app_web_start(&s_config) != ESP_OK;
            if (s_retry_ingest) s_retry_ingest = app_ingest_init(s_config.data_recept_mode) != ESP_OK;
            if (s_retry_mqtt) s_retry_mqtt = app_mqtt_start(&s_config) != ESP_OK;
            if (s_retry_loxone) s_retry_loxone = app_loxone_start(s_config.display.touch_wake_seconds) != ESP_OK;
            next_retry = esp_timer_get_time() + 10000000LL;
        }
        vTaskDelay(pdMS_TO_TICKS(s_config.data_recept_mode == APP_DATA_INVERTER ? 250 : 10000));
    }
}
