#pragma once
#include "app_state.h"

typedef struct {
    int64_t last_progress_us;
    bool scanning;
} slave_4b_link_watchdog_t;

/* Visszaadja a lejárt újraindítási időkorlátot, vagy nullát, amíg a kapcsolat
 * vagy a közvetlen inverteres Slave új keresési ciklusai működnek. */
static inline int64_t slave_4b_link_timed_out(slave_4b_link_watchdog_t *watchdog,
    const app_runtime_state_t *state, bool direct, bool validating, unsigned attempts, int64_t now)
{
    bool new_scan = state->ble_ready && state->ble_scanning && !watchdog->scanning;
    watchdog->scanning = state->ble_ready && state->ble_scanning;
    if (!watchdog->last_progress_us || state->slave_connected || (direct && new_scan))
        watchdog->last_progress_us = now;
    int64_t limit = !state->ble_ready ? INT64_C(90000000) :
        attempts == 0 ? INT64_C(600000000) : attempts == 1 ? INT64_C(1800000000) : INT64_C(7200000000);
    /* Az OTA saját határidejéig (elérhetetlen Master esetén 20 perc) maga
     * a validáló feladat dönt az esetleges visszaállításról. */
    return !validating && now - watchdog->last_progress_us >= limit ? limit : 0;
}
