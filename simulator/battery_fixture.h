#pragma once
#include "app_battery_diagnostics.h"

/* Kizárólag a desktop/UI-tesztekhez; a valódi modellből készít demóeredményt. */
void app_battery_fixture(app_battery_report_t *out, double nominal, int64_t now);
