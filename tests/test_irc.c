/* Az IRC valódi modellje: kezdőállapot, egységek, parancsok és hibás import. */
#include "app_loxone_irc.h"
#include "app_loxone_home_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const irc_id = "20000000-0000-0000-0000000000000001";
static const char *const legacy_id = "20000000-0000-0000-0000000000000002";
static const char *const light_id = "20000000-0000-0000-0000000000000003";
static const char *const switch_id = "20000000-0000-0000-0000000000000004";

static void number(lx_irc_block_t *block, const char *uuid, double value)
{
    lx_irc_block_event(block, uuid, true, value, NULL, 0, 1000000);
}

static void lock_text(lx_irc_block_t *block, const char *text)
{
    lx_irc_block_event(block, block->lock_state, false, 0, text, strlen(text), 1000000);
}

static cJSON *catalog_from(cJSON *structure)
{
    char *encoded = cJSON_PrintUnformatted(structure);
    assert(encoded);
    cJSON *catalog = lx_catalog_parse(encoded, strlen(encoded));
    free(encoded);
    assert(catalog);
    return catalog;
}

static cJSON *control(cJSON *structure)
{
    return cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(structure, "controls"), irc_id);
}

static void replace(cJSON *object, const char *name, cJSON *item)
{
    assert(item);
    cJSON_DeleteItemFromObjectCaseSensitive(object, name);
    assert(cJSON_AddItemToObject(object, name, item));
}

static lx_home_config_t *home_config(void)
{
    cJSON *json = cJSON_Parse(
        "{\"schema\":2,\"consumers\":[],\"pages\":[{\"id\":\"room\",\"title\":\"Nappali\",\"cards\":["
        "{\"id\":\"irc\",\"name\":\"Nappali\",\"type\":\"irc\","
        "\"control_uuid\":\"20000000-0000-0000-0000000000000001\"}]}]}");
    lx_home_config_t *home = calloc(1, sizeof(*home));
    char error[LX_HOME_ERROR_SIZE];
    assert(json && home && lx_home_config_parse(json, home, error));
    assert(home->cards[0].type == LX_HOME_CARD_IRC);
    assert(!strcmp(home->cards[0].icon, LX_IRC_ICON_ID));
    assert(!home->cards[0].source.uuid[0]);
    cJSON *card = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json, "pages"), 0), "cards"), 0);
    assert(cJSON_AddArrayToObject(card, "icon"));
    assert(lx_home_config_parse(json, home, error));
    replace(card, "icon", cJSON_CreateString("../bad-icon"));
    assert(lx_home_config_parse(json, home, error));
    assert(!strcmp(home->cards[0].icon, LX_IRC_ICON_ID));
    strcpy(home->cards[0].icon, "wrong-icon");
    cJSON *saved = lx_home_config_json(home);
    assert(saved);
    const cJSON *saved_card = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(saved, "pages"), 0), "cards"), 0);
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(saved_card, "icon")), LX_IRC_ICON_ID));
    assert(!cJSON_HasObjectItem(saved_card, "source"));
    assert(lx_home_config_parse(saved, home, error));
    cJSON_Delete(saved);
    cJSON_Delete(json);
    return home;
}

static void check_model(const cJSON *entry)
{
    lx_irc_block_t *block = lx_irc_block_create(entry);
    assert(block && block->unit == LX_IRC_UNIT_CELSIUS && !block->blocked);
    lx_irc_summary_t summary;
    lx_irc_summary(block, &summary);
    assert(!summary.available && !summary.controllable && summary.direction == LX_IRC_DIRECTION_UNKNOWN);
    number(block, block->target_state, 22.5);
    number(block, block->actual_state, 21.8);
    number(block, block->operating_mode_state, 1);
    lx_irc_summary(block, &summary);
    assert(summary.available && summary.target_valid && summary.actual_valid);
    assert(summary.target_c == 22.5 && summary.actual_c == 21.8);
    assert(!summary.controllable && summary.direction == LX_IRC_DIRECTION_HEATING);
    lock_text(block, "");
    lx_irc_summary(block, &summary);
    assert(summary.controllable);
    number(block, block->current_mode_state, 2);
    lx_irc_summary(block, &summary);
    assert(summary.direction == LX_IRC_DIRECTION_COOLING);
    number(block, block->actual_state, 35);
    lx_irc_summary(block, &summary);
    assert(summary.direction == LX_IRC_DIRECTION_COOLING); /* Nincs hőfokkülönbségből következtetés. */
    number(block, block->current_mode_state, 4);
    lx_irc_summary(block, &summary);
    assert(summary.direction == LX_IRC_DIRECTION_HEATING);
    number(block, block->operating_mode_state, 0);
    number(block, block->current_mode_state, NAN);
    lx_irc_summary(block, &summary);
    assert(summary.direction == LX_IRC_DIRECTION_UNKNOWN);
    number(block, block->actual_state, -1000);
    lx_irc_summary(block, &summary);
    assert(!summary.actual_valid && summary.target_valid && summary.controllable);
    number(block, block->target_state, NAN);
    lx_irc_summary(block, &summary);
    assert(!summary.target_valid && !summary.available && !summary.controllable);
    number(block, block->target_state, INFINITY);
    assert(!block->target_valid);
    number(block, block->target_state, 23);
    lx_irc_block_event(block, block->target_state, false, 0, "23", 2, 0);
    assert(!block->target_valid);
    number(block, block->target_state, 23);
    lock_text(block, "{\"locked\":1}");
    lx_irc_summary(block, &summary);
    assert(block->lock_known && !summary.controllable);
    lock_text(block, "{\"locked\":0}");
    lx_irc_summary(block, &summary);
    assert(summary.controllable);
    number(block, block->lock_state, 0);
    assert(!block->lock_known && block->locked);
    lock_text(block, "");
    lock_text(block, "{\"locked\":0} trailing");
    assert(!block->lock_known && block->locked);
    lock_text(block, "[]");
    assert(!block->lock_known && block->locked);
    lock_text(block, "");
    number(block, "20000000-0000-0000-000000000000ffff", 99);
    assert(block->target_c == 23 && block->target_valid && block->lock_known);
    lx_irc_block_free(block);
}

static void check_commands(lx_home_config_t *home, const cJSON *catalog)
{
    char command[96];
    assert(lx_home_command_build(home, catalog, "irc", LX_HOME_COMMAND_IRC_INCREASE, command));
    assert(!strcmp(command, "jdev/sps/io/20000000-0000-0000-00000000000000a1/increasetemperature/0.500000"));
    assert(lx_home_command_build(home, catalog, "irc", LX_HOME_COMMAND_IRC_DECREASE, command));
    assert(strstr(command, "/decreasetemperature/0.500000"));
    for (int action = LX_HOME_COMMAND_PULSE; action <= LX_HOME_COMMAND_LIGHT_OFF; ++action)
        assert(!lx_home_command_build(home, catalog, "irc", (lx_home_command_t)action, command));
    assert(!lx_home_command_build(home, catalog, "irc", (lx_home_command_t)-1, command));
    assert(!lx_home_command_build(home, catalog, "irc", (lx_home_command_t)999, command));
    home->card_count = 3;
    home->cards[1].type = LX_HOME_CARD_LIGHTING;
    strcpy(home->cards[1].id, "light");
    strcpy(home->cards[1].control_uuid, light_id);
    home->cards[2].type = LX_HOME_CARD_SWITCH;
    strcpy(home->cards[2].id, "switch");
    strcpy(home->cards[2].control_uuid, switch_id);
    strcpy(home->cards[2].source.uuid, "20000000-0000-0000-0000000000000041");
    home->cards[2].source.scale = 1;
    assert(lx_home_command_build(home, catalog, "light", LX_HOME_COMMAND_LIGHT_OFF, command));
    assert(lx_home_command_build(home, catalog, "switch", LX_HOME_COMMAND_ON, command));
    for (int action = LX_HOME_COMMAND_IRC_DECREASE; action <= LX_HOME_COMMAND_IRC_INCREASE; ++action)
    {
        assert(!lx_home_command_build(home, catalog, "light", (lx_home_command_t)action, command));
        assert(!lx_home_command_build(home, catalog, "switch", (lx_home_command_t)action, command));
    }
    strcpy(home->cards[0].control_uuid, legacy_id);
    assert(!lx_home_command_build(home, catalog, "irc", LX_HOME_COMMAND_IRC_INCREASE, command));
    strcpy(home->cards[0].control_uuid, irc_id);
    home->card_count = 1;
}

static void check_units(cJSON *source, lx_home_config_t *home)
{
    cJSON *ms = cJSON_GetObjectItemCaseSensitive(source, "msInfo");
    cJSON *details = cJSON_GetObjectItemCaseSensitive(control(source), "details");
    replace(ms, "tempUnit", cJSON_CreateNumber(1));
    replace(details, "format", cJSON_CreateString("%.1f °F"));
    for (unsigned mode = 0; mode < 5; ++mode)
    {
        if (mode == 1)
            cJSON_DeleteItemFromObjectCaseSensitive(ms, "tempUnit");
        if (mode == 2)
            replace(ms, "tempUnit", cJSON_CreateString("F"));
        if (mode == 3)
            replace(ms, "tempUnit", cJSON_CreateNumber(0));
        if (mode == 4)
        {
            cJSON_DeleteItemFromObjectCaseSensitive(ms, "tempUnit");
            cJSON_DeleteItemFromObjectCaseSensitive(details, "format");
        }
        cJSON *catalog = catalog_from(source);
        lx_irc_block_t *block = lx_irc_block_create(lx_irc_catalog_find(catalog, irc_id));
        assert(block);
        number(block, block->target_state, 77);
        number(block, block->actual_state, 68);
        lock_text(block, "");
        lx_irc_summary_t summary;
        lx_irc_summary(block, &summary);
        char command[96];
        if (mode < 2)
        {
            assert(summary.target_valid && summary.actual_valid && summary.controllable);
            assert(fabs(summary.target_c - 25) < 0.00001 && fabs(summary.actual_c - 20) < 0.00001);
            assert(lx_home_command_build(home, catalog, "irc", LX_HOME_COMMAND_IRC_INCREASE, command));
            assert(strstr(command, "/increasetemperature/0.900000"));
        }
        else
        {
            assert(!summary.target_valid && !summary.actual_valid && !summary.controllable);
            assert(!lx_home_command_build(home, catalog, "irc", LX_HOME_COMMAND_IRC_INCREASE, command));
        }
        lx_irc_block_free(block);
        cJSON_Delete(catalog);
    }
    replace(ms, "tempUnit", cJSON_CreateNumber(0));
    replace(details, "format", cJSON_CreateString("%.1f °C"));
}

static void check_protection(cJSON *source, lx_home_config_t *home)
{
    for (unsigned kind = 0; kind < 6; ++kind)
    {
        cJSON *changed = cJSON_Duplicate(source, true);
        cJSON *item = control(changed);
        cJSON *states = cJSON_GetObjectItemCaseSensitive(item, "states");
        if (kind == 0) replace(item, "isSecured", cJSON_CreateTrue());
        if (kind == 1) replace(item, "restrictions", cJSON_CreateNumber(2));
        if (kind == 2) replace(item, "restrictions", cJSON_CreateString("0"));
        if (kind == 3) replace(item, "uuidAction", cJSON_CreateString("wrong/action"));
        if (kind == 4) replace(states, "jLocked", cJSON_CreateNull());
        if (kind == 5) cJSON_DeleteItemFromObjectCaseSensitive(states, "tempTarget");
        cJSON *catalog = catalog_from(changed);
        lx_irc_block_t *block = lx_irc_block_create(lx_irc_catalog_find(catalog, irc_id));
        assert(block && block->blocked);
        number(block, block->target_state, 22);
        lock_text(block, "");
        lx_irc_summary_t summary;
        lx_irc_summary(block, &summary);
        assert(!summary.controllable);
        char command[96];
        assert(!lx_home_command_build(home, catalog, "irc", LX_HOME_COMMAND_IRC_INCREASE, command));
        lx_irc_block_free(block);
        cJSON_Delete(catalog);
        cJSON_Delete(changed);
    }
    cJSON *changed = cJSON_Duplicate(source, true);
    cJSON *states = cJSON_GetObjectItemCaseSensitive(control(changed), "states");
    cJSON_DeleteItemFromObjectCaseSensitive(states, "jLocked");
    cJSON_DeleteItemFromObjectCaseSensitive(states, "currentMode");
    cJSON *catalog = catalog_from(changed);
    lx_irc_block_t *block = lx_irc_block_create(lx_irc_catalog_find(catalog, irc_id));
    assert(block && block->lock_known && !block->locked && !block->blocked);
    number(block, block->target_state, 22);
    number(block, block->operating_mode_state, 5);
    lx_irc_summary_t summary;
    lx_irc_summary(block, &summary);
    assert(summary.controllable && summary.direction == LX_IRC_DIRECTION_COOLING);
    lx_irc_block_free(block);
    cJSON_Delete(catalog);
    cJSON_Delete(changed);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
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
    const cJSON *entry = lx_irc_catalog_find(catalog, irc_id);
    assert(entry && !lx_irc_catalog_find(catalog, legacy_id));
    assert(!lx_irc_catalog_find(catalog, NULL) && !lx_irc_block_create(NULL));
    check_model(entry);
    lx_home_config_t *home = home_config();
    check_commands(home, catalog);
    check_units(source, home);
    check_protection(source, home);
    free(home);
    cJSON_Delete(catalog);
    cJSON_Delete(source);
    printf("PASS: IRC native step, units, exact command target, fixed icon, locks and independent state validity\n");
    return 0;
}
