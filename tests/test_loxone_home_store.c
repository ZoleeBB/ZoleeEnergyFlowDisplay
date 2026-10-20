/* Valódi SD-mentés, megszakadt FAT-csere és sérült fájlok tesztje. */
#include "app_loxone_home_store.h"
#include "app_storage.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    assert(app_loxone_home_store_save(config) == ESP_OK);
    memset(out, 0, sizeof(*out));
    assert(app_loxone_home_store_load(out) == ESP_OK && memcmp(out, config, sizeof(*out)) == 0);
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
    unsigned char snapshot[sizeof(*out)]; memcpy(snapshot, out, sizeof(*out));
    raw("{\"schema\":1} trailing");
    assert(app_loxone_home_store_load(out) == ESP_FAIL && memcmp(snapshot, out, sizeof(*out)) == 0);
    raw("{\"schema\":2}"); assert(app_loxone_home_store_load(out) == ESP_FAIL);
    unavailable = true; assert(app_loxone_home_store_save(config) == ESP_FAIL);
    assert(app_loxone_home_store_load(out) == ESP_FAIL); unavailable = false;
    config->consumer_count = 13; assert(app_loxone_home_store_save(config) != ESP_OK);
    secret = fopen(APP_SD_MOUNT_POINT "/loxone.json", "rb"); char password[64] = {0};
    assert(secret && fread(password, 1, sizeof(password)-1, secret)); fclose(secret);
    assert(strcmp(password, "existing-encrypted-secret") == 0);
    free(config); free(out);
    puts("PASS: 12+12 profile persistence, no credentials touched, fsync/rename fault injection, reboot recovery, validated backup and atomic corrupted-file rejection");
}
