#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
typedef void *httpd_handle_t;
typedef struct { size_t content_len; void *user_ctx; } httpd_req_t;
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); void *user_ctx; } httpd_uri_t;
typedef struct { unsigned stack_size, max_open_sockets; bool lru_purge_enable; } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() (httpd_config_t){0}
#define HTTP_GET 1
#define HTTP_POST 3
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
esp_err_t httpd_start(httpd_handle_t *server, const httpd_config_t *cfg);
esp_err_t httpd_stop(httpd_handle_t server);
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *value);
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *req, const char *chunk);
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *text);
esp_err_t httpd_resp_send_err(httpd_req_t *req, int code, const char *text);
esp_err_t httpd_query_key_value(const char *body, const char *key, char *out, size_t size);
int httpd_req_recv(httpd_req_t *req, char *out, size_t size);
