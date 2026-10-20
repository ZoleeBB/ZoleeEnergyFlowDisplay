/* Korlátos, platformfüggetlen konfiguráció a v3 Master Loxone-oldalaihoz.
 * A hálózati klienshez és a widgetekhez kötés külön munkaszakaszban készül. */
#include "app_loxone_home_model.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, const char *section, unsigned index, const char *field)
{
    if (error)
        snprintf(error, LX_HOME_ERROR_SIZE, "%s[%u].%s: hiányzó vagy érvénytelen érték.",
                 section, index + 1, field);
    return false;
}

static bool text(const cJSON *object, const char *key, char *out, size_t size)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || !value->valuestring[0] ||
        strlen(value->valuestring) >= size)
        return false;
    bool visible = false;
    for (const unsigned char *p = (const unsigned char *)value->valuestring; *p; p++)
    {
        if (*p < 32 || *p == 127)
            return false;
        if (*p != ' ')
            visible = true;
    }
    if (!visible)
        return false;
    memcpy(out, value->valuestring, strlen(value->valuestring) + 1);
    return true;
}

static bool identifier(const cJSON *object, const char *key, char *out)
{
    if (!text(object, key, out, LX_HOME_ID_SIZE))
        return false;
    for (const char *p = out; *p; p++)
        if (!(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9') &&
            *p != '-' && *p != '_')
            return false;
    return true;
}

static bool number(const cJSON *object, const char *key, double *out)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble))
        return false;
    *out = value->valuedouble;
    return true;
}

static bool uuid(const cJSON *object, const char *key, char *out)
{
    if (!text(object, key, out, LX_UUID_LEN) || !lx_uuid_valid(out))
        return false;
    for (char *p = out; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    return true;
}

static bool mapping(const cJSON *object, lx_mapping_t *out)
{
    if (!cJSON_IsObject(object) || !uuid(object, "uuid", out->uuid) ||
        !number(object, "scale", &out->scale) || out->scale == 0 || fabs(out->scale) > 1e6)
        return false;
    const cJSON *offset = cJSON_GetObjectItemCaseSensitive(object, "offset");
    out->offset = 0;
    return !offset || (number(object, "offset", &out->offset) && fabs(out->offset) <= 1e6);
}

static bool common(const cJSON *entry, char *id, char *name, char *icon,
                   const char *section, unsigned index, char *error)
{
    if (!cJSON_IsObject(entry) || !identifier(entry, "id", id))
        return fail(error, section, index, "id");
    if (!text(entry, "name", name, LX_HOME_NAME_SIZE))
        return fail(error, section, index, "name");
    if (!identifier(entry, "icon", icon))
        return fail(error, section, index, "icon");
    return true;
}

static bool consumer(const cJSON *entry, lx_home_consumer_t *out, unsigned index, char *error)
{
    if (!common(entry, out->id, out->name, out->icon, "consumers", index, error))
        return false;
    if (!mapping(cJSON_GetObjectItemCaseSensitive(entry, "source"), &out->source) ||
        out->source.offset != 0)
        return fail(error, "consumers", index, "source");
    const cJSON *average = cJSON_GetObjectItemCaseSensitive(entry, "average_kw");
    if (average && !cJSON_IsNull(average))
    {
        if (!number(entry, "average_kw", &out->average_kw) ||
            out->average_kw <= 0 || out->average_kw > 1000)
            return fail(error, "consumers", index, "average_kw");
        out->average_known = true;
    }
    const cJSON *suggest = cJSON_GetObjectItemCaseSensitive(entry, "suggest_start");
    if (suggest && !cJSON_IsBool(suggest))
        return fail(error, "consumers", index, "suggest_start");
    out->suggest_start = cJSON_IsTrue(suggest);
    return true;
}

static bool warnings(const cJSON *entry, lx_home_card_t *out)
{
    const cJSON *low = cJSON_GetObjectItemCaseSensitive(entry, "warning_low");
    const cJSON *high = cJSON_GetObjectItemCaseSensitive(entry, "warning_high");
    out->warning_low_set = low && !cJSON_IsNull(low);
    out->warning_high_set = high && !cJSON_IsNull(high);
    if ((out->warning_low_set && !number(entry, "warning_low", &out->warning_low)) ||
        (out->warning_high_set && !number(entry, "warning_high", &out->warning_high)) ||
        (out->warning_low_set && out->warning_high_set && out->warning_low > out->warning_high))
        return false;
    strcpy(out->warning_color, "#E66B6B");
    if (cJSON_GetObjectItemCaseSensitive(entry, "warning_color"))
    {
        if (!text(entry, "warning_color", out->warning_color, sizeof(out->warning_color)) ||
            strlen(out->warning_color) != 7 || out->warning_color[0] != '#')
            return false;
        for (unsigned i = 1; i < 7; i++)
            if (!isxdigit((unsigned char)out->warning_color[i]))
                return false;
    }
    return true;
}

static bool card(const cJSON *entry, lx_home_card_t *out, unsigned index, char *error)
{
    if (!common(entry, out->id, out->name, out->icon, "cards", index, error))
        return false;
    char type[8];
    if (!text(entry, "type", type, sizeof(type)))
        return fail(error, "cards", index, "type");
    if (!strcmp(type, "value"))
        out->type = LX_HOME_CARD_VALUE;
    else if (!strcmp(type, "pulse"))
        out->type = LX_HOME_CARD_PULSE;
    else if (!strcmp(type, "switch"))
        out->type = LX_HOME_CARD_SWITCH;
    else
        return fail(error, "cards", index, "type");

    if (out->type != LX_HOME_CARD_VALUE && !uuid(entry, "control_uuid", out->control_uuid))
        return fail(error, "cards", index, "control_uuid");
    if (out->type != LX_HOME_CARD_PULSE &&
        !mapping(cJSON_GetObjectItemCaseSensitive(entry, "source"), &out->source))
        return fail(error, "cards", index, "source");
    if (out->type == LX_HOME_CARD_SWITCH &&
        (out->source.scale != 1 || out->source.offset != 0))
        return fail(error, "cards", index, "source");
    if (out->type != LX_HOME_CARD_VALUE)
        return true;

    const cJSON *unit = cJSON_GetObjectItemCaseSensitive(entry, "unit");
    if (unit && !(cJSON_IsString(unit) && !unit->valuestring[0]) &&
        !text(entry, "unit", out->unit, sizeof(out->unit)))
        return fail(error, "cards", index, "unit");
    double decimals = 1;
    if (cJSON_GetObjectItemCaseSensitive(entry, "decimals") &&
        (!number(entry, "decimals", &decimals) || decimals != floor(decimals) ||
         decimals < 0 || decimals > 3))
        return fail(error, "cards", index, "decimals");
    out->decimals = (unsigned)decimals;
    if (!warnings(entry, out))
        return fail(error, "cards", index, "warnings");
    return true;
}

bool lx_home_config_parse(const cJSON *root, lx_home_config_t *out,
                          char error[LX_HOME_ERROR_SIZE])
{
    if (error)
        error[0] = 0;
    if (!out)
        return fail(error, "home", 0, "output");
    /* Atomi elfogadás: hibás második elem sem ronthatja el a korábbi jó profilt.
     * A több kB-os ideiglenes profil nem kerül a hálózati feladat stackjére. */
    lx_home_config_t *next = calloc(1, sizeof(*next));
    if (!next)
        return fail(error, "home", 0, "memory");
    bool ok = root == NULL;
    if (root)
    {
        const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
        ok = cJSON_IsObject(root) && cJSON_IsNumber(schema) && schema->valuedouble == 1;
        if (!ok)
            fail(error, "home", 0, "schema");
        const char *sections[] = {"consumers", "cards"};
        const unsigned limits[] = {LX_HOME_MAX_CONSUMERS, LX_HOME_MAX_CARDS};
        for (unsigned section = 0; ok && section < 2; section++)
        {
            const cJSON *array = cJSON_GetObjectItemCaseSensitive(root, sections[section]);
            if (!array)
                continue;
            if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > (int)limits[section])
            {
                ok = fail(error, sections[section], 0, "count");
                break;
            }
            unsigned count = (unsigned)cJSON_GetArraySize(array);
            for (unsigned i = 0; ok && i < count; i++)
            {
                const cJSON *entry = cJSON_GetArrayItem(array, (int)i);
                ok = section == 0 ? consumer(entry, &next->consumers[i], i, error)
                                  : card(entry, &next->cards[i], i, error);
                for (unsigned j = 0; ok && j < i; j++)
                {
                    const char *id = section == 0 ? next->consumers[i].id : next->cards[i].id;
                    const char *other = section == 0 ? next->consumers[j].id : next->cards[j].id;
                    if (!strcmp(id, other))
                        ok = fail(error, sections[section], i, "duplicate_id");
                }
            }
            if (section == 0)
                next->consumer_count = count;
            else
                next->card_count = count;
        }
    }
    if (ok)
        *out = *next;
    free(next);
    return ok;
}

static cJSON *source_json(const lx_mapping_t *source)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;
    if (!cJSON_AddStringToObject(root, "uuid", source->uuid) ||
        !cJSON_AddNumberToObject(root, "scale", source->scale) ||
        !cJSON_AddNumberToObject(root, "offset", source->offset))
    {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static cJSON *common_json(const char *id, const char *name, const char *icon)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;
    if (!cJSON_AddStringToObject(root, "id", id) ||
        !cJSON_AddStringToObject(root, "name", name) ||
        !cJSON_AddStringToObject(root, "icon", icon))
    {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static bool add_source(cJSON *entry, const lx_mapping_t *source)
{
    cJSON *value = source_json(source);
    if (value && cJSON_AddItemToObject(entry, "source", value))
        return true;
    cJSON_Delete(value);
    return false;
}

cJSON *lx_home_config_json(const lx_home_config_t *config)
{
    if (!config || config->consumer_count > LX_HOME_MAX_CONSUMERS ||
        config->card_count > LX_HOME_MAX_CARDS)
        return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root || !cJSON_AddNumberToObject(root, "schema", 1))
        goto failed;
    const char *sections[] = {"consumers", "cards"};
    for (unsigned section = 0; section < 2; section++)
    {
        cJSON *array = cJSON_AddArrayToObject(root, sections[section]);
        if (!array)
            goto failed;
        unsigned count = section == 0 ? config->consumer_count : config->card_count;
        for (unsigned i = 0; i < count; i++)
        {
            const lx_home_consumer_t *consumer = &config->consumers[i];
            const lx_home_card_t *card = &config->cards[i];
            cJSON *entry = section == 0 ? common_json(consumer->id, consumer->name, consumer->icon)
                                        : common_json(card->id, card->name, card->icon);
            if (!entry)
                goto failed;
            if (!cJSON_AddItemToArray(array, entry))
            {
                cJSON_Delete(entry);
                goto failed;
            }
            if (section == 0)
            {
                if (!add_source(entry, &consumer->source) ||
                    !cJSON_AddBoolToObject(entry, "suggest_start", consumer->suggest_start) ||
                    !(consumer->average_known ?
                          cJSON_AddNumberToObject(entry, "average_kw", consumer->average_kw) :
                          cJSON_AddNullToObject(entry, "average_kw")))
                    goto failed;
            }
            else
            {
                const char *type = card->type == LX_HOME_CARD_VALUE ? "value" :
                                    card->type == LX_HOME_CARD_PULSE ? "pulse" : "switch";
                if (!cJSON_AddStringToObject(entry, "type", type))
                    goto failed;
                if (card->type != LX_HOME_CARD_VALUE &&
                    !cJSON_AddStringToObject(entry, "control_uuid", card->control_uuid))
                    goto failed;
                if (card->type != LX_HOME_CARD_PULSE && !add_source(entry, &card->source))
                    goto failed;
                if (card->type == LX_HOME_CARD_VALUE &&
                    (!cJSON_AddStringToObject(entry, "unit", card->unit) ||
                     !cJSON_AddNumberToObject(entry, "decimals", card->decimals) ||
                     !cJSON_AddStringToObject(entry, "warning_color", card->warning_color) ||
                     !(card->warning_low_set ?
                           cJSON_AddNumberToObject(entry, "warning_low", card->warning_low) :
                           cJSON_AddNullToObject(entry, "warning_low")) ||
                     !(card->warning_high_set ?
                           cJSON_AddNumberToObject(entry, "warning_high", card->warning_high) :
                           cJSON_AddNullToObject(entry, "warning_high"))))
                    goto failed;
            }
        }
    }
    return root;

failed:
    cJSON_Delete(root);
    return NULL;
}

bool lx_home_consumer_start_kw(const lx_home_consumer_t *consumer, double *kw)
{
    if (!consumer || !kw || !consumer->suggest_start || !consumer->average_known ||
        !isfinite(consumer->average_kw) || consumer->average_kw <= 0 || consumer->average_kw > 1000)
        return false;
    *kw = consumer->average_kw;
    return true;
}

cJSON *lx_home_document_parse(const char *body, size_t size)
{
    if (!body || size < 2 || size > 32768 || memchr(body, 0, size))
        return NULL;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < size; i++)
    {
        char ch = body[i];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                quoted = false;
        }
        else if (ch == '"')
            quoted = true;
        else if (ch == '{' || ch == '[')
        {
            if (++depth > 8)
                return NULL;
        }
        else if (ch == '}' || ch == ']')
        {
            if (!depth)
                return NULL;
            depth--;
        }
    }
    if (depth || quoted)
        return NULL;
    return cJSON_ParseWithLengthOpts(body, size + 1, NULL, true);
}
