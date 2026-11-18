/* A kezelt fájlokat név szerint töröljük. Egyetlen könyvtárat vagy teljes
 * NVS-partíciót sem ürítünk: a felhasználó fájljai és a resetjelző megmaradnak. */
#include "app_factory_reset_storage.h"
#include "app_backup.h"
#include "esp_partition.h"
#include "nvs.h"
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static esp_err_t remove_file(const char *root, const char *name, const char *suffix)
{
    char path[256];
    int n = snprintf(path, sizeof(path), "%s/%s%s", root, name, suffix);
    if (n < 0 || (size_t)n >= sizeof(path)) return ESP_ERR_INVALID_SIZE;
    struct stat st;
    if (stat(path, &st) != 0) return errno == ENOENT ? ESP_OK : ESP_FAIL;
    if (!S_ISREG(st.st_mode)) return ESP_ERR_INVALID_STATE;
    return unlink(path) == 0 ? ESP_OK : ESP_FAIL;
}

static bool history_file(const char *name)
{
    if (strlen(name) < 15 || name[4] != '-' || name[7] != '-') return false;
    for (unsigned i = 0; i < 10; ++i) {
        if (i != 4 && i != 7 && (name[i] < '0' || name[i] > '9')) return false;
    }
    const char *ending = name + 10;
    if (strcasecmp(ending, ".json") && strcasecmp(ending, ".json.tmp") &&
        strcasecmp(ending, ".json.bak")) return false;

    int year = (name[0] - '0') * 1000 + (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
    int month = (name[5] - '0') * 10 + (name[6] - '0');
    int day = (name[8] - '0') * 10 + (name[9] - '0');
    static const unsigned char days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!year || month < 1 || month > 12 || !day) return false;
    bool leap_day = month == 2 && !(year % 4) && (year % 100 || !(year % 400));
    return day <= days[month-1] + leap_day;
}

static esp_err_t remove_history(const char *root)
{
    char path[256];
    int n = snprintf(path, sizeof(path), "%s/stats", root);
    if (n < 0 || (size_t)n >= sizeof(path)) return ESP_ERR_INVALID_SIZE;
    DIR *dir = opendir(path);
    if (!dir) return errno == ENOENT ? ESP_OK : ESP_FAIL;
    esp_err_t err = ESP_OK;
    while (err == ESP_OK) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) err = ESP_FAIL;
            break;
        }
        if (history_file(entry->d_name)) err = remove_file(path, entry->d_name, "");
    }
    if (closedir(dir) != 0 && err == ESP_OK) err = ESP_FAIL;
    return err;
}

static esp_err_t check_sd(const char *root)
{
    DIR *dir = opendir(root);
    return dir && closedir(dir) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t erase_sd(const char *root)
{
    static const char *const settings[] = {
        "config.json", "loxone.json", "loxone-home.json", "templates/order.txt", "templates/selection.txt"
    };
    static const char *const restore_suffixes[] = {"", ".rstmp", ".rsbak"};
    static const char *const other_files[] = {
        "config.json.webtmp", "config.json.webbak", "config.json.rscheck",
        "loxone.json.tmp", "loxone.json.bak", "loxone-home.json.tmp", "loxone-home.json.bak",
        "templates/order.tmp", "templates/order.bak", "templates/selection.tmp", "templates/selection.bak",
        "logs/events.log", "logs/events-prev.log"
    };
    esp_err_t err = check_sd(root);
    if (err != ESP_OK) return err;
    for (size_t i = 0; i < sizeof(settings)/sizeof(settings[0]); ++i) {
        for (size_t j = 0; j < sizeof(restore_suffixes)/sizeof(restore_suffixes[0]); ++j) {
            err = remove_file(root, settings[i], restore_suffixes[j]);
            if (err != ESP_OK) return err;
        }
    }
    for (size_t i = 0; i < sizeof(other_files)/sizeof(other_files[0]); ++i) {
        err = remove_file(root, other_files[i], "");
        if (err != ESP_OK) return err;
    }
    err = remove_history(root);
    return err == ESP_OK ? check_sd(root) : err;
}

static esp_err_t erase_namespace(const char *name)
{
    nvs_handle_t handle;
    /* READWRITE létrehozná a hiányzó névteret egy majdnem teli NVS-ben is. */
    esp_err_t err = nvs_open(name, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    nvs_close(handle);
    err = nvs_open(name, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t app_factory_reset_storage(const char *sd_root)
{
    if (sd_root) {
        esp_err_t err = erase_sd(sd_root);
        if (err != ESP_OK) return err;
    }

    /* Mindkét slot teljes tartalma törlődik, a rövidebb mentések korábbi
     * maradéka is. A storage többi részéhez és az OTA-hoz nem nyúlunk. */
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, APP_BACKUP_FLASH_PARTITION);
    if (part) {
        size_t backup_bytes = APP_BACKUP_FLASH_SLOT_COUNT * APP_BACKUP_FLASH_SLOT_SIZE;
        if (part->size < backup_bytes) return ESP_ERR_INVALID_SIZE;
        esp_err_t err = esp_partition_erase_range(part, 0, backup_bytes);
        if (err != ESP_OK) return err;
    }

    /* A STA WIFI_STORAGE_RAM-ot használ; a Wi-Fi hitelesítést a device_boot,
     * a védett BLE-profil, az SD config és a két flash-tartalék hordozza.
     * Az ESP-IDF 5.5.4 NimBLE "nimble_bond" névterében vannak a kötések és a helyi IRK. */
    static const char *const namespaces[] = {
        "device_boot", "display_ui", "ble_profile", "ble_presence", "nimble_bond",
        "inverter_v4", "loxone_secret", "ota_auto", "ota_result"
    };
    for (size_t i = 0; i < sizeof(namespaces)/sizeof(namespaces[0]); ++i) {
        esp_err_t err = erase_namespace(namespaces[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
