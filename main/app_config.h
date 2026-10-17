#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Szöveges konfigurációs mezők maximális méretei. */
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

/* Projekt- és lokalizációs beállítások. */
typedef struct {
    char title[APP_PROJECT_TITLE_MAX];
    char language[APP_LANGUAGE_MAX];
    char currency[APP_CURRENCY_MAX];
} app_project_config_t;

/* Opcionális statikus IPv4 hálózati beállítások. */
typedef struct {
    bool use_static_ip;
    char ip[APP_IPV4_TEXT_MAX];
    char gateway[APP_IPV4_TEXT_MAX];
    char subnet[APP_IPV4_TEXT_MAX];
    char dns[APP_IPV4_TEXT_MAX];
} app_network_config_t;

/* Wi-Fi kliens beállításai. */
typedef struct {
    char ssid[APP_WIFI_SSID_MAX];
    char password[APP_WIFI_PASSWORD_MAX];
    uint32_t reconnect_seconds;
    app_network_config_t network;
} app_wifi_config_t;

/* NTP, időzóna és újraszinkronizálási beállítások. */
typedef struct {
    char ntp_server[APP_NTP_SERVER_MAX];
    char timezone[APP_TIMEZONE_MAX];
    bool auto_dst;
    int32_t utc_offset_minutes;
    uint32_t startup_retry_seconds;
    uint32_t resync_hours;
    uint32_t background_retry_minutes;
} app_time_config_t;

typedef enum { APP_DATA_MQTT = 0, APP_DATA_HTTP = 1 } app_data_mode_t;
typedef struct { uint32_t data_timeout_seconds; } app_http_config_t;

/* MQTT broker és adat-timeout beállítások. */
typedef struct {
    char broker[APP_HOST_MAX];
    uint16_t port;
    char topic[APP_TOPIC_MAX];
    char username[APP_MQTT_USER_MAX];
    char password[APP_MQTT_PASSWORD_MAX];
    uint32_t data_timeout_seconds;
    uint32_t reconnect_seconds;
} app_mqtt_config_t;

/* Nappali és éjszakai kijelzőfényerő százalékban. */
typedef struct {
    uint8_t day_brightness;
    uint8_t night_brightness;
    uint16_t rotation_degrees; /* Csak 270 (jobb) vagy 90 (bal) */
} app_display_config_t;

/* Statisztika mentési és integrálási paraméterek. */
typedef struct {
    uint32_t save_interval_minutes;
    uint32_t max_integration_gap_seconds;
} app_statistics_config_t;

/* Pénzügyi és környezeti számítási konstansok. */
typedef struct {
    double battery_capacity_kwh; /* 0 = nincs megadva */
    double electricity_purchase_price;
    double feed_in_price;
    double co2_factor_kg_per_kwh;
    double tree_absorption_kg_per_year;
} app_energy_config_t;

/* A helyi web/OTA felület opcionális Basic Auth hitelesítése. */
typedef struct {
    char username[APP_OTA_USER_MAX];
    char password[APP_OTA_PASSWORD_MAX];
} app_ota_config_t;

/* A teljes, induláskor betöltött alkalmazáskonfiguráció. */
typedef struct {
    app_data_mode_t data_recept_mode;
    app_http_config_t http;
    app_project_config_t project;
    app_wifi_config_t wifi;
    app_time_config_t time;
    app_mqtt_config_t mqtt;
    app_display_config_t display;
    app_statistics_config_t statistics;
    app_energy_config_t energy;
    app_ota_config_t ota;
} app_config_t;

/* Beépített alapértékek betöltése. */
void app_config_set_defaults(app_config_t *cfg);

/* config.json beolvasása, validálása és az alapértékekkel való kiegészítése. */
esp_err_t app_config_load_file(
    const char *path,
    app_config_t *cfg,
    char *error,
    size_t error_len);


static inline uint32_t app_config_data_timeout(const app_config_t *cfg)
{
    return cfg->data_recept_mode == APP_DATA_HTTP ? cfg->http.data_timeout_seconds : cfg->mqtt.data_timeout_seconds;
}
