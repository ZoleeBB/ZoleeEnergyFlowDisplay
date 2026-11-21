#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct http_client *esp_http_client_handle_t;
#define HTTP_EVENT_ON_HEADER 1
#define HTTP_EVENT_ON_DATA 2
typedef enum { HTTP_METHOD_GET, HTTP_METHOD_POST } esp_http_client_method_t;
typedef struct
{
    int event_id;
    esp_http_client_handle_t client;
    void *user_data;
    char *header_key;
    char *header_value;
    char *data;
    int data_len;
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
    const char *cert_pem;
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

int esp_http_client_get_socket(esp_http_client_handle_t client);
int esp_http_client_get_errno(esp_http_client_handle_t client);
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t client,
                                                       int *tls_code, int *flags);

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, int length);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
