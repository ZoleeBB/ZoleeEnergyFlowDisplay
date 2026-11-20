/* Valódi hálózatválasztó és STA eseménykezelő determinisztikus rádióval.
 * A késleltetés és a DHCP-várás közben játsszuk vissza az érintéseket. */
#include "platform.h"
#include "app_setup_wifi.h"
#include "app_wifi.h"
#include "app_events.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum scenario { RETRY, OPEN, SAVED, NO_DHCP, LATE_DHCP, FULL_SSID, LEGACY, CROWDED };
static enum scenario scenario;
static const char *const secured_ssid = "Ház-5GHz", *const open_ssid = "Vendég Wi-Fi";
static const char *const unicode_key = "árvíz tűrő?!@#'\"\\";
static const char *const long_ssid = "12345678901234567890123456789012";
static app_config_t config;
static wifi_config_t credentials;
static event_fn_t on_wifi, on_ip;
static EventBits_t bits;
static unsigned scans, clears, joins, waits, actions, notifications, starts;
static bool online;
static unsigned scan_index;

/* A régi firmware által C6 flashbe írt profil a P4 újraindítása után is megvan.
 * Az elvesztett válasz külön esemény a távoli művelet tényleges sikerétől. */
static bool radio_driver, in_reset, flash_credentials = true;
static unsigned reset_inits, reset_erases, reset_closes, radio_tasks, netifs;
static int storage;
static const char *reject_step, *reject_close;

static bool reject_once(const char **value, const char *step)
{
    if (!*value || strcmp(*value, step)) return false;
    *value = NULL;
    return true;
}

static void got_ip(void)
{
    ip_event_got_ip_t event = {0};
    event.ip_info.ip.addr = inet_addr("192.0.2.44");
    on_ip(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
}
esp_err_t esp_wifi_start(void) { starts++; on_wifi(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL); return ESP_OK; }
esp_err_t esp_wifi_connect(void)
{
    joins++;
    on_wifi(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    /* Társítva van, de a kliens még nem kapott IPv4-címet. */
    assert(!app_wifi_is_connected() && !online);
    return ESP_OK;
}
esp_err_t esp_wifi_disconnect(void)
{
    on_wifi(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    return ESP_OK;
}
esp_err_t esp_wifi_set_config(int iface, wifi_config_t *value)
{
    assert(iface == WIFI_IF_STA);
    assert(radio_driver && storage == WIFI_STORAGE_RAM);
    credentials = *value;
    return ESP_OK;
}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *options, bool block)
{
    scans++;
    scan_index = 0;
    assert(block && options && !options->ssid);
    return ESP_OK;
}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count)
{
    *count = scenario == CROWDED ? 97 : 5;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *ap)
{
    unsigned i = scan_index++;
    if ((scenario == RETRY && scans == 1) || (scenario == CROWDED && scans == 2 && i == 40)) return ESP_FAIL;
    memset(ap, 0, sizeof(*ap));
    if (scenario == CROWDED) {
        /* Az első 65 BSSID mind ugyanahhoz az AP-hálózathoz tartozik. */
        if (i < 65) strcpy((char *)ap->ssid, "Mesh");
        else snprintf((char *)ap->ssid, sizeof(ap->ssid), "SSID-%u", i - 64);
        ap->authmode = i ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    } else {
        const char *names[] = {secured_ssid, "", secured_ssid, open_ssid, long_ssid};
        assert(i < 5);
        strcpy((char *)ap->ssid, names[i]);
        ap->authmode = i == 3 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    }
    return ESP_OK;
}
esp_err_t esp_wifi_clear_ap_list(void) { clears++; return ESP_OK; }
EventGroupHandle_t xEventGroupCreate(void) { return &bits; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t value)
{
    /* Az IP-re váró boot-feladat a másik magon azonnal elindulhat. */
    assert(online);
    return *g |= value;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t value) { return *g &= ~value; }
EventBits_t xEventGroupGetBits(EventGroupHandle_t g) { return *g; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t mask, int clear, int all, unsigned timeout)
{
    assert(g == &bits && !clear && all && mask == BIT0 && timeout == 15000);
    app_setup_wifi_state_t view;
    app_setup_wifi_snapshot(&view);
    assert(view.phase == APP_SETUP_CONNECTING && !online && !view.ip[0]);
    assert(!app_setup_wifi_scan() && !app_setup_wifi_connect("racing", "retry"));
    if (scenario != SAVED && scenario != NO_DHCP && scenario != LATE_DHCP) assert(!config.wifi.ssid[0] && !config.wifi.password[0]);
    waits++;
    if (scenario == RETRY && waits == 1) {
        assert(!strcmp((char *)credentials.sta.password, "wrong-password"));
        esp_wifi_disconnect();
        return *g;
    }
    if (scenario == NO_DHCP && waits == 1) {
        /* Helyes kulcs és sikeres társítás, de a DHCP-szerver nem válaszol. */
        assert(!strcmp((char *)credentials.sta.password, unicode_key));
        return *g;
    }
    assert(credentials.sta.threshold.authmode == (scenario == OPEN ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK));
    assert(!strcmp((char *)credentials.sta.password, scenario == OPEN ? "" : unicode_key));
    got_ip();
    return *g;
}
void vTaskDelay(TickType_t ticks)
{
    assert(ticks == 50 && ++actions <= 5);
    app_setup_wifi_state_t view;
    app_setup_wifi_snapshot(&view);
    if (scenario == CROWDED) {
        if (actions == 1) {
            assert(view.phase == APP_SETUP_SELECT && view.page == 0 && view.count == 16 && view.total == 33);
            assert(scan_index == 97 && !view.aps[0].secured && !strcmp(view.aps[0].ssid, "Mesh"));
            assert(!app_setup_wifi_page(3) && app_setup_wifi_page(1));
            app_setup_wifi_snapshot(&view);
            assert(view.count == 16 && !strcmp(view.aps[0].ssid, "SSID-16"));
            assert(!app_setup_wifi_connect("Mesh", "") && app_setup_wifi_page(0) && app_setup_wifi_page(2));
            assert(app_setup_wifi_scan() && !app_setup_wifi_page(1));
        } else {
            assert(actions == 2 && view.phase == APP_SETUP_FAILED && view.total == 33 && view.page == 2);
            assert(view.count == 1 && !strcmp(view.aps[0].ssid, "SSID-32") && clears == 1);
            assert(app_setup_wifi_connect(view.aps[0].ssid, unicode_key));
        }
        return;
    }
    if (scenario == RETRY && actions == 1) {
        assert(view.phase == APP_SETUP_FAILED && !view.count && clears == 1);
        assert(app_setup_wifi_scan() && !app_setup_wifi_scan());
        return;
    }
    assert(view.count == 3 && view.aps[0].secured && !view.aps[1].secured);
    assert(!strcmp(view.aps[0].ssid, secured_ssid) && !strcmp(view.aps[1].ssid, open_ssid));
    assert(!strcmp(view.aps[2].ssid, long_ssid));
    if ((scenario == RETRY || scenario == NO_DHCP || scenario == LATE_DHCP) && waits) {
        assert(view.phase == APP_SETUP_FAILED && !strcmp(view.selected_ssid, secured_ssid));
        assert(app_setup_wifi_connect(secured_ssid, unicode_key));
    } else {
        assert(view.phase == APP_SETUP_SELECT);
        assert(!app_setup_wifi_connect("", "") && !app_setup_wifi_connect(NULL, NULL));
        assert(app_setup_wifi_connect(scenario == OPEN ? open_ssid : scenario == FULL_SSID ? long_ssid : secured_ssid,
                                      scenario == OPEN ? "" : scenario == FULL_SSID ? unicode_key : "wrong-password"));
    }
}
void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{ assert(caps == 3); return calloc(count, size); }
void mbedtls_platform_zeroize(void *bytes, size_t count)
{ volatile unsigned char *dst = bytes; while (count--) *dst++ = 0; }
void app_state_set_wifi_connected(bool connected) { online = connected; }
void app_events_add(app_event_level_t level, const char *format, ...) { (void)level; (void)format; }
esp_err_t esp_event_handler_register(int base, int id, event_fn_t cb, void *arg)
{
    assert(!in_reset && !arg);
    if (base == WIFI_EVENT) { assert(id == ESP_EVENT_ANY_ID); on_wifi = cb; }
    else { assert(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP); on_ip = cb; }
    return ESP_OK;
}
esp_netif_t *esp_netif_create_default_wifi_sta(void)
{ static esp_netif_t sta; assert(!in_reset); ++netifs; return &sta; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info)
{ assert(netif && online); info->ip.addr = inet_addr("192.0.2.44"); return ESP_OK; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap)
{
    assert(online);
    if (scenario == LATE_DHCP && waits == 1) strcpy((char *)ap->ssid, open_ssid);
    else memcpy(ap->ssid, credentials.sta.ssid, sizeof(credentials.sta.ssid));
    return ESP_OK;
}
esp_err_t esp_wifi_set_mode(int x) { assert(x == WIFI_MODE_STA); return ESP_OK; }
esp_err_t esp_wifi_set_storage(int value)
{
    assert(radio_driver && value == (in_reset ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM));
    if (reject_once(&reject_step, "storage")) return ESP_FAIL;
    storage = value;
    return ESP_OK;
}
esp_err_t esp_wifi_restore(void)
{
    assert(in_reset && radio_driver && storage == WIFI_STORAGE_FLASH);
    ++reset_erases;
    if (reject_once(&reject_step, "restore")) return ESP_FAIL;
    flash_credentials = false;
    return ESP_OK;
}
esp_err_t esp_wifi_deinit(void)
{
    assert(in_reset);
    ++reset_closes;
    if (reject_once(&reject_close, "busy")) return ESP_FAIL;
    if (!radio_driver) return ESP_ERR_WIFI_NOT_INIT;
    radio_driver = false;
    if (reject_once(&reject_close, "lost")) return ESP_FAIL;
    return ESP_OK;
}
esp_err_t esp_wifi_set_ps(int x) { assert(x == WIFI_PS_NONE); return ESP_OK; }
esp_err_t esp_wifi_init(const wifi_init_config_t *init)
{
    assert(init && !radio_driver);
    radio_driver = true;
    storage = -1;
    if (in_reset) {
        assert(init->nvs_enable);
        ++reset_inits;
        if (reject_once(&reject_step, "init")) return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t esp_netif_set_hostname(esp_netif_t *sta, const char *name)
{ assert(sta && !strcmp(name, APP_DEVICE_HOSTNAME)); return ESP_OK; }
esp_err_t esp_netif_dhcpc_stop(esp_netif_t *sta) { (void)sta; abort(); }
esp_err_t esp_netif_set_dns_info(esp_netif_t *sta, int type, esp_netif_dns_info_t *info)
{ (void)sta; (void)type; (void)info; abort(); }
esp_err_t esp_netif_set_ip_info(esp_netif_t *sta, const esp_netif_ip_info_t *info)
{ (void)sta; (void)info; abort(); }
unsigned ulTaskNotifyTake(int clear, unsigned timeout) { (void)clear; (void)timeout; abort(); }
void xTaskNotifyGive(TaskHandle_t handle) { assert(handle); notifications++; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{
    assert(!in_reset && fn && !strcmp(name, "wifi_reconnect") && stack && !arg && priority);
    ++radio_tasks;
    *(TaskHandle_t *)handle = (void *)&starts;
    return pdPASS;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const bool reset = !strncmp(argv[1], "reset", 5);
    if (reset) {
        const char *fault = argv[1] + 5;
        in_reset = true;
        bool retry_only_close = false;
        if (!strcmp(fault, "-init")) reject_step = "init";
        else if (!strcmp(fault, "-storage")) reject_step = "storage";
        else if (!strcmp(fault, "-restore")) reject_step = "restore";
        else if (!strcmp(fault, "-close")) { reject_close = "busy"; retry_only_close = true; }
        else if (!strcmp(fault, "-lost-close")) { reject_close = "lost"; retry_only_close = true; }
        else if (!strcmp(fault, "-double-fault")) { reject_step = "restore"; reject_close = "busy"; }
        else assert(!*fault);
        if (*fault) {
            assert(app_wifi_factory_reset_credentials() != ESP_OK);
            assert(flash_credentials == !retry_only_close);
        }
        assert(app_wifi_factory_reset_credentials() == ESP_OK);
        assert(!flash_credentials && !radio_driver && !starts && !joins && !netifs && !radio_tasks);
        assert(reset_inits == ((!*fault || retry_only_close) ? 1U : 2U));
        if (retry_only_close) assert(reset_erases == 1 && reset_closes == 2);
        in_reset = false;
    }
    scenario = !strcmp(argv[1], "late-dhcp") ? LATE_DHCP : !strcmp(argv[1], "full-ssid") ? FULL_SSID : reset ? OPEN : !strcmp(argv[1],"retry") ? RETRY : !strcmp(argv[1],"open") ? OPEN :
               !strcmp(argv[1],"crowded") ? CROWDED : !strcmp(argv[1],"saved") ? SAVED : !strcmp(argv[1],"no-dhcp") ? NO_DHCP : LEGACY;
    if (scenario == SAVED || scenario == LEGACY || scenario == NO_DHCP || scenario == LATE_DHCP) {
        strcpy(config.wifi.ssid, secured_ssid);
        strcpy(config.wifi.password, unicode_key);
    }
    if (scenario == LEGACY) {
        assert(app_wifi_start(&config) == ESP_OK && joins == 1 && !online);
        got_ip();
    } else {
        assert(app_setup_wifi_provision(&config) == ESP_OK);
        assert(!strcmp(config.wifi.password, scenario == OPEN ? "" : unicode_key));
        assert(!strcmp(config.wifi.ssid, scenario == OPEN ? open_ssid : scenario == CROWDED ? "SSID-32" : scenario == FULL_SSID ? long_ssid : secured_ssid));
        assert(waits == (scenario == RETRY || scenario == NO_DHCP || scenario == LATE_DHCP ? 2U : 1U));
        assert(scans == (scenario == SAVED ? 0U : (scenario == RETRY || scenario == CROWDED) ? 2U : 1U));
        assert(actions == (scenario == SAVED ? 0U : scenario == RETRY ? 3U : scenario == CROWDED ? 2U : 1U));
        app_setup_wifi_state_t view;
        app_setup_wifi_snapshot(&view);
        assert(view.phase == APP_SETUP_WAIT_TIME && !view.count);
        app_setup_wifi_set_phase(APP_SETUP_WEB);
        app_setup_wifi_snapshot(&view);
        assert(!strcmp(view.ip, "192.0.2.44"));
        app_setup_wifi_set_phase(APP_SETUP_DONE);
    }
    assert(online && starts == 1 && !notifications && radio_tasks == 1 && netifs == 1);
    assert(app_wifi_factory_reset_credentials() == ESP_ERR_INVALID_STATE);
    assert(flash_credentials == !reset);
    if (scenario == LEGACY) assert(!scans && !actions);
    assert(!app_setup_wifi_scan() && !app_setup_wifi_connect("other", "password"));
    esp_wifi_disconnect();
    assert(!online && notifications == 1); /* A mentett hálózati profil újracsatlakozója aktív. */
    return 0;
}
