#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef struct { int method; size_t content_len; const char *uri; } httpd_req_t;
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
#define HTTP_ANY 2147483647
#define HTTP_POST 3
#define HTTP_GET 1
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_SOCK_ERR_TIMEOUT -3
esp_err_t httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *route);
esp_err_t httpd_resp_set_status(httpd_req_t *req,const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *req,const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req,const char *name,const char *value);
esp_err_t httpd_resp_send(httpd_req_t *req,const char *body,int length);
size_t httpd_req_get_hdr_value_len(httpd_req_t *req,const char *name);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req,const char *name,char *out,size_t length);
int httpd_req_recv(httpd_req_t *req,char *out,size_t length);

#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_408_REQ_TIMEOUT 408
#define HTTPD_404_NOT_FOUND 404
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
esp_err_t httpd_resp_send_chunk(httpd_req_t *req,const char *body,int length);
esp_err_t httpd_resp_sendstr(httpd_req_t *req,const char *body);
esp_err_t httpd_resp_send_err(httpd_req_t *req,int status,const char *body);
