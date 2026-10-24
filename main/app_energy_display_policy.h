#pragma once

#include "app_config.h"

/* A készenléti 10% felülírja a nappali és éjszakai alapértéket, de egy
 * kaputelefon-hívás azonnal visszakapja az aktuális napszak fényerejét. */
static inline unsigned app_energy_display_target(const app_config_t *config,
                                                 bool night, bool empty_home,
                                                 bool intercom_active)
{
    if (config->energy_flow_disabled && empty_home && !intercom_active)
        return 10;
    return night ? config->display.night_brightness : config->display.day_brightness;
}
