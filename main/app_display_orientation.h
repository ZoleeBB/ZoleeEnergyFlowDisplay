#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A Waveshare panel és a GT911 közös, fekvő tájolása. */
typedef struct
{
    uint16_t rotation_degrees;
    bool touch_swap_xy;
    bool touch_mirror_x;
    bool touch_mirror_y;
} app_display_orientation_t;

static inline app_display_orientation_t app_display_orientation_for_rotation(uint16_t degrees)
{
    const bool left = degrees == 90;
    return (app_display_orientation_t){.rotation_degrees = left ? 90 : 270,
                                       .touch_swap_xy = true,
                                       .touch_mirror_x = left,
                                       .touch_mirror_y = !left};
}

/* A 270 fokos GT911 koordináták a 90 fokos nézetben 180 fokkal tükröződnek. */
static inline void app_display_map_touch_point(uint16_t degrees, int32_t *x, int32_t *y)
{
    if (degrees == 90)
    {
        *x = 1279 - *x;
        *y = 799 - *y;
    }
}

/* Host demók és régi, memóriából feltöltött konfigurációk biztonságos alapértéke. */
static inline uint16_t app_display_normalize_rotation(uint16_t degrees)
{
    return degrees == 90 ? 90 : 270;
}
