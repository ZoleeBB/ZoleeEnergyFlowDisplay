#pragma once
#include "app_inverter_schedule.h"
#include "app_ble_protocol.h"

/* A hitelesített profil kiegészítése. A korábbi 1004-es karakterisztika mérete 102 bájt. */
#define APP_BLE_POLICY_SIZE 33
typedef struct {
    bool direct;
    bool intercom;
    app_inverter_schedule_t schedule;
} app_ble_display_profile_t;

bool app_ble_policy_decode(const uint8_t wire[APP_BLE_POLICY_SIZE], app_ble_display_profile_t *out);
void app_ble_policy_encode(const app_ble_display_profile_t *profile, uint8_t wire[APP_BLE_POLICY_SIZE]);
