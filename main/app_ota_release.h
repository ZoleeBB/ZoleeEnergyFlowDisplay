#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

/* Minden hardver saját OTA assetet kap; a fő kijelző neve változatlan. */
#ifndef APP_OTA_ASSET_PREFIX
#define APP_OTA_ASSET_PREFIX "ZoleeEnergyFlowDisplay"
#endif

#define APP_OTA_RELEASE_BASE "https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/releases/download/"
#define APP_OTA_RELEASE_API                                                                        \
    "https://api.github.com/repos/ZoleeBB/ZoleeEnergyFlowDisplay/releases/latest"

typedef struct
{
    char tag[32];
    char filename[96];
    char url[256];
    char checksums_url[256];
    uint32_t size;
} app_ota_release_t;

/* Csak stabil, szigorúan újabb vMAJOR.MINOR.PATCH verzió telepíthető. */
bool app_ota_version_newer(const char *candidate, const char *current);
bool app_ota_release_parse(const cJSON *root,
                           const char *current,
                           size_t partition_size,
                           app_ota_release_t *out);
bool app_ota_checksum_parse(const char *text, const char *filename, uint8_t sha256[32]);

/* Az átirányítás sem válthat titkosítatlan vagy más célú szerverre. */
bool app_ota_download_url_allowed(const char *url);
