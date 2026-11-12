#pragma once

#include "app_loxone_model.h"

/* Egyetlen látható oldal korlátos, PSRAM-ban élő világításadatai.
 * A teljes Miniserverhez nem tartunk fent élő világítási állapottárat. */
#define LX_LIGHT_MAX_CIRCUITS 64
#define LX_LIGHT_MAX_MOODS 128
#define LX_LIGHT_TEXT_MAX (32U * 1024U)
#define LX_LIGHT_ACTION_SIZE 64
#define LX_LIGHT_NAME_SIZE 81
#define LX_LIGHT_COMMAND_SIZE 128
#define LX_LIGHT_INTERVAL_US 100000LL
#define LX_LIGHT_CONFIRM_US 2000000LL

typedef enum
{
    LX_LIGHT_UNSUPPORTED,
    LX_LIGHT_SWITCH,
    LX_LIGHT_DIMMER,
    LX_LIGHT_COLOR
} lx_light_type_t;
typedef enum
{
    LX_LIGHT_MODE_UNKNOWN,
    LX_LIGHT_MODE_HSV,
    LX_LIGHT_MODE_WHITE
} lx_light_mode_t;
typedef enum
{
    LX_LIGHT_UNKNOWN,
    LX_LIGHT_OFF,
    LX_LIGHT_CUSTOM,
    LX_LIGHT_ONE_MOOD,
    LX_LIGHT_MANY_MOODS
} lx_light_label_t;

typedef struct
{
    lx_light_mode_t mode;
    unsigned hue, saturation, brightness, kelvin;
    bool valid;
} lx_light_value_t;

typedef struct
{
    char id[LX_LIGHT_ACTION_SIZE], action[LX_LIGHT_ACTION_SIZE];
    char name[LX_LIGHT_NAME_SIZE], state[LX_UUID_LEN], lock_state[LX_UUID_LEN];
    lx_light_type_t type;
    unsigned min_kelvin, max_kelvin;
    bool rgb, white, blocked, locked, lock_known;
    lx_light_value_t actual, desired;
    bool dirty, pending;
    unsigned serial;
    int64_t last_sent_us, pending_until_us;
} lx_light_circuit_t;

/* A zárt kártyának csak az ismert/be állapot kell; nem őriz parancscélt,
 * nevet vagy színválasztó-munkapéldányt minden egyes körhöz. */
typedef struct
{
    char state[LX_UUID_LEN];
    lx_light_type_t type;
    bool valid, on;
} lx_light_observation_t;

typedef struct
{
    unsigned id;
    char name[LX_LIGHT_NAME_SIZE];
} lx_light_mood_t;
typedef struct
{
    char action[LX_LIGHT_ACTION_SIZE];
    char active_state[LX_UUID_LEN], moods_state[LX_UUID_LEN], names_state[LX_UUID_LEN],
        lock_state[LX_UUID_LEN];
    bool blocked, locked, lock_known, active_known;
    unsigned active_count, active_ids[LX_LIGHT_MAX_MOODS];
    unsigned mood_count, circuit_count;
    lx_light_mood_t *moods;
    lx_light_circuit_t *circuits;
    lx_light_observation_t *observations;
} lx_light_block_t;

typedef struct
{
    lx_light_label_t label;
    unsigned active_count, circuit_count;
    bool available, controllable;
    char name[LX_LIGHT_NAME_SIZE];
} lx_light_summary_t;

/* A katalógus egyetlen sorába tömör JSON-szöveget tesz; nem másolja minden
 * állapothoz a teljes subControls-fát. Nem fogad szabadon megadott URL-t. */
bool lx_light_catalog_attach(cJSON *entry, const cJSON *control, bool parent_secured);
const cJSON *lx_light_catalog_find(const cJSON *catalog, const char *control_uuid);
bool lx_light_action_valid(const char *action);
void *lx_light_alloc(size_t count, size_t size);
lx_light_block_t *lx_light_block_create(const cJSON *entry);
/* A részletes állapottárból kompakt kártyaállapotot készít; hiba esetén
 * változatlan marad. A részletes ablak külön, igény szerinti blokkot kap. */
bool lx_light_block_minimize(lx_light_block_t *block);
void lx_light_block_free(lx_light_block_t *block);
void lx_light_block_cancel(lx_light_block_t *block);
void lx_light_block_event(lx_light_block_t *block,
                          const char *uuid,
                          bool numeric,
                          double value,
                          const char *text,
                          size_t length,
                          int64_t now);
void lx_light_summary(const lx_light_block_t *block, lx_light_summary_t *out);
bool lx_light_color_parse(const char *text, size_t length, lx_light_value_t *value);
bool lx_light_command_build(const lx_light_block_t *block,
                            unsigned circuit,
                            const lx_light_value_t *value,
                            char out[LX_LIGHT_COMMAND_SIZE]);
bool lx_light_queue(lx_light_block_t *block,
                    unsigned circuit,
                    const lx_light_value_t *value,
                    int64_t now);
bool lx_light_take(lx_light_block_t *block,
                   unsigned circuit,
                   int64_t now,
                   char out[LX_LIGHT_COMMAND_SIZE]);
void lx_light_circuit_snapshot(const lx_light_block_t *block,
                               unsigned circuit,
                               int64_t now,
                               lx_light_circuit_t *out);
