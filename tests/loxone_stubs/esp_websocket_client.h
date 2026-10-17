#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
typedef void *esp_websocket_client_handle_t;
typedef const char *esp_event_base_t;
#define WEBSOCKET_EVENT_ERROR 0
#define WEBSOCKET_EVENT_CONNECTED 1
#define WEBSOCKET_EVENT_DISCONNECTED 2
#define WEBSOCKET_EVENT_DATA 3
#define WEBSOCKET_EVENT_ANY -1
/* A használt mezők a rögzített Espressif 1.6.1 API-t követik. */
typedef struct {const char *data_ptr;int data_len;bool fin;uint8_t op_code;int payload_len;int payload_offset;} esp_websocket_event_data_t;
typedef struct {const char *uri,*subprotocol,*cert_pem;bool disable_auto_reconnect;int buffer_size,task_stack,network_timeout_ms;} esp_websocket_client_config_t;
esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *);
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t,int,void (*)(void *,esp_event_base_t,int32_t,void *),void *);
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t);
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t);
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t);
bool esp_websocket_client_is_connected(esp_websocket_client_handle_t);
int esp_websocket_client_send_text(esp_websocket_client_handle_t,const char *,int,TickType_t);
