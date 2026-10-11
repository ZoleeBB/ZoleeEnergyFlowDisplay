#pragma once
#include "app_state.h"
typedef struct {
    app_runtime_state_t runtime;
    char ip[32], firmware[33];
    int rssi;
    int64_t packet_age_seconds, uptime_seconds;
    unsigned free_heap_kib;
} app_diagnostics_t;
/* Rövid snapshot; nem olvas SD-t és nem tartja az LVGL zárat. */
void app_diagnostics_capture(app_diagnostics_t *out);
