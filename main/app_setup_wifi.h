#pragma once
#include "app_config.h"

#define APP_SETUP_WIFI_MAX_APS 16
typedef enum {
    APP_SETUP_DONE = 0, APP_SETUP_SCANNING, APP_SETUP_SELECT, APP_SETUP_CONNECTING,
    APP_SETUP_FAILED, APP_SETUP_WAIT_TIME, APP_SETUP_WEB
} app_setup_phase_t;

typedef struct {
    char ssid[APP_WIFI_SSID_MAX];
    bool secured;
} app_setup_ap_t;

typedef struct {
    app_setup_phase_t phase;
    app_setup_ap_t aps[APP_SETUP_WIFI_MAX_APS];
    unsigned count;
    char ip[APP_IPV4_TEXT_MAX], selected_ssid[APP_WIFI_SSID_MAX];
} app_setup_wifi_state_t;

/* A panelhívások csak rövid üzenetet tesznek a boot feldolgozójának sorába. */
bool app_setup_wifi_scan(void);
bool app_setup_wifi_connect(const char *ssid, const char *password);
void app_setup_wifi_snapshot(app_setup_wifi_state_t *out);
void app_setup_wifi_set_phase(app_setup_phase_t phase);
esp_err_t app_setup_wifi_provision(app_config_t *config);
