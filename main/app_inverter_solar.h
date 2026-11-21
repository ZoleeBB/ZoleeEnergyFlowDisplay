#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double azimuth, altitude;
    int rise_minute, set_minute;
    bool night;
} app_solar_position_t;

/* A rendszer beállított helyi időzónáját használja. A sarki határesetben a percek -1 értékűek. */
bool app_inverter_solar(int64_t utc, double lat, double lon, app_solar_position_t *position);
#ifdef __cplusplus
}
#endif
