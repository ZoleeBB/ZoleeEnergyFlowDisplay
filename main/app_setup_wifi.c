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
    app_setup_ap_t ap;
    uint16_t scan_order;
} scan_entry_t;

typedef struct {
    app_setup_wifi_state_t view;
    scan_entry_t *list;
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
        /* A legutolsó kirajzolás óta lapozás vagy új keresés is történhetett. */
        ok = false;
        for (unsigned i = 0; i < chooser->view.count; ++i) {
            const app_setup_ap_t *ap = &chooser->view.aps[i];
            if (!strcmp(ap->ssid, ssid) && ap->secured == (password[0] != 0)) {
                ok = true;
                break;
            }
        }
    }
    if (ok) {
        strcpy(chooser->view.selected_ssid,ssid);
        strcpy(chooser->password,password);
        chooser->command = CMD_CONNECT;
        phase = APP_SETUP_CONNECTING;
    }
    portEXIT_CRITICAL(&guard);
    return ok;
}

/* A scan_order az SDK jelerősség szerinti sorrendje. Azonos nevű AP-kból
 * így azt a biztonsági módot választjuk, amely az első találathoz tartozik. */
static int by_scan_order(const void *a, const void *b)
{
    const scan_entry_t *first = a, *second = b;
    return (int)first->scan_order - (int)second->scan_order;
}

static int by_ssid(const void *a, const void *b)
{
    const scan_entry_t *first = a, *second = b;
    int cmp = strcmp(first->ap.ssid, second->ap.ssid);
    return cmp ? cmp : by_scan_order(a, b);
}

/* Csak a kiválasztott oldal kerül az LVGL által is olvasott pillanatképbe. */
static void show_page(chooser_t *c, unsigned page)
{
    unsigned offset = page * APP_SETUP_WIFI_MAX_APS;
    c->view.page = page;
    c->view.count = c->view.total - offset;
    if (c->view.count > APP_SETUP_WIFI_MAX_APS) c->view.count = APP_SETUP_WIFI_MAX_APS;
    memset(c->view.aps, 0, sizeof(c->view.aps));
    for (unsigned i = 0; i < c->view.count; ++i) c->view.aps[i] = c->list[offset + i].ap;
}

bool app_setup_wifi_page(unsigned page)
{
    portENTER_CRITICAL(&guard);
    bool ok = chooser && (phase == APP_SETUP_SELECT || phase == APP_SETUP_FAILED) &&
              chooser->view.total && page <= (chooser->view.total - 1) / APP_SETUP_WIFI_MAX_APS;
    if (ok) show_page(chooser, page);
    portEXIT_CRITICAL(&guard);
    return ok;
}

static void scan_aps(chooser_t *c, app_setup_phase_t success_phase)
{
    wifi_scan_config_t options = {0};
    uint16_t count = 0;
    bool started = esp_wifi_scan_start(&options, true) == ESP_OK;
    bool ok = started && esp_wifi_scan_get_ap_num(&count) == ESP_OK;
    scan_entry_t *list = NULL;
    if (ok && count) {
        list = heap_caps_calloc(count, sizeof(*list), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ok = list != NULL;
    }
    unsigned visible = 0;
    for (unsigned i = 0; ok && i < count; ++i) {
        wifi_ap_record_t record;
        ok = esp_wifi_scan_get_ap_record(&record) == ESP_OK;
        if (!ok) break;
        size_t len = strnlen((const char *)record.ssid, sizeof(record.ssid));
        if (!len || len >= APP_WIFI_SSID_MAX) continue;
        scan_entry_t *entry = &list[visible++];
        memcpy(entry->ap.ssid, record.ssid, len);
        entry->ap.secured = record.authmode != WIFI_AUTH_OPEN;
        entry->scan_order = (uint16_t)i;
    }
    if (started && !ok) esp_wifi_clear_ap_list();
    unsigned unique = 0;
    if (ok && visible) {
        qsort(list, visible, sizeof(*list), by_ssid);
        for (unsigned i = 0; i < visible; ++i) {
            if (unique && !strcmp(list[unique - 1].ap.ssid, list[i].ap.ssid)) continue;
            list[unique++] = list[i];
        }
        qsort(list, unique, sizeof(*list), by_scan_order);
    }
    /* A hibás/frissítés alatt álló pásztázás nem cseréli ki a meglévő oldalt. */
    portENTER_CRITICAL(&guard);
    if (ok) {
        scan_entry_t *previous = c->list;
        c->list = list;
        list = previous;
        c->view.total = unique;
        show_page(c, 0);
    }
    phase = ok ? success_phase : APP_SETUP_FAILED;
    portEXIT_CRITICAL(&guard);
    free(list);
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
        if (cmd == CMD_SCAN) scan_aps(c, APP_SETUP_SELECT);
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
            if (!c->view.total) scan_aps(c, APP_SETUP_FAILED);
            else app_setup_wifi_set_phase(APP_SETUP_FAILED);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    portENTER_CRITICAL(&guard);
    chooser = NULL;
    phase = APP_SETUP_WAIT_TIME;
    portEXIT_CRITICAL(&guard);
    free(c->list);
    mbedtls_platform_zeroize(c,sizeof(*c));
    free(c);
    app_wifi_finish_setup();
    return ESP_OK;
}
