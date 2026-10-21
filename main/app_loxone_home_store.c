/* Korlátos SD-tároló, FAT-on is helyreállítható tmp/bak cserével.
 * Kizárólag a háttérfeladat hívja; nincs LVGL-lock vagy hitelesítési adat. */
#include "app_loxone_home_store.h"
#include "app_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HOME_PATH APP_SD_MOUNT_POINT "/loxone-home.json"
#define HOME_TMP HOME_PATH ".tmp"
#define HOME_BACKUP HOME_PATH ".bak"

static bool read_profile(const char *path, lx_home_config_t *out)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    char *body = NULL;
    bool ok = false;
    if (fseek(file, 0, SEEK_END) != 0)
        goto done;
    long size = ftell(file);
    if (size < 2 || size > LX_HOME_STORE_MAX || fseek(file, 0, SEEK_SET) != 0)
        goto done;
    body = calloc(1, (size_t)size + 1);
    if (!body || fread(body, 1, (size_t)size, file) != (size_t)size)
        goto done;
    /* A fájl teljes tartalma egyetlen JSON; záró szemét nem fogadható el. */
    cJSON *root = lx_home_document_parse(body, (size_t)size);
    ok = root && lx_home_config_parse(root, out, NULL);
    cJSON_Delete(root);
done:
    free(body);
    fclose(file);
    return ok;
}

esp_err_t app_loxone_home_store_load(lx_home_config_t *out)
{
    if (!out)
        return ESP_ERR_INVALID_ARG;
    esp_err_t error = app_storage_lock();
    if (error != ESP_OK)
        return error;
    bool present = access(HOME_PATH, F_OK) == 0 || access(HOME_BACKUP, F_OK) == 0;
    bool ok = read_profile(HOME_PATH, out);
    if (!ok && read_profile(HOME_BACKUP, out))
    {
        /* Egy megszakadt csere vagy sérült új fájl után a jó mentés megmarad.
         * Nem törlünk fájlt boot közben: a következő mentés rendezi a cserét. */
        ok = true;
    }
    app_storage_unlock();
    return ok ? ESP_OK : present ? ESP_FAIL : ESP_ERR_NOT_FOUND;
}

esp_err_t app_loxone_home_store_save(const lx_home_config_t *config)
{
    cJSON *root = lx_home_config_json(config);
    if (!root)
        return ESP_ERR_NO_MEM;
    /* A publikus C API is csak érvényes, körbejárható profilt tárolhat. */
    lx_home_config_t *validated = malloc(sizeof(*validated));
    bool valid = validated && lx_home_config_parse(root, validated, NULL);
    char *body = valid ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!body)
    {
        free(validated);
        return valid ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_ARG;
    }
    size_t size = strlen(body);
    if (size > LX_HOME_STORE_MAX)
    {
        free(body);
        free(validated);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t error = app_storage_lock();
    if (error != ESP_OK)
    {
        free(body);
        free(validated);
        return error;
    }
    /* Ha a főfájl eltűnt a korábbi csere közben, előbb állítsuk vissza a bakot.
     * Így egy későbbi hibás rename sem teheti elérhetetlenné a jó profilt. */
    bool ok = true;
    if (!read_profile(HOME_PATH, validated) && read_profile(HOME_BACKUP, validated))
    {
        if (unlink(HOME_PATH) != 0 && access(HOME_PATH, F_OK) == 0)
            ok = false;
        if (ok)
            ok = rename(HOME_BACKUP, HOME_PATH) == 0;
    }
    FILE *file = ok ? fopen(HOME_TMP, "wb") : NULL;
    ok = ok && file != NULL;
    if (file)
    {
        ok = fwrite(body, 1, size, file) == size;
        if (fflush(file) != 0 || fsync(fileno(file)) != 0)
            ok = false;
        if (fclose(file) != 0)
            ok = false;
    }
    bool previous = access(HOME_PATH, F_OK) == 0;
    if (ok && previous)
    {
        if (unlink(HOME_BACKUP) != 0 && access(HOME_BACKUP, F_OK) == 0)
            ok = false;
        if (ok)
            ok = rename(HOME_PATH, HOME_BACKUP) == 0;
    }
    if (ok && rename(HOME_TMP, HOME_PATH) != 0)
    {
        ok = false;
        if (previous)
            (void)rename(HOME_BACKUP, HOME_PATH);
    }
    if (!ok)
        (void)unlink(HOME_TMP);
    app_storage_unlock();
    free(body);
    free(validated);
    return ok ? ESP_OK : ESP_FAIL;
}
