/* A címtáblák forrásai és modellhatárai: docs/inverters/modbus.md. */
#include "app_inverter_modbus_internal.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    inv_mb_t *bus;
    const app_inverter_config_t *cfg;
    app_energy_sample_t *sample;
    const app_inverter_register_t *fields;
    size_t field_count;
    uint32_t valid_words;
} reader_t;

static bool same_space(const char *left, const char *right)
{
    return left == right || (left && right && !strcmp(left, right));
}

static unsigned address_of(const reader_t *r, size_t index)
{
    return r->cfg->registers.mask & (UINT32_C(1) << index)
        ? r->cfg->registers.address[index] : r->fields[index].address;
}

static unsigned anchor(const reader_t *r, unsigned factory)
{
    for (size_t i = 0; i < r->field_count; ++i)
        if (r->fields[i].type == INV_REG_BASE && r->fields[i].address == factory)
            return address_of(r, i);
    return factory;
}

static uint32_t word_bits(unsigned offset, unsigned words)
{
    return (UINT32_MAX >> (32 - words)) << offset;
}

static void missing_value(const app_inverter_register_t *field, uint16_t *value)
{
    /* Az érvénytelen szavak a gyári dekóder saját „nincs mérés” ágába kerülnek. */
    for (unsigned i = 0; i < field->words; ++i) value[i] = UINT16_MAX;
    switch (field->type) {
    case INV_REG_S16:
        value[0] = 0x8000;
        break;
    case INV_REG_F32_BE:
    case INV_REG_S32_BE:
        value[0] = field->type == INV_REG_F32_BE ? 0x7fc0 : 0x8000;
        value[1] = 0;
        break;
    case INV_REG_F32_LE:
    case INV_REG_S32_LE:
        value[0] = 0;
        value[1] = field->type == INV_REG_F32_LE ? 0x7fc0 : 0x8000;
        break;
    default:
        break;
    }
}

/* A bázist és a modellhosszt 32 biten ellenőrizzük, hogy egy kézzel megadott
 * eltolás ne csordulhasson át egy másik modellbe vagy a nullás PDU-címre. */
static bool in_bounds(uint32_t base, unsigned limit, unsigned address, unsigned words)
{
    uint32_t end = (uint32_t)address + words;
    return end <= limit && base + end <= 65536U;
}

static bool mapped_block(reader_t *r, const char *space, uint32_t base, unsigned limit,
                         uint8_t fn, unsigned start, unsigned count, uint32_t used, uint16_t *out)
{
    r->valid_words = 0;
    if (!count || count > 32) return false;
    bool custom = false;
    uint8_t indices[32];
    unsigned members = 0;
    if (r->cfg->registers.mask) {
        for (size_t i = 0; i < r->field_count; ++i) {
            const app_inverter_register_t *f = r->fields + i;
            if (f->function != fn || f->address < start || f->address + f->words > start + count ||
                !same_space(space, f->base) || !(used & (UINT32_C(1) << (f->address - start)))) continue;
            indices[members++] = (uint8_t)i;
            if (address_of(r, i) != f->address) custom = true;
        }
    }
    if (!custom) {
        if (!in_bounds(base, limit, start, count) ||
            !inv_mb_read(r->bus, fn, (uint16_t)(base + start), count, out)) return false;
        r->valid_words = word_bits(0, count);
        return true;
    }
    /* Csak a módosított csoport bomlik elemi olvasásokra. A szomszédos mezők
     * ismét egy csomagba kerülnek, a 32 bites érték saját szópárja oszthatatlan. */
    for (unsigned i = 0; i < members; ++i) {
        const app_inverter_register_t *f = r->fields + indices[i];
        missing_value(f, out + (f->address - start));
    }
    for (unsigned i = 0; i < members;) {
        unsigned first = i++;
        const app_inverter_register_t *f = r->fields + indices[first];
        unsigned offset = f->address - start;
        unsigned mapped = address_of(r, indices[first]);
        unsigned n = f->words;
        if (!in_bounds(base, limit, mapped, n)) continue;
        while (i < members) {
            const app_inverter_register_t *next = r->fields + indices[i];
            if (next->address != f->address + n || address_of(r, indices[i]) != mapped + n ||
                !in_bounds(base, limit, mapped, n + next->words)) break;
            n += next->words;
            ++i;
        }
        if (inv_mb_read(r->bus, fn, (uint16_t)(base + mapped), n, out + offset)) {
            r->valid_words |= word_bits(offset, n);
        } else if (i - first > 1 && r->bus->fd >= 0) {
            /* Egy opcionális, nem létező regiszter miatt az egész kérés kivételt adhat. */
            for (unsigned j = first; j < i; ++j) {
                const app_inverter_register_t *one = r->fields + indices[j];
                offset = one->address - start;
                mapped = address_of(r, indices[j]);
                if (inv_mb_read(r->bus, fn, (uint16_t)(base + mapped), one->words, out + offset))
                    r->valid_words |= word_bits(offset, one->words);
            }
        }
    }
    return r->valid_words != 0;
}

static bool read_regs(reader_t *r, uint8_t fn, unsigned addr, unsigned count, uint16_t *w)
{
    return mapped_block(r, NULL, 0, 65536, fn, addr, count, UINT32_MAX, w);
}

static bool model_regs(reader_t *r, const char *model, uint32_t base, unsigned len,
                       unsigned offset, unsigned count, uint16_t *w)
{
    return mapped_block(r, model, base, len, 3, offset, count, UINT32_MAX, w);
}

static double u16(uint16_t word)
{
    return word == UINT16_MAX ? NAN : word;
}

static double s16(uint16_t word)
{
    return word == 0x8000 ? NAN : (int16_t)word;
}

static uint32_t pair(const uint16_t *p, bool low_first)
{
    return ((uint32_t)p[low_first ? 1 : 0] << 16) | p[low_first ? 0 : 1];
}

static double u32(const uint16_t *p, bool low_first)
{
    uint32_t raw = pair(p, low_first);
    return raw == UINT32_MAX ? NAN : raw;
}

static double s32(const uint16_t *p, bool low_first)
{
    uint32_t raw = pair(p, low_first);
    return raw == 0x80000000U ? NAN : (int32_t)raw;
}

static void watts(double value, double *kw, bool *unavailable)
{
    if (!isfinite(value)) return;
    *kw = value / 1000;
    *unavailable = false;
}

static void grid(reader_t *r, double value)
{
    watts(value, &r->sample->power_grid_kw, &r->sample->grid_unavailable);
}

static void house(reader_t *r, double value)
{
    if (isfinite(value))
        watts(fmax(0, value), &r->sample->power_full_kw, &r->sample->home_unavailable);
}

static void house_from_ac(reader_t *r, double ac)
{
    if (!r->sample->grid_unavailable) house(r, ac + 1000 * r->sample->power_grid_kw);
}

static void battery(reader_t *r, double power, double soc, double temperature)
{
    watts(power, &r->sample->power_bat_kw, &r->sample->battery_power_unavailable);
    if (soc >= 0 && soc <= 100) {
        r->sample->soc_bat_pct = soc;
        r->sample->soc_unavailable = false;
    }
    if (temperature >= -60 && temperature <= 150) {
        r->sample->battery_temperature_c = temperature;
        r->sample->battery_temperature_valid = true;
    }
}

/* A SUN2000 előjeles szabványértékeinek kimaradása a legnagyobb pozitív számmal jelölt. */
static double huawei_i32(const uint16_t *p)
{
    return pair(p, false) == 0x7fffffff ? NAN : s32(p, false);
}

static double huawei(reader_t *r)
{
    uint16_t w[26];
    if (!read_regs(r, 3, 32064, 26, w)) return NAN;
    double pv = huawei_i32(w);
    double ac = huawei_i32(w + 16);
    if (w[25] == 0x0203) r->sample->status = 3;
    if (w[25] == 0x0300) r->sample->status = 4;
    if (read_regs(r, 3, 37100, 15, w) && w[0] == 1) {
        grid(r, -huawei_i32(w + 13));
        house_from_ac(r, ac);
    }
    if (r->cfg->battery) {
        /* A rendszer összegzett adata az elsődleges; a régebbi firmware egy egységet közöl. */
        bool online = false;
        if (read_regs(r, 3, 37760, 7, w)) {
            online = w[2] == 1 || w[2] == 2 || w[2] == 4;
            if (online) battery(r, -huawei_i32(w + 5), u16(w[0]) / 10, NAN);
        } else if (read_regs(r, 3, 37000, 5, w)) {
            online = w[0] == 1 || w[0] == 2 || w[0] == 4;
            if (online) battery(r, -huawei_i32(w + 1), u16(w[4]) / 10, NAN);
        }
        if (online && read_regs(r, 3, 37022, 1, w))
            battery(r, NAN, NAN, s16(w[0]) / 10);
    }
    return pv;
}

static double goodwe(reader_t *r)
{
    uint16_t w[14];
    unsigned channels = 4;
    if (!read_regs(r, 3, 35105, 14, w)) {
        /* Az egyfázisú EH és egyes ET verziók csak két MPPT-t címeznek. */
        channels = 2;
        if (!read_regs(r, 3, 35105, 6, w)) return NAN;
    }
    double pv = NAN;
    for (unsigned i = 0; i < channels * 4; i += 4) {
        double input = u32(w + i, false);
        if (isfinite(input)) pv = (isnan(pv) ? 0 : pv) + input;
    }
    if (read_regs(r, 3, 36004, 1, w) && w[0] == 1 &&
        read_regs(r, 3, 35138, 3, w)) {
        grid(r, -s16(w[2]));
        house_from_ac(r, s16(w[0]));
    }
    if (r->cfg->battery && read_regs(r, 3, 35182, 3, w) && w[2] && w[2] != 0xffff) {
        battery(r, s32(w, false), NAN, NAN);
        if (read_regs(r, 3, 37003, 5, w))
            battery(r, NAN, u16(w[4]), s16(w[0]) / 10);
    }
    return pv;
}

static double sungrow(reader_t *r)
{
    uint16_t w[12];
    if (!read_regs(r, 4, 5016, 2, w)) return NAN;
    double pv = u32(w, true);
    if (!read_regs(r, 4, 12999, 12, w)) return pv;
    if (w[0] == 0x1000 || w[0] == 0x0041) r->sample->status = 3;
    if (w[0] == 0x0100 || w[0] == 0x5500) r->sample->status = 4;
    uint16_t flow = w[1];
    double load = s32(w + 8, true);
    double exported = s32(w + 10, true);
    bool invalid_meter = false;
    if (read_regs(r, 4, 13049, 2, w)) {
        uint32_t alarms = pair(w, true);
        invalid_meter = alarms != UINT32_MAX && (alarms & ((1U << 30) | (1U << 14)));
    }
    if (!invalid_meter) {
        house(r, load);
        grid(r, -exported);
    }
    if (r->cfg->battery && read_regs(r, 4, 13019, 6, w) && w[0] && w[0] != 0xffff) {
        double magnitude = u16(w[2]);
        unsigned direction = flow & 6;
        double power = magnitude == 0 ? 0 : NAN;
        if (flow != 0xffff && direction == 2) power = -magnitude;
        if (flow != 0xffff && direction == 4) power = magnitude;
        battery(r, power, u16(w[3]) / 10, s16(w[5]) / 10);
    }
    return pv;
}

static bool solax_battery_block(reader_t *r, uint16_t *w)
{
    /* Az első tároló négy értékét kérjük. A tartományba eső MPPT-számnak
     * külön olvasása csak a PRO modell PV-útvonalán indokolt. */
    const uint32_t used = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 6);
    return mapped_block(r, NULL, 0, 65536, 4, 0x16, 7, used, w);
}

static void solax_pro_battery(reader_t *r, unsigned version)
{
    uint16_t w[8];
    if (!solax_battery_block(r, w) || w[1] != 0) return;
    double temp = s16(w[2]);
    double soc = u16(w[6]);
    double power = -s16(w[0]);
    if (version >= 100) {
        /* A 100-as verziótól a közös töltöttség és teljesítmény külön blokkban van. */
        if (read_regs(r, 4, 0x38, 5, w))
            battery(r, -s32(w, true), u16(w[4]), temp);
        else
            battery(r, NAN, NAN, temp);
        return;
    }
    /* Második csatlakoztatott tároló esetén csak a közös SOC-t jelenítjük meg. */
    if (read_regs(r, 4, 0x127, 8, w) && s16(w[0]) > 0) {
        power -= s16(w[2]);
        soc = u16(w[7]);
    }
    battery(r, power, soc, temp);
}

static double solax(reader_t *r)
{
    uint16_t w[13];
    if (!read_regs(r, 4, 9, 3, w)) return NAN;
    if (w[0] == 3 || w[0] == 4) r->sample->status = 4;
    if (w[0] == 7) r->sample->status = 3;
    double pv = u16(w[1]) + u16(w[2]);
    bool pro = !strcasecmp(r->cfg->device, "X3-G4-PRO");
    unsigned version = 0;
    if (pro) {
        if (!read_regs(r, 3, 0x82, 1, w) || !w[0] || w[0] >= 1000) return NAN;
        version = w[0];
        if (!read_regs(r, 4, 0x1b, 1, w) || (w[0] != 2 && w[0] != 3)) return NAN;
        if (w[0] == 3) {
            uint16_t tracker = version >= 100 ? 0x124 : 0x2c;
            if (!read_regs(r, 4, tracker, 1, w)) return NAN;
            pv += u16(w[0]);
        }
    }
    /* A fizikai CT-hez nem tartozik soros mérő-kommunikáció. */
    bool metered = read_regs(r, 4, 0xb8, 1, w) && w[0] == 1;
    if (!metered) metered = read_regs(r, 3, 0x115, 1, w) && w[0] == 1;
    if (metered && read_regs(r, 4, 0x46, 2, w)) grid(r, -s32(w, true));
    if (!strcasecmp(r->cfg->device, "x1") || !strcasecmp(r->cfg->device, "X1-G4")) {
        if (read_regs(r, 4, 2, 1, w)) house_from_ac(r, s16(w[0]));
    } else if (read_regs(r, 4, 0x6c, 9, w)) {
        house_from_ac(r, s16(w[0]) + s16(w[4]) + s16(w[8]));
    }
    if (r->cfg->battery) {
        if (pro) solax_pro_battery(r, version);
        else if (solax_battery_block(r, w) && w[1] == 1)
            battery(r, -s16(w[0]), u16(w[6]), s16(w[2]));
    }
    return pv;
}

/* A GX kiépítésétől függően egy teljes csoport, illetve külön fázis is hiányozhat. */
static double gx_phases(reader_t *r, uint16_t addr, unsigned count, bool signed_power)
{
    uint16_t w[9];
    double total = NAN;
    bool group = read_regs(r, 3, addr, count, w);
    for (unsigned i = 0; i < count; ++i) {
        if (group && !(r->valid_words & (UINT32_C(1) << i))) continue;
        if (!group && !read_regs(r, 3, addr + i, 1, w + i)) continue;
        double value = signed_power ? (int16_t)w[i] : w[i];
        total = (isnan(total) ? 0 : total) + value;
    }
    return total;
}

static double victron(reader_t *r)
{
    uint16_t w[4];
    double ac = gx_phases(r, 808, 9, false);
    double dc = read_regs(r, 3, 850, 1, w) ? u16(w[0]) : NAN;
    grid(r, gx_phases(r, 820, 3, true));
    house(r, gx_phases(r, 817, 3, false));
    if (r->cfg->battery && read_regs(r, 3, 840, 4, w) && w[0] && w[0] != 0xffff)
        battery(r, r->valid_words & (1U << 2) ? -(double)(int16_t)w[2] : NAN, u16(w[3]), NAN);
    if (isnan(ac)) return dc;
    return isnan(dc) ? ac : ac + dc;
}

static double deye(reader_t *r)
{
    uint16_t w[5];
    if (!read_regs(r, 3, 672, 2, w)) return NAN;
    double pv = u16(w[0]) + u16(w[1]);
    if (read_regs(r, 3, 619, 1, w)) grid(r, s16(w[0]));
    if (read_regs(r, 3, 653, 1, w)) house(r, s16(w[0]));
    if (read_regs(r, 3, 500, 1, w) && w[0] == 4) r->sample->status = 4;
    if (r->cfg->battery && read_regs(r, 3, 586, 5, w) && w[1] && w[1] != 0xffff)
        battery(r, s16(w[4]), u16(w[2]), (u16(w[0]) - 1000) / 10);
    return pv;
}

static double sofar_word(reader_t *r, unsigned address, bool signed_word)
{
    uint16_t word;
    if (!read_regs(r, 3, address, 1, &word)) return NAN;
    return signed_word ? s16(word) : u16(word);
}

static void sofar_battery(reader_t *r)
{
    /* A készülék egy vagy két akkukört mér. A rendszer-SOC az elsődleges;
     * régebbi firmware-nél csak ismert számú, aktív körből képzünk összeget. */
    double charging = NAN, soc = NAN, temperature = NAN;
    uint16_t totals[2];
    if (read_regs(r, 3, 0x0667, 2, totals)) {
        charging = 100 * s16(totals[0]);
        soc = u16(totals[1]);
        if (soc > 100) soc = NAN;
    }

    unsigned active = 0, known = 0;
    double bank_charging = 0, bank_soc = 0;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned base = 0x0604 + 7 * i;
        uint16_t w[5];
        bool block = read_regs(r, 3, base, 5, w);
        double voltage = block ? u16(w[0]) : sofar_word(r, base, false);
        if (!isfinite(voltage)) continue;
        ++known;
        if (voltage == 0) continue;
        ++active;
        double temp = block ? s16(w[3]) : sofar_word(r, base + 3, true);
        if (temp >= -60 && temp <= 150) temperature = fmax(temperature, temp);
        if (!isfinite(charging))
            bank_charging += 10 * (block ? s16(w[2]) : sofar_word(r, base + 2, true));
        if (!isfinite(soc)) {
            double pct = block ? u16(w[4]) : sofar_word(r, base + 4, false);
            bank_soc += pct <= 100 ? pct : NAN;
        }
    }
    if (!active) return;
    if ((!isfinite(charging) || !isfinite(soc)) &&
        (known == 2 || sofar_word(r, 0x066a, false) == active)) {
        if (!isfinite(charging)) charging = bank_charging;
        if (!isfinite(soc)) soc = bank_soc / active;
    }
    battery(r, -charging, soc, temperature);
}

static double sofar(reader_t *r)
{
    uint16_t w[4];
    double pv = NAN;
    /* A két MPPT külön mérése tíz watt felbontású. A későbbi firmware
     * összegzett PV-regisztere száz watt felbontással használható. */
    if (read_regs(r, 3, 0x0586, 4, w))
        pv = 10 * (u16(w[0]) + u16(w[3]));
    if (!isfinite(pv)) pv = 100 * sofar_word(r, 0x05c4, false);
    if (!isfinite(pv)) return NAN;
    double state = sofar_word(r, 0x0404, false);
    if (state == 3) r->sample->status = 3;
    if (state == 4 || state == 5) r->sample->status = 4;

    /* ID105 esetén a mérőkapcsolat hibás, a terhelési mérleg sem hiteles. */
    if (read_regs(r, 3, 0x040b, 1, w) && !(w[0] & (1U << 8))) {
        grid(r, -10 * sofar_word(r, 0x0488, true));
        house(r, 10 * sofar_word(r, 0x04af, false));
    }
    if (r->cfg->battery) sofar_battery(r);
    return pv;
}

static double solplanet(reader_t *r)
{
    uint16_t w[4];
    double pv = NAN;
    /* Az ASW8000-T két MPPT-jén a régi firmware csak feszültséget és áramot közöl. */
    if (read_regs(r, 4, 1318, 4, w))
        pv = (u16(w[0]) * u16(w[1]) + u16(w[2]) * u16(w[3])) / 1000;
    if (!isfinite(pv) && read_regs(r, 4, 1600, 2, w)) pv = u32(w, false);
    if (read_regs(r, 4, 1308, 1, w) && w[0] == 2) r->sample->status = 4;
    if (read_regs(r, 3, 1107, 1, w) && w[0] == 10 &&
        read_regs(r, 3, 6433, 2, w)) {
        grid(r, s32(w, false));
        if (read_regs(r, 4, 1370, 2, w)) house_from_ac(r, s32(w, false));
    }
    return pv;
}

static double float32_value(const uint16_t *w, bool low_first)
{
    uint32_t raw = pair(w, low_first);
    float value;
    memcpy(&value, &raw, sizeof(value));
    return isfinite(value) ? value : NAN;
}

static double sunspec_scale(double value, uint16_t scale)
{
    int exponent = (int16_t)scale;
    if (exponent < -10 || exponent > 10) return NAN;
    return value * pow(10, exponent);
}

static void se_battery(reader_t *r)
{
    static const uint16_t bases[] = {0xe100, 0xe200, 0xe400};
    uint16_t w[28];
    double total_power = 0, total_energy = 0, weighted_soc = 0, highest_temperature = NAN;
    for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        /* A nem beépített tároló címét a firmware kivétellel vagy nulla kapacitással jelzi. */
        unsigned base = anchor(r, bases[i]);
        if (!model_regs(r, "StorEdge", base, 256, 0x42, 2, w)) continue;
        double capacity = float32_value(w, true);
        if (!(capacity > 0)) continue;
        total_energy += capacity;
        if (!model_regs(r, "StorEdge", base, 256, 0x6c, 28, w)) {
            total_power = weighted_soc = NAN;
            continue;
        }
        uint32_t state = pair(w + 26, true);
        if (state < 1 || state > 10 || state == 5 || state == 8 || state == 9) {
            total_power = weighted_soc = NAN;
            continue;
        }
        double raw = float32_value(w + 8, true);
        double power = raw == 0 ? 0 : NAN;
        if (state == 3) power = -fabs(raw);
        if (state == 4) power = fabs(raw);
        total_power += power;
        double soc = float32_value(w + 24, true);
        weighted_soc += (soc >= 0 && soc <= 100 ? soc : NAN) * capacity;
        double temp = float32_value(w, true);
        if (temp >= -60 && temp <= 150) highest_temperature = fmax(highest_temperature, temp);
    }
    if (total_energy > 0 && r->bus->fd >= 0)
        battery(r, total_power, weighted_soc / total_energy, highest_temperature);
}

typedef enum { SE_OTHER, SE_GRID, SE_HOME, SE_PRODUCTION } se_meter_t;

static se_meter_t se_meter_role(reader_t *r, uint16_t base, uint16_t len)
{
    uint16_t words[8];
    if (!model_regs(r, "SunSpec 1", base, len, 32, 8, words)) return SE_OTHER;
    char name[17] = {0};
    unsigned length = 0;
    for (unsigned i = 0; i < 16; ++i) {
        char c = (char)(words[i / 2] >> ((i & 1) ? 0 : 8));
        if (!c) break;
        if (c != ' ') name[length++] = c;
    }
    if (!strcasecmp(name, "Export+Import")) return SE_GRID;
    if (!strcasecmp(name, "Consumption")) return SE_HOME;
    if (!strcasecmp(name, "Production")) return SE_PRODUCTION;
    return SE_OTHER;
}

static double se_meter(reader_t *r, uint16_t id, uint16_t base, uint16_t len, se_meter_t role)
{
    uint16_t w[5];
    double power = NAN;
    if (role == SE_OTHER) return NAN;
    if (id >= 201 && id <= 204 && model_regs(r, "SunSpec 201–204", base, len, 16, 5, w))
        power = sunspec_scale(s16(w[0]), w[4]);
    if (id >= 211 && id <= 214 && model_regs(r, "SunSpec 211–214", base, len, 26, 2, w))
        power = float32_value(w, false);
    if (role == SE_GRID) grid(r, -power);
    if (role == SE_HOME) house(r, power);
    return role == SE_PRODUCTION ? power : NAN;
}

static double se_mppt(reader_t *r, uint16_t base, uint16_t len)
{
    uint16_t w[7];
    if (len < 28 || !model_regs(r, "SunSpec 160", base, len, 0, 7, w)) return NAN;
    unsigned count = w[6];
    uint16_t scale = w[2];
    if (!count || count > 8 || len < 8 + 20 * count) return NAN;
    double sum = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!model_regs(r, "SunSpec 160 MPPT", base + 8 + 20 * i, 20, 11, 1, w)) return NAN;
        sum += sunspec_scale(u16(w[0]), scale);
    }
    return sum;
}

static double solaredge(reader_t *r)
{
    uint16_t w[37];
    if (!read_regs(r, 3, 40000, 2, w) || w[0] != 0x5375 || w[1] != 0x6e53) return NAN;
    double ac = NAN, dc = NAN, pv = NAN;
    bool inverter = false;
    uint32_t pos = anchor(r, 40000) + 2;
    se_meter_t role = SE_OTHER;
    /* A modellhossz vezeti a bejárást, így egy MPPT bővítés eltolhatja a mérőt. */
    for (unsigned i = 0; i < 24 && pos <= 65533; ++i) {
        if (!model_regs(r, "SunSpec header", pos, 65536 - pos, 0, 2, w)) break;
        uint16_t id = w[0], len = w[1];
        if (!len || id == 0xffff || pos + 2U + len > 65535) break;
        uint16_t base = pos + 2;
        if (id == 1) {
            role = inverter ? se_meter_role(r, base, len) : SE_OTHER;
        } else if (id >= 101 && id <= 103) {
            if (!model_regs(r, "SunSpec 101–103", base, len, 12, 25, w)) break;
            ac = sunspec_scale(s16(w[0]), w[1]);
            dc = sunspec_scale(s16(w[17]), w[18]);
            if (w[24] == 7) r->sample->status = 4;
            inverter = true;
        } else if (id >= 111 && id <= 113) {
            if (!model_regs(r, "SunSpec 111–113", base, len, 20, 27, w)) break;
            ac = float32_value(w, false);
            dc = float32_value(w + 16, false);
            if (w[26] == 7) r->sample->status = 4;
            inverter = true;
        } else if (id == 160) {
            pv = se_mppt(r, base, len);
        } else if (id >= 201 && id <= 214) {
            double measured = se_meter(r, id, base, len, role);
            if (!isfinite(pv) && isfinite(measured)) pv = measured;
            role = SE_OTHER;
        }
        pos += 2U + len;
    }
    if (!inverter) return NAN;
    if (r->sample->home_unavailable) house_from_ac(r, ac);
    if (r->cfg->battery) {
        se_battery(r);
        if (!isfinite(pv) && !r->sample->battery_power_unavailable)
            pv = dc - 1000 * r->sample->power_bat_kw;
    } else if (!isfinite(pv)) {
        pv = dc;
    }
    return pv;
}

static bool logger_serial(const char *input, uint32_t *serial)
{
    if (!input[0] || strspn(input, "0123456789") != strlen(input)) return false;
    errno = 0;
    unsigned long n = strtoul(input, NULL, 10);
    if (errno || !n || n > UINT32_MAX) return false;
    *serial = n;
    return true;
}

typedef struct {
    const char *name;
    double (*decode)(reader_t *);
    inv_mb_frame_t frame;
    uint16_t port;
} modbus_profile_t;

static const modbus_profile_t *find_profile(const char *name)
{
    static const modbus_profile_t profiles[] = {
        {"huawei_modbus", huawei, INV_MB_TCP, 502},
        {"goodwe_modbus", goodwe, INV_MB_TCP, 502},
        {"sungrow_modbus", sungrow, INV_MB_TCP, 502},
        {"solax_modbus", solax, INV_MB_TCP, 502},
        {"victron_modbus", victron, INV_MB_TCP, 502},
        {"solaredge_modbus", solaredge, INV_MB_TCP, 1502},
        {"deye_v5", deye, INV_MB_V5, 8899},
        {"deye_modbus", deye, INV_MB_TCP, 502},
        {"deye_lse3", deye, INV_MB_LSE3, 8899},
        {"deye_rtu", deye, INV_MB_RTU, 8899},
        {"solplanet_modbus", solplanet, INV_MB_TCP, 502},
        {"sofar_modbus", sofar, INV_MB_TCP, 502},
        {"sofar_v5", sofar, INV_MB_V5, 8899},
    };
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i)
        if (!strcmp(name, profiles[i].name)) return &profiles[i];
    return NULL;
}

bool app_inverter_modbus_prepare(const app_inverter_config_t *cfg, inv_mb_t *bus, uint16_t *port)
{
    const modbus_profile_t *profile = find_profile(cfg->profile);
    if (!profile) return false;
    bus->frame = profile->frame;
    bus->unit = cfg->unit;
    *port = cfg->port ? cfg->port : profile->port;
    return bus->frame != INV_MB_V5 || logger_serial(cfg->device, &bus->serial);
}

bool app_inverter_modbus_sample(const app_inverter_config_t *cfg, inv_mb_t *bus, app_energy_sample_t *sample)
{
    const modbus_profile_t *profile = find_profile(cfg->profile);
    if (!profile) return false;
    app_energy_sample_t result = {
        .status = 1, .battery_unavailable = !cfg->battery, .home_unavailable = true,
        .grid_unavailable = true, .soc_unavailable = true, .battery_power_unavailable = true,
    };
    reader_t reader = {.bus = bus, .cfg = cfg, .sample = &result};
    reader.fields = app_inverter_registers(cfg->profile, &reader.field_count);
    double pv = profile->decode(&reader);
    if (!isfinite(pv)) return false;
    result.power_pv_kw = fmax(0, pv) / 1000;
    *sample = result;
    return true;
}

bool app_inverter_modbus_read(const app_inverter_config_t *cfg, app_energy_sample_t *sample,
                              int64_t *source_epoch)
{
    if (!cfg || !sample || !source_epoch || !cfg->host[0]) return false;
    inv_mb_t bus = {.fd = -1};
    uint16_t port;
    if (!app_inverter_modbus_prepare(cfg, &bus, &port) || !inv_mb_open(&bus, cfg->host, port)) return false;
    bool valid = app_inverter_modbus_sample(cfg, &bus, sample);
    inv_mb_close(&bus);
    if (!valid && bus.frame == INV_MB_LSE3) {
        bus.frame = INV_MB_LSE3_CRC_LENGTH;
        if (!inv_mb_open(&bus, cfg->host, port)) return false;
        valid = app_inverter_modbus_sample(cfg, &bus, sample);
        inv_mb_close(&bus);
    }
    if (valid) *source_epoch = 0;
    return valid;
}
