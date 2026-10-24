#include "app_ringtones.h"
#include "app_storage.h"
#include "app_template_zip.h"
#include "app_template_dir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static uint8_t* load(const char* directory, const char* name, size_t* size)
{
    if (!app_ringtone_name(name))
        return NULL;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* file = fopen(path, "rb");
    if (!file)
        return NULL;
    long length = 0;
    uint8_t* data = NULL;
    if (!fseek(file, 0, SEEK_END) && (length = ftell(file)) >= 44 && length <= APP_RINGTONE_MAX
        && !fseek(file, 0, SEEK_SET)) {
        data = malloc((size_t)length);
        size_t offset, bytes;
        if (data
            && (fread(data, 1, length, file) != (size_t)length
                || !app_ringtone_wav(data, length, &offset, &bytes))) {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    if (data)
        *size = (size_t)length;
    return data;
}

uint8_t* app_ringtones_load(const char* name, size_t* size)
{
    if (!size || app_storage_lock() != ESP_OK)
        return NULL;
    /* Áramszünet a két rename között: a teljes régi csomag visszaállítható. */
    struct stat st;
    if (stat(APP_RINGTONE_ROOT, &st))
        rename(APP_RINGTONE_ROOT ".old", APP_RINGTONE_ROOT);
    uint8_t* data = load(APP_RINGTONE_ROOT, name, size);
    app_storage_unlock();
    return data;
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
        uint8_t* data = load(APP_RINGTONE_ROOT ".new", entry->d_name, &size);
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
            "Hibás csengőhangcsomag. PCM WAV: 16 bit, mono, 16 kHz, legfeljebb 8 s; maximum 32 hang.");
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
