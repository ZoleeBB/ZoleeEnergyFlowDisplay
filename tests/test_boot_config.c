/* Valódi boot-konfiguráció JSON parserrel és tartós NVS hibainjektálással. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_boot_config.h"
#include "nvs.h"
static uint8_t role[3];
static char config[4096];
static size_t config_size;
static bool have_role, legacy_display, legacy_pin, fail_open, fail_commit;
static unsigned writes;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{
    if (fail_open)
        return ESP_FAIL;
    if (!strcmp(name, "device_boot"))
    {
        assert(mode == NVS_READWRITE);
        *h = 1;
        return ESP_OK;
    }
    assert(mode == NVS_READONLY);
    if (!strcmp(name, "display_ui") && legacy_display)
    {
        *h = 2;
        return ESP_OK;
    }
    if (!strcmp(name, "ble_profile") && legacy_pin)
    {
        *h = 3;
        return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    assert(h == 1);
    const void *data;
    size_t size;
    if (!strcmp(key, "role"))
    {
        if (!have_role)
            return ESP_ERR_NVS_NOT_FOUND;
        data = role;
        size = 3;
    }
    else
    {
        assert(!strcmp(key, "config"));
        if (!config_size)
            return ESP_ERR_NVS_NOT_FOUND;
        data = config;
        size = config_size;
    }
    if (*len < size)
        return ESP_ERR_INVALID_SIZE;
    memcpy(out, data, size);
    *len = size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t length)
{
    assert(h == 1);
    writes++;
    if (fail_commit)
        return ESP_OK;
    if (!strcmp(key, "role"))
    {
        assert(length == 3);
        memcpy(role, data, 3);
        have_role = true;
    }
    else
    {
        assert(!strcmp(key, "config") && length <= 3072);
        memcpy(config, data, length);
        config_size = length;
    }
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len)
{
    assert(h == 3 && !strcmp(key, "pin") && *len >= 7);
    memcpy(out, "012345", 7);
    *len = 7;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    assert(h == 1);
    return fail_commit ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t h)
{
    (void)h;
}
static char error[256];
static app_config_t cfg;
static esp_err_t fallback(void)
{
    return app_boot_config_fallback(&cfg, error, sizeof(error));
}
int main(void)
{
    assert(fallback() == ESP_OK && cfg.cardless_slave && cfg.data_recept_mode == APP_DATA_SLAVE &&
           !cfg.data_seed_enabled);
    assert(have_role && role[2] == 1);
    unsigned saved = writes;
    assert(fallback() == ESP_OK && writes == saved);
    const char *json =
        "{\"DataReceptMode\":\"Slave\",\"DataSeedEnabled\":0,\"project\":{\"language\":\"en\"}}";
    assert(app_boot_config_save(json, strlen(json), &cfg, error, sizeof(error)) == ESP_OK);
    assert(cfg.cardless_slave && config_size > 0);
    memset(&cfg, 0, sizeof(cfg));
    assert(fallback() == ESP_OK && cfg.cardless_slave);
    assert(!strcmp(cfg.project.language, "en"));
    assert(app_boot_config_save("{", 1, &cfg, error, sizeof(error)) != ESP_OK);
    const char *main_json = "{\"DataReceptMode\":\"MQTT\"}";
    assert(app_boot_config_save(main_json, strlen(main_json), &cfg, error, sizeof(error)) !=
           ESP_OK);
    const char *relay = "{\"DataReceptMode\":\"Slave\",\"DataSeedEnabled\":1}";
    assert(app_boot_config_save(relay, strlen(relay), &cfg, error, sizeof(error)) != ESP_OK);
    fail_commit = true;
    assert(app_boot_config_save(json, strlen(json), &cfg, error, sizeof(error)) == ESP_FAIL);
    fail_commit = false;
    app_config_set_defaults(&cfg);
    assert(app_boot_config_remember(&cfg) == ESP_OK && role[2] == 2);
    assert(fallback() == ESP_ERR_INVALID_STATE);
    have_role = false;
    legacy_display = true;
    assert(fallback() == ESP_ERR_INVALID_STATE);
    legacy_display = false;
    legacy_pin = true;
    assert(fallback() == ESP_ERR_INVALID_STATE);
    legacy_pin = false;
    fail_open = true;
    assert(fallback() == ESP_FAIL);
    fail_open = false;
    assert(fallback() == ESP_OK);
    role[0] = 'X';
    assert(fallback() == ESP_ERR_INVALID_STATE);
    role[0] = 'Z';
    config[0] = 'X';
    assert(fallback() != ESP_OK);
    config_size = 4000;
    assert(fallback() == ESP_ERR_INVALID_SIZE);
    config_size = 0;
    have_role = false;
    fail_commit = true;
    assert(fallback() == ESP_FAIL);
    puts("PASS: SD-less boot, NVS JSON roundtrip, role safety, legacy master, corrupt config and "
         "commit/open errors");
}
