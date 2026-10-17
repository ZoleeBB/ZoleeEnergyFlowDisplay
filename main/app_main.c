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
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "app_display_boot.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "app_events.h"
#include "app_mqtt.h"
#include "app_ingest.h"
#include "app_ota.h"
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
static app_display_boot_state_t s_boot_orientation = {.rotation_degrees=270};
static uint16_t s_active_rotation = 270;

/* Az NVS mentés csak a tájolás korai indításához és a bootloop elleni védelemhez kell.
 * A felhasználói beállítás továbbra is a config.json-ban van. */
static esp_err_t orientation_read(app_display_boot_state_t *state)
{
    nvs_handle_t h;
    esp_err_t err=nvs_open("display_ui",NVS_READONLY,&h);
    if(err==ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if(err!=ESP_OK) return err;
    (void)nvs_get_u16(h,"rotation",&state->rotation_degrees);
    (void)nvs_get_u8(h,"trial",&state->trial);
    (void)nvs_get_u8(h,"blocked",&state->blocked);
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
    if(err==ESP_OK) err=nvs_set_u8(h,"trial",state->trial);
    if(err==ESP_OK) err=nvs_set_u8(h,"blocked",state->blocked);
    if(err==ESP_OK) err=nvs_commit(h);
    nvs_close(h);
    return err;
}

/* Csak a már stabilan frissülő 90 fokos kijelzőt tekintjük sikeresnek.
 * Ha előbb összeomlik, a következő boot 270-re tér vissza. */
static void orientation_verified_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(15000));
    app_display_boot_state_t current={.rotation_degrees=270};
    if(orientation_read(&current)==ESP_OK &&
       current.rotation_degrees==90 && current.trial && !current.blocked) {
        current.trial=0;
        esp_err_t err=orientation_write(&current);
        ESP_LOGI(TAG,"90-degree orientation trial %s",
                 err==ESP_OK?"verified":"could not be persisted");
    }
    vTaskDelete(NULL);
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

    /* Az első UI-képkocka előtt kiválasztjuk az NVS-ben eltárolt tájolást.
     * Sikertelen korábbi 90 fokos boot után visszaállunk 270-re. */
    if(orientation_read(&s_boot_orientation)!=ESP_OK)
        app_events_add(APP_EVENT_WARNING,"Tájolási NVS nem olvasható; 270 fokkal indulunk.");
    const bool failed_trial=s_boot_orientation.rotation_degrees==90 && s_boot_orientation.trial;
    s_active_rotation=app_display_boot_select(&s_boot_orientation);
    if((s_active_rotation==90 || failed_trial) &&
       orientation_write(&s_boot_orientation)!=ESP_OK) {
        s_active_rotation=270; /* NVS védelem nélkül 90 fokban nem indulunk. */
        app_events_add(APP_EVENT_WARNING,"A tájolási biztonsági állapot nem menthető; 270 fok.");
    }
    if(failed_trial)
        app_events_add(APP_EVENT_WARNING,"A 90 fokos kijelzőindítás korábban meghiúsult, biztonsági 270 fok.");
    ESP_LOGI(TAG,"Display startup rotation: %u degrees",(unsigned)s_active_rotation);

    /* Eredeti indulás: a kijelző már az SD-kártya ellenőrzését is mutatja. */
    if (app_ui_init(s_active_rotation) != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed");
        /* A sikertelen 90 fokos kísérletet a következő indulás letiltja. */
        if(s_active_rotation==90) esp_restart();
        /* Az OTA-rollback validáció tovább futhat. */
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    app_ui_show_boot(APP_BOOT_SD, NULL);
    esp_err_t err = app_storage_boot_mount();
    if (err != ESP_OK) {
        fatal_forever(
            "SD-kártya hiba",
            "A rendszer nem tudja elérni az SD-kártyát. Ellenőrizd a kártyát, majd indítsd újra az eszközt.");
    }
    app_ui_show_boot(APP_BOOT_CONFIG, NULL);
    char config_error[256] = {0};
    /* Félbemaradt webes konfigurációmentés helyreállítása. */
    if (access(CONFIG_PATH,F_OK)!=0 && access(CONFIG_BACKUP_PATH,F_OK)==0) {
        if (rename(CONFIG_BACKUP_PATH,CONFIG_PATH)==0)
            app_events_add(APP_EVENT_WARNING,"Konfiguráció helyreállítva a mentésből.");
    }
    err = app_config_load_file(CONFIG_PATH,&s_config,config_error,sizeof(config_error));
    if (err != ESP_OK) fatal_forever("Konfigurációs hiba",config_error);

    /* Az SD-n tárolt konfiguráció a hiteles forrás. Változás esetén az NVS
     * gyorsindítási másolat frissül, és a kijelző az új beállítással újraindul.
     * Nem hívjuk az LVGL futás közbeni rotation API-ját. */
    if(app_display_boot_sync(&s_boot_orientation,s_config.display.rotation_degrees)) {
        if(orientation_write(&s_boot_orientation)==ESP_OK) {
            ESP_LOGI(TAG,"New display orientation saved; controlled restart");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        } else {
            app_events_add(APP_EVENT_WARNING,"Tájolási NVS írása sikertelen; jelenlegi állás marad.");
        }
    }
    if(s_boot_orientation.blocked && s_config.display.rotation_degrees==90)
        app_events_add(APP_EVENT_WARNING,"90 fokos mód letiltva korábbi kijelzőhiba után. Válassz 270 fokot a beállításokban.");
    if(s_active_rotation==90 &&
       xTaskCreate(orientation_verified_task,"orientation_ok",3072,NULL,3,NULL)!=pdPASS)
        app_events_add(APP_EVENT_WARNING,"90 fokos kijelző ellenőrző feladat nem indult el.");

    app_state_set_data_mode(s_config.data_recept_mode);
    app_state_set_config_ok(true);
    app_ui_set_config(&s_config);
    if(app_templates_init()!=ESP_OK)app_events_add(APP_EVENT_WARNING,"Sablonkezelő nem indult; alapnézet aktív.");
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

    if (app_ingest_init(s_config.data_recept_mode) != ESP_OK)
        fatal_forever(cfg_tr("Memóriahiba", "Memory error"), cfg_tr("Az adatfogadó nem indítható.", "Data receiver initialization failed"));

    /* Háttérfeladatok indulása. */
    if(app_storage_start_recovery_task()!=ESP_OK ||
       app_stats_start_task()!=ESP_OK || app_time_start_background_task()!=ESP_OK){
        fatal_forever(cfg_tr("Memóriahiba","Memory error"),
            cfg_tr("A háttérfeladat nem indítható.","A required background task could not start."));
    }

    /*
     * A web/OTA felület hibája MQTT módban nem végzetes; HTTP módban
     * a működő adatfogadó nélkül nem léphetünk tovább.
     */
    if (s_config.data_recept_mode == APP_DATA_HTTP) app_ui_show_boot(APP_BOOT_MQTT, NULL);
    err = app_web_start(&s_config);
    if (err != ESP_OK) {
        app_events_add(
            APP_EVENT_ERROR,
            "Web/OTA felület nem indult el: %s",
            esp_err_to_name(err));
        ESP_LOGE(TAG, "Web server start failed: %s", esp_err_to_name(err));
        if (s_config.data_recept_mode == APP_DATA_HTTP)
            fatal_forever(cfg_tr("HTTP inicializálási hiba", "HTTP initialization error"), esp_err_to_name(err));
    }

    if (s_config.data_recept_mode == APP_DATA_MQTT) {
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

    }
    app_ui_show_boot(APP_BOOT_WAIT_DATA, NULL);

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

