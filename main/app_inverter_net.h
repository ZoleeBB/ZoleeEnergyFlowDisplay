#pragma once
#include <stddef.h>
#include "cJSON.h"

/* A hívó a JSON-t cJSON_Delete-tel engedi el. A tokenfogadás csak a Huawei belépéshez kell. */
cJSON *app_inverter_json_request(const char *url, const char *method, const char *body,
                                 const char *const headers[][2], size_t header_count,
                                 char *response_token, size_t token_capacity,
                                 const char *trusted_pem);
