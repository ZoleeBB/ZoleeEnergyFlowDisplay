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
    /* Egy rövid jelenléti időkorlát nem írhatja felül a később lejáró,
     * legalább 15 másodperces érintési ébresztést. */
    state->awake_until_us = state->touch_until_us;
    wake_for(state, now, (int64_t)state->profile.presence_timeout_seconds * 1000000);
}

/* Érintés: a profil időkorlátja, de legalább a minimális olvasási idő. */
static void wake_touch(app_slave_display_t *state, int64_t now)
{
    int64_t duration = (int64_t)state->profile.presence_timeout_seconds * 1000000;
    if (!state->profile.scheduled && duration < APP_SLAVE_DISPLAY_TOUCH_WAKE_MIN_US)
        duration = APP_SLAVE_DISPLAY_TOUCH_WAKE_MIN_US;
    state->touch_until_us = now > INT64_MAX - duration ? INT64_MAX : now + duration;
    /* Új érintés már az új profil idejét használja. A régi érintési határidőt
     * nem tartjuk meg, csak a még jogos jelenlétvesztési visszaszámlálást. */
    if (state->presence_countdown)
        wake(state, state->presence_seen_us);
    else
        state->awake_until_us = state->touch_until_us;
}

static bool presence_holds(const app_slave_display_t *state, int64_t now)
{
    return state->presence_active &&
           (state->profile.delay_initial_absence ||
            now - state->presence_seen_us < APP_SLAVE_DISPLAY_PRESENCE_HOLD_US);
}

static unsigned brightness(const app_slave_display_t *state, int64_t now)
{
    bool blank = state->night ? state->profile.blank_at_night : state->profile.blank_at_day;
    unsigned level = state->night ? state->profile.night_brightness : state->profile.day_brightness;
    if (!blank || state->keep_visible || presence_holds(state, now) ||
        (state->profile.delay_initial_absence && !state->presence_known) ||
        state->pointer_down || now < state->awake_until_us)
        return level;
    return 0;
}

/* Az elévült tartás ugyanúgy viselkedik, mint egy jelenlétvesztés: nem azonnali
 * sötétítés, hanem a megszokott visszaszámlálás. */
static void expire_presence(app_slave_display_t *state, int64_t now)
{
    /* Közvetlen Master-forrásnál csak érvényes 0 jelent jelenlétvesztést.
     * A kapcsolat alatti hiányt a hívó láthatósági felülbírálása kezeli; a
     * visszatérő 0 így a korábbi 1 után teljes késleltetést kaphat. */
    if (state->profile.delay_initial_absence || !state->presence_active || presence_holds(state, now))
        return;
    state->presence_active = false;
    state->presence_countdown = true;
    state->presence_seen_us = now;
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
    bool timeout_changed = state->profile.presence_timeout_seconds != profile->presence_timeout_seconds;
    state->profile = *profile;
    /* A futó időzítő az eredeti távollétkezdethez képest veszi át az új időt.
     * Azonos profil ismétlése és egyéb mező módosítása nem indít új ablakot. */
    if (state->pointer_down)
        wake_touch(state, now);
    else if (state->presence_active)
        wake(state, now);
    else if (timeout_changed && state->presence_countdown)
        wake(state, state->presence_seen_us);
    return true;
}

void app_slave_display_night(app_slave_display_t *state, bool night, int64_t now_us)
{
    if (!state)
        return;
    monotonic(state, now_us);
    /* A napszak csak az engedélyezést és a fényerőt váltja. Egy friss érintés
     * vagy jelenlétvesztés hátralévő idejét nem törölheti és nem indíthatja újra. */
    state->night = night;
}

void app_slave_display_blank_at_night(app_slave_display_t *state, bool blank, int64_t now_us)
{
    if (!state)
        return;
    monotonic(state, now_us);
    state->profile.blank_at_night = blank;
}

void app_slave_display_blanking(app_slave_display_t *state, bool day, bool night, int64_t now_us)
{
    if (!state)
        return;
    monotonic(state, now_us);
    state->profile.blank_at_day = day;
    state->profile.blank_at_night = night;
}

void app_slave_display_reset_presence(app_slave_display_t *state, int64_t now_us)
{
    if (!state)
        return;
    monotonic(state, now_us);
    state->presence_known = false;
    state->presence_active = false;
    state->presence_countdown = false;
    state->presence_seen_us = 0;
    state->awake_until_us = state->touch_until_us;
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
    bool initial = !state->presence_known;
    state->presence_known = true;
    bool was_active = state->presence_active;
    state->presence_active = active;
    if (active || was_active || initial)
        state->presence_seen_us = now;
    if (active)
    {
        state->presence_countdown = false;
        wake(state, now);
    }
    else if (was_active || (initial && state->profile.delay_initial_absence))
    {
        state->presence_countdown = !state->profile.blank_on_presence_loss;
        if (state->profile.blank_on_presence_loss)
            state->awake_until_us = state->touch_until_us;
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

void app_slave_display_scheduled(app_slave_display_t *state, bool blank,
                                 uint16_t wake_seconds, int64_t now_us)
{
    if (!state || !wake_seconds) return;
    if (!state->profile.scheduled) app_slave_display_reset_presence(state, now_us);
    state->profile.scheduled = true;
    state->profile.delay_initial_absence = false;
    state->profile.presence_timeout_seconds = wake_seconds;
    app_slave_display_blanking(state, blank, blank, now_us);
}
