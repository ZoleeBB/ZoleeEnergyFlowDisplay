/*
 * Az alkalmazás fő belépési pontja.
 *
 * Feladata az alrendszerek meghatározott sorrendű inicializálása:
 * NVS -> kijelző -> SD -> konfiguráció -> Wi-Fi -> NTP -> statisztika
 * -> web/OTA -> MQTT -> normál futási mód.
 */
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "app_events.h"
#include "app_mqtt.h"
#include "app_ota.h"
#include "app_state.h"
#include "app_stats.h"
#include "app_storage.h"
#include "app_time.h"
#include "app_ui.h"
#include "app_web.h"
#include "app_wifi.h"

/* A konfiguráció fix helye a microSD gyökerében. */
#define CONFIG_PATH APP_SD_MOUNT_POINT "/config.json"

static const char *TAG = "app";
static app_config_t s_config;

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
void app_main(void)
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
    ESP_ERROR_CHECK(app_ota_start_validation_task());

    /* A kijelző már a további indulási lépések állapotát is mutatja. */
    if (app_ui_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed");

        /*
         * Nem rebootolunk azonnal: ha ez próba OTA image, a validációs task
         * három perc után szabályosan érvényteleníti és visszagörgeti.
         */
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* SD-kártya nélkül nincs konfiguráció, ezért ez blokkoló feltétel. */
    app_ui_show_boot(APP_BOOT_SD, NULL);
    esp_err_t err = app_storage_boot_mount();
    if (err != ESP_OK) {
        fatal_forever(
            "SD-kártya hiba",
            "A rendszer nem tudja elérni az SD-kártyát. Ellenőrizd a kártyát, majd indítsd újra az eszközt.");
    }

    /* A konfigurációt kizárólag induláskor olvassuk be. */
    app_ui_show_boot(APP_BOOT_CONFIG, NULL);
    char config_error[256];
    err = app_config_load_file(
        CONFIG_PATH,
        &s_config,
        config_error,
        sizeof(config_error));

    if (err != ESP_OK) {
        fatal_forever("Konfigurációs hiba", config_error);
    }

    app_state_set_config_ok(true);
    app_ui_set_config(&s_config);
    app_events_add(APP_EVENT_INFO, "Konfiguráció betöltve.");

    /* Hálózati ESP-IDF infrastruktúra inicializálása. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Wi-Fi kapcsolat létrehozása; induláskor addig várunk, amíg fel nem áll. */
    app_ui_show_boot(APP_BOOT_WIFI, NULL);
    err = app_wifi_start(&s_config);
    if (err != ESP_OK) {
        fatal_forever(
            cfg_tr("Wi-Fi inicializálási hiba", "Wi-Fi initialization error"),
            esp_err_to_name(err));
    }

    while (!app_wifi_wait_connected(pdMS_TO_TICKS(1000))) {
        /* Az eseménykezelő és a reconnect task végzi a tényleges kapcsolódást. */
    }

    /*
     * Az NTP szinkron indulási előfeltétel, mert a napi statisztika
     * helyi dátumhoz és időhöz kötött.
     */
    app_ui_show_boot(APP_BOOT_NTP, NULL);
    err = app_time_init(&s_config);
    if (err != ESP_OK) {
        fatal_forever(
            cfg_tr("NTP inicializálási hiba", "NTP initialization error"),
            esp_err_to_name(err));
    }

    while ((err = app_time_sync_once()) != ESP_OK) {
        char message[160];
        snprintf(
            message,
            sizeof(message),
            cfg_tr(
                "Az időszinkron nem sikerült (%s). Újrapróbálkozás %lu másodperc múlva.",
                "Time synchronization failed (%s). Retrying in %lu seconds."),
            esp_err_to_name(err),
            (unsigned long)s_config.time.startup_retry_seconds);

        app_ui_show_boot(APP_BOOT_NTP, message);
        vTaskDelay(pdMS_TO_TICKS(
            s_config.time.startup_retry_seconds * 1000U));
    }

    app_events_add(APP_EVENT_INFO, "NTP időszinkron sikeres.");

    /* A mai és tegnapi statisztika betöltése / létrehozása. */
    err = app_stats_init(&s_config);
    if (err != ESP_OK) {
        fatal_forever(
            cfg_tr("SD-kártya hiba", "SD card error"),
            cfg_tr(
                "A statisztikai tárhely nem inicializálható. Ellenőrizd az SD-kártyát, majd indítsd újra az eszközt.",
                "The statistics storage cannot be initialized. Check the SD card, then restart the device."));
    }

    /* Háttérfeladatok indulása. */
    if(app_storage_start_recovery_task()!=ESP_OK ||
       app_stats_start_task()!=ESP_OK || app_time_start_background_task()!=ESP_OK){
        fatal_forever(cfg_tr("Memóriahiba","Memory error"),
            cfg_tr("A háttérfeladat nem indítható.","A required background task could not start."));
    }

    /*
     * A helyi web/OTA felület hasznos, de nem blokkolja az energiamonitor
     * alapműködését, ezért hibája csak eseményként kerül naplózásra.
     */
    err = app_web_start(&s_config);
    if (err != ESP_OK) {
        app_events_add(
            APP_EVENT_ERROR,
            "Web/OTA felület nem indult el: %s",
            esp_err_to_name(err));
        ESP_LOGE(TAG, "Web server start failed: %s", esp_err_to_name(err));
    }

    /* MQTT kliens és brokerkapcsolat felépítése. */
    app_ui_show_boot(APP_BOOT_MQTT, NULL);
    err = app_mqtt_start(&s_config);
    if (err != ESP_OK) {
        fatal_forever(
            cfg_tr("MQTT inicializálási hiba", "MQTT initialization error"),
            esp_err_to_name(err));
    }

    while (!app_mqtt_wait_connected(pdMS_TO_TICKS(1000))) {
        /* A LVGL timer mutatja a Wi-Fi/MQTT hibát; a kliens magától újracsatlakozik. */
    }

    /*
     * Az első érvényes payloadra legfeljebb a konfigurált adat-timeoutig várunk.
     * Ha addig nem jön adat, a runtime UI már a megfelelő hibaoldalt kezeli.
     */
    app_ui_show_boot(APP_BOOT_WAIT_DATA, NULL);
    TickType_t wait_start = xTaskGetTickCount();

    while (true) {
        app_runtime_state_t runtime;
        app_state_get_runtime(&runtime);

        if (runtime.has_valid_sample) {
            break;
        }

        if ((xTaskGetTickCount() - wait_start) >=
            pdMS_TO_TICKS(s_config.mqtt.data_timeout_seconds * 1000U)) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    /* Innentől a kijelző runtime task kezeli a dashboard és hibaoldalak közti váltást. */
    if(app_ui_start_runtime()!=ESP_OK){
        fatal_forever(cfg_tr("Memóriahiba","Memory error"),
            cfg_tr("A kijelző feladata nem indítható.","The display task could not start."));
    }
    app_events_add(APP_EVENT_INFO, "Rendszerindítás befejeződött.");

    /* Az app_main életben marad, a tényleges munka külön FreeRTOS taskokban fut. */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

