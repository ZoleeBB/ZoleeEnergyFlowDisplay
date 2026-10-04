#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define APP_PROJECT_TITLE_MAX 64
#define APP_LANGUAGE_MAX 8
#define APP_CURRENCY_MAX 12
#define APP_WIFI_SSID_MAX 33
#define APP_WIFI_PASSWORD_MAX 65
#define APP_HOST_MAX 128
#define APP_TOPIC_MAX 192
#define APP_MQTT_USER_MAX 64
#define APP_MQTT_PASSWORD_MAX 96
#define APP_NTP_SERVER_MAX 128
#define APP_TIMEZONE_MAX 64
#define APP_IPV4_TEXT_MAX 16
#define APP_OTA_USER_MAX 64
#define APP_OTA_PASSWORD_MAX 96

typedef struct { char title[APP_PROJECT_TITLE_MAX]; char language[APP_LANGUAGE_MAX]; char currency[APP_CURRENCY_MAX]; } app_project_config_t;
typedef struct { bool use_static_ip; char ip[APP_IPV4_TEXT_MAX]; char gateway[APP_IPV4_TEXT_MAX]; char subnet[APP_IPV4_TEXT_MAX]; char dns[APP_IPV4_TEXT_MAX]; } app_network_config_t;
typedef struct { char ssid[APP_WIFI_SSID_MAX]; char password[APP_WIFI_PASSWORD_MAX]; uint32_t reconnect_seconds; app_network_config_t network; } app_wifi_config_t;
typedef struct { char ntp_server[APP_NTP_SERVER_MAX]; char timezone[APP_TIMEZONE_MAX]; bool auto_dst; int32_t utc_offset_minutes; uint32_t startup_retry_seconds; uint32_t resync_hours; uint32_t background_retry_minutes; } app_time_config_t;
typedef struct { char broker[APP_HOST_MAX]; uint16_t port; char topic[APP_TOPIC_MAX]; char username[APP_MQTT_USER_MAX]; char password[APP_MQTT_PASSWORD_MAX]; uint32_t data_timeout_seconds; uint32_t reconnect_seconds; } app_mqtt_config_t;
typedef struct { uint8_t day_brightness; uint8_t night_brightness; } app_display_config_t;
typedef struct { uint32_t save_interval_minutes; uint32_t max_integration_gap_seconds; } app_statistics_config_t;
typedef struct { double electricity_purchase_price; double feed_in_price; double co2_factor_kg_per_kwh; double tree_absorption_kg_per_year; } app_energy_config_t;
typedef struct { char username[APP_OTA_USER_MAX]; char password[APP_OTA_PASSWORD_MAX]; } app_ota_config_t;
typedef struct { app_project_config_t project; app_wifi_config_t wifi; app_time_config_t time; app_mqtt_config_t mqtt; app_display_config_t display; app_statistics_config_t statistics; app_energy_config_t energy; app_ota_config_t ota; } app_config_t;

void app_config_set_defaults(app_config_t *cfg);
esp_err_t app_config_load_file(const char *path, app_config_t *cfg, char *error, size_t error_len);
