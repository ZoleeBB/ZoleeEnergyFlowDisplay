#pragma once
/* Közös, foglalás- és zárolásmentes betöltési időkorlát-felügyelet.
 * Csak a runtime/UI szál használja. A tényleges fallback művelet nem itt fut. */
#include <stdbool.h>
#include <stdint.h>

#define APP_THEME_RESTORE_TIMEOUT_US (6LL * 1000 * 1000)
#define APP_THEME_RESTORE_MAX_RETRIES 3U
/*
 * Ha a betöltés azért nem indul el, mert a tárzárat más tartja (segédfájl
 * generálás, webes feltöltés), akkor nem beragadásról van szó: a szakasz órája
 * ilyenkor áll. Nem állhat viszont örökre, mert egy tényleg beakadt művelet
 * mögött is várakozhatunk: ennyi folyamatos foglaltság után az eszkaláció a
 * megszokott módon folytatódik.
 */
#define APP_THEME_RESTORE_BLOCKED_US (45LL * 1000 * 1000)

typedef struct {
    int64_t started_us;
    int64_t blocked_since_us;
    unsigned retries;
    bool tracking;
    bool blocked;
} app_theme_restore_policy_t;

typedef enum {
    APP_THEME_RESTORE_WAIT = 0,
    APP_THEME_RESTORE_RETRY = 1,
    APP_THEME_RESTORE_FALLBACK = 2,
} app_theme_restore_action_t;

static inline void app_theme_restore_reset(app_theme_restore_policy_t *state)
{
    *state = (app_theme_restore_policy_t){0};
}

/*
 * `loading`: a kijelző a betöltőképernyőn áll.
 * `blocked`: a betöltés csak azért nem haladt, mert a katalógus- vagy a
 * tárzárat más tartja. Ilyenkor várni helyes, nem tartalék nézetre váltani.
 */
static inline app_theme_restore_action_t app_theme_restore_tick(
    app_theme_restore_policy_t *state, bool loading, bool blocked, int64_t now_us)
{
    if (!loading) {
        app_theme_restore_reset(state);
        return APP_THEME_RESTORE_WAIT;
    }
    if (!state->tracking || now_us < state->started_us) {
        state->started_us = now_us;
        state->tracking = true;
        state->blocked = false;
        return APP_THEME_RESTORE_WAIT;
    }
    if (blocked) {
        if (!state->blocked || now_us < state->blocked_since_us) {
            state->blocked = true;
            state->blocked_since_us = now_us;
        }
        if (now_us - state->blocked_since_us < APP_THEME_RESTORE_BLOCKED_US) {
            /* A szakasz órája áll, amíg a másik munka dolgozik a kártyán. */
            state->started_us = now_us;
            return APP_THEME_RESTORE_WAIT;
        }
    } else {
        state->blocked = false;
    }
    if (now_us - state->started_us < APP_THEME_RESTORE_TIMEOUT_US)
        return APP_THEME_RESTORE_WAIT;
    state->started_us = now_us;
    if (state->retries < APP_THEME_RESTORE_MAX_RETRIES)
        ++state->retries;
    return state->retries >= APP_THEME_RESTORE_MAX_RETRIES
        ? APP_THEME_RESTORE_FALLBACK : APP_THEME_RESTORE_RETRY;
}
