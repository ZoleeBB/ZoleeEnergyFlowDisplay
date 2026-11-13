#pragma once

#include "app_loxone_model.h"

/* A redőnykártya képe a firmware része, a konfigurált ikontól független. */
#define LX_SHADING_ICON_ID "blinds-shading"
#define LX_SHADING_META_MAX 2048U

/* Csak a látható oldal blokkja él; a közös állapotcache adja a kezdőértékeket.
 * A hiányzó opcionális állapot nem tilt, a meghirdetett, de ismeretlen zár igen. */
typedef struct
{
    char action[LX_UUID_LEN];
    char position_state[LX_UUID_LEN], up_state[LX_UUID_LEN], down_state[LX_UUID_LEN];
    char locked_state[LX_UUID_LEN], safety_state[LX_UUID_LEN];
    char device_state[LX_UUID_LEN], lock_state[LX_UUID_LEN];
    double position_percent;
    int device;
    bool central, blocked, position_valid;
    bool moving_up, moving_down, up_known, down_known;
    bool hard_locked, hard_lock_known, safety_active;
    bool locked, lock_known, device_known;
} lx_shading_block_t;

typedef struct
{
    double position_percent;
    bool central, position_valid, available, controllable;
    bool moving_up, moving_down, motion_known, safety_active;
} lx_shading_summary_t;

bool lx_shading_catalog_attach(cJSON *entry, const cJSON *control, bool parent_secured);
const cJSON *lx_shading_catalog_find(const cJSON *catalog, const char *control_uuid);
lx_shading_block_t *lx_shading_block_create(const cJSON *entry);
void lx_shading_block_free(lx_shading_block_t *block);
void lx_shading_block_event(lx_shading_block_t *block, const char *uuid, bool numeric,
                            double value, const char *text, size_t length, int64_t now);
void lx_shading_summary(const lx_shading_block_t *block, lx_shading_summary_t *out);
