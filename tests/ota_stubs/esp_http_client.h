#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct http_client *esp_http_client_handle_t;
#define HTTP_EVENT_ON_HEADER 1
typedef struct
{
    int event_id;
    void *user_data;
    char *header_key;
    char *header_value;
} esp_http_client_event_t;
typedef struct
{
    const char *url;
    esp_err_t (*crt_bundle_attach)(void *);
    int timeout_ms, buffer_size, buffer_size_tx;
    bool disable_auto_redirect;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
    const char *user_agent;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t
esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int length);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c);
int esp_http_client_get_status_code(esp_http_client_handle_t c);
int64_t esp_http_client_get_content_length(esp_http_client_handle_t c);
int esp_http_client_read(esp_http_client_handle_t c, char *buffer, int size);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);
esp_err_t esp_http_client_get_user_data(esp_http_client_handle_t c, void **data);
