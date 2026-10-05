/*
 * Szálbiztos közös runtime állapot.
 *
 * Az MQTT, Wi-Fi, UI, OTA és web modulok ezen keresztül osztják meg
 * a pillanatnyi kapcsolat- és mérési állapotokat.
 */
#include "app_state.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mutex;
static app_runtime_state_t s_runtime;
static app_energy_sample_t s_sample;

/* Közös állapot inicializálása. */
void app_state_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }

    memset(&s_runtime, 0, sizeof(s_runtime));
    memset(&s_sample, 0, sizeof(s_sample));
}

/* Belső zárolási segédfüggvények. */
static void lock(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mutex);
}

/* SD-kártya állapot frissítése. */
void app_state_set_sd_ok(bool value)
{
    lock();
    s_runtime.sd_ok = value;
    unlock();
}

/* Konfiguráció érvényességének jelzése az OTA ellenőrzés felé is. */
void app_state_set_config_ok(bool value)
{
    lock();
    s_runtime.config_ok = value;
    unlock();
}

/* Wi-Fi kapcsolati állapot frissítése. */
void app_state_set_wifi_connected(bool value)
{
    lock();
    s_runtime.wifi_connected = value;
    unlock();
}

/* Sikeres időszinkron jelzése. */
void app_state_set_time_synced(bool value)
{
    lock();
    s_runtime.time_synced = value;
    unlock();
}

/*
 * MQTT brokerkapcsolat frissítése.
 * Kapcsolódáskor monotonic időbélyeget is rögzítünk az első payload timeoutjához.
 */
void app_state_set_mqtt_connected(bool value)
{
    lock();
    s_runtime.mqtt_connected = value;

    if (value) {
        s_runtime.mqtt_connected_monotonic_us = esp_timer_get_time();
    }

    unlock();
}

/*
 * Az utolsó teljesen validált energiaminta tárolása.
 * Hibás payload soha nem jut el idáig.
 */
void app_state_set_sample(const app_energy_sample_t *sample)
{
    if (!sample) {
        return;
    }

    lock();

    s_sample = *sample;
    s_runtime.has_valid_sample = true;
    s_runtime.last_valid_packet_monotonic_us =
        sample->received_monotonic_us;
    s_runtime.last_valid_packet_epoch_s =
        sample->received_epoch_s;

    unlock();
}

/* A runtime állapot atomi másolatának lekérése. */
void app_state_get_runtime(app_runtime_state_t *out)
{
    if (!out) {
        return;
    }

    lock();
    *out = s_runtime;
    unlock();
}

/* Az utolsó érvényes minta lekérése; false esetén még nem érkezett valid adat. */
bool app_state_get_sample(app_energy_sample_t *out)
{
    lock();

    bool valid = s_runtime.has_valid_sample;
    if (out && valid) {
        *out = s_sample;
    }

    unlock();
    return valid;
}


void app_state_set_data_mode(app_data_mode_t mode)
{ lock(); s_runtime.data_recept_mode = mode; unlock(); }
void app_state_set_http_ready(bool ready)
{
    lock();
    if (ready && !s_runtime.http_ready) s_runtime.http_ready_monotonic_us = esp_timer_get_time();
    s_runtime.http_ready = ready;
    unlock();
}
