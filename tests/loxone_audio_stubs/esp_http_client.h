#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct test_audio_http *esp_http_client_handle_t;
typedef struct
{
    int event_id;
    void *user_data;
    char *header_key;
    char *header_value;
} esp_http_client_event_t;
enum { HTTP_EVENT_ON_HEADER = 4 };
typedef struct
{
    const char *url;
    int timeout_ms;
    bool disable_auto_redirect;
    esp_err_t (*crt_bundle_attach)(void *configuration);
    esp_err_t (*event_handler)(esp_http_client_event_t *event);
    void *user_data;
    int buffer_size, buffer_size_tx;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *configuration);
esp_err_t esp_http_client_open(esp_http_client_handle_t handle, int write_length);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t handle);
int esp_http_client_get_status_code(esp_http_client_handle_t handle);
int esp_http_client_read(esp_http_client_handle_t handle, char *output, int capacity);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t handle);
esp_err_t esp_http_client_close(esp_http_client_handle_t handle);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle);
