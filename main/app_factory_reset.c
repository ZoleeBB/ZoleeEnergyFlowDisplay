/* A törlés naplója külön NVS-névtérben túléli az alkalmazás adatainak törlését.
 * A következő boot egészen a befejezésig nem léphet a régi flash-/SD-profil
 * betöltésére. Megszakadt törlésnél minden lépés ismételhető. */
#include "app_factory_reset.h"
#include "app_factory_reset_storage.h"
#include "app_backup.h"
#include "app_wifi.h"
#include "nvs.h"
#include <string.h>

enum { RESET_NEW, RESET_RADIO_DONE, RESET_STORAGE_DONE };
_Static_assert(sizeof(app_reset_card_t) == 16 && sizeof(app_reset_plan_t) == 20,
               "A resetjelzo NVS formatuma megvaltozott");

static esp_err_t read_record(const char *ns, const char *key, void *record, size_t capacity)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len = capacity;
    err = nvs_get_blob(h, key, record, &len);
    nvs_close(h);
    return err == ESP_OK && len != capacity ? ESP_ERR_INVALID_SIZE : err;
}

static esp_err_t write_record(const char *ns, const char *key, const void *value, size_t size)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = value ? nvs_set_blob(h, key, value, size) : nvs_erase_key(h, key);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t app_factory_reset_read(app_reset_plan_t *plan)
{
    if (!plan) return ESP_ERR_INVALID_ARG;
    memset(plan, 0, sizeof(*plan));
    esp_err_t err = read_record("factory_reset", "pending", plan, sizeof(*plan));
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    return plan->version == 1 && plan->completed <= RESET_STORAGE_DONE && (plan->scope == APP_RESET_INTERNAL || plan->scope == APP_RESET_WITH_CARD)
        ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t app_factory_reset_remember_sd(void)
{
    app_reset_card_t current, previous;
    esp_err_t err = app_factory_reset_card(&current);
    if (err != ESP_OK) return err;
    err = read_record("device_boot", "sd_origin", &previous, sizeof(previous));
    if (err == ESP_OK && !memcmp(&previous, &current, sizeof(current))) return ESP_OK;
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    return write_record("device_boot", "sd_origin", &current, sizeof(current));
}

/* A korábbi kiadások a kártyára írt config.json gyorsítótárát és a belső
 * setup-profilt azonos formában mentették. Az SD-t jelző szerep/flash-mentés
 * alapján már a jóváhagyáskor felismerhető a hiányzó kártya. */
static esp_err_t legacy_card_required(bool *required)
{
    uint8_t role[3];
    esp_err_t err = read_record("device_boot", "role", role, sizeof(role));
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (err == ESP_OK) {
        if (role[0] != 'Z' || role[1] != 1 || (role[2] != 1 && role[2] != 2))
            return ESP_ERR_INVALID_STATE;
        if (role[2] == 2) {
            *required = true;
            return ESP_OK;
        }
        /* A 3.x kártya nélküli Slave belső rekordja a "config", illetve
         * gyári alapértékeknél nincs JSON. Az SD-ről gyorsítótárazott régi
         * v4 Slave viszont a szerep mellé csak setup_v4 rekordot írt. */
        nvs_handle_t h;
        err = nvs_open("device_boot", NVS_READONLY, &h);
        if (err != ESP_OK) return err;
        size_t len = 0;
        esp_err_t setup = nvs_get_blob(h, "setup_v4", NULL, &len);
        len = 0;
        esp_err_t internal = nvs_get_blob(h, "config", NULL, &len);
        nvs_close(h);
        if (setup != ESP_OK && setup != ESP_ERR_NVS_NOT_FOUND) return setup;
        if (internal != ESP_OK && internal != ESP_ERR_NVS_NOT_FOUND) return internal;
        if (setup == ESP_OK && internal == ESP_ERR_NVS_NOT_FOUND) {
            *required = true;
            return ESP_OK;
        }
    }
    app_backup_flash_info_t info;
    err = app_backup_flash_info(&info);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) return err;
    *required = err == ESP_OK && info.available;
    return ESP_OK;
}

esp_err_t app_factory_reset_request(bool require_sd)
{
    app_reset_plan_t plan;
    esp_err_t err = app_factory_reset_read(&plan);
    if (err != ESP_OK || plan.scope) return err;

    app_reset_card_t saved;
    err = read_record("device_boot", "sd_origin", &saved, sizeof(saved));
    bool known = err == ESP_OK;
    if (!known && err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (!known && !require_sd) {
        err = legacy_card_required(&require_sd);
        if (err != ESP_OK) return err;
    }
    plan.version = 1;
    plan.scope = known || require_sd ? APP_RESET_WITH_CARD : APP_RESET_INTERNAL;
    if (plan.scope == APP_RESET_WITH_CARD) {
        err = app_factory_reset_card(&plan.card);
        if (err != ESP_OK) return err;
        if (known && memcmp(&saved, &plan.card, sizeof(saved))) return ESP_ERR_INVALID_STATE;
    }
    return write_record("factory_reset", "pending", &plan, sizeof(plan));
}

static esp_err_t check_card(const app_reset_plan_t *plan, const char *sd_root)
{
    if (!sd_root || plan->scope == APP_RESET_INTERNAL)
        return (!sd_root && plan->scope == APP_RESET_INTERNAL) ? ESP_OK : ESP_ERR_INVALID_STATE;
    app_reset_card_t mounted;
    esp_err_t err = app_factory_reset_card(&mounted);
    if (err != ESP_OK) return err;
    return memcmp(&mounted, &plan->card, sizeof(mounted)) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static esp_err_t checkpoint(app_reset_plan_t *plan, uint8_t completed)
{
    app_reset_plan_t next = *plan;
    next.completed = completed;
    esp_err_t err = write_record("factory_reset", "pending", &next, sizeof(next));
    if (err == ESP_OK) *plan = next;
    return err;
}

esp_err_t app_factory_reset_apply(app_reset_plan_t *plan, const char *sd_root, app_reset_stage_t *stage)
{
    if (!plan || !stage || plan->version != 1 || plan->completed > RESET_STORAGE_DONE ||
        (plan->scope != APP_RESET_WITH_CARD && plan->scope != APP_RESET_INTERNAL)) return ESP_ERR_INVALID_ARG;
    *stage = APP_RESET_CARD;
    esp_err_t err = check_card(plan, sd_root);
    if (err != ESP_OK) return err;
    if (plan->completed < RESET_RADIO_DONE) {
        *stage = APP_RESET_RADIO;
        err = app_wifi_factory_reset_credentials();
        if (err != ESP_OK) return err;
        *stage = APP_RESET_JOURNAL;
        err = checkpoint(plan, RESET_RADIO_DONE);
        if (err != ESP_OK) return err;
    }
    if (plan->completed < RESET_STORAGE_DONE) {
        *stage = APP_RESET_STORAGE;
        err = app_factory_reset_storage(sd_root);
        if (err != ESP_OK) return err;
        /* Kivett kártyánál a törlés nem könyvelhető el sikeresként. */
        *stage = APP_RESET_CARD;
        err = check_card(plan, sd_root);
        if (err != ESP_OK) return err;
        *stage = APP_RESET_JOURNAL;
        err = checkpoint(plan, RESET_STORAGE_DONE);
        /* A C6 modul Hosted Wi-Fi inicializáláskor eseménykezelőt regisztrál.
         * A friss boot tiszta transzporttal folytatja a felhasználói párosítást. */
        if (err == ESP_OK) *stage = APP_RESET_RESTART;
        return err;
    }
    *stage = APP_RESET_JOURNAL;
    return write_record("factory_reset", "pending", NULL, 0);
}
