#include "app_inverter_driver.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    static app_inverter_config_t cfg = {.host = "127.0.0.1"};
    snprintf(cfg.profile, sizeof(cfg.profile), "%s", argv[1]);
    cfg.port = atoi(argv[2]);
    cfg.unit = atoi(argv[3]);
    cfg.battery = atoi(argv[4]) != 0;
    snprintf(cfg.device, sizeof(cfg.device), "%s", argv[5]);
    app_energy_sample_t s = {0};
    int64_t epoch = -1;
    if (!app_inverter_modbus_read(&cfg, &s, &epoch)) {
        puts("null");
        return 0;
    }
    if (epoch != 0 || s.battery_unavailable == cfg.battery) return 3;
    printf("{\"pv\":%.6f,\"status\":%d", s.power_pv_kw, s.status);
    if (!s.home_unavailable) printf(",\"home\":%.6f", s.power_full_kw);
    if (!s.grid_unavailable) printf(",\"grid\":%.6f", s.power_grid_kw);
    if (!s.soc_unavailable) printf(",\"soc\":%.6f", s.soc_bat_pct);
    if (!s.battery_power_unavailable) printf(",\"battery\":%.6f", s.power_bat_kw);
    if (s.battery_temperature_valid) printf(",\"temp\":%.6f", s.battery_temperature_c);
    puts("}");
    return 0;
}
