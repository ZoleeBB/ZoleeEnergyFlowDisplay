#include "app_ringtones.h"
#include "app_storage.h"
#include "app_template_zip.h"
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

static void clean(const char* directory)
{
    DIR* dir = opendir(directory);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            remove(path);
        }
        closedir(dir);
    }
    rmdir(directory);
}

static uint8_t* load(const char* directory, const char* name, size_t* size,
                     app_ringtone_load_status_t* status)
{
    *status = APP_RINGTONE_LOAD_INVALID;
    *size = 0;
    if (!app_ringtone_name(name))
        return NULL;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* file = fopen(path, "rb");
    if (!file) {
        *status = errno == ENOENT ? APP_RINGTONE_LOAD_MISSING : APP_RINGTONE_LOAD_IO;
        return NULL;
    }
    /* Ne a stdio válasszon nagyobb rejtett olvasópuffert a szektorok helyett. */
    setvbuf(file, NULL, _IONBF, 0);
    uint8_t* data = NULL;
    uint8_t* transfer = NULL;
    *status = APP_RINGTONE_LOAD_IO;
    if (fseek(file, 0, SEEK_END))
        goto done;
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET))
        goto done;
    *status = APP_RINGTONE_LOAD_INVALID;
    if (length < 44 || length > APP_RINGTONE_MAX)
        goto done;
    /* A videóval együtt akár 1,9 MB hangot is fogadunk; ez nem a belső
     * DMA-memóriából vehető el. A puffer free()-vel felszabadítható. */
#ifdef ESP_PLATFORM
    data = heap_caps_malloc((size_t)length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    data = malloc((size_t)length);
#endif
    *status = APP_RINGTONE_LOAD_MEMORY;
    if (!data)
        goto done;
    /* A PSRAM és a P4 cache-vonal nem közvetlen SD-DMA célpont.
     * A fájl mindig igazított belső pufferbe érkezik, onnan másoljuk a hangot. */
#ifdef ESP_PLATFORM
    transfer = heap_caps_aligned_calloc(128, 1, 512,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
#else
    transfer = calloc(1, 512);
#endif
    if (!transfer)
        goto done;
    *status = APP_RINGTONE_LOAD_IO;
    /* A több MB-os egyetlen fread az SD-driverben nagy átmeneti DMA-
     * puffert igényelhet éppen a kamerakép indulásakor. Szektoronként
     * olvasunk: az SD átmeneti memóriaigénye nem nő a hang hosszával. */
    size_t position = 0;
    while (position < (size_t)length) {
        size_t chunk = (size_t)length - position;
        if (chunk > 512)
            chunk = 512;
        if (fread(transfer, 1, chunk, file) != chunk)
            goto done;
        memcpy(data + position, transfer, chunk);
        position += chunk;
    }
    size_t offset, bytes;
    *status = APP_RINGTONE_LOAD_INVALID;
    if (!app_ringtone_wav(data, (size_t)length, &offset, &bytes))
        goto done;
    *size = (size_t)length;
    *status = APP_RINGTONE_LOAD_OK;
done:
    free(transfer);
    fclose(file);
    if (*status != APP_RINGTONE_LOAD_OK) {
        free(data);
        data = NULL;
    }
    return data;
}

uint8_t* app_ringtones_load_ex(const char* name, size_t* size, app_ringtone_load_status_t* status)
{
    if (!status || !size)
        return NULL;
    *size = 0;
    *status = APP_RINGTONE_LOAD_BUSY;
    /* A hangfeladat újrapróbálhatja, közben hívásra azonnal megszakítható. */
    if (!app_storage_try_lock())
        return NULL;
    struct stat st;
    if (stat(APP_RINGTONE_ROOT, &st))
        rename(APP_RINGTONE_ROOT ".old", APP_RINGTONE_ROOT);
    uint8_t* data = load(APP_RINGTONE_ROOT, name, size, status);
    app_storage_unlock();
    return data;
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
    struct stat st;
    if (stat(APP_RINGTONE_ROOT, &st))
        rename(APP_RINGTONE_ROOT ".old", APP_RINGTONE_ROOT);
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

esp_err_t app_ringtones_install(const char* archive, char error[160])
{
    if (app_storage_lock() != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    clean(APP_RINGTONE_ROOT ".new");
    bool ok = mkdir(APP_RINGTONE_ROOT ".new", 0775) == 0
        && app_ringtone_unzip(archive, APP_RINGTONE_ROOT ".new", error);
    unsigned count = 0;
    DIR* dir = ok ? opendir(APP_RINGTONE_ROOT ".new") : NULL;
    struct dirent* entry;
    while (ok && dir && (entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")
            || !strcmp(entry->d_name, "ringtone.json"))
            continue;
        size_t size;
        app_ringtone_load_status_t status;
        uint8_t* data = load(APP_RINGTONE_ROOT ".new", entry->d_name, &size, &status);
        ok = data != NULL;
        free(data);
        count++;
    }
    if (dir)
        closedir(dir);
    ok = ok && count > 0 && count <= 32;
    if (ok) {
        struct stat st;
        if (stat(APP_RINGTONE_ROOT, &st) && stat(APP_RINGTONE_ROOT ".old", &st) == 0
            && rename(APP_RINGTONE_ROOT ".old", APP_RINGTONE_ROOT))
            ok = false;
        if (!ok) {
            clean(APP_RINGTONE_ROOT ".new");
            app_storage_unlock();
            snprintf(error, 160, "%s", "Az előző csengőhangcsomag nem állítható vissza; SD-hiba.");
            return ESP_FAIL;
        }
        clean(APP_RINGTONE_ROOT ".old");
        bool previous = stat(APP_RINGTONE_ROOT, &st) == 0;
        ok = !previous || rename(APP_RINGTONE_ROOT, APP_RINGTONE_ROOT ".old") == 0;
        if (ok && rename(APP_RINGTONE_ROOT ".new", APP_RINGTONE_ROOT)) {
            if (previous)
                rename(APP_RINGTONE_ROOT ".old", APP_RINGTONE_ROOT);
            ok = false;
        }
        if (ok)
            clean(APP_RINGTONE_ROOT ".old");
    }
    clean(APP_RINGTONE_ROOT ".new");
    app_storage_unlock();
    if (!ok)
        snprintf(error, 160, "%s",
            "Hibás csengőhangcsomag. PCM WAV: 16 bit, mono, 16 kHz, legfeljebb 60 s; maximum 32 hang.");
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
