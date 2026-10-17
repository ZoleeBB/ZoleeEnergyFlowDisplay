/* A Loxone protokoll platformfüggetlen adat- és párosításkezelése. */
#include "app_loxone_model.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const lx_field_t lx_fields[LX_FIELD_COUNT] = {
    {"PowerGrid",true,false,false}, {"PowerBat",true,false,false},
    {"PowerPV",true,false,false}, {"PowerFull",true,false,false},
    {"SocBat",true,false,false}, {"Status",true,false,false},
    {"NightMode",true,true,false}, {"BatteryTemperature",true,false,false},
    {"Temperature",false,false,false}, {"WindSpeed",false,false,false},
    {"HasSunRadiation",false,true,false}, {"HasPrecipitation",false,true,false},
    {"SunPosition",false,false,true}
};
static const char *str(const cJSON *o,const char *key)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    return cJSON_IsString(v)?v->valuestring:"";
}
static bool copy(char *out,size_t size,const char *in)
{
    if(strlen(in)>=size)return false;
    memcpy(out,in,strlen(in)+1);return true;
}
void lx_config_defaults(lx_config_t *cfg)
{
    memset(cfg,0,sizeof(*cfg));cfg->port=80;
    for(int i=0;i<LX_FIELD_COUNT;i++)cfg->map[i].scale=1;
}
bool lx_uuid_valid(const char *u)
{
    if(!u||strlen(u)!=35)return false;
    for(int i=0;i<35;i++) {
        if(i==8||i==13||i==18) {if(u[i]!='-')return false;}
        else if(!isxdigit((unsigned char)u[i]))return false;
    }
    return true;
}
bool lx_config_parse(const cJSON *root,lx_config_t *cfg,bool require_mappings)
{
    if(!cJSON_IsObject(root))return false;
    lx_config_defaults(cfg);
    if(!copy(cfg->host,sizeof(cfg->host),str(root,"host"))||!cfg->host[0]||
       !copy(cfg->username,sizeof(cfg->username),str(root,"username"))||!cfg->username[0]||
       !copy(cfg->ca_pem,sizeof(cfg->ca_pem),str(root,"ca_pem")))return false;
    /* Csak gépnév/IPv4, URL és fejléc-befecskendezés nélkül. */
    for(const char *p=cfg->host;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='.'&&*p!='-')return false;
    const cJSON *port=cJSON_GetObjectItemCaseSensitive(root,"port");
    if(!cJSON_IsNumber(port)||port->valuedouble!=floor(port->valuedouble)||port->valueint<1||port->valueint>65535)return false;
    cfg->port=port->valueint;
    const cJSON *tls=cJSON_GetObjectItemCaseSensitive(root,"tls");
    if(!cJSON_IsBool(tls))return false;
    cfg->tls=cJSON_IsTrue(tls);
    if(cfg->tls&&!strstr(cfg->ca_pem,"-----BEGIN CERTIFICATE-----"))return false;
    const cJSON *maps=cJSON_GetObjectItemCaseSensitive(root,"mappings");
    if(maps&&!cJSON_IsObject(maps))return false;
    for(int i=0;i<LX_FIELD_COUNT;i++) {
        const cJSON *m=cJSON_GetObjectItemCaseSensitive(maps,lx_fields[i].name);
        if(!m) {if(require_mappings&&lx_fields[i].required)return false;continue;}
        if(!cJSON_IsObject(m)||!copy(cfg->map[i].uuid,LX_UUID_LEN,str(m,"uuid")))return false;
        if(!cfg->map[i].uuid[0]) {if(require_mappings&&lx_fields[i].required)return false;continue;}
        if(!lx_uuid_valid(cfg->map[i].uuid))return false;
        for(char *p=cfg->map[i].uuid;*p;p++)*p=tolower((unsigned char)*p);
        const cJSON *scale=cJSON_GetObjectItemCaseSensitive(m,"scale"),*offset=cJSON_GetObjectItemCaseSensitive(m,"offset");
        if(!cJSON_IsNumber(scale)||!cJSON_IsNumber(offset)||!isfinite(scale->valuedouble)||!isfinite(offset->valuedouble)||fabs(scale->valuedouble)>1000000||fabs(offset->valuedouble)>1000000)return false;
        cfg->map[i].scale=scale->valuedouble;cfg->map[i].offset=offset->valuedouble;
    }
    return true;
}
cJSON *lx_config_json(const lx_config_t *cfg)
{
    cJSON *root=cJSON_CreateObject(),*maps=cJSON_CreateObject();
    if(!root||!maps){cJSON_Delete(root);cJSON_Delete(maps);return NULL;}
    if(!cJSON_AddStringToObject(root,"host",cfg->host)||!cJSON_AddNumberToObject(root,"port",cfg->port)||
       !cJSON_AddBoolToObject(root,"tls",cfg->tls)||!cJSON_AddStringToObject(root,"ca_pem",cfg->ca_pem)||
       !cJSON_AddStringToObject(root,"username",cfg->username)||!cJSON_AddItemToObject(root,"mappings",maps)) {
        cJSON_Delete(root);cJSON_Delete(maps);return NULL;
    }
    for(int i=0;i<LX_FIELD_COUNT;i++) {
        cJSON *m=cJSON_CreateObject();if(!m){cJSON_Delete(root);return NULL;}
        if(!cJSON_AddStringToObject(m,"uuid",cfg->map[i].uuid)||
           !cJSON_AddNumberToObject(m,"scale",cfg->map[i].scale)||!cJSON_AddNumberToObject(m,"offset",cfg->map[i].offset)||
           !cJSON_AddItemToObject(maps,lx_fields[i].name,m)) {cJSON_Delete(m);cJSON_Delete(root);return NULL;}
    }
    return root;
}
static uint32_t le32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
void lx_uuid_string(const uint8_t *p,char out[LX_UUID_LEN])
{
    snprintf(out,LX_UUID_LEN,"%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x",
        (unsigned)le32(p),(unsigned)(p[4]|p[5]<<8),(unsigned)(p[6]|p[7]<<8),
        p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
}
bool lx_values_update(lx_values_t *v,const lx_config_t *cfg,int type,const uint8_t *data,size_t len)
{
    if(type!=2&&type!=3)return true;
    /* Előbb teljes formaellenőrzés: hibás csomag nem írhat részleges mintát. */
    if(type==2&&len%24)return false;
    if(type==3)for(size_t p=0;p<len;) {
        if(len-p<36)return false;
        uint32_t n=le32(data+p+32);size_t padded=((size_t)n+3)&~(size_t)3;
        if(padded>len-p-36)return false;
        p+=36+padded;
    }
    lx_values_t *target=v,next=*v;v=&next;
    for(size_t p=0;p<len;) {
        char uuid[LX_UUID_LEN];lx_uuid_string(data+p,uuid);
        size_t n=type==2?0:le32(data+p+32);
        for(int i=0;i<LX_FIELD_COUNT;i++)if(strcmp(uuid,cfg->map[i].uuid)==0) {
            if(type==2&&!lx_fields[i].text) {
                uint64_t bits=0;for(int j=0;j<8;j++)bits|=(uint64_t)data[p+16+j]<<(8*j);
                double raw;memcpy(&raw,&bits,8);
                double value=raw*cfg->map[i].scale+cfg->map[i].offset;
                if(!isfinite(value))return false;
                v->value[i]=value;v->seen[i]=true;
            }else if(type==3&&lx_fields[i].text) {
                if(n>=sizeof(v->sun_position)||memchr(data+p+36,0,n))return false;
                memcpy(v->sun_position,data+p+36,n);v->sun_position[n]=0;v->seen[i]=true;
            }
        }
        p+=type==2?24:36+((n+3)&~(size_t)3);
    }
    *target=next;return true;
}
cJSON *lx_values_payload(const lx_values_t *v,const lx_config_t *cfg)
{
    for(int i=0;i<LX_FIELD_COUNT;i++)if(lx_fields[i].required&&!v->seen[i])return NULL;
    cJSON *root=cJSON_CreateObject();if(!root)return NULL;
    for(int i=0;i<LX_FIELD_COUNT;i++) {
        const lx_field_t *f=&lx_fields[i];
        if(f->text) {if(v->seen[i])cJSON_AddStringToObject(root,f->name,v->sun_position);}
        else if(!cfg->map[i].uuid[0]||!v->seen[i])cJSON_AddNullToObject(root,f->name);
        else cJSON_AddNumberToObject(root,f->name,v->value[i]);
    }
    return root;
}
static bool catalog_controls(cJSON *out,const cJSON *controls,const cJSON *rooms,const cJSON *cats,int depth)
{
    if(depth>8)return false;
    const cJSON *control;
    cJSON_ArrayForEach(control,controls) {
        const char *type=str(control,"type");
        const cJSON *state;
        cJSON_ArrayForEach(state,cJSON_GetObjectItemCaseSensitive(control,"states")) {
            if(!cJSON_IsString(state)||!lx_uuid_valid(state->valuestring))continue;
            if(cJSON_GetArraySize(out)>=LX_CATALOG_MAX)return false;
            cJSON *item=cJSON_CreateObject();if(!item)return false;
            cJSON_AddStringToObject(item,"uuid",state->valuestring);
            cJSON_AddStringToObject(item,"name",str(control,"name"));
            cJSON_AddStringToObject(item,"type",type);
            cJSON_AddStringToObject(item,"state",state->string?state->string:"");
            cJSON_AddStringToObject(item,"room",str(cJSON_GetObjectItemCaseSensitive(rooms,str(control,"room")),"name"));
            cJSON_AddStringToObject(item,"category",str(cJSON_GetObjectItemCaseSensitive(cats,str(control,"cat")),"name"));
            cJSON_AddItemToArray(out,item);
        }
        const cJSON *sub=cJSON_GetObjectItemCaseSensitive(control,"subControls");
        if(sub&&!catalog_controls(out,sub,rooms,cats,depth+1))return false;
    }
    return true;
}
cJSON *lx_catalog_parse(const char *json,size_t length)
{
    if(!json||!length||length>LX_STRUCTURE_MAX)return NULL;
    cJSON *root=cJSON_ParseWithLength(json,length),*out=cJSON_CreateArray();
    const cJSON *controls=cJSON_GetObjectItemCaseSensitive(root,"controls");
    if(!root||!out||!cJSON_IsObject(controls)||!catalog_controls(out,controls,
        cJSON_GetObjectItemCaseSensitive(root,"rooms"),cJSON_GetObjectItemCaseSensitive(root,"cats"),0)) {
        cJSON_Delete(root);cJSON_Delete(out);return NULL;
    }
    cJSON_Delete(root);return out;
}
