#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct esp_netif esp_netif_t;
typedef struct { uint32_t addr; } test_ip4_t;
typedef struct { test_ip4_t ip, netmask, gw; } esp_netif_ip_info_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
esp_err_t esp_netif_get_ip_info(esp_netif_t *net, esp_netif_ip_info_t *out);
