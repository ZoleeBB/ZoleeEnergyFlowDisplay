#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

#define LX_FIELD_COUNT 16
#define LX_SUN_FIRST 12
#define LX_SUN_COUNT 4
#define LX_UUID_LEN 37
#define LX_PASSWORD_MAX 128
#define LX_CATALOG_MAX 512
#define LX_STRUCTURE_MAX (512U * 1024U)
/* A mezők sorrendje stabil: az SD-s párosítás kulcsa a mező neve. */
typedef struct {
    const char *name;
    bool required;
    bool digital;
} lx_field_t;
extern const lx_field_t lx_fields[LX_FIELD_COUNT];
typedef struct {
    char uuid[LX_UUID_LEN];
    double scale;
    double offset;
} lx_mapping_t;
typedef struct {
    char host[128];
    uint16_t port;
    bool tls;
    char ca_pem[4096];
    char username[96];
    char password[LX_PASSWORD_MAX];
    lx_mapping_t map[LX_FIELD_COUNT];
} lx_config_t;
typedef struct {
    double value[LX_FIELD_COUNT];
    bool seen[LX_FIELD_COUNT];
} lx_values_t;
void lx_config_defaults(lx_config_t *cfg);
/* Csak konfigurációs adatok; a jelszó kezelése külön, eszközkulccsal történik. */
bool lx_config_parse(const cJSON *root, lx_config_t *cfg, bool require_mappings);
cJSON *lx_config_json(const lx_config_t *cfg);
bool lx_uuid_valid(const char *uuid);
void lx_uuid_string(const uint8_t *raw, char out[LX_UUID_LEN]);
/* Teljes Loxone alkalmazásüzenet; a WS-fragmentációt a transzport állítja össze. */
bool lx_values_update(lx_values_t *values, const lx_config_t *cfg, int type,
                      const uint8_t *data, size_t length);
cJSON *lx_values_payload(const lx_values_t *values, const lx_config_t *cfg);
/* Korlátos, lapos katalógus. UUID, név, helyiség, kategória, típus, állapot. */
cJSON *lx_catalog_parse(const char *json, size_t length);

/* Előzetes megújítás: fél hátralévő élettartam, legfeljebb egy óra. */
int64_t lx_token_refresh_delay_us(int64_t valid_until,int64_t now_epoch);
