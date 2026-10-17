#include "app_loxone_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    unsigned char text[128]={0};memcpy(text,uuid,16);const char *sun="132#38.5#420#1077";
    size_t len=strlen(sun);text[32]=len;memcpy(text+36,sun,len);strcpy(cfg.map[12].uuid,id);
    assert(lx_values_update(&v,&cfg,3,text,36+((len+3)&~3U)));assert(strcmp(v.sun_position,sun)==0);
    assert(!lx_values_update(&v,&cfg,3,text,36+len-1));text[32]=255;assert(!lx_values_update(&v,&cfg,3,text,sizeof(text)));
    cJSON *config=lx_config_json(&cfg);assert(config);lx_config_t parsed;
    assert(lx_config_parse(config,&parsed,true));assert(strcmp(parsed.map[0].uuid,id)==0);
    assert(!cJSON_GetObjectItemCaseSensitive(config,"password"));cJSON_Delete(config);
    config=cJSON_Parse("{\"host\":\"http://bad/path\",\"port\":80,\"username\":\"x\",\"tls\":false}");
    assert(!lx_config_parse(config,&parsed,false));cJSON_Delete(config);
    const char *structure="{\"rooms\":{\"r\":{\"name\":\"Gépészet\"}},\"cats\":{\"c\":{\"name\":\"Energia\"}},\"controls\":{\"x\":{\"name\":\"PV\",\"type\":\"InfoOnlyAnalog\",\"room\":\"r\",\"cat\":\"c\",\"states\":{\"value\":\"11223344-5566-7788-99aabbccddeeff00\"}}}}";
    cJSON *catalog=lx_catalog_parse(structure,strlen(structure));assert(catalog&&cJSON_GetArraySize(catalog)==1);
    cJSON *item=cJSON_GetArrayItem(catalog,0);assert(strcmp(cJSON_GetObjectItemCaseSensitive(item,"room")->valuestring,"Gépészet")==0);cJSON_Delete(catalog);
    assert(!lx_catalog_parse("{}",2));assert(!lx_catalog_parse(structure,LX_STRUCTURE_MAX+1));
    puts("PASS: Loxone UUID endian, atomic numeric/text tables, missing fields, transforms, configuration and catalog");
}
