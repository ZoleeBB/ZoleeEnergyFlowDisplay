#pragma once

#include "app_loxone_model.h"

/* Az IRC saját képe a firmware része; a szabad ikonválasztástól független. */
#define LX_IRC_ICON_ID "thermostat-irc"
#define LX_IRC_META_MAX 2048U

typedef enum
{
    LX_IRC_UNIT_INVALID = -2,
    LX_IRC_UNIT_UNKNOWN = -1,
    LX_IRC_UNIT_CELSIUS = 0,
    LX_IRC_UNIT_FAHRENHEIT = 1
} lx_irc_unit_t;

typedef enum
{
    LX_IRC_DIRECTION_UNKNOWN,
    LX_IRC_DIRECTION_HEATING,
    LX_IRC_DIRECTION_COOLING
} lx_irc_direction_t;

/* Csak a látható oldal modellje él; a nyers kezdőértékeket a közös cache őrzi. */
typedef struct
{
    char action[LX_UUID_LEN];
    char target_state[LX_UUID_LEN], actual_state[LX_UUID_LEN];
    char current_mode_state[LX_UUID_LEN], operating_mode_state[LX_UUID_LEN];
    char lock_state[LX_UUID_LEN];
    lx_irc_unit_t unit;
    double target_c, actual_c;
    int current_mode, operating_mode;
    bool target_valid, actual_valid, current_mode_known, operating_mode_known;
    bool blocked, locked, lock_known;
} lx_irc_block_t;

typedef struct
{
    double target_c, actual_c;
    bool target_valid, actual_valid, available, controllable;
    lx_irc_direction_t direction;
} lx_irc_summary_t;

bool lx_irc_catalog_attach(cJSON *entry, const cJSON *control, bool parent_secured,
                           lx_irc_unit_t miniserver_unit);
const cJSON *lx_irc_catalog_find(const cJSON *catalog, const char *control_uuid);
lx_irc_block_t *lx_irc_block_create(const cJSON *entry);
void lx_irc_block_free(lx_irc_block_t *block);
void lx_irc_block_event(lx_irc_block_t *block, const char *uuid, bool numeric,
                        double value, const char *text, size_t length, int64_t now);
void lx_irc_summary(const lx_irc_block_t *block, lx_irc_summary_t *out);
