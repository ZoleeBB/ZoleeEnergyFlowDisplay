#pragma once

#include "app_loxone_home_model.h"

/* A részletes lista korlátos és csak a látható oldallal él. A Miniserver
 * összesítő számai ettől függetlenek: csonkoláskor sem számolunk újra. */
#define LX_CONTROLS_META_MAX (64U * 1024U)
#define LX_CONTROLS_MAX_WINDOWS 256U
#define LX_CONTROLS_STATE_COUNT 23U
#define LX_CONTROLS_COUNT_COUNT 6U

typedef enum
{
    LX_CONTROLS_ON,
    LX_CONTROLS_OFF,
    LX_CONTROLS_PULSE,
    LX_CONTROLS_OPEN,
    LX_CONTROLS_STOP,
    LX_CONTROLS_CLOSE,
    LX_CONTROLS_SET
} lx_controls_action_t;

typedef enum
{
    LX_CONTROL_VALUE, LX_CONTROL_MIN, LX_CONTROL_MAX, LX_CONTROL_STEP,
    LX_CONTROL_ERROR, LX_CONTROL_MOTION, LX_CONTROL_PREVENT_OPEN, LX_CONTROL_PREVENT_CLOSE,
    LX_CONTROL_DELAY, LX_CONTROL_TOTAL, LX_CONTROL_WINDOWS,
    LX_CONTROL_NUM_OPEN, LX_CONTROL_NUM_CLOSED, LX_CONTROL_NUM_TILTED,
    LX_CONTROL_NUM_OFFLINE, LX_CONTROL_NUM_LOCKED, LX_CONTROL_NUM_UNLOCKED,
    LX_CONTROL_LOCK, LX_CONTROL_HARD_LOCK, LX_CONTROL_DEVICE,
    LX_CONTROL_REASON, LX_CONTROL_TARGET, LX_CONTROL_WINDOW_TYPE
} lx_controls_state_t;

typedef struct
{
    char name[LX_HOME_NAME_SIZE], room[65], place[65];
    unsigned state;
    bool valid;
} lx_controls_window_t;

typedef struct
{
    lx_home_card_type_t type;
    char action[LX_UUID_LEN];
    char states[LX_CONTROLS_STATE_COUNT][LX_UUID_LEN];
    double values[LX_CONTROLS_STATE_COUNT];
    uint32_t known;
    int64_t delay_received_us;
    bool blocked, locked, lock_known, stairway, list_valid, list_truncated;
    int animation;
    unsigned decimals, window_count;
    char unit[25], prefix[25], reason[81];
    lx_controls_window_t *windows;
    /* A húzás legfrissebb abszolút célértéke legfeljebb 100 ms-onként megy ki.
     * A visszajelzett értékhez ez soha nem íródik hozzá. */
    bool dirty, failed;
    double desired;
    int64_t next_send_us, desired_until_us;
} lx_controls_block_t;

typedef struct
{
    lx_home_card_type_t type;
    bool available, controllable, value_valid, range_valid, motion_valid;
    bool open_allowed, close_allowed, stairway, timer_valid, timer_running, timer_total_valid, list_valid, list_truncated;
    bool pending, failed;
    double value, minimum, maximum, step, timer_seconds, timer_total;
    int motion, animation;
    unsigned decimals, window_count, counts[LX_CONTROLS_COUNT_COUNT];
    uint32_t counts_known;
    char unit[25], prefix[25], reason[81];
} lx_controls_summary_t;

bool lx_controls_catalog_attach(cJSON *entry, const cJSON *control, bool parent_secured,
                                 const cJSON *rooms, const cJSON *structure);
const cJSON *lx_controls_catalog_find(const cJSON *catalog, const char *control_uuid,
                                      lx_home_card_type_t type);
lx_controls_block_t *lx_controls_block_create(const cJSON *entry);
void lx_controls_block_free(lx_controls_block_t *block);
void lx_controls_block_event(lx_controls_block_t *block, const char *uuid, bool numeric,
                              double value, const char *text, size_t length, int64_t received_us);
void lx_controls_summary(const lx_controls_block_t *block, int64_t now,
                          lx_controls_summary_t *out);
bool lx_controls_command(const lx_controls_block_t *block, lx_controls_action_t action,
                          double value, char out[96]);
bool lx_controls_queue(lx_controls_block_t *block, double value, int64_t now);
bool lx_controls_take(lx_controls_block_t *block, int64_t now, char out[96]);
const char *lx_controls_gate_icon(int animation);
