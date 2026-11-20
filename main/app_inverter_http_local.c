#include "app_inverter_http_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMBER cJSON_GetObjectItemCaseSensitive

static bool fronius(const app_inverter_config_t *cfg, app_energy_sample_t *sample)
{
    cJSON *root = inv_http_fetch(cfg, NULL, "/solar_api/v1/GetPowerFlowRealtimeData.fcgi",
                                 80, false, NULL, NULL, 0, NULL, 0);
    const cJSON *head = MEMBER(root, "Head"), *data = MEMBER(MEMBER(root, "Body"), "Data");
    const cJSON *site = MEMBER(data, "Site");
    double status = -1;
    bool ok = inv_http_scale(MEMBER(head, "Status"), "Code", 1, &status) && status == 0 &&
              inv_http_scale(site, "P_PV", 0.001, &sample->power_pv_kw);
    if (ok) {
        if (cJSON_IsTrue(MEMBER(site, "BackupMode"))) sample->status = 3;
        sample->grid_unavailable = !inv_http_scale(site, "P_Grid", 0.001, &sample->power_grid_kw);
        sample->home_unavailable = !inv_http_scale(site, "P_Load", -0.001, &sample->power_full_kw);
        if (cfg->battery) {
            sample->battery_power_unavailable = !inv_http_scale(site, "P_Akku", 0.001, &sample->power_bat_kw);
            const cJSON *inverters = MEMBER(data, "Inverters");
            /* Több inverterből nem választunk önkényesen akkumulátort. */
            const cJSON *inverter = cfg->device[0] ? MEMBER(inverters, cfg->device) :
                                    cJSON_GetArraySize(inverters) == 1 ? inverters->child : NULL;
            inv_http_soc(MEMBER(inverter, "SOC"), sample);
        }
    }
    cJSON_Delete(root);
    return ok;
}

static bool word(const cJSON *data, int index, uint16_t *result)
{
    double v;
    if (!inv_http_number(cJSON_GetArrayItem(data, index), &v) || v < 0 ||
        v > UINT16_MAX || floor(v) != v) return false;
    *result = (uint16_t)v;
    return true;
}

static int signed_word(uint16_t value)
{
    return value <= INT16_MAX ? (int)value : (int)value - 65536;
}

static bool solax_g4(const cJSON *root, bool battery, app_energy_sample_t *sample)
{
    /* A Pocket firmware a mezőneveket kétféle kis-/nagybetűzéssel adja vissza.
     * A pozíciós tömb csak a 14-es X3-Hybrid G4 modellnél értelmezhető így. */
    const cJSON *type = cJSON_GetObjectItem(root, "type");
    const cJSON *data = cJSON_GetObjectItem(root, "data");
    int count = cJSON_GetArraySize(data);
    if (!cJSON_IsNumber(type) || type->valuedouble != 14 || !cJSON_IsArray(data) ||
        count < 200 || count > 300) return false;
    uint16_t a, b, mode;
    if (!word(data, 14, &a) || !word(data, 15, &b) || a == UINT16_MAX || b == UINT16_MAX) return false;
    sample->power_pv_kw = (a + b) * 0.001;
    if (word(data, 19, &mode)) {
        if (mode == 3 || mode == 4) sample->status = 4;
        if (mode == 7) sample->status = 3;
    }
    if (word(data, 34, &a) && word(data, 35, &b)) {
        int64_t feed = (int64_t)signed_word(b) * 65536 + a;
        if (feed != INT32_MIN) {
            sample->grid_unavailable = false;
            sample->power_grid_kw = -0.001 * feed;
        }
    }
    uint16_t c;
    if (word(data, 6, &a) && word(data, 7, &b) && word(data, 8, &c))
        inv_http_home_from_ac(sample, (signed_word(a) + signed_word(b) + signed_word(c)) * 0.001);
    if (battery && word(data, 103, &a) && a <= 100) {
        inv_http_soc(cJSON_GetArrayItem(data, 103), sample);
        if (word(data, 41, &a) && a != 0x8000) {
            sample->power_bat_kw = signed_word(a) * -0.001;
            sample->battery_power_unavailable = false;
        }
        if (word(data, 105, &a) && signed_word(a) >= -50 && signed_word(a) <= 120) {
            sample->battery_temperature_c = signed_word(a);
            sample->battery_temperature_valid = true;
        }
    }
    return true;
}

static bool solax(const app_inverter_config_t *cfg, app_energy_sample_t *sample)
{
    char *escaped = inv_http_escape(cfg->secret);
    char *form = NULL;
    if (!escaped) return false;
    int count = asprintf(&form, "optType=ReadRealTimeData&pwd=%s", escaped);
    free(escaped);
    if (count < 0) return false;
    const inv_http_header_t headers[] = {{"Content-Type", "application/x-www-form-urlencoded"}};
    cJSON *json = inv_http_fetch(cfg, NULL, "/", 80, false, form, headers, 1, NULL, 0);
    free(form);
    bool ok = solax_g4(json, cfg->battery, sample);
    cJSON_Delete(json);
    return ok;
}

/* A mérés egyes stickeken az isn vagy sn kulccsal visszaadja az inverter
 * sorozatszámát. A régi, sorozatszám nélküli válasz is támogatott. */
bool inv_http_solplanet_matches(const cJSON *inverter, const char *serial)
{
    const cJSON *isn = MEMBER(inverter, "isn"), *sn = MEMBER(inverter, "sn");
    return (!isn || (cJSON_IsString(isn) && !strcmp(isn->valuestring, serial))) &&
           (!sn || (cJSON_IsString(sn) && !strcmp(sn->valuestring, serial)));
}

bool inv_http_solplanet_dc(const cJSON *inverter, double *kw)
{
    const cJSON *v = MEMBER(inverter, "vpv"), *i = MEMBER(inverter, "ipv");
    int count = cJSON_GetArraySize(v);
    if (!cJSON_IsArray(v) || !cJSON_IsArray(i) || count != cJSON_GetArraySize(i) ||
        count < 1 || count > 16) return false;
    double sum = 0;
    for (int j = 0; j < count; j++) {
        double voltage, current;
        if (!inv_http_number(cJSON_GetArrayItem(v, j), &voltage) ||
            !inv_http_number(cJSON_GetArrayItem(i, j), &current) ||
            voltage < 0 || current < 0 || voltage >= 65535 || current >= 65535) return false;
        sum += voltage * current * 0.000001; /* 0,1 V × 0,01 A → kW. */
    }
    *kw = sum;
    return true;
}

static cJSON *solplanet_device(const app_inverter_config_t *cfg, const char *serial, int device, bool v1)
{
    char *path = NULL;
    bool tls = !strcmp(cfg->profile, "solplanet_https");
    if (device == 3) return inv_http_fetch(cfg, NULL, v1 ? "/emeter.cgi" : "/getdevdata.cgi?device=3",
                                          tls ? 443 : 8484, tls, NULL, NULL, 0, NULL, 0);
    int n = v1 ? asprintf(&path, "/invdata.cgi?sn=%s", serial) :
                 asprintf(&path, "/getdevdata.cgi?device=%d&sn=%s", device, serial);
    if (n < 0) return NULL;
    cJSON *json = inv_http_fetch(cfg, NULL, path, tls ? 443 : 8484, tls, NULL, NULL, 0, NULL, 0);
    free(path);
    return json;
}

/* Egyes dongle-ok induláskor teljes, nullával kitöltött BMS-választ adnak. */
static bool solplanet_empty_battery(const cJSON *battery)
{
    static const char *const fields[] = {"cst", "bst", "vb", "tb", "soc", "soh", "cli", "clo"};
    for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
        double value;
        if (!inv_http_number(MEMBER(battery, fields[i]), &value) || value != 0) return false;
    }
    return true;
}

static bool solplanet(const app_inverter_config_t *cfg, app_energy_sample_t *sample)
{
    char *sn = inv_http_escape(cfg->device);
    if (!sn || !*sn) {
        free(sn);
        return false;
    }
    double ac = 0;
    bool v1 = false, ac_ok = false, pv = false;
    unsigned variants = !strcmp(cfg->profile, "solplanet_http") ? 2 : 1;
    for (unsigned i = 0; i < variants; ++i) {
        v1 = i != 0;
        cJSON *inverter = solplanet_device(cfg, sn, 2, v1);
        if (!inv_http_solplanet_matches(inverter, cfg->device)) {
            cJSON_Delete(inverter);
            break;
        }
        ac_ok = inv_http_scale(inverter, "pac", 0.001, &ac);
        pv = inv_http_solplanet_dc(inverter, &sample->power_pv_kw);
        double err;
        if ((ac_ok || pv) && inv_http_scale(inverter, "err", 1, &err) && err != 0) sample->status = 4;
        cJSON_Delete(inverter);
        if (ac_ok || pv) break;
    }
    if (!ac_ok && !pv) {
        free(sn);
        return false;
    }
    if (!pv && ac_ok && !cfg->battery) {
        /* Az ASW8000-T string inverter AC-termelése, ha nincsenek MPPT-mérései. */
        sample->power_pv_kw = fmax(0.0, ac);
        pv = true;
    }
    cJSON *meter = solplanet_device(cfg, sn, 3, v1);
    const cJSON *enabled = MEMBER(meter, "enb");
    if ((!cJSON_IsNumber(enabled) || enabled->valueint != 0) &&
        inv_http_scale(meter, "pac", 0.001, &sample->power_grid_kw)) {
        sample->grid_unavailable = false;
        if (ac_ok) inv_http_home_from_ac(sample, ac);
    }
    cJSON_Delete(meter);
    if (!v1 && cfg->battery) {
        cJSON *battery = solplanet_device(cfg, sn, 4, false);
        if (battery && !solplanet_empty_battery(battery)) {
            double battery_pv;
            if (inv_http_scale(battery, "ppv", 0.001, &battery_pv) && battery_pv >= 0) {
                sample->power_pv_kw = battery_pv;
                pv = true;
            }
            inv_http_soc(MEMBER(battery, "soc"), sample);
            sample->battery_power_unavailable = !inv_http_scale(battery, "pb", 0.001, &sample->power_bat_kw);
            sample->battery_temperature_valid = inv_http_scale(battery, "tb", 0.1, &sample->battery_temperature_c) &&
                sample->battery_temperature_c >= -50 && sample->battery_temperature_c <= 120;
        }
        cJSON_Delete(battery);
    }
    free(sn);
    return pv;
}

bool inv_http_local(const app_inverter_config_t *cfg, app_energy_sample_t *sample)
{
    if (!strcmp(cfg->profile, "fronius_http")) return fronius(cfg, sample);
    if (!strcmp(cfg->profile, "solax_http")) return solax(cfg, sample);
    if (!strcmp(cfg->profile, "solplanet_http") || !strcmp(cfg->profile, "solplanet_https"))
        return solplanet(cfg, sample);
    return false;
}
