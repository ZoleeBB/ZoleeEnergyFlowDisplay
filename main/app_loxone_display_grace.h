#pragma once
/* Kizárólag UI-megjelenítési türelmi idő. A Loxone worker és a statisztika
 * adatérvényességét, újracsatlakozását vagy parancsait nem változtatja meg.
 * A kijelző runtime feladata használja, heap- és lock-művelet nélkül. */
#include <stdbool.h>
#include <stdint.h>

#define APP_LOXONE_DISPLAY_GRACE_US (10LL * 1000 * 1000)

typedef struct {
    int64_t lost_since_us;
    bool ever_valid;
    bool loss_started;
} app_loxone_display_grace_t;

static inline void app_loxone_display_grace_reset(app_loxone_display_grace_t *state)
{
    *state = (app_loxone_display_grace_t){0};
}

/* true: a korábban kirajzolt nézetet minden változtatás nélkül megtartjuk.
 * Kezdeti kapcsolatfelépítéskor nincs mit befagyasztani; a megszokott
 * várakozóképernyő marad. A türelmi idő csak ÚJ ÉRVÉNYES mintától indulhat
 * újra, egy átmenetileg visszaállt WebSocket nem nullázza. */
static inline bool app_loxone_display_grace_tick(
    app_loxone_display_grace_t *state, bool loxone_mode,
    bool valid_sample, bool visible_live_scene, int64_t now_us)
{
    if (!loxone_mode) {
        app_loxone_display_grace_reset(state);
        return false;
    }
    if (valid_sample) {
        state->ever_valid = true;
        state->loss_started = false;
        return false;
    }
    if (!state->ever_valid || !visible_live_scene)
        return false;
    if (!state->loss_started) {
        state->lost_since_us = now_us;
        state->loss_started = true;
    }
    if (now_us < state->lost_since_us)
        return false;
    return now_us - state->lost_since_us < APP_LOXONE_DISPLAY_GRACE_US;
}
