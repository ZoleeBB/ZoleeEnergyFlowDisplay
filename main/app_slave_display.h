#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A Master és a 4B Slave közös, hálózattól/LVGL-től független ébresztési szabályai.
 * A fényerők a Slave saját beállításai, nem az energia-BLE csomag fényereje. */
typedef struct
{
    uint8_t day_brightness;
    uint8_t night_brightness;
    bool blank_at_night;
    uint32_t presence_timeout_seconds;
    /* Master jelenlétvezérlés: a jelenlét 1 -> 0 éle azonnal sötétít, nem ad
     * újabb időkorlátot. Az érintéses ébresztés időkorlátja változatlan. */
    bool blank_on_presence_loss;
} app_slave_display_profile_t;

typedef struct
{
    app_slave_display_profile_t profile;
    bool night;
    bool presence_active;
    bool pointer_down;
    bool swallow_pointer;
    bool keep_visible;
    int64_t awake_until_us;
    int64_t last_now_us;
} app_slave_display_t;

bool app_slave_display_init(app_slave_display_t *state,
                            const app_slave_display_profile_t *profile, int64_t now_us);
bool app_slave_display_profile(app_slave_display_t *state,
                               const app_slave_display_profile_t *profile, int64_t now_us);
void app_slave_display_night(app_slave_display_t *state, bool night, int64_t now_us);
/* Érvénytelen jelenlét (pl. kapcsolatvesztés) nem hosszabbítja meg az ébrenlétet. */
void app_slave_display_presence(app_slave_display_t *state, bool valid, bool active, int64_t now_us);
/* Indulás/PIN alatt látható marad, de az ideiglenes felülbírálás nem jelenlétjel. */
void app_slave_display_keep_visible(app_slave_display_t *state, bool visible, int64_t now_us);
/* A true a teljes ébresztő érintés lenyelését jelenti, a felengedést is beleértve.
 * A hívó inputréteg ilyenkor ne továbbítson kattintást/gesztust az LVGL-nek. */
bool app_slave_display_pointer(app_slave_display_t *state, bool pressed, int64_t now_us);
unsigned app_slave_display_brightness(app_slave_display_t *state, int64_t now_us);
