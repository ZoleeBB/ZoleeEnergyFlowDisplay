/* A címtáblák forrásai és modellhatárai: docs/direct-inverter-registers.md. */
#include "app_inverter_driver.h"
#include "app_inverter_modbus_wire.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    inv_mb_t *bus;
    const app_inverter_config_t *cfg;
    app_energy_sample_t *sample;
} reader_t;

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
    if (!inv_mb_read(r->bus, 3, 32064, 26, w)) return NAN;
    double pv = huawei_i32(w);
    double ac = huawei_i32(w + 16);
    if (w[25] == 0x0203) r->sample->status = 3;
    if (w[25] == 0x0300) r->sample->status = 4;
    if (inv_mb_read(r->bus, 3, 37100, 15, w) && w[0] == 1) {
        grid(r, -huawei_i32(w + 13));
        house_from_ac(r, ac);
    }
    if (r->cfg->battery) {
        /* A rendszer összegzett adata az elsődleges; a régebbi firmware egy egységet közöl. */
        bool online = false;
        if (inv_mb_read(r->bus, 3, 37760, 7, w)) {
            online = w[2] == 1 || w[2] == 2 || w[2] == 4;
            if (online) battery(r, -huawei_i32(w + 5), u16(w[0]) / 10, NAN);
        } else if (inv_mb_read(r->bus, 3, 37000, 5, w)) {
            online = w[0] == 1 || w[0] == 2 || w[0] == 4;
            if (online) battery(r, -huawei_i32(w + 1), u16(w[4]) / 10, NAN);
        }
        if (online && inv_mb_read(r->bus, 3, 37022, 1, w))
            battery(r, NAN, NAN, s16(w[0]) / 10);
    }
    return pv;
}

static double goodwe(reader_t *r)
{
    uint16_t w[14];
    if (!inv_mb_read(r->bus, 3, 35105, 14, w)) return NAN;
    double pv = NAN;
    for (unsigned i = 0; i <= 12; i += 4) {
        double input = u32(w + i, false);
        if (isfinite(input)) pv = (isnan(pv) ? 0 : pv) + input;
    }
    if (inv_mb_read(r->bus, 3, 36004, 1, w) && w[0] == 1 &&
        inv_mb_read(r->bus, 3, 35138, 3, w)) {
        grid(r, -s16(w[2]));
        house_from_ac(r, s16(w[0]));
    }
    if (r->cfg->battery && inv_mb_read(r->bus, 3, 35182, 3, w) && w[2] && w[2] != 0xffff) {
        battery(r, s32(w, false), NAN, NAN);
        if (inv_mb_read(r->bus, 3, 37003, 5, w))
            battery(r, NAN, u16(w[4]), s16(w[0]) / 10);
    }
    return pv;
}

static double sungrow(reader_t *r)
{
    uint16_t w[12];
    if (!inv_mb_read(r->bus, 4, 5016, 2, w)) return NAN;
    double pv = u32(w, true);
    if (!inv_mb_read(r->bus, 4, 12999, 12, w)) return pv;
    if (w[0] == 0x1000 || w[0] == 0x0041) r->sample->status = 3;
    if (w[0] == 0x0100 || w[0] == 0x5500) r->sample->status = 4;
    uint16_t flow = w[1];
    house(r, s32(w + 8, true));
    grid(r, -s32(w + 10, true));
    if (r->cfg->battery && inv_mb_read(r->bus, 4, 13019, 6, w) && w[0] && w[0] != 0xffff) {
        double magnitude = u16(w[2]);
        unsigned direction = flow & 6;
        double power = magnitude == 0 ? 0 : NAN;
        if (flow != 0xffff && direction == 2) power = -magnitude;
        if (flow != 0xffff && direction == 4) power = magnitude;
        battery(r, power, u16(w[3]) / 10, s16(w[5]) / 10);
    }
    return pv;
}

static void solax_pro_battery(reader_t *r, unsigned version)
{
    uint16_t w[8];
    if (!inv_mb_read(r->bus, 4, 0x16, 7, w) || w[1] != 0) return;
    double temp = s16(w[2]);
    double soc = u16(w[6]);
    double power = -s16(w[0]);
    if (version >= 100) {
        /* A 100-as verziótól a közös töltöttség és teljesítmény külön blokkban van. */
        if (inv_mb_read(r->bus, 4, 0x38, 5, w))
            battery(r, -s32(w, true), u16(w[4]), temp);
        else
            battery(r, NAN, NAN, temp);
        return;
    }
    /* Második csatlakoztatott tároló esetén csak a közös SOC-t jelenítjük meg. */
    if (inv_mb_read(r->bus, 4, 0x127, 8, w) && w[0] && w[0] != 0xffff) {
        power -= s16(w[2]);
        soc = u16(w[7]);
    }
    battery(r, power, soc, temp);
}

static double solax(reader_t *r)
{
    uint16_t w[13];
    if (!inv_mb_read(r->bus, 4, 9, 3, w)) return NAN;
    if (w[0] == 3 || w[0] == 4) r->sample->status = 4;
    if (w[0] == 7) r->sample->status = 3;
    double pv = u16(w[1]) + u16(w[2]);
    bool pro = !strcasecmp(r->cfg->device, "X3-G4-PRO");
    unsigned version = 0;
    if (pro) {
        if (!inv_mb_read(r->bus, 3, 0x82, 1, w) || !w[0] || w[0] >= 1000) return NAN;
        version = w[0];
        if (!inv_mb_read(r->bus, 4, 0x1b, 1, w) || (w[0] != 2 && w[0] != 3)) return NAN;
        if (w[0] == 3) {
            uint16_t tracker = version >= 100 ? 0x124 : 0x2c;
            if (!inv_mb_read(r->bus, 4, tracker, 1, w)) return NAN;
            pv += u16(w[0]);
        }
    }
    if (inv_mb_read(r->bus, 4, 0x46, 2, w)) grid(r, -s32(w, true));
    if (!strcasecmp(r->cfg->device, "x1") || !strcasecmp(r->cfg->device, "X1-G4")) {
        if (inv_mb_read(r->bus, 4, 2, 1, w)) house_from_ac(r, s16(w[0]));
    } else if (inv_mb_read(r->bus, 4, 0x6c, 9, w)) {
        house_from_ac(r, s16(w[0]) + s16(w[4]) + s16(w[8]));
    }
    if (r->cfg->battery) {
        if (pro) solax_pro_battery(r, version);
        else if (inv_mb_read(r->bus, 4, 0x16, 7, w) && w[1] == 1)
            battery(r, -s16(w[0]), u16(w[6]), s16(w[2]));
    }
    return pv;
}

/* A GX kiépítésétől függően egy teljes csoport, illetve külön fázis is hiányozhat. */
static double gx_phases(reader_t *r, uint16_t addr, unsigned count, bool signed_power)
{
    uint16_t w[9];
    double total = NAN;
    bool group = inv_mb_read(r->bus, 3, addr, count, w);
    for (unsigned i = 0; i < count; ++i) {
        if (!group && !inv_mb_read(r->bus, 3, addr + i, 1, w + i)) continue;
        double value = signed_power ? (int16_t)w[i] : w[i];
        total = (isnan(total) ? 0 : total) + value;
    }
    return total;
}

static double victron(reader_t *r)
{
    uint16_t w[4];
    double ac = gx_phases(r, 808, 9, false);
    double dc = inv_mb_read(r->bus, 3, 850, 1, w) ? u16(w[0]) : NAN;
    grid(r, gx_phases(r, 820, 3, true));
    house(r, gx_phases(r, 817, 3, false));
    if (r->cfg->battery && inv_mb_read(r->bus, 3, 840, 4, w) && w[0] && w[0] != 0xffff)
        battery(r, -(double)(int16_t)w[2], u16(w[3]), NAN);
    if (isnan(ac)) return dc;
    return isnan(dc) ? ac : ac + dc;
}

static double deye(reader_t *r)
{
    uint16_t w[5];
    if (!inv_mb_read(r->bus, 3, 672, 2, w)) return NAN;
    double pv = u16(w[0]) + u16(w[1]);
    if (inv_mb_read(r->bus, 3, 619, 1, w)) grid(r, s16(w[0]));
    if (inv_mb_read(r->bus, 3, 653, 1, w)) house(r, s16(w[0]));
    if (inv_mb_read(r->bus, 3, 500, 1, w) && w[0] == 4) r->sample->status = 4;
    if (r->cfg->battery && inv_mb_read(r->bus, 3, 586, 5, w) && w[1] && w[1] != 0xffff)
        battery(r, s16(w[4]), u16(w[2]), (u16(w[0]) - 1000) / 10);
    return pv;
}

static double solplanet(reader_t *r)
{
    uint16_t w[4];
    double pv = NAN;
    /* Az ASW8000-T két MPPT-jén a régi firmware csak feszültséget és áramot közöl. */
    if (inv_mb_read(r->bus, 4, 1318, 4, w))
        pv = (u16(w[0]) * u16(w[1]) + u16(w[2]) * u16(w[3])) / 1000;
    if (!isfinite(pv) && inv_mb_read(r->bus, 4, 1600, 2, w)) pv = u32(w, false);
    if (inv_mb_read(r->bus, 4, 1308, 1, w) && w[0] == 2) r->sample->status = 4;
    if (inv_mb_read(r->bus, 3, 1107, 1, w) && w[0] == 10 &&
        inv_mb_read(r->bus, 3, 6433, 2, w)) {
        grid(r, s32(w, false));
        if (inv_mb_read(r->bus, 4, 1370, 2, w)) house_from_ac(r, s32(w, false));
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
    uint16_t w[28];
    /* A StorEdge első akkumulátora szófordított IEEE-754 formátumot használ. */
    if (!inv_mb_read(r->bus, 3, 0xe16c, 28, w)) return;
    uint32_t state = pair(w + 26, true);
    if (state < 1 || state > 10 || state == 5 || state == 8 || state == 9) return;
    double raw = float32_value(w + 8, true);
    double power = raw == 0 ? 0 : NAN;
    if (state == 3) power = -fabs(raw);
    if (state == 4) power = fabs(raw);
    battery(r, power, float32_value(w + 24, true), float32_value(w, true));
}

typedef enum { SE_OTHER, SE_GRID, SE_HOME, SE_PRODUCTION } se_meter_t;

static se_meter_t se_meter_role(inv_mb_t *bus, uint16_t base)
{
    uint16_t words[8];
    if (!inv_mb_read(bus, 3, base + 32, 8, words)) return SE_OTHER;
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
    if (id >= 201 && id <= 204 && len >= 21 && inv_mb_read(r->bus, 3, base + 16, 5, w))
        power = sunspec_scale(s16(w[0]), w[4]);
    if (id >= 211 && id <= 214 && len >= 28 && inv_mb_read(r->bus, 3, base + 26, 2, w))
        power = float32_value(w, false);
    if (role == SE_GRID) grid(r, -power);
    if (role == SE_HOME) house(r, power);
    return role == SE_PRODUCTION ? power : NAN;
}

static double se_mppt(reader_t *r, uint16_t base, uint16_t len)
{
    uint16_t w[7];
    if (len < 28 || !inv_mb_read(r->bus, 3, base, 7, w)) return NAN;
    unsigned count = w[6];
    uint16_t scale = w[2];
    if (!count || count > 8 || len < 8 + 20 * count) return NAN;
    double sum = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!inv_mb_read(r->bus, 3, base + 19 + 20 * i, 1, w)) return NAN;
        sum += sunspec_scale(u16(w[0]), scale);
    }
    return sum;
}

static double solaredge(reader_t *r)
{
    uint16_t w[37];
    if (!inv_mb_read(r->bus, 3, 40000, 2, w) || w[0] != 0x5375 || w[1] != 0x6e53) return NAN;
    double ac = NAN, dc = NAN, pv = NAN;
    bool inverter = false;
    uint32_t pos = 40002;
    se_meter_t role = SE_OTHER;
    /* A modellhossz vezeti a bejárást, így egy MPPT bővítés eltolhatja a mérőt. */
    for (unsigned i = 0; i < 24 && pos <= 65533; ++i) {
        if (!inv_mb_read(r->bus, 3, pos, 2, w)) break;
        uint16_t id = w[0], len = w[1];
        if (!len || id == 0xffff || pos + 2U + len > 65535) break;
        uint16_t base = pos + 2;
        if (id == 1) {
            role = inverter && len >= 40 ? se_meter_role(r->bus, base) : SE_OTHER;
        } else if (id >= 101 && id <= 103 && len >= 37) {
            if (!inv_mb_read(r->bus, 3, base + 12, 25, w)) break;
            ac = sunspec_scale(s16(w[0]), w[1]);
            dc = sunspec_scale(s16(w[17]), w[18]);
            if (w[24] == 7) r->sample->status = 4;
            inverter = true;
        } else if (id >= 111 && id <= 113 && len >= 47) {
            if (!inv_mb_read(r->bus, 3, base + 20, 27, w)) break;
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

bool app_inverter_modbus_read(const app_inverter_config_t *cfg, app_energy_sample_t *sample,
                              int64_t *source_epoch)
{
    static const struct {
        const char *name;
        double (*decode)(reader_t *);
        inv_mb_frame_t frame;
        uint16_t port;
    } profiles[] = {
        {"huawei_modbus", huawei, INV_MB_TCP, 502},
        {"goodwe_modbus", goodwe, INV_MB_TCP, 502},
        {"sungrow_modbus", sungrow, INV_MB_TCP, 502},
        {"solax_modbus", solax, INV_MB_TCP, 502},
        {"victron_modbus", victron, INV_MB_TCP, 502},
        {"solaredge_modbus", solaredge, INV_MB_TCP, 1502},
        {"deye_v5", deye, INV_MB_V5, 8899},
        {"deye_modbus", deye, INV_MB_TCP, 502},
        {"deye_rtu", deye, INV_MB_RTU, 8899},
        {"solplanet_modbus", solplanet, INV_MB_TCP, 502},
    };
    if (!cfg || !sample || !source_epoch || !cfg->host[0]) return false;
    for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        if (strcmp(cfg->profile, profiles[i].name)) continue;
        inv_mb_t bus = {.fd = -1, .unit = cfg->unit, .frame = profiles[i].frame};
        if (bus.frame == INV_MB_V5 && !logger_serial(cfg->device, &bus.serial)) return false;
        if (!inv_mb_open(&bus, cfg->host, cfg->port ? cfg->port : profiles[i].port)) return false;
        app_energy_sample_t result = {
            .status = 1, .battery_unavailable = !cfg->battery, .home_unavailable = true,
            .grid_unavailable = true, .soc_unavailable = true, .battery_power_unavailable = true,
        };
        reader_t reader = {.bus = &bus, .cfg = cfg, .sample = &result};
        double pv = profiles[i].decode(&reader);
        inv_mb_close(&bus);
        if (!isfinite(pv)) return false;
        result.power_pv_kw = fmax(0, pv) / 1000;
        *sample = result;
        *source_epoch = 0;
        return true;
    }
    return false;
}
