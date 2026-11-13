#include "shading_fixture.h"

lx_shading_summary_t app_sim_shading_summaries[LX_HOME_MAX_TOTAL_CARDS];
unsigned app_sim_shading_positions, app_sim_shading_last_percent;

void app_sim_shading_reset(void)
{
    app_sim_shading_positions = app_sim_shading_last_percent = 0;
    for (unsigned i = 0; i < LX_HOME_MAX_TOTAL_CARDS; ++i)
        app_sim_shading_summaries[i] = (lx_shading_summary_t){
            .position_percent = 45, .position_valid = true,
            .available = true, .controllable = true, .motion_known = true,
        };
}

bool app_loxone_shading_summary(const char *id, unsigned revision, lx_shading_summary_t *out)
{
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    if (!id || revision != 1 || !app_sim_home_enabled || !app_sim_home_connected ||
        sim_light_page < 0)
        return false;
    unsigned first, count;
    if (!lx_home_page_range(&app_sim_home_config, (unsigned)sim_light_page, &first, &count))
        return false;
    for (unsigned i = first; i < first + count; ++i)
        if (app_sim_home_config.cards[i].type == LX_HOME_CARD_SHADING &&
            !strcmp(id, app_sim_home_config.cards[i].id))
        {
            *out = app_sim_shading_summaries[i];
            return true;
        }
    return false;
}

esp_err_t app_loxone_shading_command(const char *id, unsigned revision, lx_home_command_t action)
{
    lx_shading_summary_t summary;
    if ((action != LX_HOME_COMMAND_SHADING_UP && action != LX_HOME_COMMAND_SHADING_DOWN &&
         action != LX_HOME_COMMAND_SHADING_STOP) ||
        !app_loxone_shading_summary(id, revision, &summary) ||
        !summary.available || !summary.controllable)
        return ESP_ERR_INVALID_STATE;
    return app_loxone_home_command(id, action);
}

esp_err_t app_loxone_shading_position(const char *id, unsigned revision, unsigned percent)
{
    lx_shading_summary_t summary;
    if (percent > 100 || !app_loxone_shading_summary(id, revision, &summary) ||
        !summary.available || !summary.controllable || summary.central || !summary.position_valid)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = app_loxone_home_command(id, LX_HOME_COMMAND_SHADING_POSITION);
    if (result == ESP_OK)
    {
        ++app_sim_shading_positions;
        app_sim_shading_last_percent = percent;
    }
    /* A célérték elfogadása nem változtatja meg a mért pozíciót. */
    return result;
}
