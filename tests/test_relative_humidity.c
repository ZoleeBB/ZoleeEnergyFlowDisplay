/* A valódi Loxone → közös minta → BLE út opcionális páratartalom-tesztje. */
#include "app_loxone_model.h"
#include "app_payload.h"
#include "app_ble_protocol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t esp_timer_get_time(void) { return 1000000; }
static lx_config_t cfg, restored;
static lx_values_t values;
static const uint8_t uuid[16] = {0x44, 0x33, 0x22, 0x11, 0x66, 0x55, 0x88, 0x77,
                                0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};

static app_energy_sample_t parse_sample(void)
{
    cJSON *payload = lx_values_payload(&values, &cfg);
    assert(payload);
    char *json = cJSON_PrintUnformatted(payload);
    assert(json);
    app_energy_sample_t sample;
    char error[128];
    assert(app_payload_parse(json, strlen(json), &sample, error, sizeof(error)) == ESP_OK);
    free(json);
    cJSON_Delete(payload);
    return sample;
}

static void event(double raw)
{
    uint8_t packet[24];
    memcpy(packet, uuid, 16);
    memcpy(packet + 16, &raw, 8);
    assert(lx_values_update(&values, &cfg, 2, packet, sizeof(packet)));
}

int main(void)
{
    lx_config_defaults(&cfg);
    strcpy(cfg.host, "192.168.2.10");
    strcpy(cfg.username, "display");
    const double required[] = {1, -1, 2, 2, 50, 1, 0, 25};
    for (unsigned i = 0; i < 8; i++)
    {
        strcpy(cfg.map[i].uuid, "ffffffff-ffff-ffff-ffffffffffffffff");
        values.seen[i] = true;
        values.value[i] = required[i];
    }
    app_energy_sample_t sample = parse_sample();
    assert(!sample.relative_humidity_valid);
    lx_uuid_string(uuid, cfg.map[LX_FIELD_HUMIDITY].uuid);
    sample = parse_sample();
    assert(!sample.relative_humidity_valid); /* Párosítva, kezdőérték még nincs. */
    assert(!lx_fields[LX_FIELD_HUMIDITY].required);
    assert(!lx_field_enabled(true, LX_FIELD_HUMIDITY));
    assert(lx_field_enabled(true, LX_FIELD_PRESENCE) && lx_field_enabled(true, LX_FIELD_MAIL));

    cfg.map[LX_FIELD_HUMIDITY].scale = 100;
    cfg.map[LX_FIELD_HUMIDITY].offset = -2;
    event(0.625);
    sample = parse_sample();
    assert(sample.relative_humidity_valid && sample.relative_humidity_pct == 61);
    cJSON *saved = lx_config_json(&cfg);
    assert(saved && lx_config_parse(saved, &restored, true));
    assert(restored.map[LX_FIELD_HUMIDITY].scale == 100);
    assert(restored.map[LX_FIELD_HUMIDITY].offset == -2);
    assert(!strcmp(restored.map[LX_FIELD_HUMIDITY].uuid, cfg.map[LX_FIELD_HUMIDITY].uuid));
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(saved, "mappings"),
                                           "RelativeHumidity");
    assert(lx_config_parse(saved, &restored, true) && !restored.map[LX_FIELD_HUMIDITY].uuid[0]);
    cJSON_Delete(saved);

    cfg.map[LX_FIELD_HUMIDITY].scale = 1;
    cfg.map[LX_FIELD_HUMIDITY].offset = 0;
    const double raw[] = {-1e300, -0.5, 0, 0.49, 0.5, 49.49, 49.5, 99.49, 99.5, 100, 1e300};
    const unsigned expected[] = {0, 0, 0, 0, 1, 49, 50, 99, 100, 100, 100};
    app_config_t config = {0};
    config.data_recept_mode = APP_DATA_LOXONE;
    config.display.day_brightness = 80;
    config.display.night_brightness = 20;
    uint8_t energy[20], weather[20];
    for (unsigned i = 0; i < sizeof(raw) / sizeof(raw[0]); i++)
    {
        event(raw[i]);
        sample = parse_sample();
        assert(sample.relative_humidity_valid && sample.relative_humidity_pct == expected[i]);
        app_ble_protocol_encode(&sample, &config, 2000000, i, energy, weather);
        assert(energy[0] == 2 && weather[0] == 2 && energy[3] == expected[i]);
        app_energy_sample_t decoded = {0};
        assert(app_ble_protocol_decode(energy, 20, weather, 20, 3000000, &decoded) == ESP_OK);
        assert(decoded.relative_humidity_valid && decoded.relative_humidity_pct == expected[i]);
        assert(decoded.power_grid_kw == sample.power_grid_kw && decoded.status == sample.status);
    }
    /* Hibás adat eltünteti az utolsó ismert páratartalmat, az energia él tovább. */
    event(NAN);
    sample = parse_sample();
    assert(!sample.relative_humidity_valid && sample.power_pv_kw == 2);
    event(INFINITY);
    assert(!parse_sample().relative_humidity_valid);
    cfg.map[LX_FIELD_HUMIDITY].scale = 1e6;
    event(1e308);
    assert(!parse_sample().relative_humidity_valid);
    cfg.map[LX_FIELD_HUMIDITY].scale = 1;
    event(42);
    assert(parse_sample().relative_humidity_pct == 42);
    cfg.map[LX_FIELD_HUMIDITY].uuid[0] = 0;
    assert(!parse_sample().relative_humidity_valid); /* Megszüntetett párosítás. */

    /* Régi v1 Masterrel hiányzó páratartalom, a korábbi minta nem öröklődhet. */
    sample.relative_humidity_valid = false;
    app_ble_protocol_encode(&sample, &config, 2000000, 5, energy, weather);
    app_energy_sample_t decoded = {.relative_humidity_valid = true, .relative_humidity_pct = 88};
    assert(energy[0] == 1 && energy[3] == 80);
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 3000000, &decoded) == ESP_OK);
    assert(!decoded.relative_humidity_valid && decoded.relative_humidity_pct == 0);

    /* Hibás v2 és kevert verzió nem írhat részleges állapotot. */
    app_energy_sample_t previous = decoded;
    energy[0] = weather[0] = 2;
    energy[3] = 101;
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 3000000, &decoded) == ESP_ERR_INVALID_ARG);
    assert(memcmp(&decoded, &previous, sizeof(decoded)) == 0);
    energy[3] = 0;
    weather[0] = 1;
    assert(app_ble_protocol_decode(energy, 20, weather, 20, 3000000, &decoded) == ESP_ERR_INVALID_ARG);
    puts("PASS: optional humidity mapping, transform, round/clamp, invalid/recovery, legacy config, BLE v1/v2 boundaries and atomic rejection");
}
