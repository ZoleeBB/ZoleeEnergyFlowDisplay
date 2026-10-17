#include "app_loxone_model.h"
#include "app_payload.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int64_t esp_timer_get_time(void){return 123456;}
static unsigned char uuid[16]={0x44,0x33,0x22,0x11,0x66,0x55,0x88,0x77,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00};
static void numeric(unsigned char *p,double v){memcpy(p,uuid,16);memcpy(p+16,&v,8);}
int main(void)
{
    lx_config_t cfg;lx_config_defaults(&cfg);lx_values_t v={0};char id[LX_UUID_LEN];
    lx_uuid_string(uuid,id);assert(strcmp(id,"11223344-5566-7788-99aabbccddeeff00")==0);
    assert(lx_uuid_valid(id)&&!lx_uuid_valid("11223344-5566-7788-99aa-bbccddeeff00"));
    snprintf(cfg.host,sizeof(cfg.host),"192.168.2.1");strcpy(cfg.username,"display");
    for(int i=0;i<8;i++)strcpy(cfg.map[i].uuid,id);
    cfg.map[0].scale=-0.001;cfg.map[1].offset=4;
    unsigned char bytes[48];numeric(bytes,2500);
    assert(lx_values_payload(&v,&cfg)==NULL);
    assert(lx_values_update(&v,&cfg,2,bytes,24));
    assert(v.value[0]==-2.5&&v.value[1]==2504);
    cJSON *payload=lx_values_payload(&v,&cfg);assert(payload);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(payload,"Temperature")));cJSON_Delete(payload);
    lx_values_t previous=v;numeric(bytes,1);numeric(bytes+24,NAN);
    assert(!lx_values_update(&v,&cfg,2,bytes,48));assert(memcmp(&v,&previous,sizeof(v))==0);
    assert(!lx_values_update(&v,&cfg,2,bytes,23));
    unsigned char text[128]={0};memcpy(text,uuid,16);text[32]=3;memcpy(text+36,"old",3);
    assert(lx_values_update(&v,&cfg,3,text,40));
    assert(!lx_values_update(&v,&cfg,3,text,38));text[32]=255;assert(!lx_values_update(&v,&cfg,3,text,sizeof(text)));
    /* Négy külön állapot, eltérő csomagokban; csak teljes kezdőértékből lesz nappálya. */
    const double solar[4]={132,38.5,420,1077};
    for(int i=0;i<4;i++){
        unsigned char entry[24];numeric(entry,solar[i]);entry[0]+=i+1;char sid[LX_UUID_LEN];lx_uuid_string(entry,sid);strcpy(cfg.map[LX_SUN_FIRST+i].uuid,sid);
        if(i==1){cfg.map[LX_SUN_FIRST+i].scale=0.1;numeric(entry,385);entry[0]+=i+1;}
        assert(lx_values_update(&v,&cfg,2,entry,24));
        payload=lx_values_payload(&v,&cfg);assert(payload);
        if(i<3)assert(!cJSON_GetObjectItemCaseSensitive(payload,"SunPosition"));
        else assert(strcmp(cJSON_GetObjectItemCaseSensitive(payload,"SunPosition")->valuestring,"132#38.5#420#1077")==0);
        assert(!cJSON_GetObjectItemCaseSensitive(payload,"SunAzimuth"));cJSON_Delete(payload);
    }
    /* A Loxone négy száma a valódi, közös feldolgozón át kerül a témák mintájába. */
    const double energy[8]={1,-1,2,2,50,1,0,25};for(int i=0;i<8;i++)v.value[i]=energy[i];
    payload=lx_values_payload(&v,&cfg);char *json=cJSON_PrintUnformatted(payload);app_energy_sample_t sample;char error[128];
    assert(app_payload_parse(json,strlen(json),&sample,error,sizeof(error))==ESP_OK);
    assert(sample.sun_position_valid&&sample.sun_azimuth_deg==132&&sample.sun_altitude_deg==38.5);
    assert(sample.sunrise_minute==420&&sample.sunset_minute==1077);free(json);cJSON_Delete(payload);
    cJSON *config=lx_config_json(&cfg);assert(config);lx_config_t parsed;
    assert(lx_config_parse(config,&parsed,true));assert(strcmp(parsed.map[0].uuid,id)==0);
    assert(strcmp(parsed.map[LX_SUN_FIRST].uuid,cfg.map[LX_SUN_FIRST].uuid)==0);
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(config,"mappings"),"Sunset");
    assert(!lx_config_parse(config,&parsed,true));
    cJSON *maps=cJSON_GetObjectItemCaseSensitive(config,"mappings");
    for(int i=LX_SUN_FIRST;i<LX_FIELD_COUNT;i++)cJSON_DeleteItemFromObjectCaseSensitive(maps,lx_fields[i].name);
    cJSON *legacy=cJSON_CreateObject();cJSON_AddStringToObject(legacy,"uuid",id);cJSON_AddNumberToObject(legacy,"scale",1);cJSON_AddNumberToObject(legacy,"offset",0);cJSON_AddItemToObject(maps,"SunPosition",legacy);
    assert(lx_config_parse(config,&parsed,true));assert(strcmp(parsed.map[0].uuid,id)==0);assert(!parsed.map[LX_SUN_FIRST].uuid[0]);
    assert(!cJSON_GetObjectItemCaseSensitive(config,"password"));cJSON_Delete(config);
    config=cJSON_Parse("{\"host\":\"http://bad/path\",\"port\":80,\"username\":\"x\",\"tls\":false}");
    assert(!lx_config_parse(config,&parsed,false));cJSON_Delete(config);
    const char *structure="{\"rooms\":{\"r\":{\"name\":\"Gépészet\"}},\"cats\":{\"c\":{\"name\":\"Energia\"}},\"controls\":{\"x\":{\"name\":\"PV\",\"type\":\"InfoOnlyAnalog\",\"room\":\"r\",\"cat\":\"c\",\"states\":{\"value\":\"11223344-5566-7788-99aabbccddeeff00\"}}}}";
    cJSON *catalog=lx_catalog_parse(structure,strlen(structure));assert(catalog&&cJSON_GetArraySize(catalog)==1);
    cJSON *item=cJSON_GetArrayItem(catalog,0);assert(strcmp(cJSON_GetObjectItemCaseSensitive(item,"room")->valuestring,"Gépészet")==0);cJSON_Delete(catalog);
    assert(!lx_catalog_parse("{}",2));assert(!lx_catalog_parse(structure,LX_STRUCTURE_MAX+1));
    puts("PASS: Loxone UUID endian, atomic numeric/text tables, missing fields, transforms, four numeric solar states through actual payload parser, configuration and catalog");
}
