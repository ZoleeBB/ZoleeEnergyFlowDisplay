#include "app_diagnostics.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#ifndef APP_TEMPLATE_DESKTOP
#include "app_wifi.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#endif
void app_diagnostics_capture(app_diagnostics_t *out){
    memset(out,0,sizeof(*out));app_state_get_runtime(&out->runtime);
    int64_t now=esp_timer_get_time();out->uptime_seconds=now/1000000;
    out->packet_age_seconds=out->runtime.has_valid_sample?
        (now-out->runtime.last_valid_packet_monotonic_us)/1000000:-1;
    snprintf(out->ip,sizeof(out->ip),"-");
#ifdef APP_TEMPLATE_DESKTOP
    snprintf(out->firmware,sizeof(out->firmware),"Desktop simulator");
#else
    snprintf(out->firmware,sizeof(out->firmware),"%.32s",esp_app_get_description()->version);
    out->free_heap_kib=esp_get_free_heap_size()/1024;
    if(out->runtime.wifi_connected){(void)app_wifi_get_ip(out->ip,sizeof(out->ip));(void)app_wifi_get_rssi(&out->rssi);}
#endif
}
