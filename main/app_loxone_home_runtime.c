/* Korlátos értéktár és engedélyezett parancsok a közös Loxone-kapcsolathoz. */
#include "app_loxone_home_runtime.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool lx_home_values_update(lx_home_values_t *values, const lx_home_config_t *config,
                           int type, const uint8_t *data, size_t size)
{
    if (!values || !config || config->consumer_count > LX_HOME_MAX_CONSUMERS ||
        config->card_count > LX_HOME_MAX_TOTAL_CARDS)
        return false;
    if (type != 2)
        return true;
    if (size % 24 || (!data && size))
        return false;
    lx_home_values_t next = *values;
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
            next.consumers[i] = (lx_home_value_t){
                .value = isfinite(value) ? value : 0,
                .valid = isfinite(value) && value >= 0 && value <= 1000};
        }
        for (unsigned i = 0; i < config->card_count; i++)
        {
            const lx_home_card_t *card = &config->cards[i];
            if (card->type == LX_HOME_CARD_PULSE || strcmp(uuid, card->source.uuid))
                continue;
            double value = raw * card->source.scale + card->source.offset;
            next.cards[i] = (lx_home_value_t){
                .value = isfinite(value) ? value : 0,
                .valid = isfinite(value) && (card->type != LX_HOME_CARD_SWITCH ||
                                              value == 0 || value == 1)};
        }
    }
    *values = next;
    return true;
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

bool lx_home_command_build(const lx_home_config_t *config, const cJSON *catalog,
                           const char *card_id, lx_home_command_t action, char out[96])
{
    if (!config || !card_id || !out || !cJSON_IsArray(catalog) ||
        config->card_count > LX_HOME_MAX_TOTAL_CARDS || action > LX_HOME_COMMAND_OFF ||
        action < LX_HOME_COMMAND_PULSE)
        return false;
    const lx_home_card_t *card = NULL;
    for (unsigned i = 0; i < config->card_count; i++)
        if (!strcmp(config->cards[i].id, card_id))
            card = &config->cards[i];
    if (!card || !lx_uuid_valid(card->control_uuid) || card->type == LX_HOME_CARD_VALUE ||
        (card->type == LX_HOME_CARD_PULSE) != (action == LX_HOME_COMMAND_PULSE))
        return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, catalog)
    {
        const cJSON *target = cJSON_GetObjectItemCaseSensitive(item, "control_uuid");
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "type"));
        const cJSON *secured = cJSON_GetObjectItemCaseSensitive(item, "secured");
        if (!cJSON_IsString(target) || strcmp(target->valuestring, card->control_uuid) ||
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
        snprintf(out, 96, "jdev/sps/io/%s/%s", card->control_uuid, command);
        return true;
    }
    return false;
}
