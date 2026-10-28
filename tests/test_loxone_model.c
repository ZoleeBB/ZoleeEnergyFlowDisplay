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
    assert(lx_token_refresh_delay_us(0,1700000000)==3600000000LL);
    assert(lx_token_refresh_delay_us(1700000000-1230768000+600,1700000000)==300000000LL);
    assert(lx_token_refresh_delay_us(1700000000-1230768000-1,1700000000)==1000000LL);
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
    const double energy[8]={1,-1,2,2,50,5,0,25};for(int i=0;i<8;i++)v.value[i]=energy[i];
    payload=lx_values_payload(&v,&cfg);char *json=cJSON_PrintUnformatted(payload);app_energy_sample_t sample;char error[128];
    assert(app_payload_parse(json,strlen(json),&sample,error,sizeof(error))==ESP_OK);
    assert(sample.status == 5);
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
    /* Jelenlét és postaláda: opcionális kijelzőjel, tiltott energiaáramlás mellett is
     * frissül, a közös energiamintába viszont nem kerül be. */
    {
        lx_config_t aux;lx_config_defaults(&aux);lx_values_t av={0};
        snprintf(aux.host,sizeof(aux.host),"192.168.2.1");strcpy(aux.username,"display");
        unsigned char presence[24],mail[24];numeric(presence,1);numeric(mail,0);mail[0]^=0x55;
        char presence_id[LX_UUID_LEN],mail_id[LX_UUID_LEN];
        lx_uuid_string(presence,presence_id);lx_uuid_string(mail,mail_id);
        strcpy(aux.map[LX_FIELD_PRESENCE].uuid,presence_id);strcpy(aux.map[LX_FIELD_MAIL].uuid,mail_id);
        aux.energy_flow_disabled=true;
        unsigned char both[48];memcpy(both,presence,24);memcpy(both+24,mail,24);
        assert(lx_values_update(&av,&aux,2,both,48));
        assert(av.seen[LX_FIELD_PRESENCE]&&av.value[LX_FIELD_PRESENCE]==1);
        assert(av.seen[LX_FIELD_MAIL]&&av.value[LX_FIELD_MAIL]==0);
        assert(!strcmp(lx_fields[LX_FIELD_PRESENCE].name,"Presence")&&!lx_fields[LX_FIELD_PRESENCE].required);
        assert(!strcmp(lx_fields[LX_FIELD_MAIL].name,"MailArrived")&&!lx_fields[LX_FIELD_MAIL].required);
        assert(lx_field_enabled(true,LX_FIELD_PRESENCE)&&lx_field_enabled(true,6)&&!lx_field_enabled(true,0));
        for(int i=0;i<8;i++)strcpy(aux.map[i].uuid,id);
        aux.energy_flow_disabled=false;
        lx_values_t full=v;full.seen[LX_FIELD_PRESENCE]=full.seen[LX_FIELD_MAIL]=true;
        cJSON *json=lx_values_payload(&full,&aux);assert(json);
        assert(!cJSON_GetObjectItemCaseSensitive(json,"Presence")&&!cJSON_GetObjectItemCaseSensitive(json,"MailArrived"));
        cJSON_Delete(json);
        /* Mentés/visszaolvasás: a két új párosítás megmarad, régi fájlnál hiányozhat. */
        cJSON *saved=lx_config_json(&aux);lx_config_t back;
        assert(saved&&lx_config_parse(saved,&back,true));
        assert(!strcmp(back.map[LX_FIELD_PRESENCE].uuid,presence_id)&&!strcmp(back.map[LX_FIELD_MAIL].uuid,mail_id));
        cJSON *maps=cJSON_GetObjectItemCaseSensitive(saved,"mappings");
        cJSON_DeleteItemFromObjectCaseSensitive(maps,"Presence");cJSON_DeleteItemFromObjectCaseSensitive(maps,"MailArrived");
        assert(lx_config_parse(saved,&back,true)&&!back.map[LX_FIELD_PRESENCE].uuid[0]);
        cJSON_Delete(saved);
    }
    puts("PASS: Loxone presence/mail display signals; Loxone UUID endian, atomic numeric/text tables, missing fields, transforms, four numeric solar states through actual payload parser, configuration and catalog");
}
