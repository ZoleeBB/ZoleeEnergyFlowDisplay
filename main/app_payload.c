#include "app_payload.h"
#include <math.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>
#include "cJSON.h"
#include "esp_timer.h"

#define REQUIRED_FIELD_COUNT 12
typedef enum { F_POWER_GRID=0,F_POWER_BAT,F_POWER_PV,F_SOC_BAT,F_POWER_FULL,F_TEMPERATURE,F_WIND_SPEED,F_HAS_SUN,F_HAS_PRECIP,F_STATUS,F_NIGHT_MODE,F_BAT_TEMP } field_id_t;
static const char *s_names[REQUIRED_FIELD_COUNT]={"PowerGrid","PowerBat","PowerPV","SocBat","PowerFull","Temperature","WindSpeed","HasSunRadiation","HasPrecipitation","Status","NightMode","BatteryTemperature"};

static int field_id_for_name(const char *name){ if(!name)return -1; for(int i=0;i<REQUIRED_FIELD_COUNT;i++)if(strcasecmp(name,s_names[i])==0)return i; return -1; }
static void fail(char *e,size_t n,const char *msg,const char *field){ if(!e||!n)return; if(field)snprintf(e,n,"%s: %s",msg,field); else snprintf(e,n,"%s",msg); }
static bool num(cJSON *i,double min,double max,double *o){ if(!cJSON_IsNumber(i)||!isfinite(i->valuedouble)||i->valuedouble<min||i->valuedouble>max)return false; *o=i->valuedouble; return true; }
static bool integer(cJSON *i,int min,int max,int *o){ if(!cJSON_IsNumber(i)||!isfinite(i->valuedouble)||floor(i->valuedouble)!=i->valuedouble||i->valuedouble<min||i->valuedouble>max)return false; *o=(int)i->valuedouble; return true; }

esp_err_t app_payload_parse(const char *json,size_t len,app_energy_sample_t *out,char *error,size_t error_len){
    if(error&&error_len)error[0]=0;
    if(!json||!len||!out){fail(error,error_len,"Üres vagy érvénytelen MQTT payload",NULL);return ESP_ERR_INVALID_ARG;}
    cJSON *root=cJSON_ParseWithLength(json,len);
    if(!root||!cJSON_IsObject(root)){cJSON_Delete(root);fail(error,error_len,"Az MQTT payload nem érvényes JSON objektum",NULL);return ESP_ERR_INVALID_ARG;}
    cJSON *fields[REQUIRED_FIELD_COUNT]={0},*it=NULL;
    cJSON_ArrayForEach(it,root){
        int id=field_id_for_name(it->string); if(id<0)continue;
        if(fields[id]){fail(error,error_len,"Duplikált mező (kis-/nagybetű eltérés sem megengedett)",s_names[id]);cJSON_Delete(root);return ESP_ERR_INVALID_ARG;}
        fields[id]=it;
    }
    for(int i=0;i<REQUIRED_FIELD_COUNT;i++)if(!fields[i]){fail(error,error_len,"Hiányzó kötelező mező",s_names[i]);cJSON_Delete(root);return ESP_ERR_INVALID_ARG;}
    app_energy_sample_t s={0};
    if(!num(fields[F_POWER_GRID],-15,15,&s.power_grid_kw)){fail(error,error_len,"Érvénytelen tartomány vagy típus",s_names[F_POWER_GRID]);goto bad;}
    if(!num(fields[F_POWER_BAT],-15,15,&s.power_bat_kw)){fail(error,error_len,"Érvénytelen tartomány vagy típus",s_names[F_POWER_BAT]);goto bad;}
    if(!num(fields[F_POWER_PV],-0.05,15,&s.power_pv_kw)){fail(error,error_len,"Érvénytelen tartomány vagy típus",s_names[F_POWER_PV]);goto bad;}
    if(!num(fields[F_SOC_BAT],0,100,&s.soc_bat_pct)){fail(error,error_len,"Érvénytelen tartomány vagy típus",s_names[F_SOC_BAT]);goto bad;}
    if(!num(fields[F_POWER_FULL],-0.05,15,&s.power_full_kw)){fail(error,error_len,"Érvénytelen tartomány vagy típus",s_names[F_POWER_FULL]);goto bad;}
    if(!num(fields[F_TEMPERATURE],-35,50,&s.temperature_c)){fail(error,error_len,"Érvénytelen tartomány vagy típus",s_names[F_TEMPERATURE]);goto bad;}
    if(!num(fields[F_WIND_SPEED],0,150,&s.wind_speed_kmh)){fail(error,error_len,"Érvénytelen tartomány vagy típus",s_names[F_WIND_SPEED]);goto bad;}
    if(!num(fields[F_BAT_TEMP],-20,80,&s.battery_temperature_c)){fail(error,error_len,"Érvénytelen tartomány vagy típus",s_names[F_BAT_TEMP]);goto bad;}
    int v;
    if(!integer(fields[F_HAS_SUN],0,1,&v)){fail(error,error_len,"Csak numerikus 0 vagy 1 megengedett",s_names[F_HAS_SUN]);goto bad;} s.has_sun_radiation=v==1;
    if(!integer(fields[F_HAS_PRECIP],0,1,&v)){fail(error,error_len,"Csak numerikus 0 vagy 1 megengedett",s_names[F_HAS_PRECIP]);goto bad;} s.has_precipitation=v==1;
    if(!integer(fields[F_STATUS],1,4,&s.status)){fail(error,error_len,"Csak 1..4 megengedett",s_names[F_STATUS]);goto bad;}
    if(!integer(fields[F_NIGHT_MODE],0,1,&v)){fail(error,error_len,"Csak numerikus 0 vagy 1 megengedett",s_names[F_NIGHT_MODE]);goto bad;} s.night_mode=v==1;
    if(s.power_pv_kw<0)s.power_pv_kw=0; if(s.power_full_kw<0)s.power_full_kw=0;
    s.received_monotonic_us=esp_timer_get_time(); s.received_epoch_s=(int64_t)time(NULL); *out=s; cJSON_Delete(root); return ESP_OK;
bad: cJSON_Delete(root); return ESP_ERR_INVALID_ARG;
}
