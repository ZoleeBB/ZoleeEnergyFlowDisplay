/* Az első indítás hálózatválasztóját a boot-feladat szolgálja ki. A szkennelés
 * és az IP-címre várás alatt az LVGL tovább kezeli az érintéseket. */
#include "app_setup_wifi.h"
#include "app_wifi.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    app_setup_wifi_state_t view;
    enum { CMD_IDLE, CMD_SCAN, CMD_CONNECT } command;
    char password[APP_WIFI_PASSWORD_MAX];
} chooser_t;

static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
static chooser_t *chooser;
static app_setup_phase_t phase;

void app_setup_wifi_set_phase(app_setup_phase_t value)
{
    portENTER_CRITICAL(&guard);
    phase = value;
    portEXIT_CRITICAL(&guard);
}

void app_setup_wifi_snapshot(app_setup_wifi_state_t *out)
{
    if (!out) return;
    memset(out,0,sizeof(*out));
    portENTER_CRITICAL(&guard);
    if (chooser) *out = chooser->view;
    out->phase = phase;
    portEXIT_CRITICAL(&guard);
    if (out->phase == APP_SETUP_WEB && app_wifi_is_connected()) app_wifi_get_ip(out->ip,sizeof(out->ip));
}

bool app_setup_wifi_scan(void)
{
    portENTER_CRITICAL(&guard);
    bool ok = chooser && (phase == APP_SETUP_SELECT || phase == APP_SETUP_FAILED);
    if (ok) {
        chooser->command = CMD_SCAN;
        phase = APP_SETUP_SCANNING;
    }
    portEXIT_CRITICAL(&guard);
    return ok;
}

bool app_setup_wifi_connect(const char *ssid,const char *password)
{
    if (!ssid || !password || !ssid[0] || strlen(ssid) >= APP_WIFI_SSID_MAX ||
        strlen(password) >= APP_WIFI_PASSWORD_MAX) return false;
    portENTER_CRITICAL(&guard);
    bool ok = chooser && (phase == APP_SETUP_SELECT || phase == APP_SETUP_FAILED);
    if (ok) {
        strcpy(chooser->view.selected_ssid,ssid);
        strcpy(chooser->password,password);
        chooser->command = CMD_CONNECT;
        phase = APP_SETUP_CONNECTING;
    }
    portEXIT_CRITICAL(&guard);
    return ok;
}

static void scan_aps(chooser_t *c)
{
    uint16_t count = APP_SETUP_WIFI_MAX_APS;
    wifi_ap_record_t *aps = heap_caps_calloc(count,sizeof(*aps),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    wifi_scan_config_t options = {0};
    bool ok = aps && esp_wifi_scan_start(&options,true)==ESP_OK;
    if (ok) {
        ok = esp_wifi_scan_get_ap_records(&count,aps)==ESP_OK;
        if (!ok) esp_wifi_clear_ap_list();
    }
    portENTER_CRITICAL(&guard);
    c->view.count = 0;
    for (unsigned i=0; ok && i<count; ++i) {
        const char *ssid = (const char *)aps[i].ssid;
        size_t len = strnlen(ssid,sizeof(aps[i].ssid));
        if (!len || len>=APP_WIFI_SSID_MAX) continue;
        bool duplicate = false;
        for (unsigned j=0; j<c->view.count; ++j)
            if (!strcmp(ssid,c->view.aps[j].ssid)) duplicate = true;
        if (duplicate) continue;
        app_setup_ap_t *a = &c->view.aps[c->view.count++];
        memcpy(a->ssid,ssid,len+1);
        a->secured = aps[i].authmode != WIFI_AUTH_OPEN;
    }
    phase = ok ? APP_SETUP_SELECT : APP_SETUP_FAILED;
    portEXIT_CRITICAL(&guard);
    free(aps);
}

esp_err_t app_setup_wifi_provision(app_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    chooser_t *c = heap_caps_calloc(1,sizeof(*c),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!c) return ESP_ERR_NO_MEM;
    esp_err_t err = app_wifi_start_setup(cfg);
    if (err != ESP_OK) { free(c); return err; }
    portENTER_CRITICAL(&guard);
    chooser = c;
    phase = APP_SETUP_SCANNING;
    c->command = CMD_SCAN;
    if (cfg->wifi.ssid[0]) {
        strcpy(c->view.selected_ssid,cfg->wifi.ssid);
        strcpy(c->password,cfg->wifi.password);
        c->command = CMD_CONNECT;
        phase = APP_SETUP_CONNECTING;
    }
    portEXIT_CRITICAL(&guard);
    for (;;) {
        char ssid[APP_WIFI_SSID_MAX]={0}, password[APP_WIFI_PASSWORD_MAX]={0};
        portENTER_CRITICAL(&guard);
        int cmd = c->command;
        c->command = CMD_IDLE;
        if (cmd == CMD_CONNECT) {
            strcpy(ssid,c->view.selected_ssid);
            strcpy(password,c->password);
            mbedtls_platform_zeroize(c->password,sizeof(c->password));
        }
        portEXIT_CRITICAL(&guard);
        if (cmd == CMD_SCAN) scan_aps(c);
        if (cmd == CMD_CONNECT) {
            bool joined = app_wifi_join_setup(ssid,password)==ESP_OK &&
                          app_wifi_wait_connected(pdMS_TO_TICKS(15000));
            if (joined) {
                strcpy(cfg->wifi.ssid,ssid);
                strcpy(cfg->wifi.password,password);
            }
            mbedtls_platform_zeroize(password,sizeof(password));
            if (joined) break;
            esp_wifi_disconnect();
            if (!c->view.count) scan_aps(c);
            app_setup_wifi_set_phase(APP_SETUP_FAILED);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    portENTER_CRITICAL(&guard);
    chooser = NULL;
    phase = APP_SETUP_WAIT_TIME;
    portEXIT_CRITICAL(&guard);
    mbedtls_platform_zeroize(c,sizeof(*c));
    free(c);
    app_wifi_finish_setup();
    return ESP_OK;
}
