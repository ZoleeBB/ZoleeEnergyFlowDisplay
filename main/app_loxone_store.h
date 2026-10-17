#pragma once
#include "app_loxone_model.h"
#include "esp_err.h"
/* A kulcs csak a készülék NVS-ében él. Az SD-re AES-256-GCM titkosítvány kerül. */
esp_err_t app_loxone_store_load(lx_config_t *out);
esp_err_t app_loxone_store_save(const lx_config_t *cfg);
