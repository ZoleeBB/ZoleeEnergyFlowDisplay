/* A tényleges firmware-hez használt, heap-mentes timeout policy tesztje. */
#include "app_theme_restore_policy.h"
#include <assert.h>
#include <stdio.h>

static void test_timeout_and_recovery(void)
{
    app_theme_restore_policy_t state = {0};
    assert(app_theme_restore_tick(&state, true, false, 0) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, false, 5999999) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, false, 6000000) == APP_THEME_RESTORE_RETRY);
    assert(state.retries == 1);
    assert(app_theme_restore_tick(&state, true, false, 11999999) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, false, 12000000) == APP_THEME_RESTORE_RETRY);
    assert(app_theme_restore_tick(&state, true, false, 18000000) == APP_THEME_RESTORE_FALLBACK);
    assert(state.retries == APP_THEME_RESTORE_MAX_RETRIES);
    assert(app_theme_restore_tick(&state, true, false, 24000000) == APP_THEME_RESTORE_FALLBACK);
    assert(state.retries == APP_THEME_RESTORE_MAX_RETRIES); /* Nem túlcsorduló számláló. */
    assert(app_theme_restore_tick(&state, false, false, 24000001) == APP_THEME_RESTORE_WAIT);
    assert(!state.tracking && !state.retries);
    assert(app_theme_restore_tick(&state, true, false, 25000000) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, false, 26000000) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, false, 20000000) == APP_THEME_RESTORE_WAIT);
    assert(!state.retries); /* Óra visszalépésénél új időablak. */
}

static void test_independent_home_and_template_states(void)
{
    app_theme_restore_policy_t theme = {0}, home = {0};
    assert(app_theme_restore_tick(&theme, true, false, 1000) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&theme, true, false, 6001000) == APP_THEME_RESTORE_RETRY);
    assert(app_theme_restore_tick(&home, true, false, 6001000) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&theme, false, false, 6001001) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&home, true, false, 12001000) == APP_THEME_RESTORE_RETRY);
    assert(home.retries == 1 && theme.retries == 0);
}

/*
 * Foglalt tároló: a segédfájl-generálás vagy a webes feltöltés alatt a betöltés
 * nem haladhat, de ez nem beragadás. A szakasz órája áll, tartalék nézet nincs
 * – egy felső korlátig, mert egy tényleg beakadt művelet mögött sem várhatunk
 * örökre.
 */
static void test_blocked_storage_does_not_escalate(void)
{
    app_theme_restore_policy_t state = {0};
    assert(app_theme_restore_tick(&state, true, true, 0) == APP_THEME_RESTORE_WAIT);
    for (int64_t now = 1000000; now <= 40000000; now += 1000000)
        assert(app_theme_restore_tick(&state, true, true, now) == APP_THEME_RESTORE_WAIT);
    assert(!state.retries); /* 40 s foglaltság után sincs eszkaláció. */
    /* A zár felszabadul: a szakasz órája az utolsó foglalt pillanattól jár. */
    assert(app_theme_restore_tick(&state, true, false, 41000000) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, false, 45999999) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, false, 46000000) == APP_THEME_RESTORE_RETRY);
    assert(state.retries == 1);
}

/* Tartósan foglalt zár mögött is kell eszkaláció: a felső korlát után indul. */
static void test_blocked_storage_has_upper_bound(void)
{
    app_theme_restore_policy_t state = {0};
    /* Az első hívás indítja a mérést; a foglaltság a másodiktól számít. */
    assert(app_theme_restore_tick(&state, true, true, 0) == APP_THEME_RESTORE_WAIT);
    const int64_t blocked_from = 1000000;
    assert(app_theme_restore_tick(&state, true, true, blocked_from) == APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, true,
                                  blocked_from + APP_THEME_RESTORE_BLOCKED_US - 1) ==
           APP_THEME_RESTORE_WAIT);
    assert(!state.retries);
    /* A korláton túl a türelem elfogy, és a szokásos szakasz órája jár tovább. */
    assert(app_theme_restore_tick(&state, true, true,
                                  blocked_from + APP_THEME_RESTORE_BLOCKED_US) ==
           APP_THEME_RESTORE_WAIT);
    assert(app_theme_restore_tick(&state, true, true,
                                  blocked_from + APP_THEME_RESTORE_BLOCKED_US +
                                      APP_THEME_RESTORE_TIMEOUT_US) == APP_THEME_RESTORE_RETRY);
    assert(state.retries == 1);
}

int main(void)
{
    test_timeout_and_recovery();
    test_independent_home_and_template_states();
    test_blocked_storage_does_not_escalate();
    test_blocked_storage_has_upper_bound();
    puts("PASS: theme/home timeout, fallback, blocked storage patience and bound, counter saturation, reset and clock regression");
    return 0;
}
