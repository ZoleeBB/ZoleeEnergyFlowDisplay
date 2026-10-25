#include "app_ringtones.h"
#include "app_storage.h"
#include "app_ringtone_zip.h"
#include "app_template_dir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#ifndef APP_RINGTONE_ROOT
#define APP_RINGTONE_ROOT APP_SD_MOUNT_POINT "/ringtones"
#endif

/* Kizárólag a dedikált hangkönyvtárakat töröljük. Hibát nem rejtünk el. */
static bool clean_depth(const char *directory, unsigned depth)
{
    if (depth > 4) {
        errno = ELOOP;
        return false;
    }
    DIR *dir = opendir(directory);
    if (!dir)
        return errno == ENOENT || (errno == ENOTDIR && remove(directory) == 0);
    bool ok = true;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char path[512];
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >= (int)sizeof(path)) {
            ok = false;
            break;
        }
        struct stat info;
#if defined(_WIN32) || defined(ESP_PLATFORM)
        int result = stat(path, &info);
#else
        int result = lstat(path, &info);
#endif
        if (result || (S_ISDIR(info.st_mode) ? !clean_depth(path, depth + 1) : remove(path) != 0)) {
            ok = false;
            break;
        }
    }
    closedir(dir);
    return ok && rmdir(directory) == 0;
}

static bool clean(const char *directory)
{
    return clean_depth(directory, 0);
}

esp_err_t app_ringtones_reset(char error[160])
{
    error[0] = 0;
    if (app_storage_lock() != ESP_OK) {
        snprintf(error, 160, "%s", "Az SD-zár nem érhető el a régi hangcsomag törléséhez.");
        return ESP_ERR_INVALID_STATE;
    }
    bool ok = clean(APP_RINGTONE_ROOT) && clean(APP_RINGTONE_ROOT ".new")
        && clean(APP_RINGTONE_ROOT ".old") && mkdir(APP_RINGTONE_ROOT, 0775) == 0;
    if (!ok)
        snprintf(error, 160, "A régi hangcsomag törlése/üres könyvtár létrehozása sikertelen (errno=%d).", errno);
    app_storage_unlock();
    return ok ? ESP_OK : ESP_FAIL;
}

static uint8_t* load(const char* directory, const char* name, size_t* size,
                     app_ringtone_load_status_t* status, char error[160])
{
    const char *stage = "névellenőrzés";
    size_t position = 0;
    if (error) error[0] = 0;
    *status = APP_RINGTONE_LOAD_INVALID;
    *size = 0;
    if (!app_ringtone_name(name)) {
        if (error) snprintf(error, 160, "Érvénytelen hangfájlnév: %.64s", name ? name : "(üres)");
        return NULL;
    }
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* file = fopen(path, "rb");
    if (!file) {
        *status = errno == ENOENT ? APP_RINGTONE_LOAD_MISSING : APP_RINGTONE_LOAD_IO;
        if (error) snprintf(error, 160, "WAV megnyitás: %.64s (errno=%d).", name, errno);
        return NULL;
    }
    uint8_t* data = NULL;
    struct stat info;
    *status = APP_RINGTONE_LOAD_IO;
    stage = "méretlekérés / stat";
    errno = 0;
    /* Az SD-zár alatt a fájl nem cserélődhet. Méretet stat-tal kérünk,
     * az adatot az elejétől sorban olvassuk, nincs SEEK_END/rewind. */
    if (stat(path, &info))
        goto done;
    *status = APP_RINGTONE_LOAD_INVALID;
    stage = "fájlméret-ellenőrzés";
    if (info.st_size < 44 || info.st_size > APP_RINGTONE_MAX)
        goto done;
    size_t length = (size_t)info.st_size;
    /* A videóval együtt akár 1,9 MB hangot is fogadunk; ez nem a belső
     * DMA-memóriából vehető el. A puffer free()-vel felszabadítható. */
    stage = "PSRAM-puffer foglalása";
#ifdef ESP_PLATFORM
    data = heap_caps_malloc((size_t)length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    data = malloc((size_t)length);
#endif
    *status = APP_RINGTONE_LOAD_MEMORY;
    if (!data)
        goto done;
    /* A főági fájlkezeléshez hasonló, pufferelt stdio. A driver kezeli az
     * SD adatút memóriaigazítását; nincs saját DMA-puffer vagy setvbuf. */
    *status = APP_RINGTONE_LOAD_IO;
    stage = "adatolvasás";
    errno = 0;
    while (position < length) {
        size_t chunk = length - position;
        if (chunk > 2048)
            chunk = 2048;
        if (fread(data + position, 1, chunk, file) != chunk)
            goto done;
        position += chunk;
    }
    size_t offset, bytes;
    *status = APP_RINGTONE_LOAD_INVALID;
    stage = "PCM WAV-ellenőrzés";
    if (!app_ringtone_wav(data, (size_t)length, &offset, &bytes))
        goto done;
    *size = (size_t)length;
    *status = APP_RINGTONE_LOAD_OK;
done:
    if (*status != APP_RINGTONE_LOAD_OK && error)
        snprintf(error, 160, "WAV %s (errno=%d, bájt=%u): %.64s",
            stage, errno, (unsigned)position, name);
    fclose(file);
    if (*status != APP_RINGTONE_LOAD_OK) {
        free(data);
        data = NULL;
    }
    return data;
}

uint8_t* app_ringtones_load_detailed(const char* name, size_t* size,
                                    app_ringtone_load_status_t* status, char error[160])
{
    if (!status || !size)
        return NULL;
    *size = 0;
    *status = APP_RINGTONE_LOAD_BUSY;
    if (error) snprintf(error, 160, "%s", "Az SD-kártya foglalt; a hangbetöltés várakozik.");
    /* A hangfeladat újrapróbálhatja, közben hívásra azonnal megszakítható. */
    if (!app_storage_try_lock())
        return NULL;
    uint8_t* data = load(APP_RINGTONE_ROOT, name, size, status, error);
    app_storage_unlock();
    return data;
}

uint8_t* app_ringtones_load_ex(const char* name, size_t* size, app_ringtone_load_status_t* status)
{
    return app_ringtones_load_detailed(name, size, status, NULL);
}

uint8_t* app_ringtones_load(const char* name, size_t* size)
{
    app_ringtone_load_status_t status;
    return app_ringtones_load_ex(name, size, &status);
}

cJSON* app_ringtones_list(void)
{
    cJSON* list = cJSON_CreateArray();
    if (!list || app_storage_lock() != ESP_OK)
        return list;
    DIR* dir = opendir(APP_RINGTONE_ROOT);
    struct dirent* entry;
    unsigned count = 0;
    while (dir && count < 32 && (entry = readdir(dir))) {
        if (!app_ringtone_name(entry->d_name))
            continue;
        cJSON_AddItemToArray(list, cJSON_CreateString(entry->d_name));
        count++;
    }
    if (dir)
        closedir(dir);
    app_storage_unlock();
    return list;
}

/* A törlés külön, a HTTP-törzs fogadása ELŐTT történik. A készletet nem
 * nevezzük át és nem állítjuk vissza egy korábbi, esetleg hibás mentésből. */
esp_err_t app_ringtones_install_prepared(const char *archive, cJSON **report, char error[160])
{
    error[0] = 0;
    *report = cJSON_CreateObject();
    if (!*report || !cJSON_AddArrayToObject(*report, "failed")) {
        cJSON_Delete(*report);
        *report = NULL;
        snprintf(error, 160, "%s", "Nincs memória a telepítési jelentéshez.");
        return ESP_ERR_NO_MEM;
    }
    if (app_storage_lock() != ESP_OK) {
        snprintf(error, 160, "%s", "Az SD-zár nem érhető el a csomag telepítéséhez.");
        return ESP_ERR_INVALID_STATE;
    }
    bool ok = app_ringtone_unpack(archive, APP_RINGTONE_ROOT, *report, error);
    app_storage_unlock();
    if (ok && cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(*report, "installed")) == 0) {
        snprintf(error, 160, "%s", "Egyetlen hangot sem sikerült telepíteni.");
        ok = false;
    }
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t app_ringtones_install(const char *archive, char error[160])
{
    esp_err_t result = app_ringtones_reset(error);
    cJSON *report = NULL;
    if (result == ESP_OK)
        result = app_ringtones_install_prepared(archive, &report, error);
    cJSON_Delete(report);
    return result;
}
