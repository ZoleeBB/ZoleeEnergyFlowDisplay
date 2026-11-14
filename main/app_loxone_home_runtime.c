/* Korlátos értéktár és engedélyezett parancsok a közös Loxone-kapcsolathoz. */
#include "app_loxone_home_runtime.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool home_values_update_range(lx_home_values_t *values, const lx_home_config_t *config,
                                     unsigned first, unsigned count,
                                     int type, const uint8_t *data, size_t size)
{
    if (!values || !config || config->consumer_count > LX_HOME_MAX_CONSUMERS ||
        config->card_count > LX_HOME_MAX_TOTAL_CARDS || first > config->card_count ||
        count > config->card_count - first)
        return false;
    if (type != 2)
        return true;
    if (size % 24 || (!data && size))
        return false;
    /* A teljes numerikus tábla és minden indexhatár ellenőrzése megelőzi az
     * első írást; a ciklusban már nincs visszautasító hibaág. A kliens a teljes
     * hívás alatt ugyanazt a mutexet tartja, mint a pillanatképek olvasói.
     * Nem kell az összes 96 kártya értéktárát a taskveremre másolni. */
    for (size_t position = 0; position < size; position += 24)
    {
        char uuid[LX_UUID_LEN];
        lx_uuid_string(data + position, uuid);
        uint64_t bits = 0;
        for (unsigned byte = 0; byte < 8; byte++)
            bits |= (uint64_t)data[position + 16 + byte] << (8 * byte);
        double raw;
        memcpy(&raw, &bits, sizeof(raw));
        for (unsigned i = 0; i < config->consumer_count; i++)
        {
            const lx_mapping_t *source = &config->consumers[i].source;
            if (strcmp(uuid, source->uuid))
                continue;
            double value = raw * source->scale;
            values->consumers[i] = (lx_home_value_t){
                .value = isfinite(value) ? value : 0,
                .valid = isfinite(value) && value >= 0 && value <= 1000};
        }
        for (unsigned i = first; i < first + count; i++)
        {
            const lx_home_card_t *card = &config->cards[i];
            if ((card->type != LX_HOME_CARD_VALUE && card->type != LX_HOME_CARD_SWITCH) ||
                strcmp(uuid, card->source.uuid))
                continue;
            double value = raw * card->source.scale + card->source.offset;
            values->cards[i] = (lx_home_value_t){
                .value = isfinite(value) ? value : 0,
                .valid = isfinite(value) && (card->type != LX_HOME_CARD_SWITCH ||
                                              value == 0 || value == 1)};
        }
    }
    return true;
}

bool lx_home_values_update(lx_home_values_t *values, const lx_home_config_t *config,
                           int type, const uint8_t *data, size_t size)
{
    return home_values_update_range(values, config, 0, config ? config->card_count : 0, type, data, size);
}

bool lx_home_values_update_visible(lx_home_values_t *values, const lx_home_config_t *config,
                                   int page, int type, const uint8_t *data, size_t size)
{
    unsigned first = 0, count = 0;
    if (page >= 0 && !lx_home_page_range(config, (unsigned)page, &first, &count))
        return false;
    return home_values_update_range(values, config, first, count, type, data, size);
}

bool lx_home_control_supported(const char *type, bool secured, bool pulse)
{
    if (!type || secured)
        return false;
    /* Más típusoknak eltérő parancsszemantikája lehet; nem küldünk rájuk
     * általános On/Off parancsot. A védett objektum vizuális jelszót igényel. */
    return !strcmp(type, "Switch") ||
           (pulse && (!strcmp(type, "Pushbutton") || !strcmp(type, "TimedSwitch")));
}

static const char *home_shading_target(const lx_home_card_t *card, const cJSON *catalog,
                                       bool position)
{
    if (!card || card->type != LX_HOME_CARD_SHADING)
        return NULL;
    const cJSON *entry = lx_shading_catalog_find(catalog, card->control_uuid);
    if (!entry || !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "shading_blocked")) ||
        (position && !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "shading_central"))))
        return NULL;
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(entry, "action_uuid");
    const char *target = action ? cJSON_GetStringValue(action) : card->control_uuid;
    return lx_uuid_valid(target) ? target : NULL;
}

bool lx_home_command_build(const lx_home_config_t *config, const cJSON *catalog,
                           const char *card_id, lx_home_command_t action, char out[96])
{
    if (!config || !card_id || !out || !cJSON_IsArray(catalog) ||
        config->card_count > LX_HOME_MAX_TOTAL_CARDS || action > LX_HOME_COMMAND_SHADING_STOP ||
        action < LX_HOME_COMMAND_PULSE)
        return false;
    const lx_home_card_t *card = NULL;
    for (unsigned i = 0; i < config->card_count; i++)
        if (!strcmp(config->cards[i].id, card_id))
            card = &config->cards[i];
    if (!strcmp(card_id, LX_INTERCOM_GATE_ID) && config->intercom.enabled)
        card = &config->intercom.gate;
    if (card && card->type == LX_HOME_CARD_LIGHTING)
    {
        const cJSON *entry = lx_light_catalog_find(catalog, card->control_uuid);
        const cJSON *blocked = cJSON_GetObjectItemCaseSensitive(entry, "lighting_blocked");
        if (!entry || !cJSON_IsFalse(blocked) || action < LX_HOME_COMMAND_LIGHT_PREV ||
            action > LX_HOME_COMMAND_LIGHT_OFF)
            return false;
        const char *target = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "action_uuid"));
        if (!target) target = card->control_uuid;
        if (!lx_uuid_valid(target)) return false;
        const char *command = action == LX_HOME_COMMAND_LIGHT_PREV ? "minus" :
                              action == LX_HOME_COMMAND_LIGHT_NEXT ? "plus" : "changeTo/0";
        snprintf(out, 96, "jdev/sps/io/%s/%s", target, command);
        return true;
    }
    if (card && card->type == LX_HOME_CARD_IRC)
    {
        if (action != LX_HOME_COMMAND_IRC_DECREASE && action != LX_HOME_COMMAND_IRC_INCREASE)
            return false;
        const cJSON *entry = lx_irc_catalog_find(catalog, card->control_uuid);
        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(entry, "irc_unit");
        if (!entry || !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "irc_blocked")) ||
            !cJSON_IsNumber(unit) || (unit->valuedouble != 0 && unit->valuedouble != 1))
            return false;
        const cJSON *action_target = cJSON_GetObjectItemCaseSensitive(entry, "action_uuid");
        const char *target = action_target ? cJSON_GetStringValue(action_target) : card->control_uuid;
        if (!lx_uuid_valid(target))
            return false;
        /* A kijelző mindig 0,5 °C-ot léptet. Fahrenheit-adatfolyamnál ez
         * 0,9 °F; a célérték és az ideiglenes állítás szabálya a Loxone-é. */
        const char *step = unit->valuedouble == 0 ? "0.500000" : "0.900000";
        const char *command = action == LX_HOME_COMMAND_IRC_DECREASE ?
                              "decreasetemperature" : "increasetemperature";
        snprintf(out, 96, "jdev/sps/io/%s/%s/%s", target, command, step);
        return true;
    }
    if (card && card->type == LX_HOME_CARD_SHADING)
    {
        if (action < LX_HOME_COMMAND_SHADING_UP || action > LX_HOME_COMMAND_SHADING_STOP)
            return false;
        const char *target = home_shading_target(card, catalog, false);
        if (!target)
            return false;
        const char *command = action == LX_HOME_COMMAND_SHADING_UP ? "FullUp" :
                              action == LX_HOME_COMMAND_SHADING_DOWN ? "FullDown" : "stop";
        snprintf(out, 96, "jdev/sps/io/%s/%s", target, command);
        return true;
    }
    if (action > LX_HOME_COMMAND_OFF) return false;
    if (!card || !lx_uuid_valid(card->control_uuid) || card->type == LX_HOME_CARD_VALUE ||
        (card->type == LX_HOME_CARD_PULSE) != (action == LX_HOME_COMMAND_PULSE))
        return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, catalog)
    {
        const cJSON *target = cJSON_GetObjectItemCaseSensitive(item, "control_uuid");
        const cJSON *action_target = cJSON_GetObjectItemCaseSensitive(item, "action_uuid");
        if (!action_target)
            action_target = target;
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "type"));
        const cJSON *secured = cJSON_GetObjectItemCaseSensitive(item, "secured");
        if (!cJSON_IsString(target) || strcmp(target->valuestring, card->control_uuid) ||
            !cJSON_IsString(action_target) || !lx_uuid_valid(action_target->valuestring) ||
            !cJSON_IsBool(secured) ||
            !lx_home_control_supported(type, cJSON_IsTrue(secured), action == LX_HOME_COMMAND_PULSE))
            continue;
        if (card->type == LX_HOME_CARD_SWITCH)
        {
            const char *state = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "uuid"));
            const char *key = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "state"));
            if (!state || strcmp(state, card->source.uuid) || !key || strcmp(key, "active"))
                continue;
        }
        const char *command = action == LX_HOME_COMMAND_PULSE ? "pulse" :
                              action == LX_HOME_COMMAND_ON ? "On" : "Off";
        snprintf(out, 96, "jdev/sps/io/%s/%s", action_target->valuestring, command);
        return true;
    }
    return false;
}

bool lx_home_shading_position_build(const lx_home_config_t *config, const cJSON *catalog,
                                    const char *card_id, unsigned percent, char out[96])
{
    if (!config || !card_id || !out || !cJSON_IsArray(catalog) || percent > 100 ||
        config->card_count > LX_HOME_MAX_TOTAL_CARDS)
        return false;
    for (unsigned i = 0; i < config->card_count; ++i)
    {
        const lx_home_card_t *card = &config->cards[i];
        if (strcmp(card->id, card_id))
            continue;
        const char *target = home_shading_target(card, catalog, true);
        if (!target)
            return false;
        snprintf(out, 96, "jdev/sps/io/%s/manualPosition/%u", target, percent);
        return true;
    }
    return false;
}

bool lx_intercom_bell_update(bool *ringing, const lx_intercom_config_t *config,
                             int type, const uint8_t *data, size_t size)
{
    if (!ringing || !config)
        return false;
    if (type != 2 || !config->enabled)
        return true;
    if (size % 24 || (!data && size))
        return false;
    for (size_t position = 0; position < size; position += 24)
    {
        char uuid[LX_UUID_LEN];
        lx_uuid_string(data + position, uuid);
        if (strcmp(uuid, config->bell_uuid))
            continue;
        uint64_t bits = 0;
        for (unsigned byte = 0; byte < 8; ++byte)
            bits |= (uint64_t)data[position + 16 + byte] << (8 * byte);
        double value;
        memcpy(&value, &bits, sizeof(value));
        if (value == 0 || value == 1)
            *ringing = value == 1;
    }
    return true;
}
