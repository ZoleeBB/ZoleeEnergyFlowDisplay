#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "app_factory_reset_storage.h"
#include "esp_partition.h"
#include "nvs.h"

static const char *const namespaces[] = {
    "device_boot", "display_ui", "ble_profile", "ble_presence", "nimble_bond",
    "inverter_v4", "loxone_secret", "ota_auto", "ota_result",
    "factory_reset", "phy", "unrelated"
};
static bool content[12], erased[12];
static const char *fault;
static unsigned flash_calls;
static bool has_partition = true, missing_optional_namespace;
static esp_partition_t part = {.size = 7 * 1024 * 1024};
static unsigned char flash[7 * 1024 * 1024];
static const unsigned backup_end = 2 * 1024 * 1024;

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *name)
{
    assert(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_ANY);
    assert(!strcmp(name, "storage"));
    return has_partition ? &part : NULL;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t start, size_t length)
{
    assert(p == &part && start + length <= backup_end);
    ++flash_calls;
    if (!strcmp(fault, "flash")) {
        memset(flash + start, 0xff, length / 2); /* tápvesztés / félbemaradt művelet */
        return ESP_FAIL;
    }
    memset(flash + start, 0xff, length);
    return ESP_OK;
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out)
{
    for (unsigned i=0; i<12; ++i) {
        if (strcmp(namespaces[i], name)) continue;
        if (!strcmp(fault, "nvs_read") && mode == NVS_READONLY && i == 4) return ESP_FAIL;
        if (!strcmp(fault, "nvs_write") && mode == NVS_READWRITE && i == 4) return ESP_FAIL;
        if (i == 8 && missing_optional_namespace) { /* Az opcionális OTA-névtér üres kártyán nem is létezik. */
            assert(mode == NVS_READONLY);
            return ESP_ERR_NVS_NOT_FOUND;
        }
        *out = i;
        return ESP_OK;
    }
    assert(false);
    return ESP_FAIL;
}

esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    assert(handle < 9);
    if (!strcmp(fault, "nvs_erase") && handle == 4) return ESP_FAIL;
    erased[handle] = true;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(erased[handle]);
    if (!strcmp(fault, "nvs_commit") && handle == 4) return ESP_FAIL;
    content[handle] = false;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle < 12); }

int __real_unlink(const char *);
int __wrap_unlink(const char *path)
{
    if (!strcmp(fault, "sd_write") && strstr(path, "/loxone-home.json")) {
        errno = EACCES;
        return -1;
    }
    return __real_unlink(path);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const char *root = strcmp(argv[1], "--cardless") ? argv[1] : NULL;
    fault = argv[2];
    memset(content, 1, sizeof(content));
    missing_optional_namespace = !root;
    if (missing_optional_namespace) content[8] = false;
    memset(flash, 0x4b, sizeof(flash));
    if (!strcmp(fault, "small_flash")) part.size = 1024;
    if (!strcmp(fault, "missing_flash")) has_partition = false;
    esp_err_t result = app_factory_reset_storage(root);
    bool ok = !strcmp(fault, "none") || !strcmp(fault, "missing_flash");
    assert((result == ESP_OK) == ok);
    if (ok) {
        for (unsigned i=0; i<9; ++i) assert(!content[i]);
        assert(app_factory_reset_storage(root) == ESP_OK);
    } else if (!strncmp(fault, "sd_", 3) || !strcmp(fault, "small_flash")) {
        assert(!flash_calls && content[0] && content[4]);
    } else if (!strcmp(fault, "flash")) {
        assert(flash_calls && content[0] && content[4]);
    } else {
        assert(!content[0] && content[4]);
    }
    /* Megszakított SD/NVS/flash művelet után, ugyanazzal a tartós adathalmazzal. */
    if (!ok && strcmp(fault, "sd_unmounted") && strcmp(fault, "sd_conflict")) {
        fault = "none";
        part.size = sizeof(flash);
        assert(app_factory_reset_storage(root) == ESP_OK);
        ok = true;
    }
    if (ok) {
        for (unsigned i=0; i<9; ++i) assert(!content[i]);
        for (unsigned i=0; i<backup_end; ++i) assert(flash[i] == (has_partition ? 0xff : 0x4b));
    }
    for (unsigned i=9; i<12; ++i) assert(content[i]);
    for (size_t i=backup_end; i<sizeof(flash); ++i) assert(flash[i] == 0x4b);
    return 0;
}
