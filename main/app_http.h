#pragma once
#include <stdbool.h>
#include "esp_http_server.h"

/* A paraméterezett application/json médiatípust is elfogadja. */
bool app_http_is_json_request(httpd_req_t *req);
esp_err_t app_http_register(httpd_handle_t server);
