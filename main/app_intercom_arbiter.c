#include "app_intercom_control.h"

void app_intercom_arbiter_tick(app_intercom_arbiter_t *state, uint32_t revision, int64_t now)
{
    if (state->revision != revision) {
        state->revision = revision;
        state->silenced = false;
        /* Az előző tulajdonos bontásáig új hívás nem kaphat engedélyt. */
    }
    if (state->owner && state->owner != 1 && now >= state->deadline) state->owner = 0;
}

bool app_intercom_arbiter_command(app_intercom_arbiter_t *state, uint32_t owner,
                                  uint8_t operation, uint32_t grant, bool available, int64_t now)
{
    if (!owner) return false;
    if (operation == APP_INTERCOM_ACQUIRE) {
        if (!available || now < state->quarantine || state->owner) return false;
        state->owner = owner;
        if (++state->grant == 0) ++state->grant;
        state->deadline = now + APP_INTERCOM_LEASE_US;
        state->silenced = true;
        return true;
    }
    if (operation == APP_INTERCOM_GATE) {
        if (!available) return false;
        state->silenced = true;
        return true;
    }
    if (state->owner != owner || state->grant != grant) return false;
    if (operation == APP_INTERCOM_KEEP) {
        state->deadline = now + APP_INTERCOM_LEASE_US;
        return true;
    }
    if (operation == APP_INTERCOM_RELEASE || operation == APP_INTERCOM_CANCEL) {
        state->owner = 0;
        return true;
    }
    return false;
}
