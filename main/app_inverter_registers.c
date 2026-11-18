/* A vezetékes regiszterek nullától számozott PDU-címei. A profil összetartozó szavait
 * a dekóder egy műveletben olvassa. Új sort a táblák végére kell felvenni. */
#include "app_inverter_registers.h"
#include <string.h>

#define ROW(key, hu, en, base, fc, addr, words, type, fmt) \
    {key, hu, en, base, fmt, addr, fc, words, INV_REG_##type}
#define H(key, hu, en, addr, type, fmt) ROW(key, hu, en, NULL, 3, addr, WIDTH_##type, type, fmt)
#define I(key, hu, en, addr, type, fmt) ROW(key, hu, en, NULL, 4, addr, WIDTH_##type, type, fmt)
#define SE(key, hu, en, base, addr, type, fmt) ROW(key, hu, en, base, 3, addr, WIDTH_##type, type, fmt)
#define WIDTH_U16 1
#define WIDTH_S16 1
#define WIDTH_U32_BE 2
#define WIDTH_U32_LE 2
#define WIDTH_S32_BE 2
#define WIDTH_S32_LE 2
#define WIDTH_F32_BE 2
#define WIDTH_F32_LE 2

static const app_inverter_register_t huawei[] = {
    H("pv", "PV-teljesítmény", "PV power", 32064, S32_BE, "S32 BE · W"),
    H("ac", "AC-kimenet", "AC output", 32080, S32_BE, "S32 BE · W"),
    H("state", "Inverterállapot", "Inverter state", 32089, U16, "U16"),
    H("meter_state", "Mérőállapot", "Meter state", 37100, U16, "U16"),
    H("export", "Hálózati betáplálás", "Grid export", 37113, S32_BE, "S32 BE · W"),
    H("soc", "Rendszer-SOC", "System SOC", 37760, U16, "U16 · 0.1 %"),
    H("bat_state", "Tárolórendszer állapota", "Storage system state", 37762, U16, "U16"),
    H("charge", "Rendszer töltési teljesítménye", "System charging power", 37765, S32_BE, "S32 BE · W"),
    H("bat1_state", "Első tároló állapota (régi kiosztás)", "First battery state (legacy)", 37000, U16, "U16"),
    H("bat1_charge", "Első tároló töltése (régi kiosztás)", "First battery charging (legacy)", 37001, S32_BE, "S32 BE · W"),
    H("bat1_soc", "Első tároló SOC (régi kiosztás)", "First battery SOC (legacy)", 37004, U16, "U16 · 0.1 %"),
    H("bat_temp", "Első tároló hőmérséklete", "First battery temperature", 37022, S16, "S16 · 0.1 °C"),
};

static const app_inverter_register_t goodwe[] = {
    H("pv1", "PV1 teljesítmény", "PV1 power", 35105, U32_BE, "U32 BE · W"),
    H("pv2", "PV2 teljesítmény", "PV2 power", 35109, U32_BE, "U32 BE · W"),
    H("pv3", "PV3 teljesítmény", "PV3 power", 35113, U32_BE, "U32 BE · W"),
    H("pv4", "PV4 teljesítmény", "PV4 power", 35117, U32_BE, "U32 BE · W"),
    H("meter_state", "Mérőkapcsolat", "Meter connection", 36004, U16, "U16"),
    H("ac", "AC-kimenet", "AC output", 35138, S16, "S16 · W"),
    H("export", "Hálózati betáplálás", "Grid export", 35140, S16, "S16 · W"),
    H("discharge", "Akkukisütés", "Battery discharge", 35182, S32_BE, "S32 BE · W"),
    H("bat_state", "Akkumulátor üzemállapota", "Battery mode", 35184, U16, "U16"),
    H("bat_temp", "Akkumulátor-hőmérséklet", "Battery temperature", 37003, S16, "S16 · 0.1 °C"),
    H("soc", "Töltöttség", "State of charge", 37007, U16, "U16 · %"),
};

static const app_inverter_register_t sungrow[] = {
    I("pv", "Összes PV-teljesítmény", "Total PV power", 5016, U32_LE, "U32 LE · W"),
    I("state", "Inverterállapot", "Inverter state", 12999, U16, "U16"),
    I("flow", "Energiaáramlás iránybitjei", "Power flow direction bits", 13000, U16, "U16"),
    I("house", "Házfogyasztás", "House consumption", 13007, S32_LE, "S32 LE · W"),
    I("export", "Hálózati betáplálás", "Grid export", 13009, S32_LE, "S32 LE · W"),
    I("alarms", "Mérő- és inverterriasztások", "Meter and inverter alarms", 13049, U32_LE, "U32 LE"),
    I("bat_voltage", "Akkufeszültség", "Battery voltage", 13019, U16, "U16 · 0.1 V"),
    I("bat_power", "Akkuteljesítmény nagysága", "Battery power magnitude", 13021, U16, "U16 · W"),
    I("soc", "Töltöttség", "State of charge", 13022, U16, "U16 · 0.1 %"),
    I("bat_temp", "Akkumulátor-hőmérséklet", "Battery temperature", 13024, S16, "S16 · 0.1 °C"),
};

static const app_inverter_register_t solax[] = {
    I("state", "Inverterállapot", "Inverter state", 9, U16, "U16"),
    I("pv1", "PV1 teljesítmény", "PV1 power", 10, U16, "U16 · W"),
    I("pv2", "PV2 teljesítmény", "PV2 power", 11, U16, "U16 · W"),
    H("pro_protocol", "PRO protokollverzió", "PRO protocol version", 0x82, U16, "U16"),
    I("pro_mppt_count", "PRO MPPT-k száma", "PRO MPPT count", 0x1b, U16, "U16"),
    I("pro_pv3_old", "PRO PV3 (verzió < 100)", "PRO PV3 (version < 100)", 0x2c, U16, "U16 · W"),
    I("pro_pv3_new", "PRO PV3 (verzió ≥ 100)", "PRO PV3 (version ≥ 100)", 0x124, U16, "U16 · W"),
    I("meter_state", "Mérőkapcsolat", "Meter connection", 0xb8, U16, "U16"),
    H("meter_ct", "Mérő vagy áramváltó", "Meter or CT selection", 0x115, U16, "U16"),
    I("export", "Hálózati betáplálás", "Grid export", 0x46, S32_LE, "S32 LE · W"),
    I("x1_ac", "X1 AC-kimenet", "X1 AC output", 2, S16, "S16 · W"),
    I("x3_ac_l1", "X3 L1 AC-kimenet", "X3 L1 AC output", 0x6c, S16, "S16 · W"),
    I("x3_ac_l2", "X3 L2 AC-kimenet", "X3 L2 AC output", 0x70, S16, "S16 · W"),
    I("x3_ac_l3", "X3 L3 AC-kimenet", "X3 L3 AC output", 0x74, S16, "S16 · W"),
    I("bat1_charge", "Első tároló töltése", "First battery charging", 0x16, S16, "S16 · W"),
    I("bat_state", "BMS-kapcsolat", "BMS connection", 0x17, U16, "U16"),
    I("bat_temp", "Első tároló hőmérséklete", "First battery temperature", 0x18, S16, "S16 · °C"),
    I("bat1_soc", "Első tároló töltöttsége", "First battery SOC", 0x1c, U16, "U16 · %"),
    I("pro_charge", "PRO összes töltés (verzió ≥ 100)", "PRO total charging (version ≥ 100)", 0x38, S32_LE, "S32 LE · W"),
    I("pro_soc", "PRO rendszer-SOC (verzió ≥ 100)", "PRO system SOC (version ≥ 100)", 0x3c, U16, "U16 · %"),
    I("pro_bat2_voltage", "PRO második tároló feszültsége (verzió < 100)", "PRO second battery voltage (version < 100)", 0x127, S16, "S16 · 0.1 V"),
    I("pro_bat2_charge", "PRO második tároló töltése (verzió < 100)", "PRO second battery charging (version < 100)", 0x129, S16, "S16 · W"),
    I("pro_soc_old", "PRO rendszer-SOC (verzió < 100)", "PRO system SOC (version < 100)", 0x12e, U16, "U16 · %"),
};

#define GX_PV(key, hu, en, addr) H(key, hu, en, addr, U16, "U16 · W")
static const app_inverter_register_t victron[] = {
    GX_PV("pv_out_l1", "AC-kimeneti PV L1", "AC output PV L1", 808),
    GX_PV("pv_out_l2", "AC-kimeneti PV L2", "AC output PV L2", 809),
    GX_PV("pv_out_l3", "AC-kimeneti PV L3", "AC output PV L3", 810),
    GX_PV("pv_grid_l1", "Hálózatra kötött PV L1", "AC grid PV L1", 811),
    GX_PV("pv_grid_l2", "Hálózatra kötött PV L2", "AC grid PV L2", 812),
    GX_PV("pv_grid_l3", "Hálózatra kötött PV L3", "AC grid PV L3", 813),
    GX_PV("pv_gen_l1", "Generátoroldali PV L1", "AC generator PV L1", 814),
    GX_PV("pv_gen_l2", "Generátoroldali PV L2", "AC generator PV L2", 815),
    GX_PV("pv_gen_l3", "Generátoroldali PV L3", "AC generator PV L3", 816),
    GX_PV("house_l1", "Házfogyasztás L1", "House consumption L1", 817),
    GX_PV("house_l2", "Házfogyasztás L2", "House consumption L2", 818),
    GX_PV("house_l3", "Házfogyasztás L3", "House consumption L3", 819),
    H("import_l1", "Hálózati vételezés L1", "Grid import L1", 820, S16, "S16 · W"),
    H("import_l2", "Hálózati vételezés L2", "Grid import L2", 821, S16, "S16 · W"),
    H("import_l3", "Hálózati vételezés L3", "Grid import L3", 822, S16, "S16 · W"),
    GX_PV("pv_dc", "DC-csatolt PV", "DC-coupled PV", 850),
    H("bat_voltage", "Akkufeszültség", "Battery voltage", 840, U16, "U16 · 0.1 V"),
    H("charge", "Akkutöltés", "Battery charging", 842, S16, "S16 · W"),
    H("soc", "Töltöttség", "State of charge", 843, U16, "U16 · %"),
};

static const app_inverter_register_t deye[] = {
    H("pv1", "PV1 teljesítmény", "PV1 power", 672, U16, "U16 · W"),
    H("pv2", "PV2 teljesítmény", "PV2 power", 673, U16, "U16 · W"),
    H("import", "Külső CT összes vételezése", "External CT total import", 619, S16, "S16 · W"),
    H("house", "Teljes fogyasztás", "Total consumption", 653, S16, "S16 · W"),
    H("state", "Inverterállapot", "Inverter state", 500, U16, "U16"),
    H("bat_temp", "Akkumulátor-hőmérséklet", "Battery temperature", 586, U16, "(U16 − 1000) × 0.1 °C"),
    H("bat_voltage", "Akkufeszültség", "Battery voltage", 587, U16, "U16 · 0.01 V"),
    H("soc", "Töltöttség", "State of charge", 588, U16, "U16 · %"),
    H("discharge", "Akkukisütés", "Battery discharge", 590, S16, "S16 · W"),
};

static const app_inverter_register_t solplanet[] = {
    I("pv1_voltage", "PV1 feszültség", "PV1 voltage", 1318, U16, "U16 · 0.1 V"),
    I("pv1_current", "PV1 áram", "PV1 current", 1319, U16, "U16 · 0.01 A"),
    I("pv2_voltage", "PV2 feszültség", "PV2 voltage", 1320, U16, "U16 · 0.1 V"),
    I("pv2_current", "PV2 áram", "PV2 current", 1321, U16, "U16 · 0.01 A"),
    I("pv", "Összes PV (tartalék adatforrás)", "Total PV (fallback)", 1600, U32_BE, "U32 BE · W"),
    I("state", "Inverterállapot", "Inverter state", 1308, U16, "U16"),
    H("meter_state", "Mérőkapcsolat", "Meter connection", 1107, U16, "U16"),
    H("import", "Hálózati vételezés", "Grid import", 6433, S32_BE, "S32 BE · W"),
    I("ac", "AC-kimenet", "AC output", 1370, S32_BE, "S32 BE · W"),
};

static const app_inverter_register_t solaredge[] = {
    ROW("sunspec_base", "SunS azonosító és modelllista kezdete", "SunS identifier and model list base", NULL, 3, 40000, 2, BASE, "SunS"),
    ROW("bank1", "Első StorEdge tároló báziscíme", "First StorEdge bank base", NULL, 3, 0xe100, 1, BASE, "StorEdge"),
    ROW("bank2", "Második StorEdge tároló báziscíme", "Second StorEdge bank base", NULL, 3, 0xe200, 1, BASE, "StorEdge"),
    ROW("bank3", "Harmadik StorEdge tároló báziscíme", "Third StorEdge bank base", NULL, 3, 0xe400, 1, BASE, "StorEdge"),
    SE("model_id", "Modellazonosító", "Model identifier", "SunSpec header", 0, U16, "U16"),
    SE("model_length", "Modellhossz", "Model length", "SunSpec header", 1, U16, "U16"),
    ROW("meter_role", "Mérőszerep", "Meter role", "SunSpec 1", 3, 32, 8, TEXT, "ASCII"),
    SE("ac", "Inverter AC-kimenet", "Inverter AC output", "SunSpec 101–103", 12, S16, "S16 · W × 10^SF"),
    SE("ac_sf", "AC skálakitevő", "AC scale factor", "SunSpec 101–103", 13, S16, "S16"),
    SE("dc", "Inverter DC-teljesítmény", "Inverter DC power", "SunSpec 101–103", 29, S16, "S16 · W × 10^SF"),
    SE("dc_sf", "DC skálakitevő", "DC scale factor", "SunSpec 101–103", 30, S16, "S16"),
    SE("state", "Inverterállapot", "Inverter state", "SunSpec 101–103", 36, U16, "U16"),
    SE("ac_float", "Inverter AC-kimenet", "Inverter AC output", "SunSpec 111–113", 20, F32_BE, "F32 BE · W"),
    SE("dc_float", "Inverter DC-teljesítmény", "Inverter DC power", "SunSpec 111–113", 36, F32_BE, "F32 BE · W"),
    SE("state_float", "Inverterállapot", "Inverter state", "SunSpec 111–113", 46, U16, "U16"),
    SE("mppt_sf", "MPPT teljesítményskála", "MPPT power scale factor", "SunSpec 160", 2, S16, "S16"),
    SE("mppt_count", "MPPT-k száma", "MPPT count", "SunSpec 160", 6, U16, "U16"),
    SE("mppt_power", "Teljesítmény MPPT-nként", "Per-tracker power", "SunSpec 160 MPPT", 11, U16, "U16 · W × 10^SF"),
    SE("meter_power", "Mérőteljesítmény", "Meter power", "SunSpec 201–204", 16, S16, "S16 · W × 10^SF"),
    SE("meter_sf", "Mérőteljesítmény skálája", "Meter power scale factor", "SunSpec 201–204", 20, S16, "S16"),
    SE("meter_float", "Mérőteljesítmény", "Meter power", "SunSpec 211–214", 26, F32_BE, "F32 BE · W"),
    SE("bank_capacity", "Névleges tárolókapacitás", "Bank rated capacity", "StorEdge", 0x42, F32_LE, "F32 LE · Wh"),
    SE("bat_temp", "Tároló-hőmérséklet", "Bank temperature", "StorEdge", 0x6c, F32_LE, "F32 LE · °C"),
    SE("charge", "Tárolóteljesítmény", "Bank power", "StorEdge", 0x74, F32_LE, "F32 LE · W"),
    SE("soc", "Tároló töltöttsége", "Bank state of charge", "StorEdge", 0x84, F32_LE, "F32 LE · %"),
    SE("bat_state", "Tároló üzemállapota", "Bank state", "StorEdge", 0x86, U32_LE, "U32 LE"),
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define PROFILE(id, table) {id, table, COUNT(table)}
#define CHECK_SIZE(table) _Static_assert(COUNT(table) <= APP_INVERTER_REGISTER_MAX, "register table too long")
CHECK_SIZE(huawei); CHECK_SIZE(goodwe); CHECK_SIZE(sungrow); CHECK_SIZE(solax);
CHECK_SIZE(victron); CHECK_SIZE(deye); CHECK_SIZE(solplanet); CHECK_SIZE(solaredge);

const app_inverter_register_t *app_inverter_registers(const char *profile, size_t *count)
{
    static const struct {
        const char *profile;
        const app_inverter_register_t *fields;
        size_t count;
    } profiles[] = {
        PROFILE("huawei_modbus", huawei), PROFILE("goodwe_modbus", goodwe),
        PROFILE("sungrow_modbus", sungrow), PROFILE("solax_modbus", solax),
        PROFILE("victron_modbus", victron), PROFILE("solaredge_modbus", solaredge),
        PROFILE("deye_modbus", deye), PROFILE("deye_v5", deye), PROFILE("deye_rtu", deye),
        PROFILE("deye_lse3", deye), PROFILE("solplanet_modbus", solplanet),
    };
    for (size_t i = 0; profile && i < COUNT(profiles); ++i) {
        if (!strcmp(profile, profiles[i].profile)) {
            if (count) *count = profiles[i].count;
            return profiles[i].fields;
        }
    }
    if (count) *count = 0;
    return NULL;
}
