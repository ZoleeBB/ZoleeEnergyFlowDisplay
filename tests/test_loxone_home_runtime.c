/* Valódi numerikus Loxone-tábla, null/0 és korlátos parancsépítés. */
#include "app_loxone_home_runtime.h"
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
int main(void)
{
    lx_home_config_t config = {0}; lx_home_values_t values = {0};
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
    printf("PASS: actual home numeric stream, scale/offset, genuine zero, invalid samples, atomic malformed packet, allowed card/type/state targets and inherited secured control rejection; values=%zu bytes\n",sizeof(values));
}
