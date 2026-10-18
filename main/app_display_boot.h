#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint16_t rotation_degrees; /* NVS-ben gyorsindításhoz tárolt érték */
    uint8_t trial;             /* 90 fokos boot még nem igazolt */
    uint8_t blocked;           /* előző sikertelen 90 fokos indítás */
} app_display_boot_state_t;

static inline uint16_t app_display_boot_select(app_display_boot_state_t *state)
{
    if (state->rotation_degrees != 90 || state->blocked)
        return 270;
    if (state->trial)
    {
        state->trial = 0;
        state->blocked = 1;
        return 270;
    }
    state->trial = 1;
    return 90;
}

static inline bool app_display_boot_sync(app_display_boot_state_t *state, uint16_t desired)
{
    desired = desired == 90 ? 90 : 270;
    if (state->rotation_degrees == desired)
        return false;
    state->rotation_degrees = desired;
    state->trial = 0;
    state->blocked = 0;
    return true;
}
