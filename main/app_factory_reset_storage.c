/* A kezelt fájlokat név szerint töröljük. Egyetlen könyvtárat vagy teljes
 * NVS-partíciót sem ürítünk: a felhasználó fájljai és a resetjelző megmaradnak. */
#include "app_factory_reset_storage.h"
#include "app_backup_flash_partition.h"
#include "app_stats_files.h"
#include "nvs.h"
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
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
        if (entry->d_type != DT_DIR && app_stats_history_file(entry->d_name))
            err = remove_file(path, entry->d_name, "");
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

/* A nyers olvasás titkosított tárolón is a törölt 0xff mintát látja.
 * A félbemaradt törlés megismétlésekor csak az írt szektorokat terheljük. */
static esp_err_t erase_backups(void)
{
    const esp_partition_t *part = app_backup_flash_partition();
    if (!part) return ESP_OK;
    size_t bytes = APP_BACKUP_FLASH_SLOT_COUNT * APP_BACKUP_FLASH_SLOT_SIZE;
    if (!part->erase_size || bytes % part->erase_size || part->size < bytes)
        return ESP_ERR_INVALID_SIZE;
    unsigned char block[512];
    for (size_t sector = 0; sector < bytes; sector += part->erase_size) {
        size_t end = sector + part->erase_size;
        for (size_t pos = sector; pos < end; pos += sizeof(block)) {
            size_t n = end - pos < sizeof(block) ? end - pos : sizeof(block);
            esp_err_t err = esp_partition_read_raw(part, pos, block, n);
            if (err != ESP_OK) return err;
            size_t i = 0;
            while (i < n && block[i] == 0xff) ++i;
            if (i != n) {
                err = esp_partition_erase_range(part, sector, part->erase_size);
                if (err != ESP_OK) return err;
                break;
            }
        }
    }
    return ESP_OK;
}

esp_err_t app_factory_reset_storage(const char *sd_root)
{
    if (sd_root) {
        esp_err_t err = erase_sd(sd_root);
        if (err != ESP_OK) return err;
    }

    /* Mindkét slot teljes tartalma törlődik, a rövidebb mentések korábbi
     * maradéka is. A storage többi részéhez és az OTA-hoz nem nyúlunk. */
    esp_err_t err = erase_backups();
    if (err != ESP_OK) return err;

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
