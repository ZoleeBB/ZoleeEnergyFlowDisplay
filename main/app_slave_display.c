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

static void wake_for(app_slave_display_t *state, int64_t now, int64_t duration)
{
    int64_t until = now > INT64_MAX - duration ? INT64_MAX : now + duration;
    if (until > state->awake_until_us)
        state->awake_until_us = until;
}

static void wake(app_slave_display_t *state, int64_t now)
{
    state->awake_until_us = 0;
    wake_for(state, now, (int64_t)state->profile.presence_timeout_seconds * 1000000);
}

/* Érintés: a profil időkorlátja, de legalább a minimális olvasási idő. */
static void wake_touch(app_slave_display_t *state, int64_t now)
{
    wake(state, now);
    wake_for(state, now, APP_SLAVE_DISPLAY_TOUCH_WAKE_MIN_US);
}

static bool presence_holds(const app_slave_display_t *state, int64_t now)
{
    return state->presence_active &&
           now - state->presence_seen_us < APP_SLAVE_DISPLAY_PRESENCE_HOLD_US;
}

static unsigned brightness(const app_slave_display_t *state, int64_t now)
{
    if (!state->night)
        return state->profile.day_brightness;
    if (!state->profile.blank_at_night || state->keep_visible || presence_holds(state, now) ||
        state->pointer_down || now < state->awake_until_us)
        return state->profile.night_brightness;
    return 0;
}

/* Az elévült tartás ugyanúgy viselkedik, mint egy jelenlétvesztés: nem azonnali
 * sötétítés, hanem a megszokott visszaszámlálás. */
static void expire_presence(app_slave_display_t *state, int64_t now)
{
    if (!state->presence_active || presence_holds(state, now))
        return;
    state->presence_active = false;
    wake(state, now);
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

void app_slave_display_blank_at_night(app_slave_display_t *state, bool blank, int64_t now_us)
{
    if (!state)
        return;
    monotonic(state, now_us);
    state->profile.blank_at_night = blank;
}

void app_slave_display_presence(app_slave_display_t *state, bool valid, bool active, int64_t now_us)
{
    if (!state)
        return;
    int64_t now = monotonic(state, now_us);
    /* Csak érvényes minta változtathat állapotot: egy kimaradt BLE-olvasás vagy
     * a lejárt lease nem jelenti azt, hogy a jelenlét megszűnt. */
    if (!valid)
        return;
    bool was_active = state->presence_active;
    state->presence_active = active;
    state->presence_seen_us = now;
    if (active)
        wake(state, now);
    else if (was_active)
    {
        if (state->profile.blank_on_presence_loss)
            state->awake_until_us = 0;
        else
            wake(state, now);
    }
}

void app_slave_display_keep_visible(app_slave_display_t *state, bool visible, int64_t now_us)
{
    if (!state)
        return;
    monotonic(state, now_us);
    state->keep_visible = visible;
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
        wake_touch(state, now);
    if (!pressed)
        state->swallow_pointer = false;
    return consume;
}

unsigned app_slave_display_brightness(app_slave_display_t *state, int64_t now_us)
{
    if (!state)
        return 0;
    int64_t now = monotonic(state, now_us);
    expire_presence(state, now);
    return brightness(state, now);
}
