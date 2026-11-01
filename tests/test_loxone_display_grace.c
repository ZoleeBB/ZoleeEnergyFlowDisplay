/* A firmware-rel közös, heap- és zármentes Loxone megjelenítési policy. */
#include "app_loxone_display_grace.h"
#include <assert.h>
#include <stdio.h>

static void test_silent_ten_second_hold(void)
{
    app_loxone_display_grace_t state = {0};
    const int64_t t = 30000000LL;
    /* Első bejelentkezés: nincs korábbi élő kép, a várakozó nézet látszik. */
    assert(!app_loxone_display_grace_tick(&state, true, false, false, t));
    assert(!app_loxone_display_grace_tick(&state, true, false, true, t));
    assert(!state.loss_started);
    /* Érvényes minta után a dashboard megjelent. */
    assert(!app_loxone_display_grace_tick(&state, true, true, true, t));
    assert(state.ever_valid && !state.loss_started);
    /* A korábbi, kirajzolt kép 0 és 10 másodperc között változatlan. */
    assert(app_loxone_display_grace_tick(&state, true, false, true, t));
    assert(app_loxone_display_grace_tick(&state, true, false, true, t + 9000000));
    assert(app_loxone_display_grace_tick(&state, true, false, true, t + 9999999));
    /* Az ideiglenes websocket-visszatérés minta nélkül nem nulláz. */
    assert(!app_loxone_display_grace_tick(&state, true, false, true, t + 10000000));
    assert(!app_loxone_display_grace_tick(&state, true, false, true, t + 12000000));
    /* A hibaoldalból nem indul újabb 10 másodperc. */
    assert(!app_loxone_display_grace_tick(&state, true, false, false, t + 13000000));
    /* Csak érvényes minta nyitja újra a türelmi időt. */
    assert(!app_loxone_display_grace_tick(&state, true, true, true, t + 14000000));
    assert(app_loxone_display_grace_tick(&state, true, false, true, t + 15000000));
    assert(app_loxone_display_grace_tick(&state, true, false, true, t + 24999999));
    assert(!app_loxone_display_grace_tick(&state, true, false, true, t + 25000000));
}

static void test_mode_switch_and_non_visible_view(void)
{
    app_loxone_display_grace_t state = {0};
    assert(!app_loxone_display_grace_tick(&state, true, true, true, 1000));
    /* A videó-, OTA-, illetve betöltőképernyőt ne fagyasszuk be. */
    assert(!app_loxone_display_grace_tick(&state, true, false, false, 2000));
    assert(!state.loss_started);
    /* Ha még kirajzolható dashboard áll rendelkezésre, a számlálás indul. */
    assert(app_loxone_display_grace_tick(&state, true, false, true, 3000));
    /* Módváltáskor ne ragadjon a korábbi Loxone állapot. */
    assert(!app_loxone_display_grace_tick(&state, false, false, true, 4000));
    assert(!state.ever_valid && !state.loss_started);
    assert(!app_loxone_display_grace_tick(&state, true, false, true, 5000));
}

static void test_clock_reset(void)
{
    app_loxone_display_grace_t state = {0};
    assert(!app_loxone_display_grace_tick(&state, true, true, true, 11000000));
    assert(app_loxone_display_grace_tick(&state, true, false, true, 12000000));
    assert(!app_loxone_display_grace_tick(&state, true, false, true, 10000000));
}

int main(void)
{
    test_silent_ten_second_hold();
    test_mode_switch_and_non_visible_view();
    test_clock_reset();
    puts("PASS: Loxone 10s silent scene hold, exact boundary, recovery, reset, no repeated grace");
    return 0;
}
