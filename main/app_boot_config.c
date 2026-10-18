/* Verziótól független JSON NVS-beállítások, külön tartós szerepjelölővel. */
#include "app_boot_config.h"
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
