/* Az időzítés kizárólag monoton időt használ: NTP és nyári időszámítás nem ébreszt.
 * A modul nem foglal memóriát, és nem írja a flash-t érintés/jelenlét hatására. */
#include "app_slave_display.h"
#include <limits.h>
#include <string.h>

static bool valid_profile(const app_slave_display_profile_t *profile)
{
    return profile && profile->day_brightness >= 1 && profile->day_brightness <= 100 &&
           profile->night_brightness >= 1 && profile->night_brightness <= 100 &&
           profile->presence_timeout_seconds >= 1 && profile->presence_timeout_seconds <= 86400;
}

static int64_t monotonic(app_slave_display_t *state, int64_t now)
{
    /* A hívó hibás/korábbi időbélyege se tudja visszaforgatni az időzítőt. */
    if (now > state->last_now_us)
        state->last_now_us = now;
    return state->last_now_us;
}

static void wake(app_slave_display_t *state, int64_t now)
{
    int64_t duration = (int64_t)state->profile.presence_timeout_seconds * 1000000;
    state->awake_until_us = now > INT64_MAX - duration ? INT64_MAX : now + duration;
}

static unsigned brightness(const app_slave_display_t *state, int64_t now)
{
    if (!state->night)
        return state->profile.day_brightness;
    if (!state->profile.blank_at_night || state->presence_active || state->pointer_down ||
        now < state->awake_until_us)
        return state->profile.night_brightness;
    return 0;
}

bool app_slave_display_init(app_slave_display_t *state,
                            const app_slave_display_profile_t *profile, int64_t now_us)
{
    if (!state || !valid_profile(profile))
        return false;
    memset(state, 0, sizeof(*state));
    state->profile = *profile;
    state->last_now_us = now_us > 0 ? now_us : 0;
    return true;
}

bool app_slave_display_profile(app_slave_display_t *state,
                               const app_slave_display_profile_t *profile, int64_t now_us)
{
    if (!state || !valid_profile(profile))
        return false;
    int64_t now = monotonic(state, now_us);
    state->profile = *profile;
    /* Érvényes aktivitás új beállításnál az új időkorlátot követi. Profilfrissítés
     * önmagában viszont nem ébreszthet fel egy sötét kijelzőt. */
    if (state->presence_active || state->pointer_down)
        wake(state, now);
    return true;
}

void app_slave_display_night(app_slave_display_t *state, bool night, int64_t now_us)
{
    if (!state)
        return;
    monotonic(state, now_us);
    if (state->night != night)
    {
        state->night = night;
        /* Nappali mozgás lejárt időzítője ne világítson bele az éjszakába.
         * Ténylegesen még aktív jelenlét vagy nyomott ujj továbbra is ébren tart. */
        state->awake_until_us = 0;
    }
}

void app_slave_display_presence(app_slave_display_t *state, bool valid, bool active, int64_t now_us)
{
    if (!state)
        return;
    int64_t now = monotonic(state, now_us);
    bool was_active = state->presence_active;
    state->presence_active = valid && active;
    if (state->presence_active || (valid && was_active))
        wake(state, now);
}

bool app_slave_display_pointer(app_slave_display_t *state, bool pressed, int64_t now_us)
{
    if (!state)
        return false;
    int64_t now = monotonic(state, now_us);
    if (pressed && !state->pointer_down && brightness(state, now) == 0)
        state->swallow_pointer = true;
    bool consume = state->swallow_pointer;
    bool was_down = state->pointer_down;
    state->pointer_down = pressed;
    if (pressed || was_down)
        wake(state, now);
    if (!pressed)
        state->swallow_pointer = false;
    return consume;
}

unsigned app_slave_display_brightness(app_slave_display_t *state, int64_t now_us)
{
    return state ? brightness(state, monotonic(state, now_us)) : 0;
}
