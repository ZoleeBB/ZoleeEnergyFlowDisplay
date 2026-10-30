/* Valódi numerikus Loxone-tábla, null/0 és korlátos parancsépítés. */
#include "app_loxone_home_runtime.h"
#include "app_ev_charging.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void value(uint8_t out[24], double number)
{
    memset(out, 0, 24);
    uint64_t bits; memcpy(&bits, &number, 8);
    for (unsigned i = 0; i < 8; i++) out[16+i] = bits >> (8*i);
}

static void action_target_test(void)
{
    const char *identity = "aabbccdd-5566-7788-99aabbccddeeff00";
    const char *action = "cccccccc-5566-7788-99aabbccddeeff00";
    const char *state = "00000000-0000-0000-0000000000000000";
    /* A régi párosítás változatlan marad; csak a dróton küldött cím változik.
     * Hibás explicit címnél állapot olvasható, parancs nem küldhető. */
    const char *variants[] = {NULL, "\"aabbccdd-5566-7788-99aabbccddeeff00\"",
        "\"cccccccc-5566-7788-99aabbccddeeff00\"", "\"\"", "\"bad/On\"", "42", "null"};
    static lx_home_config_t config;
    config.card_count = 1;
    lx_home_card_t *card = &config.cards[0];
    strcpy(card->id, "saved-switch");
    strcpy(card->source.uuid, state);
    for (unsigned alias = 0; alias < 2; alias++)
    {
        for (unsigned i = 0; i < sizeof(variants) / sizeof(variants[0]); i++)
        {
            cJSON *root = cJSON_CreateObject();
            cJSON *controls = cJSON_AddObjectToObject(root, "controls");
            cJSON *control = cJSON_AddObjectToObject(controls, alias ? "named-object" : identity);
            cJSON_AddStringToObject(control, "type", "Switch");
            cJSON_AddBoolToObject(control, "isSecured", false);
            cJSON *states = cJSON_AddObjectToObject(control, "states");
            cJSON_AddStringToObject(states, "active", state);
            if (variants[i])
                assert(cJSON_AddItemToObject(control, "uuidAction", cJSON_Parse(variants[i])));
            char *json = cJSON_PrintUnformatted(root);
            assert(json);
            cJSON *catalog = lx_catalog_parse(json, strlen(json));
            cJSON_free(json);
            assert(catalog && cJSON_GetArraySize(catalog) == 1);
            cJSON *entry = cJSON_GetArrayItem(catalog, 0);
            const char *target = i == 2 ? action : identity;
            bool allowed = i < 3 && (!alias || i > 0);
            const char *saved = !alias ? identity : allowed ? target : "";
            assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "control_uuid")), saved));
            assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "uuid")), state));
            strcpy(card->control_uuid, saved);
            const lx_home_command_t commands[] = {LX_HOME_COMMAND_ON, LX_HOME_COMMAND_OFF, LX_HOME_COMMAND_PULSE};
            const char *verbs[] = {"On", "Off", "pulse"};
            for (unsigned j = 0; j < 3; j++)
            {
                card->type = j == 2 ? LX_HOME_CARD_PULSE : LX_HOME_CARD_SWITCH;
                char command[96] = "unchanged", expected[96];
                assert(lx_home_command_build(&config, catalog, card->id, commands[j], command) == allowed);
                snprintf(expected, sizeof(expected), "jdev/sps/io/%s/%s", target, verbs[j]);
                assert(!strcmp(command, allowed ? expected : "unchanged"));
            }
            cJSON_Delete(catalog);
            cJSON_DeleteItemFromObject(control, "states");
            json = cJSON_PrintUnformatted(root);
            assert(json);
            catalog = lx_catalog_parse(json, strlen(json));
            cJSON_free(json);
            assert(catalog && cJSON_GetArraySize(catalog) == (allowed ? 1 : 0));
            card->type = LX_HOME_CARD_PULSE;
            char command[96];
            assert(lx_home_command_build(&config, catalog, card->id, LX_HOME_COMMAND_PULSE, command) == allowed);
            cJSON_Delete(catalog);
            cJSON_Delete(root);
        }
    }
}

int main(void)
{
    /* Az ikon dönt, nem az elem neve vagy a kártyák ikonkészlete. */
    static lx_home_config_t ev_config;
    lx_home_values_t ev_values = {0};
    app_ev_charging_t ev = app_ev_charging_sum(&ev_config, &ev_values, true);
    assert(!ev.configured && !ev.valid && ev.power_kw == 0);
    ev_config.consumer_count = 4;
    strcpy(ev_config.consumers[0].icon, "ev-charger");
    strcpy(ev_config.consumers[1].icon, "ev-charger");
    strcpy(ev_config.consumers[2].icon, "electric-car");
    strcpy(ev_config.consumers[3].icon, "oven");
    ev_values.consumers[0] = (lx_home_value_t){.value = 3.6, .valid = true};
    ev_values.consumers[1] = (lx_home_value_t){.value = 7.2, .valid = true};
    ev_values.consumers[2] = (lx_home_value_t){.value = 0.7, .valid = true};
    ev_values.consumers[3] = (lx_home_value_t){.value = 100, .valid = true};
    ev = app_ev_charging_sum(&ev_config, &ev_values, true);
    assert(ev.configured && ev.valid && fabs(ev.power_kw - 11.5) < 1e-9);
    /* Csak autóikonnal is konfigurált; más fogyasztó nem számít bele. */
    strcpy(ev_config.consumers[0].icon, "oven");
    strcpy(ev_config.consumers[1].icon, "oven");
    ev = app_ev_charging_sum(&ev_config, &ev_values, true);
    assert(ev.configured && ev.valid && fabs(ev.power_kw - 0.7) < 1e-9);
    strcpy(ev_config.consumers[0].icon, "ev-charger");
    strcpy(ev_config.consumers[1].icon, "ev-charger");
    ev_values.consumers[2].valid = false;
    ev = app_ev_charging_sum(&ev_config, &ev_values, true);
    assert(ev.configured && !ev.valid && ev.power_kw == 0);
    ev_values.consumers[2].valid = true;
    ev_values.consumers[1].valid = false;
    ev = app_ev_charging_sum(&ev_config, &ev_values, true);
    assert(ev.configured && !ev.valid && ev.power_kw == 0);
    ev_values.consumers[1].valid = true;
    ev = app_ev_charging_sum(&ev_config, &ev_values, false);
    assert(ev.configured && !ev.valid && ev.power_kw == 0);
    ev_values.consumers[2].value = .0009;
    ev_values.consumers[0].value = 0;
    ev_values.consumers[1].value = .0009;
    ev = app_ev_charging_sum(&ev_config, &ev_values, true);
    assert(ev.valid && ev.power_kw == 0);
    ev_values.consumers[1].value = .001;
    assert(app_ev_charging_sum(&ev_config, &ev_values, true).power_kw == .001);
    ev_values.consumers[1].value = NAN;
    assert(!app_ev_charging_sum(&ev_config, &ev_values, true).valid);
    ev_values.consumers[1].value = -1;
    assert(!app_ev_charging_sum(&ev_config, &ev_values, true).valid);
    action_target_test();
    static lx_home_config_t config; lx_home_values_t values = {0};
    config.consumer_count = 1; config.card_count = 2;
    strcpy(config.consumers[0].source.uuid,"00000000-0000-0000-0000000000000000");
    config.consumers[0].source.scale=.001;
    config.cards[0].source=config.consumers[0].source; config.cards[0].source.scale=2;
    config.cards[0].source.offset=5;
    config.cards[1].type=LX_HOME_CARD_SWITCH;config.cards[1].source=config.consumers[0].source;config.cards[1].source.scale=1;
    uint8_t packet[24]; value(packet, 0);
    assert(lx_home_values_update(&values,&config,2,packet,24));
    assert(values.consumers[0].valid&&values.consumers[0].value==0);
    assert(values.cards[0].value==5&&values.cards[1].valid);
    value(packet, 1500); assert(lx_home_values_update(&values,&config,2,packet,24));
    assert(values.consumers[0].value==1.5&&values.cards[0].value==3005&&!values.cards[1].valid);
    lx_home_values_t before=values;
    assert(!lx_home_values_update(&values,&config,2,packet,23)&&memcmp(&values,&before,sizeof(values))==0);
    value(packet,NAN);assert(lx_home_values_update(&values,&config,2,packet,24));
    assert(!values.consumers[0].valid&&!values.cards[0].valid&&!values.cards[1].valid);
    value(packet,-1);assert(lx_home_values_update(&values,&config,2,packet,24)&&!values.consumers[0].valid);
    value(packet,1);assert(lx_home_values_update(&values,&config,2,packet,24)&&values.cards[1].valid);
    assert(lx_home_values_update(&values,&config,6,NULL,0));
    config.card_count=1;strcpy(config.cards[0].id,"light");config.cards[0].type=LX_HOME_CARD_SWITCH;
    strcpy(config.cards[0].control_uuid,"aabbccdd-5566-7788-99aabbccddeeff00");
    cJSON *catalog=cJSON_Parse("[{\"uuid\":\"00000000-0000-0000-0000000000000000\",\"control_uuid\":\"aabbccdd-5566-7788-99aabbccddeeff00\",\"type\":\"Switch\",\"state\":\"active\",\"secured\":false}]");assert(catalog);
    char command[96]="unchanged";
    assert(lx_home_command_build(&config,catalog,"light",LX_HOME_COMMAND_ON,command));
    assert(!strcmp(command,"jdev/sps/io/aabbccdd-5566-7788-99aabbccddeeff00/On"));
    assert(!lx_home_command_build(&config,catalog,"not-configured",LX_HOME_COMMAND_ON,command));
    assert(!lx_home_command_build(&config,catalog,"light",LX_HOME_COMMAND_PULSE,command));
    assert(!lx_home_command_build(&config,catalog,"light",(lx_home_command_t)99,command));
    cJSON *entry=cJSON_GetArrayItem(catalog,0);
    cJSON_ReplaceItemInObject(entry,"secured",cJSON_CreateBool(true));
    assert(!lx_home_command_build(&config,catalog,"light",LX_HOME_COMMAND_ON,command));
    cJSON_ReplaceItemInObject(entry,"secured",cJSON_CreateBool(false));
    cJSON_ReplaceItemInObject(entry,"state",cJSON_CreateString("lockedOn"));
    assert(!lx_home_command_build(&config,catalog,"light",LX_HOME_COMMAND_ON,command));
    config.cards[0].type=LX_HOME_CARD_PULSE;
    assert(lx_home_command_build(&config,catalog,"light",LX_HOME_COMMAND_PULSE,command));
    cJSON_ReplaceItemInObject(entry,"type",cJSON_CreateString("InfoOnlyDigital"));
    assert(!lx_home_command_build(&config,catalog,"light",LX_HOME_COMMAND_PULSE,command));
    cJSON_ReplaceItemInObject(entry,"type",cJSON_CreateString("Pushbutton"));
    assert(lx_home_command_build(&config,catalog,"light",LX_HOME_COMMAND_PULSE,command));
    cJSON_DeleteItemFromObject(entry,"secured");
    assert(!lx_home_command_build(&config,catalog,"light",LX_HOME_COMMAND_PULSE,command));
    cJSON_Delete(catalog);
    cJSON *structure=lx_catalog_parse("{\"controls\":{\"parent\":{\"isSecured\":true,\"subControls\":{\"aabbccdd-5566-7788-99aabbccddeeff00\":{\"type\":\"Switch\",\"states\":{\"active\":\"00000000-0000-0000-0000000000000000\"}}}}}}",strlen("{\"controls\":{\"parent\":{\"isSecured\":true,\"subControls\":{\"aabbccdd-5566-7788-99aabbccddeeff00\":{\"type\":\"Switch\",\"states\":{\"active\":\"00000000-0000-0000-0000000000000000\"}}}}}}"));
    assert(structure&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(structure,0),"secured")));cJSON_Delete(structure);
    const char *button_structure = "{\"controls\":{\"aabbccdd-5566-7788-99aabbccddeeff00\":{\"name\":\"Gomb\",\"type\":\"Pushbutton\"}}}";
    cJSON *buttons = lx_catalog_parse(button_structure, strlen(button_structure));
    assert(buttons && cJSON_GetArraySize(buttons) == 1);
    assert(!cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(buttons,0),"uuid")->valuestring[0]);
    config.cards[0].type = LX_HOME_CARD_PULSE;
    assert(lx_home_command_build(&config, buttons, "light", LX_HOME_COMMAND_PULSE, command));
    cJSON_Delete(buttons);
    config.card_count = LX_HOME_MAX_TOTAL_CARDS;
    for (unsigned i = 0; i < config.card_count; i++)
    {
        config.cards[i] = config.cards[0];
        config.cards[i].type = LX_HOME_CARD_VALUE;
        config.cards[i].source.scale = 1;
        config.cards[i].source.offset = i;
    }
    value(packet, 3);
    assert(lx_home_values_update(&values, &config, 2, packet, 24));
    for (unsigned i = 0; i < config.card_count; i++)
        assert(values.cards[i].valid && values.cards[i].value == 3 + i);
    config.cards[95].type = LX_HOME_CARD_PULSE;
    strcpy(config.cards[95].id, "last-page-button");
    catalog = cJSON_Parse("[{\"control_uuid\":\"aabbccdd-5566-7788-99aabbccddeeff00\",\"type\":\"Pushbutton\",\"secured\":false}]");
    assert(catalog && lx_home_command_build(&config, catalog, "last-page-button", LX_HOME_COMMAND_PULSE, command));
    cJSON_Delete(catalog);
    printf("PASS: actual home numeric stream, scale/offset, genuine zero, invalid samples, atomic malformed packet, allowed card/type/state targets and inherited secured control rejection; values=%zu bytes\n",sizeof(values));
}
