#include "app_loxone_home_model.h"
#include "app_loxone_home_layout.h"
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

static void layout_tests(void)
{
    _Static_assert(LX_HOME_GRID_MAX == LX_HOME_MAX_CONSUMERS,
                   "A konfiguráció és az oldal elemszámkorlátjának egyeznie kell.");
    /* Minden lehetséges feltöltöttség: középre igazított sor, változatlan
     * kártyaméret, egyforma hézag és nincs átfedés/képernyőn túlfutás. */
    for (unsigned count = 1; count <= LX_HOME_GRID_MAX; count++)
    {
        for (unsigned first = 0; first < count; first += LX_HOME_GRID_COLUMNS)
        {
            unsigned last = first + LX_HOME_GRID_COLUMNS - 1;
            if (last >= count)
                last = count - 1;
            lx_home_card_rect_t left, right, previous = {0};
            assert(lx_home_card_rect(count, first, &left));
            assert(lx_home_card_rect(count, last, &right));
            assert(left.x == LX_HOME_SCREEN_WIDTH - right.x - right.width);
            assert(left.y >= 120 && right.y + right.height <= 752);
            for (unsigned i = first; i <= last; i++)
            {
                lx_home_card_rect_t rect;
                assert(lx_home_card_rect(count, i, &rect));
                assert(rect.width == 292 && rect.height == 200);
                assert(rect.y == left.y);
                assert(rect.x >= 32 && rect.x + rect.width <= 1248);
                if (i != first)
                    assert(rect.x == previous.x + previous.width + 16);
                previous = rect;
            }
        }
    }
    /* A felhasználó konkrét 7 kártyás példája: négy fent, három középen lent. */
    const unsigned seven_x[] = {32, 340, 648, 956, 186, 494, 802};
    for (unsigned i = 0; i < 7; i++)
    {
        lx_home_card_rect_t rect;
        assert(lx_home_card_rect(7, i, &rect));
        assert(rect.x == seven_x[i] && rect.y == (i < 4 ? 120U : 336U));
    }
    lx_home_card_rect_t invalid = {1, 2, 3, 4};
    lx_home_card_rect_t saved = invalid;
    assert(!lx_home_card_rect(0, 0, &invalid));
    assert(!lx_home_card_rect(13, 0, &invalid));
    assert(!lx_home_card_rect(7, 7, &invalid));
    assert(!lx_home_card_rect(7, 0, NULL));
    assert(!memcmp(&invalid, &saved, sizeof(invalid)));
}

static void page_tests(void)
{
    lx_home_config_t *config = calloc(1, sizeof(*config));
    lx_home_config_t *again = calloc(1, sizeof(*again));
    assert(config && again);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema", 2);
    cJSON *pages = cJSON_AddArrayToObject(root, "pages");
    char error[LX_HOME_ERROR_SIZE];
    for (unsigned p = 0; p < LX_HOME_MAX_PAGES; p++)
    {
        cJSON *page = cJSON_CreateObject();
        char id[33];
        snprintf(id, sizeof(id), "page-%u", p);
        cJSON_AddStringToObject(page, "id", id);
        cJSON_AddStringToObject(page, "title", p ? "Emelet / Kert" : "Földszint – fények");
        cJSON *cards = cJSON_AddArrayToObject(page, "cards");
        for (unsigned i = 0; i < LX_HOME_MAX_CARDS; i++)
        {
            snprintf(id, sizeof(id), "p%u-card-%u", p, i);
            cJSON_AddItemToArray(cards, entry(id, "value"));
        }
        cJSON_AddItemToArray(pages, page);
    }
    assert(lx_home_config_parse(root, config, error));
    assert(config->page_count == 8 && config->card_count == 96);
    for (unsigned p = 0; p < 8; p++)
    {
        unsigned first, count;
        assert(lx_home_page_range(config, p, &first, &count) && first == p * 12 && count == 12);
    }
    cJSON *serialized = lx_home_config_json(config);
    assert(serialized && lx_home_config_parse(serialized, again, error));
    assert(!memcmp(config, again, sizeof(*config)));
    char *body = cJSON_PrintUnformatted(serialized);
    assert(body && strlen(body) < LX_HOME_JSON_MAX);
    cJSON *document = lx_home_document_parse(body, strlen(body));
    assert(document);
    cJSON_Delete(document);
    free(body);
    cJSON_Delete(serialized);
    cJSON_AddItemToArray(pages, cJSON_CreateObject());
    rejected(root, config, "pages[1].count");
    cJSON_DeleteItemFromArray(pages, 8);
    cJSON *second = cJSON_GetArrayItem(pages, 1);
    cJSON_ReplaceItemInObject(second, "id", cJSON_CreateString("page-0"));
    rejected(root, config, "duplicate_id");
    cJSON_ReplaceItemInObject(second, "id", cJSON_CreateString("page-1"));
    cJSON_ReplaceItemInObject(second, "title", cJSON_CreateString("   "));
    rejected(root, config, "id/title");
    cJSON_ReplaceItemInObject(second, "title", cJSON_CreateString("Második"));
    cJSON *cards = cJSON_GetObjectItemCaseSensitive(second, "cards");
    cJSON_ReplaceItemInObject(cJSON_GetArrayItem(cards, 0), "id", cJSON_CreateString("p0-card-0"));
    rejected(root, config, "duplicate_id");
    cJSON_ReplaceItemInObject(cJSON_GetArrayItem(cards, 0), "id", cJSON_CreateString("p1-card-0"));
    cJSON_AddItemToArray(cards, entry("overflow", "value"));
    rejected(root, config, "count");
    cJSON_DeleteItemFromArray(cards, 12);
    while (cJSON_GetArraySize(cards))
        cJSON_DeleteItemFromArray(cards, 0);
    assert(lx_home_config_parse(root, config, error));
    assert(config->pages[1].card_count == 0 && config->pages[2].first_card == 12);
    assert(lx_home_page_count(config) == 8);
    cJSON_Delete(root);
    free(config);
    free(again);
    puts("PASS: 8 named pages / 96 cards, per-page bounds, empty page, unique page/card IDs, migration and schema-2 roundtrip");
}

static void sample_test(const char *path)
{
    /* A telepítési mintát ugyanaz a szigorú parser olvassa, mint az SD-profilt. */
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size > 0 && size <= LX_HOME_JSON_MAX);
    rewind(file);
    char *bytes = malloc((size_t)size + 1);
    assert(bytes && fread(bytes, 1, size, file) == (size_t)size);
    assert(fclose(file) == 0);
    bytes[size] = 0;
    cJSON *json = lx_home_document_parse(bytes, size);
    free(bytes);
    lx_home_config_t *config = calloc(1, sizeof(*config));
    lx_home_config_t *again = calloc(1, sizeof(*again));
    char error[LX_HOME_ERROR_SIZE];
    assert(json && config && again && lx_home_config_parse(json, config, error));
    assert(config->consumer_count == 1 && config->page_count == 2 && config->card_count == 3);
    assert(!config->consumers[0].suggest_start && config->consumers[0].average_known);
    assert(config->pages[1].card_count == 0);
    assert(config->cards[0].type == LX_HOME_CARD_VALUE);
    assert(config->cards[1].type == LX_HOME_CARD_PULSE);
    assert(config->cards[2].type == LX_HOME_CARD_SWITCH);
    assert(strcmp(config->cards[2].source.uuid, config->cards[2].control_uuid));
    cJSON *saved = lx_home_config_json(config);
    assert(saved && lx_home_config_parse(saved, again, error));
    assert(!memcmp(config, again, sizeof(*config)));
    cJSON_Delete(saved);
    cJSON_Delete(json);
    free(config);
    free(again);
    puts("PASS: checked-in home sample through actual SD parser and save/reload, three card types and opt-in disabled");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    sample_test(argv[1]);
    char deep[2049]; memset(deep, '[', 1024); memset(deep+1024, ']', 1024); deep[2048] = 0;
    assert(!lx_home_document_parse(deep, 2048));
    assert(!lx_home_document_parse("{} trailing", 11));
    char embedded[] = {'{', '}', 0, 'x', 0};
    assert(!lx_home_document_parse(embedded, 4));
    cJSON *small = lx_home_document_parse("{\"name\":\"[\\\"{\"}", strlen("{\"name\":\"[\\\"{\"}"));
    assert(small); cJSON_Delete(small);

    layout_tests();
    page_tests();
    lx_home_config_t *config = calloc(1, sizeof(*config));
    lx_home_config_t *roundtrip = calloc(1, sizeof(*roundtrip));
    assert(config && roundtrip);
    char error[LX_HOME_ERROR_SIZE];
    assert(lx_home_config_parse(NULL, config, error));
    assert(config->intercom.speaker_volume == 55);
    assert(config->intercom.microphone_gain == 80);
    assert(!config->consumer_count && !config->card_count);
    cJSON *json = profile();
    assert(lx_home_config_parse(json, config, error));
    assert(config->consumer_count == 1 && config->card_count == 3);
    assert(config->intercom.speaker_volume == 55);
    cJSON *door_volume = cJSON_AddObjectToObject(json, "intercom");
    cJSON_AddBoolToObject(door_volume, "enabled", false);
    cJSON_AddNumberToObject(door_volume, "speaker_volume", 100);
    assert(lx_home_config_parse(json, config, error) && config->intercom.speaker_volume == 100);
    cJSON_SetNumberValue(cJSON_GetObjectItem(door_volume, "speaker_volume"), 100.5);
    assert(!lx_home_config_parse(json, config, error));
    cJSON_SetNumberValue(cJSON_GetObjectItem(door_volume, "speaker_volume"), 0);
    assert(lx_home_config_parse(json, config, error) && !config->intercom.speaker_volume);
    cJSON_AddNumberToObject(door_volume, "microphone_gain", 100);
    assert(lx_home_config_parse(json, config, error) && config->intercom.microphone_gain == 100);
    cJSON_SetNumberValue(cJSON_GetObjectItem(door_volume, "microphone_gain"), 101);
    assert(!lx_home_config_parse(json, config, error));
    cJSON_SetNumberValue(cJSON_GetObjectItem(door_volume, "microphone_gain"), 0);
    assert(lx_home_config_parse(json, config, error) && !config->intercom.microphone_gain);
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
    assert(encoded && !strstr(encoded, "\"video_password\"") && !strstr(encoded, "\"password\"") && !strstr(encoded, "token"));
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
            assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(serialized, "pages"), 0), "cards")) == 12);
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
    printf("PASS: centered 1..12 card layout including 7-card case; actual v3 home config, "
           "12+12 limits, atomic rejection, optional power, "
           "value/pulse/switch targets, JSON OOM sweep; profile=%zu bytes\n", sizeof(*config));
    free(config);
    free(roundtrip);
}
