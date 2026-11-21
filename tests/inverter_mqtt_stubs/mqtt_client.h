#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
typedef struct mqtt_client *esp_mqtt_client_handle_t;
typedef const char *esp_event_base_t;
typedef struct { const char *filter; int qos; } esp_mqtt_topic_t;
typedef struct {
    char *topic, *data;
    int retain, topic_len, data_len, total_data_len, current_data_offset;
} *esp_mqtt_event_handle_t, esp_mqtt_event_t;
enum { MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_SUBSCRIBED, MQTT_EVENT_DATA };
enum { MQTT_TRANSPORT_OVER_SSL, MQTT_TRANSPORT_OVER_TCP, ESP_EVENT_ANY_ID = -1 };
typedef struct {
    struct {
        struct { const char *hostname; int port, transport; } address;
        struct { const char *certificate; esp_err_t (*crt_bundle_attach)(void *); } verification;
    } broker;
    struct { const char *username; struct { const char *password; } authentication; } credentials;
    struct { int timeout_ms; bool disable_auto_reconnect; } network;
    struct { int stack_size; } task;
    struct { int size, out_size; } buffer;
} esp_mqtt_client_config_t;
typedef void (*event_fn)(void *, esp_event_base_t, int32_t, void *);
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *cfg);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t c, int id, event_fn fn, void *arg);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t c);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t c);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t c);
int esp_mqtt_client_subscribe_multiple(esp_mqtt_client_handle_t c, const esp_mqtt_topic_t *topics, int count);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t c, const char *topic, const char *data, int len, int qos, int retain);
