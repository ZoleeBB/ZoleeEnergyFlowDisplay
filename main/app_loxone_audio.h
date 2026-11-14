#pragma once

#include "app_loxone_model.h"

#define LX_AUDIO_ICON_ID "audio-player"
#define LX_AUDIO_META_MAX 3072U
#define LX_AUDIO_HOST_SIZE 192
#define LX_AUDIO_TEXT_SIZE 161
#define LX_AUDIO_URL_SIZE 769
#define LX_AUDIO_FAVORITE_PAGE 32U
#define LX_AUDIO_JSON_MAX (48U * 1024U)
#define LX_AUDIO_SESSION_SIZE 257

typedef enum
{
    LX_AUDIO_PLAY,
    LX_AUDIO_PAUSE,
    LX_AUDIO_PREVIOUS,
    LX_AUDIO_NEXT,
    LX_AUDIO_VOLUME,
    LX_AUDIO_VOLUME_UP,
    LX_AUDIO_VOLUME_DOWN,
    LX_AUDIO_FAVORITE
} lx_audio_action_t;

typedef enum
{
    LX_AUDIO_MODE_UNKNOWN,
    LX_AUDIO_MODE_STOP,
    LX_AUDIO_MODE_PAUSE,
    LX_AUDIO_MODE_PLAY
} lx_audio_mode_t;

typedef struct
{
    char host[LX_AUDIO_HOST_SIZE];
    char action[LX_UUID_LEN];
    char locked_state[LX_UUID_LEN], lock_state[LX_UUID_LEN];
    char volume_state[LX_UUID_LEN], max_volume_state[LX_UUID_LEN];
    char play_state[LX_UUID_LEN], server_state[LX_UUID_LEN], client_state[LX_UUID_LEN];
    unsigned player, port, volume_modes;
    bool blocked;
} lx_audio_endpoint_t;

typedef struct
{
    char title[LX_AUDIO_TEXT_SIZE];
    char artist[LX_AUDIO_TEXT_SIZE];
    char album[LX_AUDIO_TEXT_SIZE];
    char station[LX_AUDIO_TEXT_SIZE];
    char cover[LX_AUDIO_URL_SIZE];
    unsigned version;
    unsigned volume;
    lx_audio_mode_t mode;
    bool volume_known;
} lx_audio_track_t;

typedef struct
{
    unsigned id;
    int slot;
    char name[LX_AUDIO_TEXT_SIZE];
} lx_audio_favorite_t;

typedef struct
{
    unsigned player, start, total, count, generation;
    lx_audio_favorite_t items[LX_AUDIO_FAVORITE_PAGE];
    bool ready, failed;
} lx_audio_favorites_t;

typedef enum
{
    LX_AUDIO_FAVORITES_UNRELATED,
    LX_AUDIO_FAVORITES_MATCH,
    LX_AUDIO_FAVORITES_INVALID
} lx_audio_favorites_match_t;

/* A metaadat kizárólag a struktúrafájl nyilvános kapcsolási adatait tartalmazza.
 * JWT, munkamenetkulcs és kedvenc-lista nem kerül az SD-katalógusba. */
bool lx_audio_catalog_attach(cJSON *entry, const cJSON *control, bool secured,
                             const cJSON *structure);
const cJSON *lx_audio_catalog_find(const cJSON *catalog, const char *control_uuid);
bool lx_audio_endpoint(const cJSON *entry, lx_audio_endpoint_t *out);
bool lx_audio_command_build(const cJSON *entry, lx_audio_action_t action,
                            unsigned value, char out[96]);

/* Egy zóna részleges eseménye nem nullázza a más zónákhoz tartozó állapotot.
 * A hibás opcionális adat nem válhat érvényes üres számmá. */
bool lx_audio_track_update(lx_audio_track_t *track, unsigned player,
                           const cJSON *message);
bool lx_audio_favorites_parse(lx_audio_favorites_t *out, unsigned player,
                              unsigned requested_start, const cJSON *message);
/* Az idegen zóna/lap válasza nem zárhatja le a folyamatban lévő kérést.
 * A MATCH az egyértelmű hozzárendelést jelzi; a tartalmat a parser ellenőrzi. */
lx_audio_favorites_match_t lx_audio_favorites_match(unsigned player,
    unsigned requested_start, const cJSON *message);
bool lx_audio_session_token(const char *greeting, char out[LX_AUDIO_SESSION_SIZE]);
bool lx_audio_cover_url(const char *url);
bool lx_audio_native_command(unsigned player, lx_audio_action_t action,
                             unsigned value, char out[96]);
