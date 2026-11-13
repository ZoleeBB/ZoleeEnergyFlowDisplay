/* A tényleges árnyékolásmodell: típuskapu, élő állapot, parancs és hibás import. */
#include "app_loxone_shading.h"
#include "app_loxone_home_runtime.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const automatic_id = "30000000-0000-0000-0000000000000001";
static const char *const central_id = "30000000-0000-0000-0000000000000002";
static const char *const manual_id = "30000000-0000-0000-0000000000000003";

static void number(lx_shading_block_t *block, const char *uuid, double value)
{
    lx_shading_block_event(block, uuid, true, value, NULL, 0, 1000000);
}

static void lock_text(lx_shading_block_t *block, const char *text)
{
    lx_shading_block_event(block, block->lock_state, false, 0, text, strlen(text), 1000000);
}

static void unlock(lx_shading_block_t *block)
{
    number(block, block->locked_state, 0);
    number(block, block->device_state, 2);
    lock_text(block, "");
}

static cJSON *catalog_from(const cJSON *source)
{
    char *encoded = cJSON_PrintUnformatted(source);
    assert(encoded);
    cJSON *catalog = lx_catalog_parse(encoded, strlen(encoded));
    free(encoded);
    assert(catalog);
    return catalog;
}

static cJSON *control(cJSON *source, const char *id)
{
    return cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(source, "controls"), id);
}

static void replace(cJSON *object, const char *name, cJSON *item)
{
    assert(object && item);
    cJSON_DeleteItemFromObjectCaseSensitive(object, name);
    assert(cJSON_AddItemToObject(object, name, item));
}

static lx_home_config_t *home_config(void)
{
    cJSON *json = cJSON_Parse(
        "{\"schema\":2,\"pages\":[{\"id\":\"shades\",\"title\":\"Árnyékolás\",\"cards\":["
        "{\"id\":\"automatic\",\"name\":\"Nappali\",\"type\":\"shading\","
        "\"control_uuid\":\"30000000-0000-0000-0000000000000001\"},"
        "{\"id\":\"central\",\"name\":\"Központi\",\"type\":\"shading\",\"icon\":\"../bad\","
        "\"control_uuid\":\"30000000-0000-0000-0000000000000002\"}]}]}");
    lx_home_config_t *home = calloc(1, sizeof(*home));
    char error[LX_HOME_ERROR_SIZE];
    assert(json && home && lx_home_config_parse(json, home, error));
    assert(home->card_count == 2 && home->cards[0].type == LX_HOME_CARD_SHADING);
    assert(home->cards[1].type == LX_HOME_CARD_SHADING);
    assert(!strcmp(home->cards[0].icon, LX_SHADING_ICON_ID));
    assert(!strcmp(home->cards[1].icon, LX_SHADING_ICON_ID));
    assert(!home->cards[0].source.uuid[0]);
    cJSON *cards = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json, "pages"), 0), "cards");
    cJSON *first = cJSON_GetArrayItem(cards, 0);
    replace(first, "icon", cJSON_CreateArray());
    assert(lx_home_config_parse(json, home, error));
    replace(first, "control_uuid", cJSON_CreateString("invalid/uuid"));
    assert(!lx_home_config_parse(json, home, error));
    assert(!strcmp(home->cards[0].control_uuid, automatic_id));
    strcpy(home->cards[0].icon, "wrong-icon");
    cJSON *saved = lx_home_config_json(home);
    assert(saved);
    const cJSON *saved_card = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(saved, "pages"), 0), "cards"), 0);
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(saved_card, "icon")), LX_SHADING_ICON_ID));
    assert(!cJSON_HasObjectItem(saved_card, "source"));
    assert(lx_home_config_parse(saved, home, error));
    cJSON_Delete(saved);
    cJSON_Delete(json);
    return home;
}

static void check_individual(const cJSON *entry)
{
    lx_shading_block_t *block = lx_shading_block_create(entry);
    assert(block && !block->central && !block->blocked);
    assert(!strcmp(block->lock_state, "30000000-0000-0000-0000000000000017"));
    lx_shading_summary_t summary;
    lx_shading_summary(block, &summary);
    assert(!summary.available && !summary.controllable && !summary.position_valid && !summary.motion_known);
    unlock(block);
    lx_shading_summary(block, &summary);
    assert(summary.available && summary.controllable && !summary.position_valid);
    number(block, block->position_state, 0.375);
    number(block, block->up_state, 1);
    number(block, block->down_state, 0);
    lx_shading_summary(block, &summary);
    assert(summary.position_valid && summary.position_percent == 37.5);
    assert(summary.motion_known && summary.moving_up && !summary.moving_down);
    number(block, block->up_state, 0);
    number(block, block->down_state, 1);
    lx_shading_summary(block, &summary);
    assert(summary.motion_known && !summary.moving_up && summary.moving_down);
    number(block, block->position_state, 0);
    lx_shading_summary(block, &summary);
    assert(summary.position_valid && summary.position_percent == 0);
    number(block, block->position_state, 1);
    number(block, block->down_state, 0);
    lx_shading_summary(block, &summary);
    assert(summary.position_valid && summary.position_percent == 100);
    assert(summary.motion_known && !summary.moving_up && !summary.moving_down);
    const double invalid_positions[] = {-0.001, 1.001, NAN, INFINITY, -INFINITY};
    for (unsigned i = 0; i < sizeof(invalid_positions) / sizeof(*invalid_positions); ++i)
    {
        number(block, block->position_state, invalid_positions[i]);
        lx_shading_summary(block, &summary);
        assert(!summary.position_valid && summary.position_percent == 0 && summary.controllable);
    }
    number(block, block->position_state, 0.2);
    lx_shading_block_event(block, block->position_state, false, 0, "0.2", 3, 0);
    assert(!block->position_valid);
    number(block, block->up_state, NAN);
    lx_shading_summary(block, &summary);
    assert(!summary.motion_known && summary.controllable);
    number(block, block->up_state, 1);
    number(block, block->down_state, 1);
    lx_shading_summary(block, &summary);
    assert(!summary.motion_known); /* Két ellentétes irány nem bizonyított mozgás. */
    number(block, block->up_state, 0);
    number(block, block->down_state, 0);
    number(block, block->safety_state, 1);
    lx_shading_summary(block, &summary);
    assert(summary.safety_active && summary.controllable);
    number(block, block->locked_state, 1);
    lx_shading_summary(block, &summary);
    assert(!summary.controllable && summary.safety_active);
    number(block, block->locked_state, 0);
    const double invalid_locks[] = {0.5, 2, NAN, INFINITY};
    for (unsigned i = 0; i < sizeof(invalid_locks) / sizeof(*invalid_locks); ++i)
    {
        number(block, block->locked_state, invalid_locks[i]);
        lx_shading_summary(block, &summary);
        assert(!summary.controllable && !block->hard_lock_known);
    }
    number(block, block->locked_state, 0);
    const char *const bad_locks[] = {"{\"locked\":1}", "{\"locked\":2}", "[]", "{}",
                                   "{\"locked\":0} trailing", "{\"locked\":\"0\"}"};
    for (unsigned i = 0; i < sizeof(bad_locks) / sizeof(*bad_locks); ++i)
    {
        lock_text(block, bad_locks[i]);
        lx_shading_summary(block, &summary);
        assert(!summary.controllable);
    }
    lock_text(block, "{\"locked\":0}");
    lx_shading_summary(block, &summary);
    assert(summary.controllable);
    number(block, block->lock_state, 0);
    lx_shading_summary(block, &summary);
    assert(!summary.controllable && !block->lock_known);
    lock_text(block, "");
    for (int state = 0; state <= 2; ++state)
    {
        number(block, block->device_state, state);
        lx_shading_summary(block, &summary);
        assert(summary.controllable == (state != 1));
        assert(summary.available == (state != 1));
    }
    number(block, block->device_state, 3);
    lx_shading_summary(block, &summary);
    assert(!summary.controllable && !block->device_known);
    number(block, block->device_state, NAN);
    assert(!block->device_known);
    number(block, block->device_state, 2);
    lx_shading_block_event(block, block->device_state, false, 0, "2", 1, 0);
    assert(!block->device_known);
    unlock(block);
    number(block, block->position_state, 0.4);
    number(block, manual_id, 0.8);
    lx_shading_summary(block, &summary);
    assert(summary.position_percent == 40 && summary.position_valid && summary.controllable);
    lx_shading_block_free(block);
}

static void check_commands(lx_home_config_t *home, const cJSON *catalog)
{
    char command[96];
    const lx_home_command_t actions[] = {
        LX_HOME_COMMAND_SHADING_UP, LX_HOME_COMMAND_SHADING_DOWN, LX_HOME_COMMAND_SHADING_STOP};
    const char *const suffixes[] = {"FullUp", "FullDown", "stop"};
    for (unsigned card = 0; card < 2; ++card)
        for (unsigned i = 0; i < 3; ++i)
        {
            char expected[96];
            snprintf(expected, sizeof(expected), "jdev/sps/io/30000000-0000-0000-00000000000000a%u/%s", card + 1, suffixes[i]);
            assert(lx_home_command_build(home, catalog, home->cards[card].id, actions[i], command));
            assert(!strcmp(command, expected));
        }
    for (int action = LX_HOME_COMMAND_PULSE; action <= LX_HOME_COMMAND_IRC_INCREASE; ++action)
        assert(!lx_home_command_build(home, catalog, "automatic", (lx_home_command_t)action, command));
    assert(!lx_home_command_build(home, catalog, "automatic", LX_HOME_COMMAND_SHADING_POSITION, command));
    assert(!lx_home_command_build(home, catalog, "central", LX_HOME_COMMAND_SHADING_POSITION, command));
    assert(!lx_home_command_build(home, catalog, "automatic", (lx_home_command_t)-1, command));
    assert(!lx_home_command_build(home, catalog, "automatic", (lx_home_command_t)999, command));
    const unsigned positions[] = {0, 37, 100};
    for (unsigned i = 0; i < 3; ++i)
    {
        char expected[96];
        snprintf(expected, sizeof(expected), "jdev/sps/io/30000000-0000-0000-00000000000000a1/manualPosition/%u", positions[i]);
        assert(lx_home_shading_position_build(home, catalog, "automatic", positions[i], command));
        assert(!strcmp(command, expected));
    }
    assert(!lx_home_shading_position_build(home, catalog, "automatic", 101, command));
    assert(!lx_home_shading_position_build(home, catalog, "automatic", UINT_MAX, command));
    assert(!lx_home_shading_position_build(home, catalog, "central", 50, command));
    assert(!lx_home_shading_position_build(home, catalog, "missing", 50, command));
    strcpy(home->cards[0].control_uuid, manual_id);
    assert(!lx_home_command_build(home, catalog, "automatic", LX_HOME_COMMAND_SHADING_UP, command));
    assert(!lx_home_shading_position_build(home, catalog, "automatic", 50, command));
    strcpy(home->cards[0].control_uuid, automatic_id);
    /* A bővülő enum egyik régi kártyatípus parancsait sem engedheti át. */
    const lx_home_card_type_t types[] = {LX_HOME_CARD_LIGHTING, LX_HOME_CARD_SWITCH, LX_HOME_CARD_IRC};
    const lx_home_command_t legacy_actions[] = {LX_HOME_COMMAND_LIGHT_OFF, LX_HOME_COMMAND_ON, LX_HOME_COMMAND_IRC_INCREASE};
    home->card_count = 5;
    for (unsigned i = 0; i < 3; ++i)
    {
        lx_home_card_t *card = &home->cards[i + 2];
        card->type = types[i];
        snprintf(card->id, sizeof(card->id), "legacy-%u", i);
        snprintf(card->control_uuid, sizeof(card->control_uuid), "30000000-0000-0000-000000000000000%u", i + 4);
        strcpy(card->source.uuid, "30000000-0000-0000-0000000000000051");
        card->source.scale = 1;
        assert(lx_home_command_build(home, catalog, card->id, legacy_actions[i], command));
        for (int action = LX_HOME_COMMAND_SHADING_UP; action <= LX_HOME_COMMAND_SHADING_POSITION; ++action)
            assert(!lx_home_command_build(home, catalog, card->id, (lx_home_command_t)action, command));
        assert(!lx_home_shading_position_build(home, catalog, card->id, 50, command));
    }
    home->card_count = 2;
}

static void check_optional_and_central(const cJSON *source, lx_home_config_t *home)
{
    cJSON *changed = cJSON_Duplicate(source, true);
    cJSON_DeleteItemFromObjectCaseSensitive(control(changed, automatic_id), "states");
    cJSON_DeleteItemFromObjectCaseSensitive(control(changed, automatic_id), "uuidAction");
    cJSON *catalog = catalog_from(changed);
    lx_shading_summary_t summary;
    lx_shading_block_t *block = lx_shading_block_create(lx_shading_catalog_find(catalog, automatic_id));
    assert(block);
    lx_shading_summary(block, &summary);
    assert(summary.available && summary.controllable && !summary.position_valid && !summary.motion_known);
    char command[96];
    assert(lx_home_command_build(home, catalog, "automatic", LX_HOME_COMMAND_SHADING_STOP, command));
    assert(!strcmp(command, "jdev/sps/io/30000000-0000-0000-0000000000000001/stop"));
    lx_shading_block_free(block);
    block = lx_shading_block_create(lx_shading_catalog_find(catalog, central_id));
    lx_shading_summary(block, &summary);
    assert(block && summary.central && summary.available && summary.controllable);
    assert(!summary.position_valid && !summary.motion_known);
    lx_shading_block_free(block);
    cJSON_Delete(catalog);
    /* A központi kártya állapotot sem vehet át egy véletlen position mezőből. */
    replace(control(changed, central_id), "states", cJSON_Duplicate(
        cJSON_GetObjectItemCaseSensitive(control((cJSON *)source, automatic_id), "states"), true));
    catalog = catalog_from(changed);
    block = lx_shading_block_create(lx_shading_catalog_find(catalog, central_id));
    assert(block);
    unlock(block);
    number(block, block->position_state, 0.5);
    number(block, block->up_state, 1);
    number(block, block->down_state, 0);
    lx_shading_summary(block, &summary);
    assert(summary.central && summary.controllable && !summary.position_valid && !summary.motion_known);
    assert(!summary.moving_up && !summary.moving_down);
    lx_shading_block_free(block);
    cJSON_Delete(catalog);
    cJSON_Delete(changed);
}

static void check_protection(const cJSON *source, lx_home_config_t *home)
{
    for (unsigned kind = 0; kind < 8; ++kind)
    {
        cJSON *changed = cJSON_Duplicate(source, true);
        cJSON *item = control(changed, automatic_id);
        cJSON *states = cJSON_GetObjectItemCaseSensitive(item, "states");
        if (kind == 0) replace(item, "isSecured", cJSON_CreateTrue());
        if (kind == 1) replace(item, "restrictions", cJSON_CreateNumber(2));
        if (kind == 2) replace(item, "restrictions", cJSON_CreateString("0"));
        if (kind == 3) replace(item, "uuidAction", cJSON_CreateString("wrong/action"));
        if (kind == 4) replace(states, "jLocked", cJSON_CreateNull());
        if (kind == 5) replace(states, "locked", cJSON_CreateString("bad-uuid"));
        if (kind == 6) replace(states, "deviceState", cJSON_CreateNumber(0));
        if (kind == 7) replace(item, "states", cJSON_CreateArray());
        cJSON *catalog = catalog_from(changed);
        lx_shading_block_t *block = lx_shading_block_create(lx_shading_catalog_find(catalog, automatic_id));
        assert(block && block->blocked);
        unlock(block);
        lx_shading_summary_t summary;
        lx_shading_summary(block, &summary);
        assert(!summary.controllable);
        char command[96];
        assert(!lx_home_command_build(home, catalog, "automatic", LX_HOME_COMMAND_SHADING_UP, command));
        assert(!lx_home_shading_position_build(home, catalog, "automatic", 50, command));
        lx_shading_block_free(block);
        cJSON_Delete(catalog);
        cJSON_Delete(changed);
    }
    cJSON *changed = cJSON_Duplicate(source, true);
    cJSON *controls = cJSON_GetObjectItemCaseSensitive(changed, "controls");
    cJSON *item = cJSON_DetachItemFromObjectCaseSensitive(controls, automatic_id);
    cJSON *parent = cJSON_AddObjectToObject(controls, "30000000-0000-0000-0000000000000099");
    assert(parent && cJSON_AddStringToObject(parent, "type", "Container") &&
           cJSON_AddBoolToObject(parent, "isSecured", true));
    cJSON *children = cJSON_AddObjectToObject(parent, "subControls");
    assert(children && cJSON_AddItemToObject(children, automatic_id, item));
    cJSON *catalog = catalog_from(changed);
    lx_shading_block_t *block = lx_shading_block_create(lx_shading_catalog_find(catalog, automatic_id));
    assert(block && block->blocked);
    lx_shading_block_free(block);
    cJSON_Delete(catalog);
    cJSON_Delete(changed);
}

static void check_type_gate(const cJSON *source)
{
    for (unsigned kind = 0; kind < 4; ++kind)
    {
        cJSON *changed = cJSON_Duplicate(source, true);
        cJSON *details = cJSON_GetObjectItemCaseSensitive(control(changed, automatic_id), "details");
        if (kind == 0) replace(details, "isAutomatic", cJSON_CreateFalse());
        if (kind == 1) replace(details, "isAutomatic", cJSON_CreateNumber(1));
        if (kind == 2) replace(details, "isAutomatic", cJSON_CreateString("true"));
        if (kind == 3) cJSON_DeleteItemFromObjectCaseSensitive(details, "isAutomatic");
        cJSON *catalog = catalog_from(changed);
        assert(!lx_shading_catalog_find(catalog, automatic_id));
        cJSON_Delete(catalog);
        cJSON_Delete(changed);
    }
}

static void check_corrupt_metadata(const cJSON *entry)
{
    for (unsigned kind = 0; kind < 4; ++kind)
    {
        cJSON *changed = cJSON_Duplicate(entry, true);
        cJSON *meta = cJSON_Parse(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(changed, "shading")));
        assert(meta);
        if (kind == 0) replace(meta, "central", cJSON_CreateTrue());
        if (kind == 1) replace(meta, "lock", cJSON_CreateString("bad-uuid"));
        if (kind == 2) cJSON_DeleteItemFromObjectCaseSensitive(meta, "device");
        if (kind == 3) replace(changed, "shading_central", cJSON_CreateTrue());
        char *encoded = cJSON_PrintUnformatted(meta);
        assert(encoded);
        replace(changed, "shading", cJSON_CreateString(encoded));
        assert(!lx_shading_block_create(changed));
        free(encoded);
        cJSON_Delete(meta);
        cJSON_Delete(changed);
    }
    char *oversized = calloc(LX_SHADING_META_MAX + 2, 1);
    assert(oversized);
    memset(oversized, ' ', LX_SHADING_META_MAX + 1);
    cJSON *changed = cJSON_Duplicate(entry, true);
    replace(changed, "shading", cJSON_CreateString(oversized));
    assert(!lx_shading_block_create(changed));
    free(oversized);
    cJSON_Delete(changed);
}

static void check_generic_values(lx_home_config_t *home)
{
    lx_home_values_t values = {0};
    uint8_t data[24] = {0};
    double value = 0.5;
    memcpy(data + 16, &value, sizeof(value));
    lx_uuid_string(data, home->cards[0].source.uuid);
    home->cards[0].source.scale = 1;
    assert(lx_home_values_update(&values, home, 2, data, sizeof(data)));
    assert(!values.cards[0].valid);
    assert(lx_home_values_update_visible(&values, home, 0, 2, data, sizeof(data)));
    assert(!values.cards[0].valid);
    home->cards[0].source.uuid[0] = 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0);
    rewind(file);
    char *text = calloc((size_t)length + 1, 1);
    assert(text && fread(text, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    cJSON *source = cJSON_Parse(text);
    free(text);
    assert(source);
    cJSON *catalog = catalog_from(source);
    const cJSON *entry = lx_shading_catalog_find(catalog, automatic_id);
    assert(entry && lx_shading_catalog_find(catalog, central_id));
    assert(!lx_shading_catalog_find(catalog, manual_id));
    assert(!lx_shading_catalog_find(catalog, NULL) && !lx_shading_block_create(NULL));
    lx_shading_summary_t empty;
    lx_shading_summary(NULL, &empty);
    assert(!empty.available && !empty.controllable && !empty.position_valid);
    check_individual(entry);
    lx_home_config_t *home = home_config();
    check_commands(home, catalog);
    check_optional_and_central(source, home);
    check_protection(source, home);
    check_type_gate(source);
    check_corrupt_metadata(entry);
    check_generic_values(home);
    free(home);
    cJSON_Delete(catalog);
    cJSON_Delete(source);
    puts("PASS: shading type gate, exact commands, percent range, locks, safety, central and fixed icon");
    return 0;
}
