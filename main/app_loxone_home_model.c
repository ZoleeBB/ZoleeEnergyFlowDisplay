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

static bool intercom(const cJSON *entry, lx_intercom_config_t *out, char *error)
{
    if (!entry)
        return true;
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(entry, "enabled");
    if (!cJSON_IsObject(entry) || !cJSON_IsBool(enabled))
        return fail(error, "intercom", 0, "enabled");
    out->enabled = cJSON_IsTrue(enabled);
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "stream_url"));
    if (url && url[0])
    {
        /* A hitelesítést kizárólag a mentett integráció adja; URL-be tett
         * jelszó, átirányítás és más protokoll nem része a konfigurációnak. */
        if (!text(entry, "stream_url", out->stream_url, sizeof(out->stream_url)) ||
            strncmp(url, "http://", 7) || !url[7] || strchr(url, '@') ||
            strchr(url, '#') || strchr(url, '\\') || strchr(url, ' '))
            return fail(error, "intercom", 0, "stream_url");
        const char *path = strchr(url + 7, '/');
        const char *query = strchr(url + 7, '?');
        if (!path || path == url + 7 || path[1] == 0 || (query && query < path))
            return fail(error, "intercom", 0, "stream_url");
    }
    const char *bell = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "bell_uuid"));
    if (bell && bell[0] && !uuid(entry, "bell_uuid", out->bell_uuid))
        return fail(error, "intercom", 0, "bell_uuid");
    if (out->enabled && (!out->stream_url[0] || !out->bell_uuid[0]))
        return fail(error, "intercom", 0, "stream_url/bell_uuid");
    const cJSON *gate = cJSON_GetObjectItemCaseSensitive(entry, "gate");
    if (gate && !cJSON_IsNull(gate))
    {
        if (!card(gate, &out->gate, 0, error) || out->gate.type != LX_HOME_CARD_PULSE ||
            strcmp(out->gate.id, LX_INTERCOM_GATE_ID))
            return fail(error, "intercom", 0, "gate");
    }
    return true;
}

static bool parse_cards(const cJSON *array, lx_home_config_t *next, char *error)
{
    if (!array)
        return true;
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > LX_HOME_MAX_CARDS)
        return fail(error, "cards", 0, "count");
    unsigned length = (unsigned)cJSON_GetArraySize(array);
    if (length > LX_HOME_MAX_TOTAL_CARDS - next->card_count)
        return fail(error, "cards", 0, "count");
    for (unsigned i = 0; i < length; i++)
    {
        unsigned index = next->card_count + i;
        if (!card(cJSON_GetArrayItem(array, (int)i), &next->cards[index], index, error))
            return false;
        if (!strcmp(next->cards[index].id, LX_INTERCOM_GATE_ID))
            return fail(error, "cards", index, "reserved_id");
        /* A parancssor oldalváltás közben is ugyanarra az egyedi kártyára mutat. */
        for (unsigned j = 0; j < index; j++)
            if (!strcmp(next->cards[index].id, next->cards[j].id))
                return fail(error, "cards", index, "duplicate_id");
    }
    next->card_count += length;
    return true;
}

bool lx_home_config_parse(const cJSON *root, lx_home_config_t *out,
                          char error[LX_HOME_ERROR_SIZE])
{
    if (error)
        error[0] = 0;
    if (!out)
        return fail(error, "home", 0, "output");
    /* A teljes profil heapen épül fel; hiba esetén nincs részleges elfogadás. */
    lx_home_config_t *next = calloc(1, sizeof(*next));
    if (!next)
        return fail(error, "home", 0, "memory");
    bool ok = root == NULL;
    if (root)
    {
        const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
        ok = cJSON_IsObject(root) && cJSON_IsNumber(schema) &&
             (schema->valuedouble == 1 || schema->valuedouble == 2);
        if (!ok)
            fail(error, "home", 0, "schema");
        const cJSON *consumers = cJSON_GetObjectItemCaseSensitive(root, "consumers");
        if (ok && consumers)
        {
            ok = cJSON_IsArray(consumers) && cJSON_GetArraySize(consumers) <= LX_HOME_MAX_CONSUMERS;
            if (!ok)
                fail(error, "consumers", 0, "count");
            for (unsigned i = 0; ok && i < (unsigned)cJSON_GetArraySize(consumers); i++)
            {
                ok = consumer(cJSON_GetArrayItem(consumers, (int)i), &next->consumers[i], i, error);
                for (unsigned j = 0; ok && j < i; j++)
                    if (!strcmp(next->consumers[i].id, next->consumers[j].id))
                        ok = fail(error, "consumers", i, "duplicate_id");
                next->consumer_count = i + 1;
            }
        }
        if (ok && schema->valueint == 1)
        {
            if (cJSON_HasObjectItem(root, "pages"))
                ok = fail(error, "home", 0, "schema");
            else
                ok = parse_cards(cJSON_GetObjectItemCaseSensitive(root, "cards"), next, error);
            if (ok && next->card_count)
            {
                next->page_count = 1;
                strcpy(next->pages[0].id, "home");
                strcpy(next->pages[0].title, "Otthon");
                next->pages[0].card_count = next->card_count;
            }
        }
        else if (ok)
        {
            const cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
            ok = !cJSON_HasObjectItem(root, "cards") && cJSON_IsArray(pages) &&
                 cJSON_GetArraySize(pages) <= LX_HOME_MAX_PAGES;
            if (!ok)
                fail(error, "pages", 0, "count");
            for (unsigned i = 0; ok && i < (unsigned)cJSON_GetArraySize(pages); i++)
            {
                const cJSON *entry = cJSON_GetArrayItem(pages, (int)i);
                lx_home_page_t *page = &next->pages[i];
                ok = cJSON_IsObject(entry) && identifier(entry, "id", page->id) &&
                     text(entry, "title", page->title, sizeof(page->title));
                if (!ok)
                    fail(error, "pages", i, "id/title");
                for (unsigned j = 0; ok && j < i; j++)
                    if (!strcmp(page->id, next->pages[j].id))
                        ok = fail(error, "pages", i, "duplicate_id");
                page->first_card = next->card_count;
                const cJSON *cards = cJSON_GetObjectItemCaseSensitive(entry, "cards");
                if (ok)
                    ok = cJSON_IsArray(cards) ? parse_cards(cards, next, error)
                                              : fail(error, "pages", i, "cards");
                page->card_count = next->card_count - page->first_card;
                next->page_count = i + 1;
            }
        }
    }
    if (ok)
        ok = intercom(cJSON_GetObjectItemCaseSensitive(root, "intercom"), &next->intercom, error);
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

static cJSON *card_json(const lx_home_card_t *card)
{
    cJSON *entry = common_json(card->id, card->name, card->icon);
    if (!entry)
        return NULL;
    const char *type = card->type == LX_HOME_CARD_VALUE ? "value" :
                       card->type == LX_HOME_CARD_PULSE ? "pulse" : "switch";
    if (!cJSON_AddStringToObject(entry, "type", type) ||
        (card->type != LX_HOME_CARD_VALUE &&
         !cJSON_AddStringToObject(entry, "control_uuid", card->control_uuid)) ||
        (card->type != LX_HOME_CARD_PULSE && !add_source(entry, &card->source)))
        goto failed;
    if (card->type == LX_HOME_CARD_VALUE &&
        (!cJSON_AddStringToObject(entry, "unit", card->unit) ||
         !cJSON_AddNumberToObject(entry, "decimals", card->decimals) ||
         !cJSON_AddStringToObject(entry, "warning_color", card->warning_color) ||
         !(card->warning_low_set ? cJSON_AddNumberToObject(entry, "warning_low", card->warning_low)
                                 : cJSON_AddNullToObject(entry, "warning_low")) ||
         !(card->warning_high_set ? cJSON_AddNumberToObject(entry, "warning_high", card->warning_high)
                                  : cJSON_AddNullToObject(entry, "warning_high"))))
        goto failed;
    return entry;
failed:
    cJSON_Delete(entry);
    return NULL;
}

cJSON *lx_home_config_json(const lx_home_config_t *config)
{
    if (!config || config->consumer_count > LX_HOME_MAX_CONSUMERS ||
        config->card_count > LX_HOME_MAX_TOTAL_CARDS || config->page_count > LX_HOME_MAX_PAGES)
        return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root || !cJSON_AddNumberToObject(root, "schema", 2))
        goto failed;
    cJSON *door = cJSON_AddObjectToObject(root, "intercom");
    if (!door || !cJSON_AddBoolToObject(door, "enabled", config->intercom.enabled) ||
        !cJSON_AddStringToObject(door, "stream_url", config->intercom.stream_url) ||
        !cJSON_AddStringToObject(door, "bell_uuid", config->intercom.bell_uuid))
        goto failed;
    cJSON *gate = config->intercom.gate.control_uuid[0] ? card_json(&config->intercom.gate) : cJSON_CreateNull();
    if (!gate || !cJSON_AddItemToObject(door, "gate", gate))
    {
        cJSON_Delete(gate);
        goto failed;
    }
    cJSON *consumers = cJSON_AddArrayToObject(root, "consumers");
    if (!consumers)
        goto failed;
    for (unsigned i = 0; i < config->consumer_count; i++)
    {
        const lx_home_consumer_t *consumer = &config->consumers[i];
        cJSON *entry = common_json(consumer->id, consumer->name, consumer->icon);
        if (!entry)
            goto failed;
        if (!cJSON_AddItemToArray(consumers, entry))
        {
            cJSON_Delete(entry);
            goto failed;
        }
        if (!add_source(entry, &consumer->source) ||
            !cJSON_AddBoolToObject(entry, "suggest_start", consumer->suggest_start) ||
            !(consumer->average_known ? cJSON_AddNumberToObject(entry, "average_kw", consumer->average_kw)
                                      : cJSON_AddNullToObject(entry, "average_kw")))
            goto failed;
    }
    cJSON *pages = cJSON_AddArrayToObject(root, "pages");
    if (!pages)
        goto failed;
    unsigned end = 0;
    for (unsigned p = 0; p < lx_home_page_count(config); p++)
    {
        unsigned first, count;
        if (!lx_home_page_range(config, p, &first, &count) || first != end)
            goto failed;
        end += count;
        cJSON *page = cJSON_CreateObject();
        if (!page)
            goto failed;
        if (!cJSON_AddItemToArray(pages, page))
        {
            cJSON_Delete(page);
            goto failed;
        }
        if (!cJSON_AddStringToObject(page, "id", config->page_count ? config->pages[p].id : "home") ||
            !cJSON_AddStringToObject(page, "title", config->page_count ? config->pages[p].title : "Otthon"))
            goto failed;
        cJSON *cards = cJSON_AddArrayToObject(page, "cards");
        if (!cards)
            goto failed;
        for (unsigned i = first; i < end; i++)
        {
            cJSON *entry = card_json(&config->cards[i]);
            if (!entry)
                goto failed;
            if (!cJSON_AddItemToArray(cards, entry))
            {
                cJSON_Delete(entry);
                goto failed;
            }
        }
    }
    if (end != config->card_count)
        goto failed;
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
    if (!body || size < 2 || size > LX_HOME_JSON_MAX || memchr(body, 0, size))
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
