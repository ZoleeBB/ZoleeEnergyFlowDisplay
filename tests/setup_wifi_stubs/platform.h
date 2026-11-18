#pragma once
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/event_groups.h"

#define ESP_RETURN_ON_ERROR(call, tag, message) do { esp_err_t err_ = (call); if (err_) return err_; } while (0)
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED 10
#define ESP_ERR_WIFI_CONN 11
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_CONNECTED 2
#define WIFI_EVENT_STA_DISCONNECTED 3
#define IP_EVENT_STA_GOT_IP 4
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) ((ip)->addr & 255), (((ip)->addr >> 8) & 255), (((ip)->addr >> 16) & 255), (((ip)->addr >> 24) & 255)
#define IPADDR_NONE INADDR_NONE
#define ipaddr_addr inet_addr
#define IPADDR_TYPE_V4 0
#define ESP_NETIF_DNS_MAIN 0
#define WIFI_IF_STA 1
#define WIFI_MODE_STA 1
#define WIFI_PS_NONE 0
#define WIFI_STORAGE_RAM 0
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_INIT_CONFIG_DEFAULT() {0}
typedef struct { uint32_t addr; } wifi_ip_t;
typedef struct { wifi_ip_t ip, netmask, gw; } esp_netif_ip_info_t;
typedef struct { esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
typedef struct { struct { int type; union { wifi_ip_t ip4; } u_addr; } ip; } esp_netif_dns_info_t;
typedef int esp_netif_t;
typedef int esp_event_base_t;
typedef void (*event_fn_t)(void *, esp_event_base_t, int32_t, void *);
typedef struct {
    struct {
        uint8_t ssid[32], password[64];
        struct { int authmode; } threshold;
        struct { bool capable; } pmf_cfg;
    } sta;
} wifi_config_t;
typedef struct { uint8_t ssid[33]; int authmode, rssi; } wifi_ap_record_t;
typedef struct { uint8_t *ssid; } wifi_scan_config_t;
typedef struct { int unused; } wifi_init_config_t;
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) (*(lock) += 1)
#define portEXIT_CRITICAL(lock) (*(lock) -= 1)

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *, bool);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *, wifi_ap_record_t *);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_set_config(int, wifi_config_t *);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
esp_err_t esp_event_handler_register(int, int, event_fn_t, void *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_set_ps(int);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_err_t esp_netif_set_hostname(esp_netif_t *, const char *);
esp_err_t esp_netif_dhcpc_stop(esp_netif_t *);
esp_err_t esp_netif_set_dns_info(esp_netif_t *, int, esp_netif_dns_info_t *);
esp_err_t esp_netif_set_ip_info(esp_netif_t *, const esp_netif_ip_info_t *);
esp_err_t esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
EventBits_t xEventGroupGetBits(EventGroupHandle_t);
unsigned ulTaskNotifyTake(int, unsigned);
void xTaskNotifyGive(TaskHandle_t);
