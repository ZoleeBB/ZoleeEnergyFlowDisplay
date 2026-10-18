/*
 * config.json beolvasása és szigorú validálása.
 *
 * A fájl induláskor egyszer kerül feldolgozásra. A nem megadott opcionális
 * mezők a beépített alapértékeket öröklik, a kötelező vagy hibás mezők
 * viszont leállítják az indulást.
 */
#include "app_config.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "lwip/ip4_addr.h"

/* Védelmi korlát: indokolatlanul nagy konfigurációs fájlt nem olvasunk be. */
#define CONFIG_FILE_MAX_BYTES (16 * 1024)

/* Formázott hibaüzenet írása a hívó pufferébe. */
static void set_error(char *dst, size_t len, const char *fmt, ...)
{
    if (!dst || !len) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(dst, len, fmt, args);
    va_end(args);
}

/* Biztonságos szövegmásolás fix méretű célpufferbe. */
static void copy_text(char *dst, size_t len, const char *src)
{
    if (!dst || !len) {
        return;
    }

    snprintf(dst, len, "%s", src ? src : "");
}

/* Az összes dokumentált alapérték beállítása. */
void app_config_set_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    /* Projekt és lokalizáció. */
    copy_text(cfg->project.title, sizeof(cfg->project.title), "Otthoni energia");
    copy_text(cfg->project.language, sizeof(cfg->project.language), "hu");
    copy_text(cfg->project.currency, sizeof(cfg->project.currency), "Ft");

    /* Kapcsolati újrapróbálkozások. */
    cfg->wifi.reconnect_seconds = 15;

    /* Időszinkron és időzóna. */
    copy_text(cfg->time.ntp_server, sizeof(cfg->time.ntp_server), "hu.pool.ntp.org");
    copy_text(cfg->time.timezone, sizeof(cfg->time.timezone), "Europe/Budapest");
    cfg->time.auto_dst = true;
    cfg->time.utc_offset_minutes = 60;
    cfg->time.startup_retry_seconds = 15;
    cfg->time.resync_hours = 6;
    cfg->time.background_retry_minutes = 15;

    /* MQTT. A broker cím kötelező, ezért nincs értelmes gyári cím. */
    cfg->http.data_timeout_seconds = 30;
    cfg->mqtt.port = 1883;
    copy_text(
        cfg->mqtt.topic,
        sizeof(cfg->mqtt.topic),
        "loxberry/mqttgateway/energymonitor/state");
    cfg->mqtt.data_timeout_seconds = 30;
    cfg->mqtt.reconnect_seconds = 15;

    /* Kijelző és statisztika. */
    cfg->display.day_brightness = 80;
    cfg->display.night_brightness = 20;
    cfg->display.rotation_degrees = 270;
    cfg->statistics.save_interval_minutes = 15;
    cfg->statistics.max_integration_gap_seconds = 30;

    /* Pénzügyi és környezeti számítások. */
    cfg->energy.battery_capacity_kwh = 0; /* ismeretlen, amíg a felhasználó nem adja meg */
    cfg->energy.electricity_purchase_price = 65;
    cfg->energy.feed_in_price = 5;
    cfg->energy.co2_factor_kg_per_kwh = 0.381;
    cfg->energy.tree_absorption_kg_per_year = 22;
}

/*
 * JSON objektumszekció lekérése.
 * Kötelező szekciónál a hiány hiba, opcionálisnál NULL adható vissza.
 */
static cJSON *get_object_section(
    cJSON *parent,
    const char *name,
    bool required,
    char *error,
    size_t error_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);

    if (!item) {
        if (required) {
            set_error(error, error_len, "Hiányzó config szekció: %s", name);
        }
        return NULL;
    }

    if (!cJSON_IsObject(item)) {
        set_error(
            error,
            error_len,
            "A(z) %s szekciónak objektumnak kell lennie.",
            name);
        return NULL;
    }

    return item;
}

/* Szöveges mező kiolvasása méret- és ürességellenőrzéssel. */
static bool read_string(
    cJSON *object,
    const char *name,
    char *dst,
    size_t dst_len,
    bool required,
    bool allow_empty,
    char *error,
    size_t error_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!item) {
        if (required) {
            set_error(error, error_len, "Hiányzó config mező: %s", name);
            return false;
        }
        return true;
    }

    if (!cJSON_IsString(item) || !item->valuestring) {
        set_error(
            error,
            error_len,
            "A(z) %s mezőnek szövegnek kell lennie.",
            name);
        return false;
    }

    size_t len = strlen(item->valuestring);

    if (!allow_empty && len == 0) {
        set_error(error, error_len, "A(z) %s mező nem lehet üres.", name);
        return false;
    }

    if (len >= dst_len) {
        set_error(error, error_len, "A(z) %s mező túl hosszú.", name);
        return false;
    }

    copy_text(dst, dst_len, item->valuestring);
    return true;
}

/* 0/1 numerikus konfigurációs kapcsoló kiolvasása. */
static bool read_bool01(
    cJSON *object,
    const char *name,
    bool *dst,
    bool required,
    char *error,
    size_t error_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!item) {
        if (required) {
            set_error(error, error_len, "Hiányzó config mező: %s", name);
            return false;
        }
        return true;
    }

    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        (item->valuedouble != 0 && item->valuedouble != 1)) {
        set_error(error, error_len, "A(z) %s mező csak 0 vagy 1 lehet.", name);
        return false;
    }

    *dst = item->valuedouble == 1;
    return true;
}

/* Előjel nélküli egész mező kiolvasása tartományellenőrzéssel. */
static bool read_u32(
    cJSON *object,
    const char *name,
    uint32_t *dst,
    uint32_t minimum,
    uint32_t maximum,
    bool required,
    char *error,
    size_t error_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!item) {
        if (required) {
            set_error(error, error_len, "Hiányzó config mező: %s", name);
            return false;
        }
        return true;
    }

    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < minimum ||
        item->valuedouble > maximum) {
        set_error(error, error_len, "Érvénytelen egész érték: %s", name);
        return false;
    }

    *dst = (uint32_t)item->valuedouble;
    return true;
}

/* Előjeles egész mező kiolvasása tartományellenőrzéssel. */
static bool read_i32(
    cJSON *object,
    const char *name,
    int32_t *dst,
    int32_t minimum,
    int32_t maximum,
    bool required,
    char *error,
    size_t error_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!item) {
        if (required) {
            set_error(error, error_len, "Hiányzó config mező: %s", name);
            return false;
        }
        return true;
    }

    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < minimum ||
        item->valuedouble > maximum) {
        set_error(error, error_len, "Érvénytelen egész érték: %s", name);
        return false;
    }

    *dst = (int32_t)item->valuedouble;
    return true;
}

/* Lebegőpontos mező kiolvasása tartományellenőrzéssel. */
static bool read_double(
    cJSON *object,
    const char *name,
    double *dst,
    double minimum,
    double maximum,
    bool required,
    char *error,
    size_t error_len)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!item) {
        if (required) {
            set_error(error, error_len, "Hiányzó config mező: %s", name);
            return false;
        }
        return true;
    }

    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        item->valuedouble < minimum ||
        item->valuedouble > maximum) {
        set_error(error, error_len, "Érvénytelen számérték: %s", name);
        return false;
    }

    *dst = item->valuedouble;
    return true;
}

/* IPv4 cím szintaktikai ellenőrzése. */
static bool valid_ipv4(const char *text)
{
    ip4_addr_t address;
    return text && *text && ip4addr_aton(text, &address) != 0;
}

/* A JSON objektum tartalmának szekciónkénti feldolgozása. */
static bool parse_config(
    cJSON *root,
    app_config_t *cfg,
    char *error,
    size_t error_len)
{
    char mode[8] = "mqtt";
    if (!read_bool01(root, "DataSeedEnabled", &cfg->data_seed_enabled, false, error, error_len))
        return false;
    if (!read_string(root, "DataReceptMode", mode, sizeof(mode), false, false, error, error_len)) return false;
    if (strcasecmp(mode, "loxone") == 0) cfg->data_recept_mode = APP_DATA_LOXONE;
    else if (strcasecmp(mode, "http") == 0) cfg->data_recept_mode = APP_DATA_HTTP;
    else if (strcasecmp(mode, "slave") == 0) cfg->data_recept_mode = APP_DATA_SLAVE;
    else if (strcasecmp(mode, "mqtt") != 0) {
        set_error(error, error_len, "A DataReceptMode csak 'mqtt', 'http', 'loxone' vagy 'slave' lehet.");
        return false;
    }
    /* Csak az aktív adatfogadási mód beállításait ellenőrizzük. */
    if (cfg->data_recept_mode == APP_DATA_HTTP) {
        cJSON *http = get_object_section(root, "http", false, error, error_len);
        if (http) {
            if (!read_u32(http, "data_timeout_seconds", &cfg->http.data_timeout_seconds, 5, 3600, false, error, error_len)) return false;
        } else if (error && *error) return false;
    }
    /* Projektbeállítások. */
    cJSON *project = get_object_section(root, "project", false, error, error_len);
    if (project) {
        if (!read_string(project, "title", cfg->project.title, sizeof(cfg->project.title), false, false, error, error_len) ||
            !read_string(project, "language", cfg->project.language, sizeof(cfg->project.language), false, false, error, error_len) ||
            !read_string(project, "currency", cfg->project.currency, sizeof(cfg->project.currency), false, false, error, error_len)) {
            return false;
        }
    } else if (error && *error) {
        return false;
    }

    if (strcasecmp(cfg->project.language, "hu") != 0 &&
        strcasecmp(cfg->project.language, "en") != 0) {
        set_error(error, error_len, "A language csak 'hu' vagy 'en' lehet.");
        return false;
    }

    /* A nyelvkódot kanonikus kisbetűs alakra normalizáljuk. */
    copy_text(
        cfg->project.language,
        sizeof(cfg->project.language),
        strcasecmp(cfg->project.language, "hu") == 0 ? "hu" : "en");

    bool slave = cfg->data_recept_mode == APP_DATA_SLAVE;
    if (slave && cfg->data_seed_enabled) {
        set_error(error, error_len, "Slave módban az adat seed kapcsolót ki kell kapcsolni.");
        return false;
    }
    /* Slave esetén Wi-Fi nélkül is működik az adat- és időszinkron. */
    cJSON *wifi = get_object_section(root, "wifi", !slave, error, error_len);
    if (!wifi && (!slave || (error && *error))) {
        return false;
    }

    if (wifi && (!read_string(wifi, "ssid", cfg->wifi.ssid, sizeof(cfg->wifi.ssid), !slave, slave, error, error_len) ||
        !read_string(wifi, "password", cfg->wifi.password, sizeof(cfg->wifi.password), false, true, error, error_len) ||
        !read_u32(wifi, "reconnect_seconds", &cfg->wifi.reconnect_seconds, 1, 3600, false, error, error_len))) {
        return false;
    }

    /* Statikus IP opcionális; engedélyezve mind a négy cím kötelezően valid. */
    cJSON *network = wifi ? get_object_section(wifi, "network", false, error, error_len) : NULL;
    if (network) {
        if (!read_bool01(network, "use_static_ip", &cfg->wifi.network.use_static_ip, false, error, error_len) ||
            !read_string(network, "ip", cfg->wifi.network.ip, sizeof(cfg->wifi.network.ip), false, true, error, error_len) ||
            !read_string(network, "gateway", cfg->wifi.network.gateway, sizeof(cfg->wifi.network.gateway), false, true, error, error_len) ||
            !read_string(network, "subnet", cfg->wifi.network.subnet, sizeof(cfg->wifi.network.subnet), false, true, error, error_len) ||
            !read_string(network, "dns", cfg->wifi.network.dns, sizeof(cfg->wifi.network.dns), false, true, error, error_len)) {
            return false;
        }
    } else if (error && *error) {
        return false;
    }

    if ((!slave || cfg->wifi.ssid[0]) && cfg->wifi.network.use_static_ip &&
        (!valid_ipv4(cfg->wifi.network.ip) ||
         !valid_ipv4(cfg->wifi.network.gateway) ||
         !valid_ipv4(cfg->wifi.network.subnet) ||
         !valid_ipv4(cfg->wifi.network.dns))) {
        set_error(
            error,
            error_len,
            "Statikus IP esetén az ip/gateway/subnet/dns mezők kötelező érvényes IPv4 címek.");
        return false;
    }

    /* Időkezelési szekció. */
    cJSON *time_cfg = slave ? NULL : get_object_section(root, "time", false, error, error_len);
    if (time_cfg) {
        if (!read_string(time_cfg, "ntp_server", cfg->time.ntp_server, sizeof(cfg->time.ntp_server), false, false, error, error_len) ||
            !read_string(time_cfg, "timezone", cfg->time.timezone, sizeof(cfg->time.timezone), false, false, error, error_len) ||
            !read_bool01(time_cfg, "auto_dst", &cfg->time.auto_dst, false, error, error_len) ||
            !read_i32(time_cfg, "utc_offset_minutes", &cfg->time.utc_offset_minutes, -720, 840, false, error, error_len) ||
            !read_u32(time_cfg, "startup_retry_seconds", &cfg->time.startup_retry_seconds, 1, 3600, false, error, error_len) ||
            !read_u32(time_cfg, "resync_hours", &cfg->time.resync_hours, 1, 168, false, error, error_len) ||
            !read_u32(time_cfg, "background_retry_minutes", &cfg->time.background_retry_minutes, 1, 1440, false, error, error_len)) {
            return false;
        }
    } else if (error && *error) {
        return false;
    }

    if (cfg->data_recept_mode == APP_DATA_MQTT) {
        /* MQTT szekció kötelező, a broker cím nélkül nem tudunk működni. */
        cJSON *mqtt = get_object_section(root, "mqtt", true, error, error_len);
        if (!mqtt) {
            return false;
        }

        uint32_t mqtt_port = cfg->mqtt.port;
        if (!read_string(mqtt, "broker", cfg->mqtt.broker, sizeof(cfg->mqtt.broker), true, false, error, error_len) ||
            !read_u32(mqtt, "port", &mqtt_port, 1, 65535, false, error, error_len) ||
            !read_string(mqtt, "topic", cfg->mqtt.topic, sizeof(cfg->mqtt.topic), false, false, error, error_len) ||
            !read_string(mqtt, "username", cfg->mqtt.username, sizeof(cfg->mqtt.username), false, true, error, error_len) ||
            !read_string(mqtt, "password", cfg->mqtt.password, sizeof(cfg->mqtt.password), false, true, error, error_len) ||
            !read_u32(mqtt, "data_timeout_seconds", &cfg->mqtt.data_timeout_seconds, 5, 3600, false, error, error_len) ||
            !read_u32(mqtt, "reconnect_seconds", &cfg->mqtt.reconnect_seconds, 1, 3600, false, error, error_len)) {
            return false;
        }
        cfg->mqtt.port = (uint16_t)mqtt_port;

    }

    /* Opcionális tájolás; régi config.json esetén marad a 270 fok. */
    cJSON *display = get_object_section(root, "display", false, error, error_len);
    if (display) {
        uint32_t day = cfg->display.day_brightness;
        uint32_t night = cfg->display.night_brightness;
        uint32_t rotation = cfg->display.rotation_degrees;

        if (!read_u32(display, "day_brightness", &day, 1, 100, false, error, error_len) ||
            !read_u32(display, "night_brightness", &night, 1, 100, false, error, error_len) ||
            !read_u32(display, "rotation_degrees", &rotation, 90, 270, false, error, error_len)) {
            return false;
        }
        if (rotation != 90 && rotation != 270) {
            set_error(error, error_len, "A display.rotation_degrees csak 90 vagy 270 lehet.");
            return false;
        }

        cfg->display.day_brightness = (uint8_t)day;
        cfg->display.night_brightness = (uint8_t)night;
        cfg->display.rotation_degrees = (uint16_t)rotation;
    } else if (error && *error) {
        return false;
    }

    /* Statisztikai mentés és maximális integrációs rés. */
    cJSON *statistics = get_object_section(root, "statistics", false, error, error_len);
    if (statistics) {
        if (!read_u32(statistics, "save_interval_minutes", &cfg->statistics.save_interval_minutes, 5, 60, false, error, error_len) ||
            !read_u32(statistics, "max_integration_gap_seconds", &cfg->statistics.max_integration_gap_seconds, 1, 300, false, error, error_len)) {
            return false;
        }
        /* A régi SD config.json még 60 percet írhat elő. A firmware
         * ezt migráció nélkül, biztonságosan legfeljebb 15 percre korlátozza. */
        if (cfg->statistics.save_interval_minutes > 15)
            cfg->statistics.save_interval_minutes = 15;
    } else if (error && *error) {
        return false;
    }

    /* Pénzügyi és környezeti tényezők. */
    cJSON *energy = get_object_section(root, "energy", false, error, error_len);
    if (energy) {
        if (!read_double(energy, "battery_capacity_kwh", &cfg->energy.battery_capacity_kwh, 0, 1000, false, error, error_len) ||
            !read_double(energy, "electricity_purchase_price", &cfg->energy.electricity_purchase_price, 0, 100000, false, error, error_len) ||
            !read_double(energy, "feed_in_price", &cfg->energy.feed_in_price, 0, 100000, false, error, error_len) ||
            !read_double(energy, "co2_factor_kg_per_kwh", &cfg->energy.co2_factor_kg_per_kwh, 0, 100, false, error, error_len) ||
            !read_double(energy, "tree_absorption_kg_per_year", &cfg->energy.tree_absorption_kg_per_year, 0.001, 100000, false, error, error_len)) {
            return false;
        }
    } else if (error && *error) {
        return false;
    }

    /*
     * OTA hitelesítés opcionális, de félpár nem lehet:
     * vagy mindkettő üres, vagy felhasználónév és jelszó is legyen megadva.
     */
    cJSON *ota = get_object_section(root, "ota", false, error, error_len);
    if (ota) {
        if (!read_string(ota, "username", cfg->ota.username, sizeof(cfg->ota.username), false, true, error, error_len) ||
            !read_string(ota, "password", cfg->ota.password, sizeof(cfg->ota.password), false, true, error, error_len) ||
            !read_bool01(ota, "auto_update", &cfg->ota.auto_update, false, error, error_len)) {
            return false;
        }

        if ((cfg->ota.username[0] == 0) != (cfg->ota.password[0] == 0)) {
            set_error(
                error,
                error_len,
                "Az OTA felhasználónév és jelszó vagy mindkettő legyen kitöltve, vagy mindkettő maradjon üres.");
            return false;
        }
    } else if (error && *error) {
        return false;
    }

    return true;
}

/* A config.json fájl teljes beolvasása és feldolgozása. */
esp_err_t app_config_load_file(
    const char *path,
    app_config_t *cfg,
    char *error,
    size_t error_len)
{
    if (!path || !cfg) {
        set_error(error, error_len, "Érvénytelen config paraméter.");
        return ESP_ERR_INVALID_ARG;
    }

    if (error && error_len) {
        error[0] = 0;
    }

    /* Az opcionális mezők miatt mindig az alapértékekből indulunk. */
    app_config_set_defaults(cfg);

    FILE *file = fopen(path, "rb");
    if (!file) {
        set_error(error, error_len, "Nem található vagy nem olvasható: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        set_error(error, error_len, "Nem olvasható a config fájl mérete.");
        return ESP_FAIL;
    }

    long size = ftell(file);
    if (size <= 0 || size > CONFIG_FILE_MAX_BYTES) {
        fclose(file);
        set_error(
            error,
            error_len,
            "A config.json mérete érvénytelen (max %d byte).",
            CONFIG_FILE_MAX_BYTES);
        return ESP_ERR_INVALID_SIZE;
    }

    rewind(file);

    char *buffer = calloc(1, (size_t)size + 1);
    if (!buffer) {
        fclose(file);
        set_error(error, error_len, "Nincs elég memória a config betöltéséhez.");
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);

    if (read != (size_t)size) {
        free(buffer);
        set_error(error, error_len, "A config.json nem olvasható végig.");
        return ESP_FAIL;
    }

    /*
     * A konfiguráció parserének a teljes fájlt el kell fogyasztania.
     * Így egy érvényes JSON objektum után maradt véletlen vagy sérült adat
     * nem kerül csendben figyelmen kívül.
     */
    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(
        buffer,
        (size_t)size,
        &parse_end,
        false);

    if (!root || !cJSON_IsObject(root)) {
        free(buffer);
        cJSON_Delete(root);
        set_error(error, error_len, "A config.json nem érvényes JSON objektum.");
        return ESP_ERR_INVALID_ARG;
    }

    while (parse_end < buffer + size &&
           (*parse_end == ' ' ||
            *parse_end == '\t' ||
            *parse_end == '\r' ||
            *parse_end == '\n')) {
        parse_end++;
    }

    if (parse_end != buffer + size) {
        free(buffer);
        cJSON_Delete(root);
        set_error(
            error,
            error_len,
            "A config.json az érvényes JSON után további adatot tartalmaz.");
        return ESP_ERR_INVALID_ARG;
    }

    free(buffer);

    bool ok = parse_config(root, cfg, error, error_len);
    cJSON_Delete(root);

    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}

