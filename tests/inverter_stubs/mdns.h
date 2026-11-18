#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { uint32_t addr[4]; uint8_t zone; } esp_ip6_addr_t;
esp_err_t mdns_query_a(const char *, uint32_t, esp_ip4_addr_t *);
esp_err_t mdns_query_aaaa(const char *, uint32_t, esp_ip6_addr_t *);

typedef struct esp_netif esp_netif_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *);
#define ESP_IPADDR_TYPE_V4 0U
typedef struct mdns_ip_addr_s {
    struct { union { esp_ip4_addr_t ip4; esp_ip6_addr_t ip6; } u_addr; uint8_t type; } addr;
    struct mdns_ip_addr_s *next;
} mdns_ip_addr_t;
typedef struct { const char *key, *value; } mdns_txt_item_t;
typedef struct mdns_result_s {
    struct mdns_result_s *next;
    esp_netif_t *esp_netif;
    char *service_type, *proto;
    mdns_txt_item_t *txt;
    uint8_t *txt_value_len;
    size_t txt_count;
    mdns_ip_addr_t *addr;
} mdns_result_t;
esp_err_t mdns_query_ptr(const char *, const char *, uint32_t, size_t, mdns_result_t **);
void mdns_query_results_free(mdns_result_t *);
