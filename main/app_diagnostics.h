#pragma once
#include "app_state.h"
typedef struct {
    app_runtime_state_t runtime;
    char ip[32], firmware[33];
    int rssi;
    int64_t packet_age_seconds, uptime_seconds;
    bool cpu_valid,sd_space_valid;
    unsigned cpu_percent[2];
    uint64_t sd_total_bytes,sd_free_bytes;
    unsigned total_internal_kib,total_psram_kib,free_psram_kib;
    unsigned free_heap_kib, free_internal_kib, largest_dma_block_bytes;
} app_diagnostics_t;
/* Rövid snapshot; nem olvas SD-t és nem tartja az LVGL zárat. */
void app_diagnostics_capture(app_diagnostics_t *out);

/* A mérési ablak foglaltsága; az idle számláló mintavételi eltérését levágjuk. */
static inline unsigned app_diagnostics_cpu_percent(uint64_t idle,uint64_t elapsed){
    return !elapsed||idle>=elapsed?0:100-(unsigned)(idle*100/elapsed);
}
