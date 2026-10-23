/* Valódi SD-mentés, megszakadt FAT-csere és sérült fájlok tesztje. */
#include "app_loxone_home_store.h"
#include "app_storage.h"
#include "nvs.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned char device_key[32];
static bool have_key;
void esp_fill_random(void *out, size_t n)
{
    FILE *file = fopen("/dev/urandom", "rb");
    assert(file && fread(out, 1, n, file) == n);
    fclose(file);
}
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h)
{ (void)ns; (void)mode; *h = 1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *name, void *out, size_t *n)
{
    (void)h; (void)name; assert(*n == 32);
    if (!have_key) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(out, device_key, 32); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *name, const void *in, size_t n)
{ (void)h; (void)name; assert(n == 32); memcpy(device_key, in, n); have_key = true; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }

static bool unavailable, sync_error;
static unsigned fail_rename, renames;
esp_err_t app_storage_lock(void) { return unavailable ? ESP_FAIL : ESP_OK; }
void app_storage_unlock(void) {}
int __real_rename(const char *, const char *);
int __wrap_rename(const char *from, const char *to)
{
    if (++renames == fail_rename) { errno = EIO; return -1; }
    return __real_rename(from, to);
}
int __real_fsync(int);
int __wrap_fsync(int fd) { return sync_error ? -1 : __real_fsync(fd); }
static void raw(const char *body)
{
    FILE *file = fopen(APP_SD_MOUNT_POINT "/loxone-home.json", "wb");
    assert(file && fwrite(body, 1, strlen(body), file) == strlen(body));
    assert(fclose(file) == 0);
}
int main(void)
{
    lx_home_config_t *config = calloc(1, sizeof(*config));
    lx_home_config_t *out = calloc(1, sizeof(*out));
    assert(config && out);
    out->consumer_count = 7;
    assert(app_loxone_home_store_load(out) == ESP_ERR_NOT_FOUND && out->consumer_count == 7);
    for (unsigned i = 0; i < 12; i++) {
        lx_home_consumer_t *item = &config->consumers[i];
        snprintf(item->id, sizeof(item->id), "consumer-%u", i);
        strcpy(item->name, "Mosógép"); strcpy(item->icon, "washing-machine");
        strcpy(item->source.uuid, "11223344-5566-7788-99aabbccddeeff00"); item->source.scale = .001;
        item->average_known = item->suggest_start = true; item->average_kw = 2.1;
        lx_home_card_t *card = &config->cards[i];
        snprintf(card->id, sizeof(card->id), "card-%u", i);
        strcpy(card->name, "Kapcsoló"); strcpy(card->icon, "lamp"); card->type = LX_HOME_CARD_SWITCH;
        card->source = item->source; card->source.scale = 1;
        strcpy(card->control_uuid, "aabbccdd-5566-7788-99aabbccddeeff00");
    }
    config->consumer_count = config->card_count = 12;
    config->page_count = 1;
    strcpy(config->pages[0].id, "home");
    strcpy(config->pages[0].title, "Otthon");
    config->pages[0].card_count = 12;
    assert(app_loxone_home_store_save(config) == ESP_OK);
    memset(out, 0, sizeof(*out));
    assert(app_loxone_home_store_load(out) == ESP_OK && memcmp(out, config, sizeof(*out)) == 0);
    /* A videós fiók külön titkosított, a publikus JSON nem tartalmaz titkot. */
    strcpy(config->intercom.stream_url, "http://camera/mjpg/video.mjpg");
    strcpy(config->intercom.video_username, "admin");
    strcpy(config->intercom.video_password, "Separate-Camera-Secret");
    assert(app_loxone_home_store_save(config) == ESP_OK);
    assert(app_loxone_home_store_load(out) == ESP_OK);
    assert(!strcmp(out->intercom.video_password, config->intercom.video_password));
    FILE *stored = fopen(APP_SD_MOUNT_POINT "/loxone-home.json", "rb");
    assert(stored);
    char *stored_body = calloc(1, LX_HOME_STORE_MAX + 1);
    assert(stored_body && fread(stored_body, 1, LX_HOME_STORE_MAX, stored));
    fclose(stored);
    assert(!strstr(stored_body, config->intercom.video_password));
    cJSON *public = lx_home_config_json(out);
    char *public_body = cJSON_PrintUnformatted(public);
    assert(public_body && !strstr(public_body, "Separate-Camera-Secret") && !strstr(public_body, "cipher"));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(public, "intercom"), "has_video_password")));
    free(public_body); cJSON_Delete(public);
    /* Másik készülék kulcsa nem tudja feloldani a jelszót. */
    unsigned char saved_key[32]; memcpy(saved_key, device_key, 32); device_key[0] ^= 1;
    assert(app_loxone_home_store_load(out) == ESP_OK && !out->intercom.video_password[0]);
    memcpy(device_key, saved_key, 32);
    cJSON *document = cJSON_Parse(stored_body);
    cJSON *door = cJSON_GetObjectItemCaseSensitive(document, "intercom");
    cJSON_ReplaceItemInObject(door, "video_username", cJSON_CreateString("other-admin"));
    char *tampered = cJSON_PrintUnformatted(document);
    raw(tampered);
    assert(app_loxone_home_store_load(out) == ESP_OK && !out->intercom.video_password[0]);
    free(tampered); cJSON_Delete(document); free(stored_body);
    assert(app_loxone_home_store_save(config) == ESP_OK);
    /* Megtartás csak azonos kameránál és fióknál; új adat külön jelszót igényel. */
    lx_intercom_config_t next = config->intercom;
    next.enabled = true; next.video_password[0] = 0;
    cJSON *entry = cJSON_Parse("{\"video_password\":\"\"}");
    assert(lx_intercom_credentials_update(&next, &config->intercom, entry));
    assert(!strcmp(next.video_password, "Separate-Camera-Secret"));
    strcpy(next.video_username, "new-admin"); next.video_password[0] = 0;
    assert(!lx_intercom_credentials_update(&next, &config->intercom, entry));
    cJSON_ReplaceItemInObject(entry, "video_password", cJSON_CreateString("new-video-password"));
    assert(lx_intercom_credentials_update(&next, &config->intercom, entry));
    assert(!strcmp(next.video_password, "new-video-password"));
    cJSON_Delete(entry);
    /* A Miniserver kapcsolat fájlját a tároló soha nem érinti. */
    FILE *secret = fopen(APP_SD_MOUNT_POINT "/loxone.json", "wb"); assert(secret);
    fputs("existing-encrypted-secret", secret); fclose(secret);
    strcpy(config->consumers[0].name, "Módosított");
    for (unsigned step = 1; step <= 2; step++) {
        fail_rename = step; renames = 0;
        assert(app_loxone_home_store_save(config) != ESP_OK);
        fail_rename = 0;
        assert(app_loxone_home_store_load(out) == ESP_OK && strcmp(out->consumers[0].name, "Mosógép") == 0);
    }
    sync_error = true; assert(app_loxone_home_store_save(config) != ESP_OK); sync_error = false;
    assert(app_loxone_home_store_load(out) == ESP_OK && strcmp(out->consumers[0].name, "Mosógép") == 0);
    /* Reboot a két rename között: backupból olvasás, majd új mentés. */
    assert(rename(APP_SD_MOUNT_POINT "/loxone-home.json", APP_SD_MOUNT_POINT "/loxone-home.json.bak") == 0);
    assert(app_loxone_home_store_load(out) == ESP_OK);
    assert(app_loxone_home_store_save(config) == ESP_OK);
    assert(app_loxone_home_store_load(out) == ESP_OK && strcmp(out->consumers[0].name, "Módosított") == 0);
    /* Sérült új fájlnál csak a validált backup fogadható el. */
    raw("{broken"); assert(app_loxone_home_store_load(out) == ESP_OK && strcmp(out->consumers[0].name, "Mosógép") == 0);
    fail_rename = 3; renames = 0;
    assert(app_loxone_home_store_save(config) != ESP_OK); fail_rename = 0;
    assert(app_loxone_home_store_load(out) == ESP_OK && strcmp(out->consumers[0].name, "Mosógép") == 0);
    unlink(APP_SD_MOUNT_POINT "/loxone-home.json.bak");
    unsigned char *snapshot = malloc(sizeof(*out)); assert(snapshot); memcpy(snapshot, out, sizeof(*out));
    raw("{\"schema\":1} trailing");
    assert(app_loxone_home_store_load(out) == ESP_FAIL && memcmp(snapshot, out, sizeof(*out)) == 0);
    raw("{\"schema\":2}"); assert(app_loxone_home_store_load(out) == ESP_FAIL);
    unavailable = true; assert(app_loxone_home_store_save(config) == ESP_FAIL);
    assert(app_loxone_home_store_load(out) == ESP_FAIL); unavailable = false;
    config->consumer_count = 13; assert(app_loxone_home_store_save(config) != ESP_OK);
    secret = fopen(APP_SD_MOUNT_POINT "/loxone.json", "rb"); char password[64] = {0};
    assert(secret && fread(password, 1, sizeof(password)-1, secret)); fclose(secret);
    assert(strcmp(password, "existing-encrypted-secret") == 0);
    config->consumer_count = 12;
    config->page_count = LX_HOME_MAX_PAGES;
    config->card_count = LX_HOME_MAX_TOTAL_CARDS;
    for (unsigned p = 0; p < LX_HOME_MAX_PAGES; p++) {
        lx_home_page_t *page = &config->pages[p];
        snprintf(page->id, sizeof(page->id), "page-%u", p);
        snprintf(page->title, sizeof(page->title), "Szoba %u", p);
        page->first_card = p * 12; page->card_count = 12;
    }
    for (unsigned i = 1; i < LX_HOME_MAX_TOTAL_CARDS; i++) {
        config->cards[i] = config->cards[0];
        snprintf(config->cards[i].id, sizeof(config->cards[i].id), "card-%u", i);
    }
    assert(app_loxone_home_store_save(config) == ESP_OK);
    assert(app_loxone_home_store_load(out) == ESP_OK && !memcmp(config, out, sizeof(*out)));
    free(snapshot);
    free(config); free(out);
    puts("PASS: 12+12 profile persistence, separate AES-GCM video credentials, no plaintext/echo, device and identity binding, password retention, integration file untouched, fsync/rename fault injection, reboot recovery, validated backup and atomic corrupted-file rejection");
}
