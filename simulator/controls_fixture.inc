#include "controls_fixture.h"
#include <math.h>

lx_controls_summary_t app_sim_controls_summaries[LX_HOME_MAX_TOTAL_CARDS];
lx_controls_window_t app_sim_controls_windows[LX_CONTROLS_MAX_WINDOWS];
double app_sim_controls_last_value;
unsigned app_sim_controls_sets;

void app_sim_controls_reset(void)
{
    app_sim_controls_sets = 0;
    app_sim_controls_last_value = 0;
    for (unsigned i = 0; i < LX_HOME_MAX_TOTAL_CARDS; ++i)
    {
        app_sim_controls_summaries[i] = (lx_controls_summary_t){
            .available = true, .controllable = true, .value_valid = true, .range_valid = true,
            .open_allowed = true, .close_allowed = true, .value = 45, .maximum = 100, .step = 1,
            .motion_valid = true, .animation = 3, .timer_valid = true, .timer_running = true, .timer_total_valid = true,
            .timer_seconds = 125, .timer_total = 180, .stairway = true, .list_valid = true,
            .window_count = 14, .counts = {2, 7, 1, 1, 2, 1}, .counts_known = 63
        };
        snprintf(app_sim_controls_summaries[i].unit, sizeof(app_sim_controls_summaries[i].unit), "%%");
    }
    for (unsigned i = 0; i < LX_CONTROLS_MAX_WINDOWS; ++i)
    {
        memset(&app_sim_controls_windows[i], 0, sizeof(app_sim_controls_windows[i]));
        snprintf(app_sim_controls_windows[i].name, sizeof(app_sim_controls_windows[i].name), "Ablak %u", i + 1);
        snprintf(app_sim_controls_windows[i].room, sizeof(app_sim_controls_windows[i].room), "%s", "Nappali");
        app_sim_controls_windows[i].valid = true;
        static const unsigned masks[] = {1, 2, 4, 9, 20, 0};
        app_sim_controls_windows[i].state = masks[i % 6];
    }
}

static int sim_controls_find(const char *id, unsigned revision)
{
    if (!id || revision != 1 || !app_sim_home_enabled || !app_sim_home_connected || sim_light_page < 0)
        return -1;
    unsigned first, count;
    if (!lx_home_page_range(&app_sim_home_config, (unsigned)sim_light_page, &first, &count))
        return -1;
    for (unsigned i = first; i < first + count; ++i)
        if (!strcmp(id, app_sim_home_config.cards[i].id) &&
            lx_home_card_is_controls(app_sim_home_config.cards[i].type))
            return (int)i;
    return -1;
}

bool app_loxone_controls_summary(const char *id, unsigned revision, lx_controls_summary_t *out)
{
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    out->animation = -1;
    int index = sim_controls_find(id, revision);
    if (index < 0)
        return false;
    *out = app_sim_controls_summaries[index];
    out->type = app_sim_home_config.cards[index].type;
    if (out->type != LX_HOME_CARD_GATE && out->type != LX_HOME_CARD_WINDOW)
        out->motion_valid = false;
    if (out->type == LX_HOME_CARD_WINDOW_MONITOR)
        out->controllable = false;
    return true;
}

bool app_loxone_controls_window(const char *id, unsigned revision, unsigned index,
                                 lx_controls_window_t *out)
{
    lx_controls_summary_t summary;
    if (!out || !app_loxone_controls_summary(id, revision, &summary) ||
        summary.type != LX_HOME_CARD_WINDOW_MONITOR || index >= summary.window_count ||
        index >= LX_CONTROLS_MAX_WINDOWS)
        return false;
    *out = app_sim_controls_windows[index];
    return true;
}

esp_err_t app_loxone_controls_command(const char *id, unsigned revision, lx_controls_action_t action)
{
    lx_controls_summary_t summary;
    int index = sim_controls_find(id, revision);
    if (index < 0 || !app_loxone_controls_summary(id, revision, &summary) ||
        !summary.available || !summary.controllable || action < LX_CONTROLS_ON || action >= LX_CONTROLS_SET ||
        (action == LX_CONTROLS_OPEN && !summary.open_allowed) ||
        (action == LX_CONTROLS_CLOSE && !summary.close_allowed) ||
        (summary.type == LX_HOME_CARD_TIMED_SWITCH && action == LX_CONTROLS_ON &&
         !app_sim_home_config.cards[index].allow_permanent))
        return ESP_ERR_INVALID_STATE;
    return app_loxone_home_command(id, (lx_home_command_t)(LX_HOME_COMMAND_CONTROL_ON + action));
}

esp_err_t app_loxone_controls_set(const char *id, unsigned revision, double value)
{
    lx_controls_summary_t summary;
    if (!app_loxone_controls_summary(id, revision, &summary) || !summary.available || !summary.controllable ||
        !summary.range_valid || !isfinite(value) || value < summary.minimum || value > summary.maximum ||
        (summary.type != LX_HOME_CARD_DIMMER && summary.type != LX_HOME_CARD_SLIDER &&
         summary.type != LX_HOME_CARD_WINDOW))
        return ESP_ERR_INVALID_STATE;
    if (summary.type == LX_HOME_CARD_WINDOW &&
        (((!summary.open_allowed || !summary.close_allowed) && !summary.value_valid) ||
         (value > summary.value && !summary.open_allowed) ||
         (value < summary.value && !summary.close_allowed)))
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = app_loxone_home_command(id, LX_HOME_COMMAND_CONTROL_SET);
    if (result == ESP_OK)
    {
        ++app_sim_controls_sets;
        app_sim_controls_last_value = value;
        if (summary.type == LX_HOME_CARD_DIMMER)
            app_sim_home_pending = false;
    }
    return result;
}
