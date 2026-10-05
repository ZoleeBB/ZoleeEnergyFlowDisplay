/*
 * MQTT kapcsolat és darabolt payload-összeállítás.
 *
 * A firmware QoS 1-gyel feliratkozik az egyetlen konfigurált topicra.
 * Csak a teljes, validált JSON csomag frissítheti az állapotot és statisztikát.
 */
#include "app_mqtt.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "freertos/event_groups.h"

#include "app_events.h"
#include "app_ingest.h"
#include "app_state.h"
#include "app_stats.h"

/* EventGroup bit a brokerkapcsolat jelzésére. */
#define MQTT_CONNECTED_BIT BIT0

/* Védelmi korlát egyetlen bejövő JSON csomagra. */
#define MQTT_MAX_PAYLOAD_BYTES 4096

/* Fix kliensazonosító a broker felé. */
#define MQTT_CLIENT_ID "zolee-energy-flow-display"

static const char *TAG = "mqtt";

static app_mqtt_config_t s_cfg;
static esp_mqtt_client_handle_t s_client;
static EventGroupHandle_t s_events;

/* Az ESP-MQTT darabokban is adhat át payloadot, ezért saját összeállító puffer kell. */
static char s_rx[MQTT_MAX_PAYLOAD_BYTES + 1];
static char s_rx_topic[APP_TOPIC_MAX];
static int s_rx_total;

/* Ismétlődő hibákat legfeljebb percenként teszünk a RAM eseménynaplóba. */
static int64_t s_last_invalid_event_us;
static int64_t s_last_transport_event_us;

/* Pontos topic-egyezés ellenőrzése hossz alapján is. */
static bool topic_matches(const char *topic, int topic_len)
{
    size_t expected_len = strlen(s_cfg.topic);

    return topic &&
        topic_len == (int)expected_len &&
        memcmp(topic, s_cfg.topic, expected_len) == 0;
}

/* Folyamatban lévő darabolt MQTT csomag alaphelyzetbe állítása. */
static void reset_rx(void)
{
    s_rx_total = 0;
    s_rx[0] = 0;
    s_rx_topic[0] = 0;
}

/* Egy teljesen összeállított MQTT csomag validálása és továbbítása. */
static void process_complete_payload(void)
{
    char error[160];

    if (app_ingest_process(
            APP_DATA_MQTT, s_rx,
            (size_t)s_rx_total,
            error,
            sizeof(error)) == ESP_OK) {

        /* Csak teljesen érvényes payload frissítheti a rendszert. */
        return;
    }

    ESP_LOGW(TAG, "Rejected MQTT payload: %s", error);

    int64_t now = esp_timer_get_time();
    if (now - s_last_invalid_event_us > 60000000LL) {
        app_events_add(
            APP_EVENT_WARNING,
            "Hibás MQTT adatcsomag elutasítva (mező: %s).",
            strrchr(error,':')?strrchr(error,':')+1:"JSON");
        s_last_invalid_event_us = now;
    }
}

/*
 * MQTT_EVENT_DATA kezelése.
 * A fragmentumokat current_data_offset alapján ugyanabba a pufferbe illesztjük.
 */
static void on_data(esp_mqtt_event_handle_t event)
{
    if (event->current_data_offset == 0) {
        reset_rx();

        if (!topic_matches(event->topic, event->topic_len)) {
            return;
        }

        if (event->total_data_len <= 0 ||
            event->total_data_len > MQTT_MAX_PAYLOAD_BYTES) {

            int64_t now = esp_timer_get_time();
            if (now - s_last_invalid_event_us > 60000000LL) {
                app_events_add(
                    APP_EVENT_WARNING,
                    "MQTT payload mérete érvénytelen: %d byte",
                    event->total_data_len);
                s_last_invalid_event_us = now;
            }
            return;
        }

        size_t topic_len = (size_t)event->topic_len;
        if (topic_len >= sizeof(s_rx_topic)) {
            topic_len = sizeof(s_rx_topic) - 1;
        }

        memcpy(s_rx_topic, event->topic, topic_len);
        s_rx_topic[topic_len] = 0;
        s_rx_total = event->total_data_len;
    }

    /*
     * Ha az első fragmentum nem a várt topichoz tartozott, nincs aktív csomag,
     * ezért a későbbi darabokat is eldobhatjuk.
     */
    if (s_rx_total <= 0 || !s_rx_topic[0]) {
        return;
    }

    /* Sérült vagy túlcsorduló fragmentum nem írhat a fogadóbufferen kívülre. */
    if (event->current_data_offset < 0 ||
        event->data_len < 0 ||
        event->current_data_offset + event->data_len > s_rx_total) {
        reset_rx();
        return;
    }

    memcpy(
        s_rx + event->current_data_offset,
        event->data,
        (size_t)event->data_len);

    /* Az utolsó fragmentum után lezárjuk és validáljuk a JSON-t. */
    if (event->current_data_offset + event->data_len == s_rx_total) {
        s_rx[s_rx_total] = 0;
        process_complete_payload();
        reset_rx();
    }
}

/* Az ESP-MQTT központi eseménykezelője. */
static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event =
        (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            app_state_set_mqtt_connected(true);
            xEventGroupSetBits(s_events, MQTT_CONNECTED_BIT);

            /*
             * A specifikáció szerint fixen QoS 1-gyel iratkozunk fel.
             * Sikertelen subscribe esetén a brokerkapcsolat ugyan él, de ezt
             * külön hibaként rögzítjük.
             */
            int message_id =
                esp_mqtt_client_subscribe(s_client, s_cfg.topic, 1);

            if (message_id < 0) {
                ESP_LOGE(
                    TAG,
                    "MQTT subscribe failed for topic %s",
                    s_cfg.topic);
                app_events_add(
                    APP_EVENT_ERROR,
                    "MQTT feliratkozás sikertelen: %s",
                    s_cfg.topic);
            } else {
                ESP_LOGI(
                    TAG,
                    "Subscribed to %s, msg_id=%d",
                    s_cfg.topic,
                    message_id);
            }

            app_events_add(
                APP_EVENT_INFO,
                "MQTT broker kapcsolat létrejött.");
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            app_state_set_mqtt_connected(false);
            xEventGroupClearBits(s_events, MQTT_CONNECTED_BIT);
            app_events_add(
                APP_EVENT_WARNING,
                "MQTT broker kapcsolat megszakadt.");
            reset_rx();
            break;

        case MQTT_EVENT_DATA:
            on_data(event);
            break;

        case MQTT_EVENT_ERROR:
            if (event->error_handle) {
                ESP_LOGW(
                    TAG,
                    "MQTT error type=%d esp=%s sock=%d",
                    event->error_handle->error_type,
                    esp_err_to_name(
                        event->error_handle->esp_tls_last_esp_err),
                    event->error_handle->esp_transport_sock_errno);

                int64_t now = esp_timer_get_time();
                if (now - s_last_transport_event_us > 60000000LL) {
                    app_events_add(
                        APP_EVENT_WARNING,
                        "MQTT transport hiba: %s, socket=%d",
                        esp_err_to_name(
                            event->error_handle->esp_tls_last_esp_err),
                        event->error_handle->esp_transport_sock_errno);
                    s_last_transport_event_us = now;
                }
            }
            break;

        default:
            /* A többi MQTT eseményhez nincs alkalmazásszintű teendő. */
            break;
    }
}

/* MQTT kliens létrehozása és aszinkron indítása. */
esp_err_t app_mqtt_start(const app_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->data_recept_mode != APP_DATA_MQTT) return ESP_ERR_INVALID_STATE;

    if (s_client) {
        return ESP_ERR_INVALID_STATE;
    }

    s_cfg = cfg->mqtt;
    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = s_cfg.broker,
        .broker.address.port = s_cfg.port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.client_id = MQTT_CLIENT_ID,
        .network.reconnect_timeout_ms =
            (int)(s_cfg.reconnect_seconds * 1000U),
        .network.timeout_ms = 10000,
        .task.stack_size = 6144,
        .buffer.size = 2048
    };

    /* Üres felhasználónév/jelszó esetén névtelen kapcsolatot használunk. */
    if (s_cfg.username[0]) {
        mqtt_cfg.credentials.username = s_cfg.username;
    }
    if (s_cfg.password[0]) {
        mqtt_cfg.credentials.authentication.password = s_cfg.password;
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(
        s_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL);

    if (err != ESP_OK) {
        return err;
    }

    return esp_mqtt_client_start(s_client);
}

/* Blokkoló várakozás a brokerkapcsolat EventGroup bitjére. */
bool app_mqtt_wait_connected(TickType_t timeout_ticks)
{
    if (!s_events) {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_events,
        MQTT_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        timeout_ticks);

    return (bits & MQTT_CONNECTED_BIT) != 0;
}

