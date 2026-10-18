/* Explicit little-endian formátum: nincs natív struktúra/padding a vezetéken. */
#include "app_ble_stats_protocol.h"
#include <string.h>
#include <math.h>

static void put(uint8_t *p, uint64_t n, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++)
        p[i] = (uint8_t)(n >> (8 * i));
}
static uint64_t get(const uint8_t *p, unsigned bytes)
{
    uint64_t n = 0;
    for (unsigned i = 0; i < bytes; i++)
        n |= (uint64_t)p[i] << (8 * i);
    return n;
}
static bool number64(uint8_t *p, double value)
{
    if (!isfinite(value) || value < 0 || value > 1e9)
        return false;
    uint64_t bits;
    memcpy(&bits, &value, 8);
    put(p, bits, 8);
    return true;
}
static bool read64(const uint8_t *p, double *value)
{
    uint64_t bits = get(p, 8);
    memcpy(value, &bits, 8);
    return isfinite(*value) && *value >= 0 && *value <= 1e9;
}

uint32_t app_ble_stats_crc(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    return ~crc;
}

static bool valid_date(const char *s)
{
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-')
        return false;
    for (unsigned i = 0; i < 10; i++)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
            return false;
    unsigned year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + s[3] - '0';
    unsigned month = (s[5] - '0') * 10 + s[6] - '0', day = (s[8] - '0') * 10 + s[9] - '0';
    static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 2020 || year > 2100 || month < 1 || month > 12)
        return false;
    unsigned maximum =
        days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    return day > 0 && day <= maximum;
}

#define TOTAL_FIELDS(X)                                                                            \
    X(production_kwh)                                                                              \
    X(consumption_kwh) X(grid_import_kwh) X(grid_export_kwh) X(battery_charge_kwh)                 \
        X(battery_discharge_kwh) X(pv_home_kwh) X(pv_export_kwh) X(pv_stored_kwh) X(grid_home_kwh) \
            X(battery_home_kwh)
#define ENERGY_FIELDS(X)                                                                           \
    X(battery_capacity_kwh)                                                                        \
    X(electricity_purchase_price) X(feed_in_price) X(co2_factor_kg_per_kwh)                        \
        X(tree_absorption_kg_per_year)
#define SPLIT_FIELDS(X) X(pv_home) X(pv_export) X(pv_stored) X(grid_home) X(battery_home)

bool app_ble_stats_encode(const app_stats_snapshot_t *s,
                          uint32_t id,
                          uint32_t epoch,
                          uint8_t out[APP_BLE_STATS_SIZE])
{
    _Static_assert(sizeof(double) == 8 && sizeof(float) == 4, "IEEE-754 adattípusok szükségesek");
    if (!s || !out || !id || !valid_date(s->date) || !s->reference_valid ||
        !s->reference_currency[0] || !memchr(s->reference_currency, 0, APP_CURRENCY_MAX) ||
        s->comparison_paired_slots > APP_STATS_SLOT_COUNT || epoch < 1577836800U ||
        epoch > 4102444800U)
        return false;
    memset(out, 0, APP_BLE_STATS_SIZE);
    memcpy(out, "ZST1", 4);
    put(out + 4, id, 4);
    put(out + 8, epoch, 4);
    memcpy(out + 12, s->date, 10);
    out[22] = (s->has_yesterday ? 1 : 0) | (s->comparison_ready ? 2 : 0) |
              (s->yesterday_comparison_partial ? 4 : 0);
    put(out + 23, s->comparison_paired_slots, 2);
    size_t offset = 32;
#define SAVE_ENERGY(field)                                                                         \
    if (!number64(out + offset, s->reference_energy.field))                                        \
        return false;                                                                              \
    offset += 8;
    ENERGY_FIELDS(SAVE_ENERGY)
#undef SAVE_ENERGY
    memcpy(out + offset, s->reference_currency, APP_CURRENCY_MAX);
    offset += APP_CURRENCY_MAX;
    const app_energy_totals_t *totals[] = {&s->totals,
                                           &s->completed_interval_totals,
                                           &s->comparison_today_totals,
                                           &s->comparison_yesterday_totals,
                                           &s->yesterday_completed_interval_totals};
    /* Hatodik blokk fenntartva: v1-ben nulla, később sem tolódhatnak a slotok. */
    for (unsigned i = 0; i < 6; i++)
    {
        app_energy_totals_t zero = {0};
        const app_energy_totals_t *t = i < 5 ? totals[i] : &zero;
#define SAVE_TOTAL(field)                                                                          \
    if (!number64(out + offset, t->field))                                                         \
        return false;                                                                              \
    offset += 8;
        TOTAL_FIELDS(SAVE_TOTAL)
#undef SAVE_TOTAL
    }
    for (unsigned i = 0; i < APP_STATS_SLOT_COUNT; i++)
    {
        const app_stats_slot_t *slot = &s->slots[i];
        out[offset++] = (slot->valid ? 1 : 0) | (slot->chart_gap ? 2 : 0);
#define SAVE_SPLIT(field)                                                                          \
    {                                                                                              \
        float value = slot->split.field;                                                           \
        if (!isfinite(value) || value < 0 || value > 30)                                           \
            return false;                                                                          \
        uint32_t bits;                                                                             \
        memcpy(&bits, &value, 4);                                                                  \
        put(out + offset, bits, 4);                                                                \
        offset += 4;                                                                               \
    }
        SPLIT_FIELDS(SAVE_SPLIT)
#undef SAVE_SPLIT
    }
    if (offset != APP_BLE_STATS_SIZE - 4)
        return false;
    put(out + offset, app_ble_stats_crc(out, offset), 4);
    return true;
}

bool app_ble_stats_decode(const uint8_t *wire,
                          size_t length,
                          uint32_t id,
                          uint32_t now,
                          const char *date,
                          app_stats_snapshot_t *out)
{
    if (!wire || !out || !date || length != APP_BLE_STATS_SIZE || memcmp(wire, "ZST1", 4) || !id ||
        get(wire + 4, 4) != id ||
        get(wire + length - 4, 4) != app_ble_stats_crc(wire, length - 4) || !valid_date(date) ||
        memcmp(wire + 12, date, 10) || (wire[22] & ~7) || get(wire + 23, 2) > 288)
        return false;
    uint32_t captured = (uint32_t)get(wire + 8, 4);
    if (captured < 1577836800U || captured > 4102444800U || (int64_t)captured > (int64_t)now + 5 ||
        (int64_t)now - captured > 120)
        return false;
    for (unsigned i = 25; i < 32; i++)
        if (wire[i])
            return false;
    memset(out, 0, sizeof(*out));
    memcpy(out->date, wire + 12, 10);
    out->has_yesterday = wire[22] & 1;
    out->comparison_ready = wire[22] & 2;
    out->yesterday_comparison_partial = wire[22] & 4;
    out->comparison_paired_slots = (unsigned)get(wire + 23, 2);
    size_t offset = 32;
#define READ_ENERGY(field)                                                                         \
    if (!read64(wire + offset, &out->reference_energy.field))                                      \
        return false;                                                                              \
    offset += 8;
    ENERGY_FIELDS(READ_ENERGY)
#undef READ_ENERGY
    if (out->reference_energy.tree_absorption_kg_per_year <= 0 ||
        out->reference_energy.electricity_purchase_price > 100000 ||
        out->reference_energy.feed_in_price > 100000 ||
        out->reference_energy.co2_factor_kg_per_kwh > 100)
        return false;
    memcpy(out->reference_currency, wire + offset, APP_CURRENCY_MAX);
    offset += APP_CURRENCY_MAX;
    if (!out->reference_currency[0] || !memchr(out->reference_currency, 0, APP_CURRENCY_MAX))
        return false;
    out->reference_valid = true;
    app_energy_totals_t reserved;
    app_energy_totals_t *totals[] = {&out->totals,
                                     &out->completed_interval_totals,
                                     &out->comparison_today_totals,
                                     &out->comparison_yesterday_totals,
                                     &out->yesterday_completed_interval_totals,
                                     &reserved};
    for (unsigned i = 0; i < 6; i++)
    {
#define READ_TOTAL(field)                                                                          \
    if (!read64(wire + offset, &totals[i]->field))                                                 \
        return false;                                                                              \
    offset += 8;
        TOTAL_FIELDS(READ_TOTAL)
#undef READ_TOTAL
    }
    for (unsigned i = 0; i < APP_STATS_SLOT_COUNT; i++)
    {
        app_stats_slot_t *slot = &out->slots[i];
        unsigned flags = wire[offset++];
        if (flags & ~3)
            return false;
        slot->valid = flags & 1;
        slot->chart_gap = flags & 2;
        slot->sample_count = slot->valid ? 1 : 0;
#define READ_SPLIT(field)                                                                          \
    {                                                                                              \
        uint32_t bits = (uint32_t)get(wire + offset, 4);                                           \
        offset += 4;                                                                               \
        memcpy(&slot->split.field, &bits, 4);                                                      \
        if (!isfinite(slot->split.field) || slot->split.field < 0 || slot->split.field > 30)       \
            return false;                                                                          \
    }
        SPLIT_FIELDS(READ_SPLIT)
#undef READ_SPLIT
        slot->pv_kw = slot->split.pv_home + slot->split.pv_export + slot->split.pv_stored;
        slot->load_kw = slot->split.pv_home + slot->split.grid_home + slot->split.battery_home;
    }
    return offset == APP_BLE_STATS_SIZE - 4;
}

void app_ble_stats_request(uint8_t out[8], bool start, uint16_t page, uint32_t id)
{
    out[0] = 1;
    out[1] = start ? 1 : 2;
    put(out + 2, page, 2);
    put(out + 4, id, 4);
}
bool app_ble_stats_request_decode(
    const uint8_t *data, size_t length, bool *start, uint16_t *page, uint32_t *id)
{
    if (!data || !start || !page || !id || length != 8 || data[0] != 1 ||
        (data[1] != 1 && data[1] != 2))
        return false;
    *start = data[1] == 1;
    *page = (uint16_t)get(data + 2, 2);
    *id = (uint32_t)get(data + 4, 4);
    return *id && *page < APP_BLE_STATS_PAGES && (!*start || !*page);
}
size_t app_ble_stats_page(const uint8_t wire[APP_BLE_STATS_SIZE],
                          uint32_t id,
                          uint16_t page,
                          uint8_t out[APP_BLE_STATS_PAGE_MAX])
{
    if (!wire || !out || !id || page >= APP_BLE_STATS_PAGES)
        return 0;
    size_t offset = (size_t)page * APP_BLE_STATS_PAGE_DATA, bytes = APP_BLE_STATS_SIZE - offset;
    if (bytes > APP_BLE_STATS_PAGE_DATA)
        bytes = APP_BLE_STATS_PAGE_DATA;
    put(out, id, 4);
    put(out + 4, page, 2);
    put(out + 6, APP_BLE_STATS_PAGES, 2);
    put(out + 8, APP_BLE_STATS_SIZE, 2);
    put(out + 10, bytes, 2);
    memcpy(out + 12, wire + offset, bytes);
    return 12 + bytes;
}
bool app_ble_stats_page_decode(const uint8_t *data,
                               size_t length,
                               uint32_t id,
                               uint16_t page,
                               uint8_t wire[APP_BLE_STATS_SIZE])
{
    if (!data || !wire || !id || page >= APP_BLE_STATS_PAGES || length < 12 ||
        length > APP_BLE_STATS_PAGE_MAX || get(data, 4) != id || get(data + 4, 2) != page ||
        get(data + 6, 2) != APP_BLE_STATS_PAGES || get(data + 8, 2) != APP_BLE_STATS_SIZE)
        return false;
    size_t offset = (size_t)page * APP_BLE_STATS_PAGE_DATA, bytes = APP_BLE_STATS_SIZE - offset;
    if (bytes > APP_BLE_STATS_PAGE_DATA)
        bytes = APP_BLE_STATS_PAGE_DATA;
    if (get(data + 10, 2) != bytes || length != 12 + bytes)
        return false;
    memcpy(wire + offset, data + 12, bytes);
    return true;
}
