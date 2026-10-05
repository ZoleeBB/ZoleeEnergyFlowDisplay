/*
 * Helyi időzóna, NTP indulási szinkron és háttér-újraszinkron.
 */
#include "app_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_events.h"
#include "app_state.h"
#include "app_wifi.h"

static const char *TAG = "time";

static app_time_config_t s_cfg;
static bool s_initialized;
static bool s_background_started;

/*
 * Fix UTC eltolás POSIX TZ formára alakítása.
 * A POSIX előjel fordított: UTC+1 helyi időhöz "UTC-1" tartozik.
 */
static void build_fixed_timezone(
    int offset_minutes,
    char *out,
    size_t out_len)
{
    int sign = offset_minutes >= 0 ? -1 : 1;
    int absolute =
        offset_minutes >= 0 ? offset_minutes : -offset_minutes;

    int hours = absolute / 60;
    int minutes = absolute % 60;

    if (minutes == 0) {
        snprintf(
            out,
            out_len,
            "UTC%c%d",
            sign < 0 ? '-' : '+',
            hours);
    } else {
        snprintf(
            out,
            out_len,
            "UTC%c%d:%02d",
            sign < 0 ? '-' : '+',
            hours,
            minutes);
    }
}

/* A konfigurált helyi időzóna alkalmazása a C runtime számára. */
static void apply_timezone(void)
{
    char tz[96];

    if (!s_cfg.auto_dst) {
        build_fixed_timezone(
            s_cfg.utc_offset_minutes,
            tz,
            sizeof(tz));
    } else if (strcmp(s_cfg.timezone, "Europe/Budapest") == 0) {
        /*
         * Budapest: CET/CEST.
         * DST kezdete március utolsó vasárnap 02:00,
         * vége október utolsó vasárnap 03:00.
         */
        snprintf(
            tz,
            sizeof(tz),
            "CET-1CEST,M3.5.0/2,M10.5.0/3");
    } else {
        /*
         * Egyéb értéknél a konfiguráció tartalmát POSIX TZ sztringként
         * adjuk át. A jelenlegi projekt alapértelmezése Europe/Budapest.
         */
        snprintf(tz, sizeof(tz), "%s", s_cfg.timezone);
    }

    setenv("TZ", tz, 1);
    tzset();

    ESP_LOGI(TAG, "Timezone applied: %s", tz);
}

/* SNTP kliens egyszeri inicializálása. */
esp_err_t app_time_init(const app_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_cfg = cfg->time;
    apply_timezone();

    esp_sntp_config_t sntp_cfg =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(s_cfg.ntp_server);

    /*
     * Nem indítjuk el automatikusan az SNTP szolgáltatást az init során.
     * Az első lekérést az app_time_sync_once() végzi, a későbbi kézi
     * újraindításokat pedig a konfigurálható resync_hours ütemezi.
     */
    sntp_cfg.start = false;

    esp_err_t err = esp_netif_sntp_init(&sntp_cfg);
    if (err == ESP_OK) {
        s_initialized = true;
    }

    return err;
}

/*
 * Egy NTP szinkronkísérlet.
 * A hívó kezeli az ismétlési időzítést.
 */
esp_err_t app_time_sync_once(void)
{
    if (!s_initialized || !app_wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_netif_sntp_start();

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(
            TAG,
            "SNTP start failed: %s",
            esp_err_to_name(err));
    }

    /* Egyetlen próbálkozás legfeljebb 10 másodpercig vár válaszra. */
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));

    if (err == ESP_OK) {
        app_state_set_time_synced(true);
    }

    return err;
}

/*
 * Háttér-újraszinkron.
 * Siker után a normál, órákban megadott periódus következik.
 * Sikertelenségnél rövidebb, percben megadott retry ciklus fut.
 */
static void time_sync_task(void *arg)
{
    (void)arg;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(
            s_cfg.resync_hours * 60U * 60U * 1000U));

        while (true) {
            if (!app_wifi_is_connected()) {
                vTaskDelay(pdMS_TO_TICKS(
                    s_cfg.background_retry_minutes *
                    60U *
                    1000U));
                continue;
            }

            esp_err_t err = app_time_sync_once();

            if (err == ESP_OK) {
                app_events_add(
                    APP_EVENT_INFO,
                    "NTP időszinkron sikeres.");
                break;
            }

            /*
             * Futás közben az NTP hiba nem állítja le a dashboardot:
             * az RTC/belső óra tovább fut a következő sikeres szinkronig.
             */
            app_events_add(
                APP_EVENT_WARNING,
                "NTP újraszinkron sikertelen; belső óra tovább fut.");

            vTaskDelay(pdMS_TO_TICKS(
                s_cfg.background_retry_minutes *
                60U *
                1000U));
        }
    }
}

/* A háttérszinkron task egyszeri elindítása. */
esp_err_t app_time_start_background_task(void)
{
    if(s_background_started)return ESP_OK;
    if(xTaskCreate(time_sync_task,"time_sync_task",4096,NULL,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    s_background_started=true;
    return ESP_OK;
}
