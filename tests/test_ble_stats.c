/* A teljes napi pillanatkép és minden lap hibás bemenetekkel is fut. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app_ble_stats_protocol.h"

static app_stats_snapshot_t source, result;
static uint8_t wire[APP_BLE_STATS_SIZE], assembled[APP_BLE_STATS_SIZE];
static void checksum(void)
{
    uint32_t crc = app_ble_stats_crc(wire, sizeof(wire) - 4);
    for (unsigned i = 0; i < 4; i++)
        wire[sizeof(wire) - 4 + i] = (uint8_t)(crc >> (8 * i));
}
static void encode(void)
{
    assert(app_ble_stats_encode(&source, 42, 1792324800, wire));
}
static bool decode(void)
{
    return app_ble_stats_decode(wire, sizeof(wire), 42, 1792324800, "2026-10-18", &result);
}
int main(void)
{
    strcpy(source.date, "2026-10-18");
    strcpy(source.reference_currency, "Ft");
    source.reference_valid = true;
    source.reference_energy.tree_absorption_kg_per_year = 21.77;
    source.reference_energy.electricity_purchase_price = 70.1;
    source.reference_energy.feed_in_price = 5.2;
    source.reference_energy.co2_factor_kg_per_kwh = .38;
    source.reference_energy.battery_capacity_kwh = 15;
    source.has_yesterday = source.comparison_ready = source.yesterday_comparison_partial = true;
    source.comparison_paired_slots = 123;
    source.totals.production_kwh = 1234.56789;
    source.completed_interval_totals.grid_import_kwh = 9.876;
    source.comparison_today_totals.battery_charge_kwh = 5.12;
    source.comparison_yesterday_totals.grid_export_kwh = 7.31;
    source.yesterday_completed_interval_totals.consumption_kwh = 12.91;
    for (unsigned i = 0; i < 288; i++)
    {
        source.slots[i].valid = i % 7 != 0;
        source.slots[i].chart_gap = i % 11 == 0;
        source.slots[i].split = (app_energy_split_t){.pv_home = i * .01,
                                                     .pv_export = i * .02,
                                                     .pv_stored = i * .03,
                                                     .grid_home = i * .04,
                                                     .battery_home = i * .05};
    }
    encode();
    assert(decode());
    assert(result.totals.production_kwh == source.totals.production_kwh);
    assert(result.completed_interval_totals.grid_import_kwh == 9.876);
    assert(result.comparison_today_totals.battery_charge_kwh == 5.12);
    assert(result.comparison_yesterday_totals.grid_export_kwh == 7.31);
    assert(result.yesterday_completed_interval_totals.consumption_kwh == 12.91);
    assert(result.comparison_paired_slots == 123 && result.comparison_ready &&
           result.has_yesterday && result.yesterday_comparison_partial);
    assert(!strcmp(result.reference_currency, "Ft") &&
           result.reference_energy.electricity_purchase_price == 70.1);
    for (unsigned i = 0; i < 288; i++)
    {
        assert(result.slots[i].valid == source.slots[i].valid);
        assert(result.slots[i].chart_gap == source.slots[i].chart_gap);
        assert(fabs(result.slots[i].split.battery_home - source.slots[i].split.battery_home) <
               .00001);
        assert(fabs(result.slots[i].pv_kw -
                    (source.slots[i].split.pv_home + source.slots[i].split.pv_export +
                     source.slots[i].split.pv_stored)) < .00001);
    }
    uint8_t page[APP_BLE_STATS_PAGE_MAX], command[8];
    for (unsigned p = 0; p < APP_BLE_STATS_PAGES; p++)
    {
        bool start;
        uint16_t index;
        uint32_t id;
        app_ble_stats_request(command, p == 0, p, 42);
        assert(app_ble_stats_request_decode(command, 8, &start, &index, &id) && index == p &&
               id == 42 && start == (p == 0));
        size_t length = app_ble_stats_page(wire, 42, p, page);
        assert(length <= 492 && length > 12);
        assert(!app_ble_stats_page_decode(page, length - 1, 42, p, assembled));
        assert(!app_ble_stats_page_decode(page, length, 43, p, assembled));
        assert(
            !app_ble_stats_page_decode(page, length, 42, (p + 1) % APP_BLE_STATS_PAGES, assembled));
        assert(app_ble_stats_page_decode(page, length, 42, p, assembled));
    }
    assert(!memcmp(wire, assembled, sizeof(wire)));
    assert(!app_ble_stats_page(wire, 42, APP_BLE_STATS_PAGES, page));
    bool start;
    uint16_t index;
    uint32_t id;
    app_ble_stats_request(command, true, 1, 42);
    assert(!app_ble_stats_request_decode(command, 8, &start, &index, &id));
    assert(!app_ble_stats_request_decode(command, 7, &start, &index, &id));
    app_ble_stats_request(command, false, 0, 0);
    assert(!app_ble_stats_request_decode(command, 8, &start, &index, &id));
    assert(!app_ble_stats_decode(wire, sizeof(wire) - 1, 42, 1792324800, source.date, &result));
    assert(!app_ble_stats_decode(wire, sizeof(wire), 43, 1792324800, source.date, &result));
    assert(!app_ble_stats_decode(wire, sizeof(wire), 42, 1792324921, source.date, &result));
    assert(!app_ble_stats_decode(wire, sizeof(wire), 42, 1792324794, source.date, &result));
    assert(!app_ble_stats_decode(wire, sizeof(wire), 42, 1792324800, "2026-10-19", &result));
    for (unsigned i = 0; i < sizeof(wire); i++)
    {
        wire[i] ^= 0x80;
        assert(!decode());
        wire[i] ^= 0x80;
    }
    wire[25] = 1;
    checksum();
    assert(!decode());
    encode();
    wire[22] = 8;
    checksum();
    assert(!decode());
    encode();
    /* Új CRC-vel is visszautasítjuk a NaN/Inf értéket és a hibás slot-flageket. */
    memset(wire + 32, 255, 8);
    checksum();
    assert(!decode());
    encode();
    wire[612] = 4;
    checksum();
    assert(!decode());
    encode();
    memset(wire + 613, 255, 4);
    checksum();
    assert(!decode());
    encode();
    source.slots[1].split.pv_home = NAN;
    assert(!app_ble_stats_encode(&source, 42, 1792324800, wire));
    source.slots[1].split.pv_home = 31;
    assert(!app_ble_stats_encode(&source, 42, 1792324800, wire));
    source.slots[1].split.pv_home = 0;
    strcpy(source.date, "2026-02-29");
    assert(!app_ble_stats_encode(&source, 42, 1792324800, wire));
    strcpy(source.date, "2028-02-29");
    assert(app_ble_stats_encode(&source, 42, 1792324800, wire));
    puts("PASS: 288-slot BLE statistics roundtrip, 14 pages, CRC every byte, dates, age, NaN, "
         "bounds and reference prices");
}
