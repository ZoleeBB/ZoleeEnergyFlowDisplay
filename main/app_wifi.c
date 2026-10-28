/*
 * Wi-Fi STA kapcsolat, opcionális statikus IPv4 és újracsatlakozás.
 */
#include "app_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"

#include "app_events.h"
#include "app_state.h"

/* EventGroup bit: az állomás IP címet kapott és használható. */
#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "wifi";

static EventGroupHandle_t s_events;
static esp_netif_t *s_sta_netif;
static app_wifi_config_t s_cfg;
static bool s_started;
static TaskHandle_t s_reconnect_task;

/* Statikus DNS szerver beállítása a STA netifre. */
static esp_err_t set_dns(esp_netif_t *netif, const char *address)
{
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ipaddr_addr(address);

    if (dns.ip.u_addr.ip4.addr == IPADDR_NONE)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
}

/* DHCP leállítása, majd a konfigurált statikus IPv4 adatok alkalmazása. */
static esp_err_t apply_static_ip(void)
{
    esp_err_t err = esp_netif_dhcpc_stop(s_sta_netif);

    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
    {
        return err;
    }

    esp_netif_ip_info_t ip = {0};
    ip.ip.addr = ipaddr_addr(s_cfg.network.ip);
    ip.netmask.addr = ipaddr_addr(s_cfg.network.subnet);
    ip.gw.addr = ipaddr_addr(s_cfg.network.gateway);

    if (ip.ip.addr == IPADDR_NONE || ip.netmask.addr == IPADDR_NONE || ip.gw.addr == IPADDR_NONE)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_sta_netif, &ip), TAG, "set static IP failed");

    return set_dns(s_sta_netif, s_cfg.network.dns);
}

/*
 * Eseményvezérelt újracsatlakozó task.
 * A kapcsolat megszakadásakor értesítést kap, majd a konfigurált időközzel próbálkozik.
 */
static void reconnect_task(void *arg)
{
    (void)arg;

    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (!(xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT))
        {
            vTaskDelay(pdMS_TO_TICKS(s_cfg.reconnect_seconds * 1000U));

            if (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT)
            {
                break;
            }

            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN)
            {
                ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
            }
        }
    }
}

/* ESP-IDF Wi-Fi és IP események kezelése. */
static void
event_handler(void *handler_arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_arg;

    /* A Wi-Fi driver indulásakor azonnal kezdeményezzük a csatlakozást. */
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        return;
    }

    /*
     * Asszociáció után, de IP esemény előtt alkalmazzuk a statikus címet,
     * ha a konfiguráció ezt kéri.
     */
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    {

        if (s_cfg.network.use_static_ip)
        {
            esp_err_t err = apply_static_ip();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Static IP configuration failed: %s", esp_err_to_name(err));
            }
        }
        return;
    }

    /* Kapcsolatvesztéskor az UI azonnal hibaállapotot fog látni. */
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {

        bool was_connected = (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT) != 0;

        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        app_state_set_wifi_connected(false);

        if (was_connected)
        {
            app_events_add(APP_EVENT_WARNING, "Wi-Fi kapcsolat megszakadt.");
        }

        if (s_reconnect_task)
        {
            xTaskNotifyGive(s_reconnect_task);
        }
        return;
    }

    /* A kapcsolat csak IP cím megszerzésekor tekinthető teljesen használhatónak. */
    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "IP acquired: " IPSTR, IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        app_state_set_wifi_connected(true);
        app_events_add(APP_EVENT_INFO, "Wi-Fi kapcsolat létrejött.");
    }
}

/* Wi-Fi STA alrendszer egyszeri inicializálása. */
esp_err_t app_wifi_start(const app_config_t *cfg)
{
    if (!cfg)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_started)
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_cfg = cfg->wifi;

    s_events = xEventGroupCreate();
    if (!s_events)
    {
        return ESP_ERR_NO_MEM;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif)
    {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(
        esp_netif_set_hostname(s_sta_netif, APP_DEVICE_HOSTNAME), TAG, "set hostname failed");

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL),
        TAG,
        "wifi handler register failed");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL),
        TAG,
        "ip handler register failed");

    /* A fix méretű ESP-IDF struktúrába már validált hosszúságú adatok kerülnek. */
    wifi_config_t wifi_cfg = {0};
    size_t ssid_len = strlen(s_cfg.ssid);
    size_t password_len = strlen(s_cfg.password);

    memcpy(wifi_cfg.sta.ssid, s_cfg.ssid, ssid_len);
    memcpy(wifi_cfg.sta.password, s_cfg.password, password_len);

    wifi_cfg.sta.threshold.authmode = s_cfg.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "set config failed");

    /*
     * Folyamatos kijelző és hálózati válaszkészség miatt kikapcsoljuk
     * a Wi-Fi energiatakarékos módot.
     */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "set power save failed");

    if (xTaskCreate(reconnect_task, "wifi_reconnect", 2560, NULL, 5, &s_reconnect_task) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return esp_wifi_start();
}

/* Várakozás a GOT_IP esemény által beállított kapcsolati bitre. */
bool app_wifi_wait_connected(TickType_t timeout_ticks)
{
    if (!s_events)
    {
        return false;
    }

    EventBits_t bits =
        xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, timeout_ticks);

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/* Pillanatnyi Wi-Fi kapcsolati állapot. */
bool app_wifi_is_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT) != 0;
}

/* Aktuális hozzáférési pont RSSI értékének lekérése. */
esp_err_t app_wifi_get_rssi(int *rssi)
{
    if (!rssi || !app_wifi_is_connected())
    {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_ap_record_t access_point = {0};
    esp_err_t err = esp_wifi_sta_get_ap_info(&access_point);

    if (err == ESP_OK)
    {
        *rssi = access_point.rssi;
    }

    return err;
}

/* Aktuális STA IPv4 cím szöveges formában. */
esp_err_t app_wifi_get_ip(char *out, size_t out_len)
{
    if (!out || !out_len || !s_sta_netif)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_ip_info_t ip = {0};

    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(s_sta_netif, &ip), TAG, "get ip failed");

    snprintf(out, out_len, IPSTR, IP2STR(&ip.ip));
    return ESP_OK;
}
