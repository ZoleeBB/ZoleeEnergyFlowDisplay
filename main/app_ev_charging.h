#pragma once

#include "app_loxone_home_runtime.h"
#include <math.h>
#include <string.h>

typedef struct
{
    bool configured, valid;
    double power_kw;
} app_ev_charging_t;

/* A már átváltott fogyasztói mintákból számolunk, új kapcsolat és foglalás
 * nélkül. Hiányzó részadatnál nincs félrevezető részösszeg vagy 0 W. */
static inline app_ev_charging_t app_ev_charging_sum(const lx_home_config_t *config,
                                                   const lx_home_values_t *values,
                                                   bool connected)
{
    app_ev_charging_t result = {0};
    if (!config || !values || config->consumer_count > LX_HOME_MAX_CONSUMERS)
        return result;
    bool valid = connected;
    for (unsigned i = 0; i < config->consumer_count; ++i)
    {
        if (strcmp(config->consumers[i].icon, "ev-charger") &&
            strcmp(config->consumers[i].icon, "electric-car"))
            continue;
        result.configured = true;
        double value = values->consumers[i].value;
        if (!values->consumers[i].valid || !isfinite(value) || value < 0 || value > 1000)
            valid = false;
        else if (value >= 0.001)
            result.power_kw += value;
    }
    result.valid = result.configured && valid;
    if (!result.valid)
        result.power_kw = 0;
    return result;
}
