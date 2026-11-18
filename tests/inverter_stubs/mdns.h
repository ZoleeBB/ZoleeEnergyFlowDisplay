#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { uint32_t addr[4]; uint8_t zone; } esp_ip6_addr_t;
esp_err_t mdns_query_a(const char *, uint32_t, esp_ip4_addr_t *);
esp_err_t mdns_query_aaaa(const char *, uint32_t, esp_ip6_addr_t *);
