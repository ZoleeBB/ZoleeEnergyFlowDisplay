#include "app_loxone_home_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const state_uuid = "11223344-5566-7788-99aabbccddeeff00";
static const char *const control_uuid = "aabbccdd-5566-7788-99aabbccddeeff00";
static bool fail_calloc;
void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size)
{
    return fail_calloc ? NULL : __real_calloc(count, size);
}

static cJSON *source(void)
{
    cJSON *root = cJSON_CreateObject();
    assert(root);
    assert(cJSON_AddStringToObject(root, "uuid", state_uuid));
    assert(cJSON_AddNumberToObject(root, "scale", 0.001));
    return root;
}

static cJSON *entry(const char *id, const char *type)
{
    cJSON *root = cJSON_CreateObject();
    assert(root);
    assert(cJSON_AddStringToObject(root, "id", id));
    assert(cJSON_AddStringToObject(root, "name", "Mosogatógép"));
    assert(cJSON_AddStringToObject(root, "icon", "dishwasher"));
    if (!type || strcmp(type, "pulse"))
        assert(cJSON_AddItemToObject(root, "source", source()));
    if (type)
    {
        assert(cJSON_AddStringToObject(root, "type", type));
        if (strcmp(type, "value"))
            assert(cJSON_AddStringToObject(root, "control_uuid", control_uuid));
        if (!strcmp(type, "switch"))
            cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(
                cJSON_GetObjectItemCaseSensitive(root, "source"), "scale"), 1);
    }
    return root;
}

static cJSON *profile(void)
{
    cJSON *root = cJSON_CreateObject();
    assert(root);
    assert(cJSON_AddNumberToObject(root, "schema", 1));
    cJSON *consumers = cJSON_AddArrayToObject(root, "consumers");
    cJSON *cards = cJSON_AddArrayToObject(root, "cards");
    assert(consumers && cards);
    assert(cJSON_AddItemToArray(consumers, entry("dishwasher", NULL)));
    assert(cJSON_AddItemToArray(cards, entry("temperature", "value")));
    assert(cJSON_AddItemToArray(cards, entry("boost", "pulse")));
    assert(cJSON_AddItemToArray(cards, entry("light", "switch")));
    return root;
}

static void rejected(cJSON *json, const lx_home_config_t *good, const char *field)
{
    lx_home_config_t *out = malloc(sizeof(*out));
    assert(out);
    *out = *good;
    char error[LX_HOME_ERROR_SIZE];
    assert(!lx_home_config_parse(json, out, error));
    assert(strstr(error, field));
    assert(memcmp(out, good, sizeof(*out)) == 0);
    free(out);
}

static long allocations, live, fail_after;
static void *json_malloc(size_t size)
{
    if (fail_after >= 0 && allocations++ == fail_after)
        return NULL;
    void *p = malloc(size);
    if (p)
        live++;
    return p;
}
static void json_free(void *p)
{
    if (p)
        live--;
    free(p);
}

int main(void)
{
    lx_home_config_t *config = calloc(1, sizeof(*config));
    lx_home_config_t *roundtrip = calloc(1, sizeof(*roundtrip));
    assert(config && roundtrip);
    char error[LX_HOME_ERROR_SIZE];
    assert(lx_home_config_parse(NULL, config, error));
    assert(!config->consumer_count && !config->card_count);
    cJSON *json = profile();
    assert(lx_home_config_parse(json, config, error));
    assert(config->consumer_count == 1 && config->card_count == 3);
    assert(config->consumers[0].source.scale == 0.001);
    assert(!config->consumers[0].average_known && !config->consumers[0].suggest_start);
    assert(config->cards[1].type == LX_HOME_CARD_PULSE && !config->cards[1].source.uuid[0]);
    assert(!strcmp(config->cards[2].control_uuid, control_uuid));
    assert(!strcmp(config->cards[2].source.uuid, state_uuid));
    assert(strcmp(config->cards[2].control_uuid, config->cards[2].source.uuid));
    double kw = -1;
    assert(!lx_home_consumer_start_kw(&config->consumers[0], &kw) && kw == -1);
    cJSON *consumers = cJSON_GetObjectItemCaseSensitive(json, "consumers");
    cJSON *first = cJSON_GetArrayItem(consumers, 0);
    cJSON_AddBoolToObject(first, "suggest_start", true);
    assert(lx_home_config_parse(json, config, error));
    assert(!lx_home_consumer_start_kw(&config->consumers[0], &kw));
    cJSON_AddNumberToObject(first, "average_kw", 1.4);
    assert(lx_home_config_parse(json, config, error));
    assert(lx_home_consumer_start_kw(&config->consumers[0], &kw) && kw == 1.4);

    cJSON *cards = cJSON_GetObjectItemCaseSensitive(json, "cards");
    cJSON *value = cJSON_GetArrayItem(cards, 0);
    cJSON_AddStringToObject(value, "unit", "°C");
    cJSON_AddNumberToObject(value, "warning_low", 10);
    cJSON_AddNumberToObject(value, "warning_high", 80);
    cJSON_AddStringToObject(value, "warning_color", "#0082FC");
    assert(lx_home_config_parse(json, config, error));
    cJSON *serialized = lx_home_config_json(config);
    assert(serialized && lx_home_config_parse(serialized, roundtrip, error));
    assert(!memcmp(config, roundtrip, sizeof(*config)));
    char *encoded = cJSON_PrintUnformatted(serialized);
    assert(encoded && !strstr(encoded, "password") && !strstr(encoded, "token"));
    free(encoded);
    cJSON_Delete(serialized);

    /* A második hibás elem nem írhatja felül az aktív jó profilt. */
    cJSON_AddItemToArray(consumers, entry("dishwasher", NULL));
    rejected(json, config, "consumers[2].duplicate_id");
    cJSON_DeleteItemFromArray(consumers, 1);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(first, "average_kw"), 0);
    rejected(json, config, "average_kw");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(first, "average_kw"), 1.4);
    cJSON_ReplaceItemInObjectCaseSensitive(first, "suggest_start", cJSON_CreateNumber(1));
    rejected(json, config, "suggest_start");
    cJSON_ReplaceItemInObjectCaseSensitive(first, "suggest_start", cJSON_CreateTrue());
    cJSON_ReplaceItemInObjectCaseSensitive(first, "icon", cJSON_CreateString("../escape.png"));
    rejected(json, config, "icon");
    cJSON_ReplaceItemInObjectCaseSensitive(first, "icon", cJSON_CreateString("dishwasher"));
    cJSON *map = cJSON_GetObjectItemCaseSensitive(first, "source");
    cJSON_AddNumberToObject(map, "offset", 1);
    rejected(json, config, "source");
    cJSON_DeleteItemFromObjectCaseSensitive(map, "offset");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(map, "scale"), NAN);
    rejected(json, config, "source");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(map, "scale"), 0.001);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(value, "warning_high"), 5);
    rejected(json, config, "warnings");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(value, "warning_high"), 80);
    cJSON_AddNumberToObject(value, "decimals", 1.5);
    rejected(json, config, "decimals");
    cJSON_DeleteItemFromObjectCaseSensitive(value, "decimals");
    cJSON *pulse = cJSON_GetArrayItem(cards, 1);
    cJSON_ReplaceItemInObjectCaseSensitive(pulse, "control_uuid", cJSON_CreateString("bad/on"));
    rejected(json, config, "control_uuid");
    cJSON_ReplaceItemInObjectCaseSensitive(pulse, "control_uuid", cJSON_CreateString(control_uuid));
    for (unsigned i = 1; i < LX_HOME_MAX_CONSUMERS; i++)
    {
        char id[32];
        snprintf(id, sizeof(id), "consumer-%u", i);
        cJSON_AddItemToArray(consumers, entry(id, NULL));
    }
    for (unsigned i = 3; i < LX_HOME_MAX_CARDS; i++)
    {
        char id[32];
        snprintf(id, sizeof(id), "card-%u", i);
        cJSON_AddItemToArray(cards, entry(id, "value"));
    }
    assert(lx_home_config_parse(json, config, error));
    cJSON_AddItemToArray(consumers, entry("overflow", NULL));
    rejected(json, config, "consumers[1].count");
    cJSON_DeleteItemFromArray(consumers, LX_HOME_MAX_CONSUMERS);
    cJSON_AddItemToArray(cards, entry("overflow", "value"));
    rejected(json, config, "cards[1].count");
    cJSON_DeleteItemFromArray(cards, LX_HOME_MAX_CARDS);
    fail_calloc = true;
    rejected(json, config, "memory");
    fail_calloc = false;
    cJSON_Delete(json);

    /* Minden cJSON-foglalási pont hibája: nincs részleges siker vagy szivárgás. */
    cJSON_Hooks hooks = {.malloc_fn = json_malloc, .free_fn = json_free};
    cJSON_InitHooks(&hooks);
    bool completed = false;
    for (long fault = 0; fault < 3000; fault++)
    {
        allocations = 0;
        fail_after = fault;
        serialized = lx_home_config_json(config);
        if (serialized)
        {
            completed = true;
            assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(serialized, "consumers")) == 12);
            assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(serialized, "cards")) == 12);
            cJSON_Delete(serialized);
        }
        assert(live == 0);
        if (completed)
            break;
    }
    assert(completed);
    cJSON_InitHooks(NULL);

    const char *structure =
        "{\"controls\":{\"aabbccdd-5566-7788-99aabbccddeeff00\":{\"name\":\"Light\","
        "\"type\":\"Switch\",\"states\":{\"active\":\"11223344-5566-7788-99aabbccddeeff00\"}}}}";
    cJSON *catalog = lx_catalog_parse(structure, strlen(structure));
    assert(catalog && cJSON_GetArraySize(catalog) == 1);
    cJSON *item = cJSON_GetArrayItem(catalog, 0);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(item, "control_uuid")->valuestring, control_uuid));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(item, "uuid")->valuestring, state_uuid));
    cJSON_Delete(catalog);
    printf("PASS: actual v3 home config, 12+12 limits, atomic rejection, optional power, "
           "value/pulse/switch targets, JSON OOM sweep; profile=%zu bytes\n", sizeof(*config));
    free(config);
    free(roundtrip);
}
