#include "app_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <math.h>
#include "cJSON.h"
#include "lwip/ip4_addr.h"

#define CONFIG_FILE_MAX_BYTES (16*1024)

static void set_error(char *d,size_t n,const char *fmt,...){if(!d||!n)return;va_list ap;va_start(ap,fmt);vsnprintf(d,n,fmt,ap);va_end(ap);}
static void copy_text(char *d,size_t n,const char *s){if(!d||!n)return;snprintf(d,n,"%s",s?s:"");}

void app_config_set_defaults(app_config_t *c){
    memset(c,0,sizeof(*c));
    copy_text(c->project.title,sizeof(c->project.title),"Otthoni energia");copy_text(c->project.language,sizeof(c->project.language),"hu");copy_text(c->project.currency,sizeof(c->project.currency),"Ft");
    c->wifi.reconnect_seconds=15;
    copy_text(c->time.ntp_server,sizeof(c->time.ntp_server),"hu.pool.ntp.org");copy_text(c->time.timezone,sizeof(c->time.timezone),"Europe/Budapest");c->time.auto_dst=true;c->time.utc_offset_minutes=60;c->time.startup_retry_seconds=15;c->time.resync_hours=6;c->time.background_retry_minutes=15;
    c->mqtt.port=1883;copy_text(c->mqtt.topic,sizeof(c->mqtt.topic),"loxberry/mqttgateway/energymonitor/state");c->mqtt.data_timeout_seconds=30;c->mqtt.reconnect_seconds=15;
    c->display.day_brightness=80;c->display.night_brightness=20;c->statistics.save_interval_minutes=60;c->statistics.max_integration_gap_seconds=30;
    c->energy.electricity_purchase_price=65;c->energy.feed_in_price=5;c->energy.co2_factor_kg_per_kwh=0.381;c->energy.tree_absorption_kg_per_year=22;
}
static cJSON *obj(cJSON *p,const char *name,bool req,char *e,size_t n){cJSON *i=cJSON_GetObjectItemCaseSensitive(p,name);if(!i){if(req)set_error(e,n,"Hiányzó config szekció: %s",name);return NULL;}if(!cJSON_IsObject(i)){set_error(e,n,"A(z) %s szekciónak objektumnak kell lennie.",name);return NULL;}return i;}
static bool strv(cJSON *o,const char *name,char *d,size_t dn,bool req,bool empty,char *e,size_t en){cJSON *i=cJSON_GetObjectItemCaseSensitive(o,name);if(!i){if(req){set_error(e,en,"Hiányzó config mező: %s",name);return false;}return true;}if(!cJSON_IsString(i)||!i->valuestring){set_error(e,en,"A(z) %s mezőnek szövegnek kell lennie.",name);return false;}size_t n=strlen(i->valuestring);if(!empty&&!n){set_error(e,en,"A(z) %s mező nem lehet üres.",name);return false;}if(n>=dn){set_error(e,en,"A(z) %s mező túl hosszú.",name);return false;}copy_text(d,dn,i->valuestring);return true;}
static bool b01(cJSON *o,const char *name,bool *d,bool req,char *e,size_t en){cJSON *i=cJSON_GetObjectItemCaseSensitive(o,name);if(!i){if(req){set_error(e,en,"Hiányzó config mező: %s",name);return false;}return true;}if(!cJSON_IsNumber(i)||!isfinite(i->valuedouble)||(i->valuedouble!=0&&i->valuedouble!=1)){set_error(e,en,"A(z) %s mező csak 0 vagy 1 lehet.",name);return false;}*d=i->valuedouble==1;return true;}
static bool u32(cJSON *o,const char *name,uint32_t *d,uint32_t min,uint32_t max,bool req,char *e,size_t en){cJSON *i=cJSON_GetObjectItemCaseSensitive(o,name);if(!i){if(req){set_error(e,en,"Hiányzó config mező: %s",name);return false;}return true;}if(!cJSON_IsNumber(i)||!isfinite(i->valuedouble)||floor(i->valuedouble)!=i->valuedouble||i->valuedouble<min||i->valuedouble>max){set_error(e,en,"Érvénytelen egész érték: %s",name);return false;}*d=(uint32_t)i->valuedouble;return true;}
static bool i32(cJSON *o,const char *name,int32_t *d,int32_t min,int32_t max,bool req,char *e,size_t en){cJSON *i=cJSON_GetObjectItemCaseSensitive(o,name);if(!i){if(req){set_error(e,en,"Hiányzó config mező: %s",name);return false;}return true;}if(!cJSON_IsNumber(i)||!isfinite(i->valuedouble)||floor(i->valuedouble)!=i->valuedouble||i->valuedouble<min||i->valuedouble>max){set_error(e,en,"Érvénytelen egész érték: %s",name);return false;}*d=(int32_t)i->valuedouble;return true;}
static bool dbl(cJSON *o,const char *name,double *d,double min,double max,bool req,char *e,size_t en){cJSON *i=cJSON_GetObjectItemCaseSensitive(o,name);if(!i){if(req){set_error(e,en,"Hiányzó config mező: %s",name);return false;}return true;}if(!cJSON_IsNumber(i)||!isfinite(i->valuedouble)||i->valuedouble<min||i->valuedouble>max){set_error(e,en,"Érvénytelen számérték: %s",name);return false;}*d=i->valuedouble;return true;}
static bool ipv4(const char *s){ip4_addr_t a;return s&&*s&&ip4addr_aton(s,&a)!=0;}

static bool parse(cJSON *r,app_config_t *c,char *e,size_t en){
    cJSON *p=obj(r,"project",false,e,en);if(p){if(!strv(p,"title",c->project.title,sizeof(c->project.title),false,false,e,en)||!strv(p,"language",c->project.language,sizeof(c->project.language),false,false,e,en)||!strv(p,"currency",c->project.currency,sizeof(c->project.currency),false,false,e,en))return false;}else if(e&&*e)return false;
    if(strcasecmp(c->project.language,"hu")&&strcasecmp(c->project.language,"en")){set_error(e,en,"A language csak 'hu' vagy 'en' lehet.");return false;}
    cJSON *w=obj(r,"wifi",true,e,en);if(!w)return false;
    if(!strv(w,"ssid",c->wifi.ssid,sizeof(c->wifi.ssid),true,false,e,en)||!strv(w,"password",c->wifi.password,sizeof(c->wifi.password),false,true,e,en)||!u32(w,"reconnect_seconds",&c->wifi.reconnect_seconds,1,3600,false,e,en))return false;
    cJSON *nw=obj(w,"network",false,e,en);if(nw){if(!b01(nw,"use_static_ip",&c->wifi.network.use_static_ip,false,e,en)||!strv(nw,"ip",c->wifi.network.ip,sizeof(c->wifi.network.ip),false,true,e,en)||!strv(nw,"gateway",c->wifi.network.gateway,sizeof(c->wifi.network.gateway),false,true,e,en)||!strv(nw,"subnet",c->wifi.network.subnet,sizeof(c->wifi.network.subnet),false,true,e,en)||!strv(nw,"dns",c->wifi.network.dns,sizeof(c->wifi.network.dns),false,true,e,en))return false;}else if(e&&*e)return false;
    if(c->wifi.network.use_static_ip&&(!ipv4(c->wifi.network.ip)||!ipv4(c->wifi.network.gateway)||!ipv4(c->wifi.network.subnet)||!ipv4(c->wifi.network.dns))){set_error(e,en,"Statikus IP esetén az ip/gateway/subnet/dns mezők kötelező érvényes IPv4 címek.");return false;}
    cJSON *t=obj(r,"time",false,e,en);if(t){if(!strv(t,"ntp_server",c->time.ntp_server,sizeof(c->time.ntp_server),false,false,e,en)||!strv(t,"timezone",c->time.timezone,sizeof(c->time.timezone),false,false,e,en)||!b01(t,"auto_dst",&c->time.auto_dst,false,e,en)||!i32(t,"utc_offset_minutes",&c->time.utc_offset_minutes,-720,840,false,e,en)||!u32(t,"startup_retry_seconds",&c->time.startup_retry_seconds,1,3600,false,e,en)||!u32(t,"resync_hours",&c->time.resync_hours,1,168,false,e,en)||!u32(t,"background_retry_minutes",&c->time.background_retry_minutes,1,1440,false,e,en))return false;}else if(e&&*e)return false;
    cJSON *m=obj(r,"mqtt",true,e,en);if(!m)return false;uint32_t port=c->mqtt.port;
    if(!strv(m,"broker",c->mqtt.broker,sizeof(c->mqtt.broker),true,false,e,en)||!u32(m,"port",&port,1,65535,false,e,en)||!strv(m,"topic",c->mqtt.topic,sizeof(c->mqtt.topic),false,false,e,en)||!strv(m,"username",c->mqtt.username,sizeof(c->mqtt.username),false,true,e,en)||!strv(m,"password",c->mqtt.password,sizeof(c->mqtt.password),false,true,e,en)||!u32(m,"data_timeout_seconds",&c->mqtt.data_timeout_seconds,5,3600,false,e,en)||!u32(m,"reconnect_seconds",&c->mqtt.reconnect_seconds,1,3600,false,e,en))return false;c->mqtt.port=(uint16_t)port;
    cJSON *d=obj(r,"display",false,e,en);if(d){uint32_t day=c->display.day_brightness,night=c->display.night_brightness;if(!u32(d,"day_brightness",&day,1,100,false,e,en)||!u32(d,"night_brightness",&night,1,100,false,e,en))return false;c->display.day_brightness=(uint8_t)day;c->display.night_brightness=(uint8_t)night;}else if(e&&*e)return false;
    cJSON *s=obj(r,"statistics",false,e,en);if(s){if(!u32(s,"save_interval_minutes",&c->statistics.save_interval_minutes,5,1440,false,e,en)||!u32(s,"max_integration_gap_seconds",&c->statistics.max_integration_gap_seconds,1,300,false,e,en))return false;}else if(e&&*e)return false;
    cJSON *g=obj(r,"energy",false,e,en);if(g){if(!dbl(g,"electricity_purchase_price",&c->energy.electricity_purchase_price,0,100000,false,e,en)||!dbl(g,"feed_in_price",&c->energy.feed_in_price,0,100000,false,e,en)||!dbl(g,"co2_factor_kg_per_kwh",&c->energy.co2_factor_kg_per_kwh,0,100,false,e,en)||!dbl(g,"tree_absorption_kg_per_year",&c->energy.tree_absorption_kg_per_year,0.001,100000,false,e,en))return false;}else if(e&&*e)return false;
    cJSON *o=obj(r,"ota",false,e,en);if(o){if(!strv(o,"username",c->ota.username,sizeof(c->ota.username),false,true,e,en)||!strv(o,"password",c->ota.password,sizeof(c->ota.password),false,true,e,en))return false;if((c->ota.username[0]==0)!=(c->ota.password[0]==0)){set_error(e,en,"Az OTA felhasználónév és jelszó vagy mindkettő legyen kitöltve, vagy mindkettő maradjon üres.");return false;}}else if(e&&*e)return false;
    return true;
}

esp_err_t app_config_load_file(const char *path,app_config_t *c,char *e,size_t en){
    if(!path||!c){set_error(e,en,"Érvénytelen config paraméter.");return ESP_ERR_INVALID_ARG;}if(e&&en)e[0]=0;app_config_set_defaults(c);
    FILE *f=fopen(path,"rb");if(!f){set_error(e,en,"Nem található vagy nem olvasható: %s",path);return ESP_ERR_NOT_FOUND;}
    if(fseek(f,0,SEEK_END)){fclose(f);set_error(e,en,"Nem olvasható a config fájl mérete.");return ESP_FAIL;}long sz=ftell(f);if(sz<=0||sz>CONFIG_FILE_MAX_BYTES){fclose(f);set_error(e,en,"A config.json mérete érvénytelen (max %d byte).",CONFIG_FILE_MAX_BYTES);return ESP_ERR_INVALID_SIZE;}rewind(f);
    char *b=calloc(1,(size_t)sz+1);if(!b){fclose(f);set_error(e,en,"Nincs elég memória a config betöltéséhez.");return ESP_ERR_NO_MEM;}size_t n=fread(b,1,(size_t)sz,f);fclose(f);if(n!=(size_t)sz){free(b);set_error(e,en,"A config.json nem olvasható végig.");return ESP_FAIL;}
    cJSON *r=cJSON_Parse(b);free(b);if(!r||!cJSON_IsObject(r)){cJSON_Delete(r);set_error(e,en,"A config.json nem érvényes JSON objektum.");return ESP_ERR_INVALID_ARG;}bool ok=parse(r,c,e,en);cJSON_Delete(r);return ok?ESP_OK:ESP_ERR_INVALID_ARG;
}
