/* Az NVS commit és a rádió/SD hibái a tényleges tranzakció bármelyik szakasza
 * között felléphetnek. Az új boot kizárólag a tartós rekordot kapja meg. */
#include "app_factory_reset.h"
#include "app_backup.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { JOURNAL = 1, BOOT };
typedef struct { uint8_t raw[64]; size_t size; } blob_t;
static blob_t marker, origin, role, old_setup, old_internal, pending;
static blob_t *changed;
static bool card_ok, radio_ok = true, erase_ok = true, remove_card, backup, commit_ok = true, flash_read_ok = true;
static unsigned radio_calls, erases, writes;
static app_reset_card_t cid = {.serial = 300};
static const char *mountpoint = "/sdcard";

static blob_t *blob(nvs_handle_t h, const char *key)
{
    if (h == JOURNAL && !strcmp(key, "pending")) return &marker;
    assert(h == BOOT);
    if (!strcmp(key, "sd_origin")) return &origin;
    if (!strcmp(key, "role")) return &role;
    if (!strcmp(key, "setup_v4")) return &old_setup;
    assert(!strcmp(key, "config"));
    return &old_internal;
}
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h)
{
    assert(mode == NVS_READONLY || mode == NVS_READWRITE);
    if (!strcmp(ns, "factory_reset")) *h = JOURNAL;
    else { assert(!strcmp(ns, "device_boot")); *h = BOOT; }
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; changed = NULL; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *value, size_t *len)
{
    blob_t *b = blob(h, key);
    if (!b->size) return ESP_ERR_NVS_NOT_FOUND;
    if (value && b->size > *len) return ESP_ERR_INVALID_SIZE;
    *len = b->size;
    if (value) memcpy(value, b->raw, b->size);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t len)
{
    assert(len && len <= sizeof(pending.raw));
    changed = blob(h, key);
    memcpy(pending.raw, value, len);
    pending.size = len;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    changed = blob(h, key);
    pending.size = 0;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    assert(changed);
    if (!commit_ok) return ESP_FAIL;
    *changed = pending;
    ++writes;
    return ESP_OK;
}
esp_err_t app_factory_reset_card(app_reset_card_t *out)
{
    if (!card_ok) return ESP_ERR_INVALID_STATE;
    *out = cid;
    return ESP_OK;
}
esp_err_t app_backup_flash_info(app_backup_flash_info_t *info)
{
    info->available = backup;
    return flash_read_ok ? ESP_OK : ESP_FAIL;
}
esp_err_t app_wifi_factory_reset_credentials(void)
{
    ++radio_calls;
    return radio_ok ? ESP_OK : ESP_FAIL;
}
esp_err_t app_factory_reset_storage(const char *sd)
{
    ++erases;
    assert(!sd || (card_ok && !strcmp(sd, mountpoint)));
    if (remove_card) card_ok = false;
    if (!erase_ok) return ESP_FAIL;
    origin = role = old_setup = old_internal = (blob_t){0};
    backup = false;
    return ESP_OK;
}

static app_reset_stage_t apply(const char *sd, esp_err_t expected)
{
    app_reset_plan_t cold;
    assert(app_factory_reset_read(&cold) == ESP_OK && cold.scope);
    app_reset_stage_t stage;
    assert(app_factory_reset_apply(&cold, sd, &stage) == expected);
    return stage;
}

int main(void)
{
    app_reset_plan_t cold;
    assert(app_factory_reset_read(&cold) == ESP_OK && !cold.scope);
    assert(app_factory_reset_request(true) == ESP_ERR_INVALID_STATE && !marker.size && !erases);

    /* Hiányzó kártyáról induló régi fő kijelző, illetve SD-ről gyorsítótárazott Slave. */
    role = (blob_t){.raw = {'Z',1,2}, .size = 3};
    assert(app_factory_reset_request(false) == ESP_ERR_INVALID_STATE && !marker.size);
    role.raw[2] = 1;
    old_setup.size = 7;
    assert(app_factory_reset_request(false) == ESP_ERR_INVALID_STATE && !marker.size);
    old_setup.size = 0;
    backup = true;
    assert(app_factory_reset_request(false) == ESP_ERR_INVALID_STATE && !marker.size);
    backup = false;
    flash_read_ok = false;
    assert(app_factory_reset_request(false) == ESP_FAIL && !marker.size);
    flash_read_ok = true;

    /* Régi kártya nélküli Slave (belső profil), illetve először beállított
     * 10.1-es: idegen behelyezett kártyán egyik sem törölhet adatot. */
    old_internal.size = 9;
    assert(app_factory_reset_request(false) == ESP_OK && marker.size && !erases);
    assert(apply(mountpoint, ESP_ERR_INVALID_STATE) == APP_RESET_CARD && !radio_calls);
    assert(apply(NULL, ESP_OK) == APP_RESET_RESTART && erases == 1 && marker.size);
    assert(apply(NULL, ESP_OK) == APP_RESET_JOURNAL && !marker.size && radio_calls == 1);
    assert(app_factory_reset_read(&cold) == ESP_OK && !cold.scope);
    assert(app_factory_reset_request(false) == ESP_OK);
    assert(apply(NULL, ESP_OK) == APP_RESET_RESTART && marker.size);
    assert(apply(NULL, ESP_OK) == APP_RESET_JOURNAL && !marker.size);
    card_ok = true;
    assert(app_factory_reset_remember_sd() == ESP_OK && origin.size);
    unsigned saved_writes = writes;
    assert(app_factory_reset_remember_sd() == ESP_OK && writes == saved_writes);
    card_ok = false;
    assert(app_factory_reset_request(false) == ESP_ERR_INVALID_STATE && !marker.size);
    card_ok = true;
    ++cid.serial;
    assert(app_factory_reset_request(true) == ESP_ERR_INVALID_STATE && !marker.size);
    --cid.serial;
    commit_ok = false;
    assert(app_factory_reset_request(true) == ESP_FAIL && !marker.size);
    commit_ok = true;
    assert(app_factory_reset_request(true) == ESP_OK && marker.size);
    saved_writes = writes;
    assert(app_factory_reset_request(false) == ESP_OK && writes == saved_writes);
    unsigned before = erases;
    assert(apply(NULL, ESP_ERR_INVALID_STATE) == APP_RESET_CARD && erases == before);
    ++cid.serial;
    assert(apply(mountpoint, ESP_ERR_INVALID_STATE) == APP_RESET_CARD && erases == before);
    --cid.serial;
    unsigned radio_before = radio_calls;
    radio_ok = false;
    assert(apply(mountpoint, ESP_FAIL) == APP_RESET_RADIO && erases == before && marker.size);
    radio_ok = true;
    erase_ok = false;
    assert(apply(mountpoint, ESP_FAIL) == APP_RESET_STORAGE && radio_calls == radio_before + 2);
    erase_ok = true;
    remove_card = true;
    assert(apply(mountpoint, ESP_ERR_INVALID_STATE) == APP_RESET_CARD && marker.size);
    card_ok = true;
    remove_card = false;
    assert(apply(mountpoint, ESP_OK) == APP_RESET_RESTART && marker.size && radio_calls == radio_before + 2);
    unsigned durable_erases = erases;
    commit_ok = false;
    assert(apply(mountpoint, ESP_FAIL) == APP_RESET_JOURNAL && marker.size);
    commit_ok = true;
    assert(apply(mountpoint, ESP_OK) == APP_RESET_JOURNAL && !marker.size && erases == durable_erases);
    assert(app_factory_reset_read(&cold) == ESP_OK && !cold.scope);
    marker.size = 1;
    assert(app_factory_reset_read(&cold) == ESP_ERR_INVALID_SIZE);
    assert(app_factory_reset_request(false) == ESP_ERR_INVALID_SIZE);
    puts("Gyári visszaállítás: tartós szakaszok, kártyaazonosság és megszakított indítás rendben.");
}
