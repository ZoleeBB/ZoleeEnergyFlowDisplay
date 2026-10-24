/* Új konfiguráció, valódi BLE-kódoló/dekódoló és a közös fényerőszabály. */
#include "app_ble_protocol.h"
#include "app_energy_display_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *base = "{\"DataReceptMode\":\"loxone\",\"wifi\":{\"ssid\":\"test\"}}";
    const char *enabled = "{\"DataReceptMode\":\"loxone\",\"EnergyFlowDisabled\":1,\"wifi\":{\"ssid\":\"test\"}}";
    const char *invalid[] = {
        "{\"DataReceptMode\":\"http\",\"EnergyFlowDisabled\":1}",
        "{\"DataReceptMode\":\"mqtt\",\"EnergyFlowDisabled\":1}",
        "{\"DataReceptMode\":\"slave\",\"EnergyFlowDisabled\":1}",
        "{\"DataReceptMode\":\"loxone\",\"EnergyFlowDisabled\":2}",
        "{\"DataReceptMode\":\"loxone\",\"EnergyFlowDisabled\":\"true\"}"
    };
    app_config_t config;
    char error[256] = {0};
    assert(app_config_parse_json(base, strlen(base), &config, error, sizeof(error)) == ESP_OK);
    assert(!config.energy_flow_disabled);
    assert(app_config_parse_json(enabled, strlen(enabled), &config, error, sizeof(error)) == ESP_OK);
    assert(config.energy_flow_disabled);
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(app_config_parse_json(invalid[i], strlen(invalid[i]), &config, error, sizeof(error)) != ESP_OK);

    app_config_set_defaults(&config);
    config.data_recept_mode = APP_DATA_LOXONE;
    config.energy_flow_disabled = true;
    config.display.day_brightness = 83;
    config.display.night_brightness = 24;
    assert(app_energy_display_target(&config, false, true, false) == 10);
    assert(app_energy_display_target(&config, true, true, false) == 10);
    assert(app_energy_display_target(&config, false, true, true) == 83);
    assert(app_energy_display_target(&config, true, true, true) == 24);
    assert(app_energy_display_target(&config, false, false, false) == 83);
    assert(app_energy_display_target(&config, true, false, false) == 24);
    config.energy_flow_disabled = false;
    assert(app_energy_display_target(&config, true, true, false) == 24);

    uint8_t energy[20], weather[20];
    app_energy_sample_t source = {.night_mode = true, .power_pv_kw = 12,
                                  .soc_bat_pct = 100, .received_epoch_s = 1800000000};
    app_energy_sample_t decoded = {0};
    config.energy_flow_disabled = true;
    app_ble_protocol_encode(&source, &config, 1, 65535, energy, weather);
    assert(energy[1] == 12 && app_ble_packet_sequence(energy) == 65535);
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 1, &decoded) == ESP_OK);
    assert(decoded.energy_flow_disabled && decoded.night_mode);
    assert(decoded.power_pv_kw == 0 && decoded.soc_bat_pct == 0 && decoded.received_epoch_s == 0);
    energy[1] |= 1;
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 1, &decoded) == ESP_ERR_INVALID_ARG);
    energy[1] = 12; weather[2] = 1;
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 1, &decoded) == ESP_ERR_INVALID_ARG);
    weather[2] = 0; weather[18] = 0;
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 1, &decoded) == ESP_ERR_INVALID_ARG);
    app_ble_protocol_encode(NULL, &config, 1, 0, energy, weather);
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 1, &decoded) == ESP_OK);
    assert(decoded.energy_flow_disabled && !decoded.night_mode);
    config.energy_flow_disabled = false;
    app_ble_protocol_encode(NULL, &config, 1, 1, energy, weather);
    assert(!(energy[1] & 8));
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 1, &decoded) == ESP_ERR_INVALID_STATE);
    app_runtime_state_t runtime = {.data_recept_mode = APP_DATA_SLAVE, .config_ok = true,
        .time_synced = true, .slave_connected = true, .cardless_slave = true,
        .energy_flow_disabled = true, .slave_packet_monotonic_us = 50000000};
    assert(app_state_ota_ready(&runtime));
    assert(!app_state_data_timed_out(&runtime, 60000000, 15));
    assert(app_state_data_timed_out(&runtime, 65000000, 15));
    runtime.energy_flow_disabled = false;
    assert(!app_state_ota_ready(&runtime));
    runtime.energy_flow_disabled = true; runtime.slave_source_stale = true;
    assert(!app_state_ota_ready(&runtime));
    puts("PASS: Loxone-only opt-in, malformed config, independent night state, no fake BLE measurements, strict mode frames and idle/call day/night brightness");
    return 0;
}
