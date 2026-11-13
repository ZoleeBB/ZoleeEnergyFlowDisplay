#include "irc_fixture.h"

lx_irc_summary_t app_sim_irc_summaries[LX_HOME_MAX_TOTAL_CARDS];

void app_sim_irc_reset(void)
{
    for (unsigned i = 0; i < LX_HOME_MAX_TOTAL_CARDS; ++i)
        app_sim_irc_summaries[i] = (lx_irc_summary_t){
            .target_c = 22.5, .actual_c = 21.8,
            .target_valid = true, .actual_valid = true,
            .available = true, .controllable = true,
            .direction = LX_IRC_DIRECTION_HEATING,
        };
}

bool app_loxone_irc_summary(const char *id, unsigned revision, lx_irc_summary_t *out)
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
        if (app_sim_home_config.cards[i].type == LX_HOME_CARD_IRC &&
            !strcmp(id, app_sim_home_config.cards[i].id))
        {
            *out = app_sim_irc_summaries[i];
            return true;
        }
    return false;
}
