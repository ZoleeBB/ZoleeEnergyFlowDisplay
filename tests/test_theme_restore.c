/* A tényleges firmware-hez használt, heap-mentes timeout policy tesztje. */
#include "app_theme_restore_policy.h"
#include <assert.h>
#include <stdio.h>

static void test_timeout_and_recovery(void)
{
    app_theme_restore_policy_t state = {0};
    assert(app_theme_restore_tick(&state, true, 0) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, 5999999) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, 6000000) == APP_THEME_RESTORE_RETRY);
    assert(state.retries == 1);
    assert(app_theme_restore_tick(&state, true, 11999999) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, 12000000) == APP_THEME_RESTORE_RETRY);
    assert(app_theme_restore_tick(&state, true, 18000000) == APP_THEME_RESTORE_FALLBACK);
    assert(state.retries == APP_THEME_RESTORE_MAX_RETRIES);
    assert(app_theme_restore_tick(&state, true, 24000000) == APP_THEME_RESTORE_FALLBACK);
    assert(state.retries == APP_THEME_RESTORE_MAX_RETRIES); /* Nem túlcsorduló számláló. */
    assert(app_theme_restore_tick(&state, false, 24000001) == APP_THEME_RESTORE_WAIT);
    assert(!state.tracking && !state.retries);
    assert(app_theme_restore_tick(&state, true, 25000000) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, 26000000) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, 20000000) == APP_THEME_RESTORE_WAIT);
    assert(!state.retries); /* Óra visszalépésénél új időablak. */
}

static void test_independent_home_and_template_states(void)
{
    app_theme_restore_policy_t theme = {0}, home = {0};
    assert(app_theme_restore_tick(&theme, true, 1000) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&theme, true, 6001000) == APP_THEME_RESTORE_RETRY);
    assert(app_theme_restore_tick(&home, true, 6001000) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&theme, false, 6001001) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&home, true, 12001000) == APP_THEME_RESTORE_RETRY);
    assert(home.retries == 1 && theme.retries == 0);
}

int main(void)
{
    test_timeout_and_recovery();
    test_independent_home_and_template_states();
    puts("PASS: theme/home timeout, fallback, counter saturation, reset and clock regression");
    return 0;
}
