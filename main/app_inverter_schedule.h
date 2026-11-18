#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Helyi idő, a hétfő indexe nulla. Azonos végpontoknál a teljes nap aktív.
 * Az éjfélen átérő időablak a következő napon folytatódik. */
typedef struct {
    bool enabled;
    uint16_t start_minute;
    uint16_t end_minute;
} app_inverter_day_window_t;

typedef struct {
    app_inverter_day_window_t days[7];
    uint16_t touch_wake_seconds;
} app_inverter_schedule_t;

bool app_inverter_display_schedule(const char *slave_id, app_inverter_schedule_t *out);

static inline bool app_inverter_schedule_active(const app_inverter_schedule_t *schedule,
                                                unsigned monday_index, unsigned minute)
{
    if (!schedule || monday_index >= 7 || minute >= 1440)
        return false;
    const app_inverter_day_window_t *today = &schedule->days[monday_index];
    const app_inverter_day_window_t *yesterday = &schedule->days[(monday_index + 6) % 7];
    if (today->enabled && today->start_minute == today->end_minute)
        return true;
    if (today->enabled && minute >= today->start_minute &&
        (today->start_minute > today->end_minute || minute < today->end_minute))
        return true;
    return yesterday->enabled && yesterday->start_minute > yesterday->end_minute &&
           minute < yesterday->end_minute;
}
