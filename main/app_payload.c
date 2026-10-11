/*
 * MQTT JSON payload validálása és normalizálása.
 *
 * A csomag csak akkor kerül tovább a rendszerbe, ha mind a 12 kötelező mező
 * megtalálható, megfelelő típusú és a specifikált tartományban van.
 * A négy időjárási mező JSON null értéke is megengedett.
 */
#include "app_payload.h"

#include <math.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>

#include "cJSON.h"
#include "esp_timer.h"

#define REQUIRED_FIELD_COUNT 12

/* A kötelező mezők belső azonosítói. */
typedef enum {
    F_POWER_GRID = 0,
    F_POWER_BAT,
    F_POWER_PV,
    F_SOC_BAT,
    F_POWER_FULL,
    F_TEMPERATURE,
    F_WIND_SPEED,
    F_HAS_SUN,
    F_HAS_PRECIP,
    F_STATUS,
    F_NIGHT_MODE,
    F_BAT_TEMP
} field_id_t;

/* A protokollban használt kanonikus mezőnevek. */
static const char *s_names[REQUIRED_FIELD_COUNT] = {
    "PowerGrid",
    "PowerBat",
    "PowerPV",
    "SocBat",
    "PowerFull",
    "Temperature",
    "WindSpeed",
    "HasSunRadiation",
    "HasPrecipitation",
    "Status",
    "NightMode",
    "BatteryTemperature"
};

/* Kis-/nagybetűtől függetlenül visszaadja a kötelező mező azonosítóját. */
static int field_id_for_name(const char *name)
{
    if (!name) {
        return -1;
    }

    for (int i = 0; i < REQUIRED_FIELD_COUNT; i++) {
        if (strcasecmp(name, s_names[i]) == 0) {
            return i;
        }
    }

    return -1;
}

/* Egységes, opcionálisan mezőnévvel kiegészített hibaüzenet készítése. */
static void fail(
    char *error,
    size_t error_len,
    const char *message,
    const char *field)
{
    if (!error || !error_len) {
        return;
    }

    if (field) {
        snprintf(error, error_len, "%s: %s", message, field);
    } else {
        snprintf(error, error_len, "%s", message);
    }
}

/* Lebegőpontos szám és megengedett tartomány ellenőrzése. */
static bool number_in_range(
    cJSON *item,
    double minimum,
    double maximum,
    double *out)
{
    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        item->valuedouble < minimum ||
        item->valuedouble > maximum) {
        return false;
    }

    *out = item->valuedouble;
    return true;
}

/* Egész numerikus mező ellenőrzése. */
static bool integer_in_range(
    cJSON *item,
    int minimum,
    int maximum,
    int *out)
{
    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < minimum ||
        item->valuedouble > maximum) {
        return false;
    }

    *out = (int)item->valuedouble;
    return true;
}

/*
 * Egy teljes MQTT JSON csomag feldolgozása.
 * Ismeretlen extra mezők megengedettek, a kötelező mezők duplikációja viszont nem.
 */
esp_err_t app_payload_parse(
    const char *json,
    size_t len,
    app_energy_sample_t *out,
    char *error,
    size_t error_len)
{
    if (error && error_len) {
        error[0] = 0;
    }

    if (!json || !len || !out) {
        fail(error, error_len, "Üres vagy érvénytelen MQTT payload", NULL);
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * A parser végét is ellenőrizzük, így egy érvényes JSON objektum után
     * becsempészett további adat nem marad észrevétlen.
     */
    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(
        json,
        len,
        &parse_end,
        false);

    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        fail(
            error,
            error_len,
            "Az MQTT payload nem érvényes JSON objektum",
            NULL);
        return ESP_ERR_INVALID_ARG;
    }

    while (parse_end < json + len &&
           (*parse_end == ' ' ||
            *parse_end == '\t' ||
            *parse_end == '\r' ||
            *parse_end == '\n')) {
        parse_end++;
    }

    if (parse_end != json + len) {
        cJSON_Delete(root);
        fail(
            error,
            error_len,
            "Az MQTT payload JSON után további adatot tartalmaz",
            NULL);
        return ESP_ERR_INVALID_ARG;
    }

    /* Először összegyűjtjük a kötelező mezők mutatóit és kiszűrjük a duplikációt. */
    cJSON *fields[REQUIRED_FIELD_COUNT] = {0};
    cJSON *item = NULL;

    cJSON_ArrayForEach(item, root) {
        int id = field_id_for_name(item->string);
        if (id < 0) {
            continue;
        }

        if (fields[id]) {
            fail(
                error,
                error_len,
                "Duplikált mező (kis-/nagybetű eltérés sem megengedett)",
                s_names[id]);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }

        fields[id] = item;
    }

    for (int i = 0; i < REQUIRED_FIELD_COUNT; i++) {
        if (!fields[i]) {
            fail(
                error,
                error_len,
                "Hiányzó kötelező mező",
                s_names[i]);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
    }

    /* A teljes mintát átmeneti objektumba töltjük; részleges frissítés nem lehetséges. */
    app_energy_sample_t sample = {0};

    if (!number_in_range(fields[F_POWER_GRID], -15, 15, &sample.power_grid_kw)) {
        fail(error, error_len, "Érvénytelen tartomány vagy típus", s_names[F_POWER_GRID]);
        goto invalid;
    }
    if (!number_in_range(fields[F_POWER_BAT], -15, 15, &sample.power_bat_kw)) {
        fail(error, error_len, "Érvénytelen tartomány vagy típus", s_names[F_POWER_BAT]);
        goto invalid;
    }
    if (!number_in_range(fields[F_POWER_PV], -0.05, 15, &sample.power_pv_kw)) {
        fail(error, error_len, "Érvénytelen tartomány vagy típus", s_names[F_POWER_PV]);
        goto invalid;
    }
    if (!number_in_range(fields[F_SOC_BAT], 0, 100, &sample.soc_bat_pct)) {
        fail(error, error_len, "Érvénytelen tartomány vagy típus", s_names[F_SOC_BAT]);
        goto invalid;
    }
    if (!number_in_range(fields[F_POWER_FULL], -0.05, 15, &sample.power_full_kw)) {
        fail(error, error_len, "Érvénytelen tartomány vagy típus", s_names[F_POWER_FULL]);
        goto invalid;
    }
    sample.temperature_valid = !cJSON_IsNull(fields[F_TEMPERATURE]);
    sample.wind_speed_valid = !cJSON_IsNull(fields[F_WIND_SPEED]);
    sample.sun_radiation_valid = !cJSON_IsNull(fields[F_HAS_SUN]);
    sample.precipitation_valid = !cJSON_IsNull(fields[F_HAS_PRECIP]);

    if (sample.temperature_valid && !number_in_range(fields[F_TEMPERATURE], -35, 50, &sample.temperature_c)) {
        fail(error, error_len, "Érvénytelen tartomány vagy típus", s_names[F_TEMPERATURE]);
        goto invalid;
    }
    if (sample.wind_speed_valid && !number_in_range(fields[F_WIND_SPEED], 0, 150, &sample.wind_speed_kmh)) {
        fail(error, error_len, "Érvénytelen tartomány vagy típus", s_names[F_WIND_SPEED]);
        goto invalid;
    }
    if (!number_in_range(fields[F_BAT_TEMP], -20, 80, &sample.battery_temperature_c)) {
        fail(error, error_len, "Érvénytelen tartomány vagy típus", s_names[F_BAT_TEMP]);
        goto invalid;
    }

    /* A logikai mezők a protokoll szerint kizárólag numerikus 0/1 értékek lehetnek. */
    int integer_value = 0;

    if (sample.sun_radiation_valid && !integer_in_range(fields[F_HAS_SUN], 0, 1, &integer_value)) {
        fail(error, error_len, "Csak numerikus 0 vagy 1 megengedett", s_names[F_HAS_SUN]);
        goto invalid;
    }
    sample.has_sun_radiation = sample.sun_radiation_valid && integer_value == 1;
    integer_value = 0;

    if (sample.precipitation_valid && !integer_in_range(fields[F_HAS_PRECIP], 0, 1, &integer_value)) {
        fail(error, error_len, "Csak numerikus 0 vagy 1 megengedett", s_names[F_HAS_PRECIP]);
        goto invalid;
    }
    sample.has_precipitation = sample.precipitation_valid && integer_value == 1;

    if (!integer_in_range(fields[F_STATUS], 1, 4, &sample.status)) {
        fail(error, error_len, "Csak 1..4 megengedett", s_names[F_STATUS]);
        goto invalid;
    }

    if (!integer_in_range(fields[F_NIGHT_MODE], 0, 1, &integer_value)) {
        fail(error, error_len, "Csak numerikus 0 vagy 1 megengedett", s_names[F_NIGHT_MODE]);
        goto invalid;
    }
    sample.night_mode = integer_value == 1;

    /* A -0,05..0 kW közötti PV/fogyasztás mérési zajt nullára normalizáljuk. */
    if (sample.power_pv_kw < 0) {
        sample.power_pv_kw = 0;
    }
    if (sample.power_full_kw < 0) {
        sample.power_full_kw = 0;
    }

    /* A fogadás időpontjai a timeout és a napi integrálás alapjai. */
    sample.received_monotonic_us = esp_timer_get_time();
    sample.received_epoch_s = (int64_t)time(NULL);

    *out = sample;
    cJSON_Delete(root);
    return ESP_OK;

invalid:
    cJSON_Delete(root);
    return ESP_ERR_INVALID_ARG;
}

