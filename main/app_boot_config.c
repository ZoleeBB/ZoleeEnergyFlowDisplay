/* Verziótól független JSON NVS-beállítások, külön tartós szerepjelölővel. */
#include "app_boot_config.h"
#include "app_ble_profile.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "cJSON.h"

#define INTERNAL_CONFIG_MAX 3072

esp_err_t app_boot_config_remember(const app_config_t *cfg)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("device_boot", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    uint8_t role[3] = {'Z', 1, cfg->data_recept_mode == APP_DATA_SLAVE ? 1 : 2};
    uint8_t old[3];
    size_t length = sizeof(old);
    if (nvs_get_blob(handle, "role", old, &length) == ESP_OK && length == sizeof(old) &&
        !memcmp(old, role, sizeof(old)))
    {
        nvs_close(handle);
        return ESP_OK;
    }
    err = nvs_set_blob(handle, "role", role, sizeof(role));
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t legacy_main(bool *found)
{
    /* Régebbi firmware még nem írt szerepjelölőt. Az alkalmazás saját
     * tájolási/master-PIN nyomát konzervatívan korábbi fő kijelzőnek tekintjük. */
    nvs_handle_t handle;
    *found = false;
    esp_err_t err = nvs_open("display_ui", NVS_READONLY, &handle);
    if (err == ESP_OK)
    {
        nvs_close(handle);
        *found = true;
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    err = nvs_open("ble_profile", NVS_READONLY, &handle);
    if (err == ESP_OK)
    {
        char pin[7];
        size_t length = sizeof(pin);
        err = nvs_get_str(handle, "pin", pin, &length);
        *found = err == ESP_OK;
        nvs_close(handle);
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t app_boot_config_fallback(app_config_t *cfg, char *error, size_t error_len)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("device_boot", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    uint8_t role[3] = {0};
    size_t length = sizeof(role);
    err = nvs_get_blob(handle, "role", role, &length);
    bool missing = err == ESP_ERR_NVS_NOT_FOUND;
    bool legacy = false;
    if (missing)
    {
        esp_err_t check = legacy_main(&legacy);
        if (check != ESP_OK)
        {
            nvs_close(handle);
            return check;
        }
    }
    if ((!missing &&
         (err != ESP_OK || length != 3 || role[0] != 'Z' || role[1] != 1 || role[2] != 1)) ||
        (missing && legacy))
    {
        nvs_close(handle);
        snprintf(error,
                 error_len,
                 "Korábbi fő kijelző: SD-hibánál nincs automatikus Slave-váltás. Ellenőrizd az "
                 "SD-kártyát.");
        return ESP_ERR_INVALID_STATE;
    }
    char *json = calloc(1, INTERNAL_CONFIG_MAX + 1);
    if (!json)
    {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    length = INTERNAL_CONFIG_MAX;
    err = nvs_get_blob(handle, "config", json, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        app_config_set_defaults(cfg);
        cfg->data_recept_mode = APP_DATA_SLAVE;
        err = ESP_OK;
    }
    else if (err == ESP_OK && length > 0 && length <= INTERNAL_CONFIG_MAX)
    {
        err = app_config_parse_json(json, length, cfg, error, error_len);
    }
    else if (err == ESP_OK)
        err = ESP_ERR_INVALID_SIZE;
    free(json);
    nvs_close(handle);
    if (err != ESP_OK || cfg->data_recept_mode != APP_DATA_SLAVE || cfg->data_seed_enabled)
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    cfg->cardless_slave = true;
    return app_boot_config_remember(cfg);
}

esp_err_t app_boot_config_save(
    const char *json, size_t length, app_config_t *cfg, char *error, size_t error_len)
{
    esp_err_t err = app_config_parse_json(json, length, cfg, error, error_len);
    if (err != ESP_OK)
        return err;
    if (cfg->data_recept_mode != APP_DATA_SLAVE || cfg->data_seed_enabled)
    {
        snprintf(error, error_len, "SD nélkül kizárólag Slave mód használható.");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_ParseWithLength(json, length);
    char *compact = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!compact)
        return ESP_ERR_NO_MEM;
    size_t bytes = strlen(compact);
    if (bytes > INTERNAL_CONFIG_MAX)
    {
        free(compact);
        snprintf(error, error_len, "Túl nagy belső konfiguráció (max. 3072 bájt).");
        return ESP_ERR_INVALID_SIZE;
    }
    nvs_handle_t handle;
    err = nvs_open("device_boot", NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        err = nvs_set_blob(handle, "config", compact, bytes);
        if (err == ESP_OK)
            err = nvs_commit(handle);
        nvs_close(handle);
    }
    free(compact);
    cfg->cardless_slave = true;
    return err;
}

/* Egyetlen NVS-blobban marad együtt az állapot és a hozzá tartozó JSON.
 * Félbeszakadt első mentésnél vagy a korábbi, vagy a teljes új profil él. */
static atomic_bool s_setup_pending, s_internal;
/* A dedikált 4B és a már üzemelő kártya nélküli Slave belső mentésének szerepkorlátja. */
static bool s_restrict_slave_role;

bool app_boot_config_setup_pending(void) { return atomic_load(&s_setup_pending); }
bool app_boot_config_internal(void) { return atomic_load(&s_internal); }

void app_boot_config_begin_setup(app_config_t *cfg, bool internal)
{
    app_config_set_defaults(cfg);
    s_restrict_slave_role = false;
    atomic_store(&s_internal, internal);
    atomic_store(&s_setup_pending, true);
}

static esp_err_t write_setup_record(const char *json, size_t length, bool pending)
{
    if (!length || length > INTERNAL_CONFIG_MAX)
        return ESP_ERR_INVALID_SIZE;
    char *record = malloc(length + 1);
    if (!record) return ESP_ERR_NO_MEM;
    record[0] = pending ? 'P' : 'C';
    memcpy(record + 1, json, length);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("device_boot", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "setup_v4", record, length + 1);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    free(record);
    return err;
}

/* Régi 4B-n a párosításhoz használt profil a szerepjelölő előtt is élhetett. */
static esp_err_t restore_paired_slave(app_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_profile", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    uint8_t profile[APP_BLE_PROFILE_SIZE];
    size_t length = sizeof(profile);
    err = nvs_get_blob(handle, "slave", profile, &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_TYPE_MISMATCH || err == ESP_ERR_NVS_INVALID_LENGTH)
        return ESP_ERR_NVS_NOT_FOUND;
    if (err != ESP_OK) return err;
    if (!app_ble_profile_decode(profile, length, cfg)) return ESP_ERR_NVS_NOT_FOUND;
    cfg->data_recept_mode = APP_DATA_SLAVE;
    cfg->cardless_slave = true;
    return ESP_OK;
}

static esp_err_t restore_setup_record(nvs_handle_t handle, app_config_t *cfg, bool slave_only,
                                      char *error, size_t error_len)
{
    /* Üres belső tárnál nincs foglalás. A mentett Wi-Fi-nél csak a néhány
     * tucat bájtos rekord kell a boot közben szűkös belső memóriából. */
    size_t length = 0;
    esp_err_t err = nvs_get_blob(handle, "setup_v4", NULL, &length);
    if (err == ESP_ERR_NVS_TYPE_MISMATCH) return ESP_ERR_NVS_NOT_FOUND;
    if (err != ESP_OK) return err;
    if (length < 3 || length > INTERNAL_CONFIG_MAX + 1) return ESP_ERR_NVS_NOT_FOUND;
    char *record = malloc(length);
    if (!record) return ESP_ERR_NO_MEM;
    err = nvs_get_blob(handle, "setup_v4", record, &length);
    if (err == ESP_OK) {
        if (length < 3 || (record[0] != 'P' && record[0] != 'C')) {
            err = ESP_ERR_NVS_NOT_FOUND;
        } else {
            err = app_config_parse_json(record + 1, length - 1, cfg, error, error_len);
            if (err == ESP_ERR_INVALID_ARG) err = ESP_ERR_NVS_NOT_FOUND;
        }
        if (err == ESP_OK && record[0] == 'C' && slave_only && cfg->data_recept_mode != APP_DATA_SLAVE)
            err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            atomic_store(&s_setup_pending, record[0] == 'P');
            cfg->cardless_slave = record[0] == 'C' && cfg->data_recept_mode == APP_DATA_SLAVE;
            if (cfg->cardless_slave) s_restrict_slave_role = true;
        }
    }
    free(record);
    return err;
}

esp_err_t app_boot_config_restore(app_config_t *cfg, bool slave_only, char *error, size_t error_len)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_restrict_slave_role = slave_only;
    atomic_store(&s_internal, true);
    atomic_store(&s_setup_pending, false);
    app_config_set_defaults(cfg);
    nvs_handle_t handle;
    uint8_t role[3] = {0};
    size_t length = sizeof(role);
    esp_err_t err = nvs_open("device_boot", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = restore_setup_record(handle, cfg, slave_only, error, error_len);
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(handle);
            return err;
        }
        /* A félbemaradt JSON-beolvasás értékei ne kerüljenek tovább a
         * korábbi profilba. A hiányzó és a hibás rekord azonos úton folytatja. */
        app_config_set_defaults(cfg);
        err = nvs_get_blob(handle, "role", role, &length);
        nvs_close(handle);
    }
    if (err == ESP_OK && length == sizeof(role) && role[0] == 'Z' && role[1] == 1 && role[2] == 1) {
        s_restrict_slave_role = true;
        return app_boot_config_fallback(cfg, error, error_len);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND && slave_only) {
        err = restore_paired_slave(cfg);
        if (err == ESP_OK) return ESP_OK;
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    app_config_set_defaults(cfg);
    atomic_store(&s_setup_pending, true);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_boot_config_save_setup_wifi(const app_config_t *cfg)
{
    if (!cfg || !cfg->wifi.ssid[0]) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = root ? cJSON_AddObjectToObject(root, "wifi") : NULL;
    char *json = wifi && cJSON_AddStringToObject(wifi, "ssid", cfg->wifi.ssid) &&
        cJSON_AddStringToObject(wifi, "password", cfg->wifi.password)
        ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;
    esp_err_t err = write_setup_record(json, strlen(json), true);
    cJSON_free(json);
    if (err == ESP_OK) atomic_store(&s_setup_pending, true);
    return err;
}

esp_err_t app_boot_config_complete(const char *json, size_t length, app_config_t *cfg,
                                   char *error, size_t error_len)
{
    if (!cfg || !json) return ESP_ERR_INVALID_ARG;
    esp_err_t err = app_config_parse_json(json, length, cfg, error, error_len);
    if (err != ESP_OK) return err;
    if (s_restrict_slave_role && cfg->data_recept_mode != APP_DATA_SLAVE) {
        snprintf(error, error_len, "Ez a kijelző a belső tárból csak Slave módban indítható.");
        return ESP_ERR_INVALID_ARG;
    }
    err = write_setup_record(json, length, false);
    if (err == ESP_OK) {
        cfg->cardless_slave = app_boot_config_internal() && cfg->data_recept_mode == APP_DATA_SLAVE;
    } else if (err == ESP_ERR_INVALID_SIZE) {
        snprintf(error, error_len, "A belső konfiguráció legfeljebb %u bájt lehet.", INTERNAL_CONFIG_MAX);
    }
    return err;
}

void app_boot_config_use_storage(bool internal)
{
    atomic_store(&s_internal, internal);
}

void app_boot_config_use_trusted_backup(void)
{
    atomic_store(&s_setup_pending, false);
}


/* Csak a már ellenőrzött, sikeresen aktivált SD-fájlt másoljuk el. Az NVS
 * változatlan blobnál nem ír új flash-lapot. A bővebb csomag külön is mentődik. */
esp_err_t app_boot_config_cache_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (size <= 0 || size > 16384 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    char *text = malloc((size_t)size);
    if (!text) { fclose(file); return ESP_ERR_NO_MEM; }
    bool ok = fread(text, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    cJSON *root = ok ? cJSON_ParseWithLength(text, (size_t)size) : NULL;
    free(text);
    char *compact = cJSON_IsObject(root) ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!compact) return ESP_FAIL;
    esp_err_t err = write_setup_record(compact, strlen(compact), false);
    cJSON_free(compact);
    return err;
}
