#include "lighting_fixture.h"

lx_light_circuit_t app_sim_light_circuits[LX_LIGHT_MAX_CIRCUITS];
lx_light_summary_t app_sim_light_summary;
unsigned app_sim_light_count, app_sim_light_commands, app_sim_light_last_index;
bool app_sim_light_failed;
static int sim_light_page = -1;
static unsigned sim_light_counter, sim_light_handle;

void app_sim_lighting_reset(void)
{
    memset(app_sim_light_circuits, 0, sizeof(app_sim_light_circuits));
    app_sim_light_count = 12;
    app_sim_light_commands = app_sim_light_last_index = 0;
    app_sim_light_failed = false;
    sim_light_page = -1;
    sim_light_handle = 0;
    app_sim_light_summary = (lx_light_summary_t){.label = LX_LIGHT_ONE_MOOD,
                                                 .active_count = 1,
                                                 .circuit_count = 12,
                                                 .available = true,
                                                 .controllable = true};
    strcpy(app_sim_light_summary.name, "TV nézés");
    const char *names[] = {"Mennyezeti kapcsolt világítás",
                           "Étkező dimmer",
                           "Nappali RGBW hangulatfény",
                           "Konyhapult hideg-meleg fehér"};
    for (unsigned i = 0; i < LX_LIGHT_MAX_CIRCUITS; ++i)
    {
        lx_light_circuit_t *circuit = &app_sim_light_circuits[i];
        snprintf(circuit->name, sizeof(circuit->name), "%s %u", names[i % 4], i + 1);
        circuit->type = i % 4 == 0 ? LX_LIGHT_SWITCH
                      : i % 4 == 1 ? LX_LIGHT_DIMMER
                                   : LX_LIGHT_COLOR;
        circuit->rgb = i % 4 == 2;
        circuit->white = true;
        circuit->min_kelvin = 2200;
        circuit->max_kelvin = 6500;
        circuit->lock_known = true;
        circuit->actual =
            (lx_light_value_t){.valid = true,
                               .brightness = i % 4 == 0 ? 100 : 65,
                               .hue = 28,
                               .saturation = 80,
                               .kelvin = 3500,
                               .mode = i % 4 == 3 ? LX_LIGHT_MODE_WHITE : LX_LIGHT_MODE_HSV};
    }
    app_sim_light_circuits[6].type = LX_LIGHT_UNSUPPORTED;
    strcpy(app_sim_light_circuits[6].name, "Ismeretlen típusú, csak olvasható kör");
    app_sim_light_circuits[7].blocked = true;
}

void app_loxone_home_visible(int page)
{
    if (page != sim_light_page)
    {
        sim_light_page = page;
        sim_light_handle = 0;
    }
}

static bool sim_light_card_visible(const char *id, unsigned revision)
{
    if (!id || revision != 1 || !app_sim_home_enabled || !app_sim_home_connected ||
        sim_light_page < 0)
        return false;
    unsigned first, count;
    if (!lx_home_page_range(&app_sim_home_config, (unsigned)sim_light_page, &first, &count))
        return false;
    for (unsigned i = first; i < first + count; ++i)
        if (app_sim_home_config.cards[i].type == LX_HOME_CARD_LIGHTING &&
            !strcmp(id, app_sim_home_config.cards[i].id))
            return true;
    return false;
}

bool app_loxone_light_summary(const char *id, unsigned revision, lx_light_summary_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!sim_light_card_visible(id, revision))
        return false;
    *out = app_sim_light_summary;
    return true;
}

unsigned app_loxone_light_open(const char *id, unsigned revision)
{
    if (!sim_light_card_visible(id, revision))
        return 0;
    if (++sim_light_counter == 0)
        ++sim_light_counter;
    return sim_light_handle = sim_light_counter;
}

void app_loxone_light_close(unsigned handle)
{
    if (handle == sim_light_handle)
        sim_light_handle = 0;
}

bool app_loxone_light_info(unsigned handle, app_loxone_light_info_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!handle || handle != sim_light_handle || !app_sim_home_connected || sim_light_page < 0)
        return false;
    *out = (app_loxone_light_info_t){.connected = true,
                                     .ready = true,
                                     .failed = app_sim_light_failed,
                                     .circuit_count = app_sim_light_count};
    return true;
}

bool app_loxone_light_circuit(unsigned handle, unsigned index, lx_light_circuit_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!handle || handle != sim_light_handle || !app_sim_home_connected || sim_light_page < 0 ||
        index >= app_sim_light_count || index >= LX_LIGHT_MAX_CIRCUITS)
        return false;
    *out = app_sim_light_circuits[index];
    return true;
}

esp_err_t app_loxone_light_set(unsigned handle, unsigned index, const lx_light_value_t *value)
{
    lx_light_circuit_t circuit;
    if (!value || !value->valid || !app_loxone_light_circuit(handle, index, &circuit) ||
        circuit.blocked || circuit.locked || !circuit.lock_known || !circuit.actual.valid ||
        circuit.type == LX_LIGHT_UNSUPPORTED)
        return ESP_ERR_INVALID_STATE;
    ++app_sim_light_commands;
    app_sim_light_last_index = index;
    app_sim_light_circuits[index].actual = *value;
    return ESP_OK;
}
