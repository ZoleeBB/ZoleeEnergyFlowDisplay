#pragma once
#include <stdbool.h>
#include <stdint.h>

/*
 * A kijelző tájolása az NVS-ben a korai (config.json előtti) indításhoz.
 * A korábbi 90 fokos „próba/letiltás" védelmet eltávolítottuk: a 90 fokos
 * mód stabil, az indulás közbeni újraindítás viszont mindig visszaállította
 * 270 fokra, mert a 15 másodperces igazolás nem futott le.
 */
typedef struct
{
    uint16_t rotation_degrees; /* NVS-ben gyorsindításhoz tárolt érték */
} app_display_boot_state_t;

static inline uint16_t app_display_boot_select(const app_display_boot_state_t *state)
{
    return state->rotation_degrees == 90 ? 90 : 270;
}

/* Igaz, ha a config.json eltér az NVS-től: ekkor mentés és vezérelt újraindulás. */
static inline bool app_display_boot_sync(app_display_boot_state_t *state, uint16_t desired)
{
    desired = desired == 90 ? 90 : 270;
    if (state->rotation_degrees == desired)
        return false;
    state->rotation_degrees = desired;
    return true;
}
