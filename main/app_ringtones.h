#pragma once
#include "app_ringtone_wav.h"
#include "cJSON.h"
#include "esp_err.h"

/* A hívóé a visszaadott puffer; minden SD-hozzáférést a modul zárol.
 * A feltöltés staging + könyvtárcsere, hibánál a régi készlet megmarad. */
cJSON* app_ringtones_list(void);
esp_err_t app_ringtones_install(const char* archive, char error[160]);
uint8_t* app_ringtones_load(const char* name, size_t* size);
uint8_t* app_ringtones_load_ex(const char* name, size_t* size, app_ringtone_load_status_t* status);
