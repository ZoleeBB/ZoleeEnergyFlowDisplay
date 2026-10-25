#pragma once
#include "app_ringtone_wav.h"
#include "cJSON.h"
#include "esp_err.h"

/* A hívóé a visszaadott puffer. Az SD-zár csak fájlműveletek alatt él.
 * A feltöltés először töröl; a hibás fájlokat kihagyja, a jókat megtartja. */
cJSON* app_ringtones_list(void);
esp_err_t app_ringtones_reset(char error[160]);
esp_err_t app_ringtones_install_prepared(const char *archive, cJSON **report, char error[160]);
esp_err_t app_ringtones_install(const char* archive, char error[160]);
uint8_t* app_ringtones_load(const char* name, size_t* size);
uint8_t* app_ringtones_load_ex(const char* name, size_t* size, app_ringtone_load_status_t* status);

/* Konkrét fájlművelet és errno, a kérés saját szövegpufferébe. */
uint8_t* app_ringtones_load_detailed(const char* name, size_t* size,
    app_ringtone_load_status_t* status, char error[160]);
