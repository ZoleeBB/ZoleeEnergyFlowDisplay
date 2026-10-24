/* Korlátos, egy kapcsolatos Loxone kliens. A UI és HTTP feladat nem vár hálózatra. */
#include "app_loxone.h"
#include "app_intercom.h"
#include "app_intercom_audio.h"
#ifdef APP_INTERCOM_DISTRIBUTED
#include "app_intercom_control.h"
#define intercom_held(revision) app_intercom_control_held(revision)
#else
#define intercom_held(revision) app_intercom_audio_active_for(revision)
#endif
#include "app_loxone_model.h"
#include "app_loxone_store.h"
#include "app_loxone_home_store.h"
#include "app_slave_presence_service.h"
#include "app_ingest.h"
#include "app_state.h"
#include "app_stats.h"
#include "app_wifi.h"
#include "esp_websocket_client.h"
#include "esp_random.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include <math.h>
#include <stdatomic.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LX_TIMEOUT_MS 12000
#define LX_QUEUE_DEPTH 8
#define LX_KEEPALIVE_US 15000000LL
#define LX_MAX_RX (LX_STRUCTURE_MAX + 1024U)
typedef struct { int type; size_t length; unsigned char *data; } message_t;
typedef struct {
    lx_config_t config;
    bool save;
    lx_home_config_t *home;
} request_t;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_messages,s_requests,s_home_commands;
static lx_config_t s_saved;
/* Az új profil nem duzzasztja a hitelesítési konfiguráció másolatait. */
static lx_home_config_t *s_home;
static bool s_home_busy;
static unsigned s_home_revision;
#ifdef ESP_PLATFORM
#include "esp_attr.h"
static EXT_RAM_BSS_ATTR lx_home_values_t s_home_values;
#else
static lx_home_values_t s_home_values;
#endif
static bool s_home_live, s_command_pending;
/* Kapcsolatvesztés nem 0 állapot: a már megnyitott kaputelefon megmarad. */
static bool s_intercom_ringing;
static unsigned s_intercom_generation;
static bool s_presence_session_live;
static unsigned s_home_session;
static int64_t s_command_deadline, s_home_response;
static char s_command_status[LX_HOME_ERROR_SIZE];
typedef struct {
    char id[LX_HOME_ID_SIZE];
    lx_home_command_t action;
    unsigned revision, session, intercom_revision;
    int64_t queued_us;
} home_command_t;
static home_command_t s_pending_command;
static bool update_values(int type, const uint8_t *data, size_t size);
static void home_command_poll(int64_t now);
static char s_home_status[LX_HOME_ERROR_SIZE] = "";
static cJSON *s_catalog;
static char s_status[192]="A webfelületen add meg a Loxone beállításait.";
static bool s_busy,s_password;
static bool s_energy_disabled;
/* A hiba csak lépésnevet és kódot tartalmazhat, parancsot/tokent soha. */
static char s_stage[80],s_failure[192];
static esp_websocket_client_handle_t s_ws;
/* A fogadópuffert kizárólag a websocket feladat éri el, destroy után a worker szabadítja. */
static unsigned char *s_rx;
static size_t s_rx_len,s_rx_cap;
static bool s_rx_bad;
static int s_message_type=-1;
static atomic_bool s_dead;
/* A fogadó callback csak hibakódot ír; a felhasználói szöveg a workerben készül. */
static atomic_int s_rx_failure;
enum { LX_RX_OK, LX_RX_TOO_LARGE, LX_RX_NO_MEMORY, LX_RX_BAD_HEADER, LX_RX_QUEUE_FULL };
static unsigned char s_aes_key[32],s_iv[16];
static char s_salt[17];
static char *s_public_key;
static lx_values_t s_values;
static const lx_config_t *s_session_config;
static int64_t s_last_response,s_token_refresh_us;

static void status(const char *message)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);snprintf(s_status,sizeof(s_status),"%s",message);xSemaphoreGive(s_lock);
}
static void phase(const char *label)
{
    snprintf(s_stage,sizeof(s_stage),"%s",label);s_failure[0]=0;status(label);
}
static bool failed(const char *reason,int code)
{
    if(code)snprintf(s_failure,sizeof(s_failure),"%.70s: %.85s (%d)",s_stage,reason,code);
    else snprintf(s_failure,sizeof(s_failure),"%.70s: %.110s",s_stage,reason);
    return false;
}
static void report_failure(const char *fallback)
{status(s_failure[0]?s_failure:fallback);}
static const char *string(const cJSON *o,const char *k)
{const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:"";}
static void hex(const unsigned char *in,size_t n,char *out)
{static const char h[]="0123456789abcdef";for(size_t i=0;i<n;i++){out[2*i]=h[in[i]>>4];out[2*i+1]=h[in[i]&15];}out[2*n]=0;}
static bool unhex(const char *in,unsigned char *out,size_t max,size_t *n)
{
    size_t len=strlen(in);if(!len||len%2||len/2>max)return false;
    for(size_t i=0;i<len;i++)if(!strchr("0123456789abcdefABCDEF",in[i]))return false;
    for(size_t i=0;i<len/2;i++){unsigned v;if(sscanf(in+2*i,"%2x",&v)!=1)return false;out[i]=v;}
    *n=len/2;return true;
}
static char *urlencode(const char *in)
{
    size_t n=strlen(in);char *out=malloc(n*3+1);if(!out)return NULL;
    char *p=out;for(size_t i=0;i<n;i++) {
        unsigned char c=in[i];
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||strchr("-_.~",c))*p++=c;
        else {snprintf(p,4,"%%%02X",c);p+=3;}
    }*p=0;return out;
}
static void free_message(message_t *m){free(m->data);m->data=NULL;}
static void receive_event(void *arg,esp_event_base_t base,int32_t event,void *data)
{
    (void)arg;(void)base;
    if(event==WEBSOCKET_EVENT_DISCONNECTED||event==WEBSOCKET_EVENT_ERROR){s_dead=true;return;}
    if(event!=WEBSOCKET_EVENT_DATA||s_dead)return;
    esp_websocket_event_data_t *e=data;
    if(e->op_code==8){s_dead=true;return;}
    if(e->op_code!=0&&e->op_code!=1&&e->op_code!=2)return;
    if(e->data_len<0||e->payload_offset<0||e->payload_len<0){s_dead=true;return;}
    if(e->payload_offset==0&&e->op_code!=0) {s_rx_len=0;s_rx_bad=false;}
    if((size_t)e->data_len>LX_MAX_RX-s_rx_len){s_rx_failure=LX_RX_TOO_LARGE;s_rx_bad=true;s_dead=true;return;}
    size_t need=s_rx_len+e->data_len+1;
    if(need>s_rx_cap) {
        size_t cap=s_rx_cap?s_rx_cap:4096;
        while(cap<need){if(cap>(LX_MAX_RX+1)/2){cap=LX_MAX_RX+1;break;}cap*=2;}
        unsigned char *next=realloc(s_rx,cap);
        if(!next){s_rx_failure=LX_RX_NO_MEMORY;s_rx_bad=true;s_dead=true;return;}
        s_rx=next;s_rx_cap=cap;
    }
    memcpy(s_rx+s_rx_len,e->data_ptr,e->data_len);s_rx_len+=e->data_len;s_rx[s_rx_len]=0;
    if(e->payload_offset+e->data_len!=e->payload_len||!e->fin)return;
    if(s_rx_bad)return;
    /* A Loxone fejléc önálló WS-üzenet, az utána következő payload külön üzenet. */
    if(s_message_type<0) {
        if(s_rx_len!=8||s_rx[0]!=3){s_rx_failure=LX_RX_BAD_HEADER;s_dead=true;return;}
        /* A 0x80-as estimated fejléc után még PONTOS fejléc jön, nem payload.
         * A becslésből nem foglalunk memóriát és nem következtetünk üzenettípusra. */
        if(s_rx[2]&0x80){s_rx_len=0;return;}
        unsigned declared=(unsigned)s_rx[4]|(unsigned)s_rx[5]<<8|(unsigned)s_rx[6]<<16|(unsigned)s_rx[7]<<24;
        if(declared>LX_MAX_RX){s_rx_failure=LX_RX_TOO_LARGE;s_dead=true;return;}
        int type=s_rx[1];
        if(type==6) {
            message_t m={.type=6};if(xQueueSend(s_messages,&m,0)!=pdTRUE){s_rx_failure=LX_RX_QUEUE_FULL;s_dead=true;}
        }else if(type==5) {s_dead=true;return;}
        else s_message_type=type;
        s_rx_len=0;return;
    }
    message_t m={.type=s_message_type,.length=s_rx_len,.data=s_rx};s_message_type=-1;
    s_rx=NULL;s_rx_len=0;s_rx_cap=0;
    if(xQueueSend(s_messages,&m,0)!=pdTRUE){free_message(&m);s_rx_failure=LX_RX_QUEUE_FULL;s_dead=true;}
}
static void mark_response(void)
{
    s_last_response = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_home_response = s_last_response;
    xSemaphoreGive(s_lock);
    if (s_presence_session_live)
        app_slave_presence_service_online(true);
}
static bool send_plain(const char *command)
{return !s_dead&&esp_websocket_client_send_text(s_ws,command,strlen(command),pdMS_TO_TICKS(LX_TIMEOUT_MS))==(int)strlen(command);}
static bool send_encrypted(const char *command)
{
    unsigned char salt[8],iv[16];char sh[17];esp_fill_random(salt,sizeof(salt));hex(salt,sizeof(salt),sh);
    size_t n=strlen(command)+strlen(sh)+strlen(s_salt)+16,padded=(n+15)&~(size_t)15;
    unsigned char *input=calloc(1,padded+1),*cipher=malloc(padded),*base=malloc(padded*2+8);
    if(!input||!cipher||!base){free(input);free(cipher);free(base);return false;}
    int actual=s_salt[0]?snprintf((char*)input,padded+1,"nextSalt/%s/%s/%s",s_salt,sh,command):
        snprintf((char*)input,padded+1,"salt/%s/%s",sh,command);
    bool ok=actual>0&&(size_t)actual<=padded;size_t b64=0;mbedtls_aes_context aes;mbedtls_aes_init(&aes);
    memcpy(iv,s_iv,sizeof(iv));
    if(ok)ok=mbedtls_aes_setkey_enc(&aes,s_aes_key,256)==0&&
        mbedtls_aes_crypt_cbc(&aes,MBEDTLS_AES_ENCRYPT,padded,iv,input,cipher)==0&&
        mbedtls_base64_encode(base,padded*2+8,&b64,cipher,padded)==0;
    char *escaped=NULL,*wrapped=NULL;
    if(ok){base[b64]=0;escaped=urlencode((char*)base);if(escaped){wrapped=malloc(strlen(escaped)+16);if(wrapped)sprintf(wrapped,"jdev/sys/enc/%s",escaped);}ok=wrapped&&send_plain(wrapped);}
    if(ok)snprintf(s_salt,sizeof(s_salt),"%s",sh);
    mbedtls_aes_free(&aes);if(input)mbedtls_platform_zeroize(input,padded);free(input);free(cipher);free(base);free(escaped);free(wrapped);return ok;
}
static cJSON *reply(const char *command,bool encrypt)
{
    if(!(encrypt?send_encrypted(command):send_plain(command))){failed("A kérés nem küldhető el.",0);return NULL;}
    int64_t deadline=esp_timer_get_time()+LX_TIMEOUT_MS*1000LL;
    while(!s_dead&&esp_timer_get_time()<deadline) {
        message_t m;if(xQueueReceive(s_messages,&m,pdMS_TO_TICKS(100))!=pdTRUE)continue;
        mark_response();
        if(m.type!=0){if(s_session_config&&!update_values(m.type,m.data,m.length))s_dead=true;free_message(&m);continue;}
        cJSON *root=cJSON_ParseWithLength((char*)m.data,m.length);free_message(&m);
        cJSON *ll=cJSON_GetObjectItemCaseSensitive(root,"LL");
        cJSON *code=cJSON_GetObjectItemCaseSensitive(ll,"Code");
        if(!code)code=cJSON_GetObjectItemCaseSensitive(ll,"code");
        bool ok=(cJSON_IsNumber(code)&&code->valueint==200)||(cJSON_IsString(code)&&strcmp(code->valuestring,"200")==0);
        if(!ok) {
            int response_code=cJSON_IsNumber(code)?code->valueint:cJSON_IsString(code)?atoi(code->valuestring):0;
            failed(response_code?"A Miniserver elutasította a kérést.":"Érvénytelen Miniserver-válasz.",response_code);
        }
        const cJSON *returned=cJSON_GetObjectItemCaseSensitive(ll,"value");
        /* A parancsnyugta 200 mellett érték nélkül is érvényes. A kapcsoló
         * sikerét ettől függetlenül kizárólag a bináris állapot igazolja. */
        cJSON *value=ok?(returned?cJSON_Duplicate(returned,true):cJSON_CreateNull()):NULL;
        cJSON_Delete(root);return value;
    }failed(s_dead?"A kapcsolat megszakadt.":"12 másodpercen belül nem érkezett válasz.",0);return NULL;
}
static int rng(void *ctx,unsigned char *out,size_t len){(void)ctx;esp_fill_random(out,len);return 0;}
static bool key_exchange(void)
{
    phase("Loxone titkosítási kulcscsere");
    if(!s_public_key)return failed("Hiányzó nyilvános kulcs.",0);
    /* Loxone egyes verziói SPKI kulcsot CERTIFICATE fejléccel küldenek. */
    const char *body=strstr(s_public_key,"-----BEGIN CERTIFICATE-----");
    char *pem=NULL;
    if(body) {
        body+=27;const char *end=strstr(body,"-----END CERTIFICATE-----");
        if(end){size_t n=end-body;pem=malloc(n+64);if(pem)snprintf(pem,n+64,"-----BEGIN PUBLIC KEY-----\n%.*s\n-----END PUBLIC KEY-----\n",(int)n,body);}
    }else pem=strdup(s_public_key);
    if(!pem)return failed("A nyilvános kulcs formátuma nem értelmezhető.",0);
    mbedtls_pk_context pk;mbedtls_pk_init(&pk);
    int pk_result=mbedtls_pk_parse_public_key(&pk,(unsigned char*)pem,strlen(pem)+1);
    bool ok=pk_result==0;free(pem);
    if(!ok)failed("A nyilvános kulcs feldolgozása sikertelen.",pk_result);
    unsigned char encrypted[512],base[704];char key[65],iv[33],plain[100];size_t n=0,b64=0;
    esp_fill_random(s_aes_key,sizeof(s_aes_key));esp_fill_random(s_iv,sizeof(s_iv));hex(s_aes_key,32,key);hex(s_iv,16,iv);
    snprintf(plain,sizeof(plain),"%s:%s",key,iv);
    if(ok){int result=mbedtls_pk_encrypt(&pk,(unsigned char*)plain,strlen(plain),encrypted,&n,sizeof(encrypted),rng,NULL);ok=result==0;if(!ok)failed("Az RSA kulcscsere előkészítése sikertelen.",result);}
    mbedtls_pk_free(&pk);mbedtls_platform_zeroize(plain,sizeof(plain));mbedtls_platform_zeroize(key,sizeof(key));
    if(ok)ok=mbedtls_base64_encode(base,sizeof(base)-1,&b64,encrypted,n)==0;
    if(!ok)return false;
    /* A kulcscsere nyers Base64-t vár; csak az AES parancsok URL-kódoltak. */
    base[b64]=0;
    char command[sizeof(base)+32];snprintf(command,sizeof(command),"jdev/sys/keyexchange/%s",base);
    cJSON *value=reply(command,false);ok=value!=NULL;cJSON_Delete(value);return ok;
}
static bool hash_hmac(const cJSON *key,const char *input,char out[65])
{
    const char *algorithm=string(key,"hashAlg");
    mbedtls_md_type_t type=strcmp(algorithm,"SHA256")==0?MBEDTLS_MD_SHA256:strcmp(algorithm,"SHA1")==0?MBEDTLS_MD_SHA1:MBEDTLS_MD_NONE;
    const mbedtls_md_info_t *md=mbedtls_md_info_from_type(type);if(!md)return false;
    unsigned char k[64],digest[32];size_t n;
    if(!unhex(string(key,"key"),k,sizeof(k),&n))return false;
    bool ok=mbedtls_md_hmac(md,k,n,(unsigned char*)input,strlen(input),digest)==0;
    if(ok)hex(digest,mbedtls_md_get_size(md),out);
    mbedtls_platform_zeroize(k,sizeof(k));return ok;
}
static void schedule_token_refresh(const cJSON *value){
    const cJSON *until=cJSON_GetObjectItemCaseSensitive(value,"validUntil");
    int64_t valid_until=cJSON_IsNumber(until)&&until->valuedouble>0&&until->valuedouble<1e12?(int64_t)until->valuedouble:0;
    s_token_refresh_us=esp_timer_get_time()+lx_token_refresh_delay_us(valid_until,(int64_t)time(NULL));
}
static bool acquire_token(const lx_config_t *cfg,char *token,size_t token_size)
{
    phase("Loxone felhasználói hitelesítési kulcs");
    char *user=urlencode(cfg->username);if(!user)return false;
    char command[1536];snprintf(command,sizeof(command),"jdev/sys/getkey2/%s",user);
    cJSON *key=reply(command,false);if(!key){free(user);return false;}
    const char *algorithm=string(key,"hashAlg");
    const mbedtls_md_info_t *md=mbedtls_md_info_from_type(strcmp(algorithm,"SHA256")==0?MBEDTLS_MD_SHA256:strcmp(algorithm,"SHA1")==0?MBEDTLS_MD_SHA1:MBEDTLS_MD_NONE);
    char salted[512],password_hash[65],input[256],hmac[65];unsigned char digest[32];
    int n=snprintf(salted,sizeof(salted),"%s:%s",cfg->password,string(key,"salt"));
    bool ok=md&&n>0&&n<(int)sizeof(salted)&&mbedtls_md(md,(unsigned char*)salted,n,digest)==0;
    if(ok) {
        hex(digest,mbedtls_md_get_size(md),password_hash);
        for(char *p=password_hash;*p;p++)if(*p>='a'&&*p<='f')*p-=32;
        snprintf(input,sizeof(input),"%s:%s",cfg->username,password_hash);ok=hash_hmac(key,input,hmac);
    }
    cJSON_Delete(key);mbedtls_platform_zeroize(salted,sizeof(salted));mbedtls_platform_zeroize(password_hash,sizeof(password_hash));mbedtls_platform_zeroize(input,sizeof(input));
    if(ok) {
        unsigned char mac[6];if(esp_efuse_mac_get_default(mac)!=ESP_OK){free(user);return false;}
        snprintf(command,sizeof(command),"jdev/sys/getjwt/%s/%s/4/7f30a221-5e2d-4100-918a%02x%02x%02x%02x%02x%02x/ZoleeEnergyDisplay",hmac,user,
            mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        phase("Loxone felhasználói bejelentkezés");
        cJSON *v=reply(command,true);const char *t=string(v,"token");
        ok=*t&&strlen(t)<token_size;if(ok){snprintf(token,token_size,"%s",t);schedule_token_refresh(v);}cJSON_Delete(v);
    }free(user);return ok;
}
static bool token_command(const lx_config_t *cfg,char *token,const char *operation)
{
    cJSON *key=reply("jdev/sys/getkey",false);char hmac[65];
    /* getkey régebbi firmware esetén egyszerű hex string, SHA1 alapú. */
    if(cJSON_IsString(key)) {cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"key",key->valuestring);cJSON_AddStringToObject(o,"hashAlg","SHA1");cJSON_Delete(key);key=o;}
    bool ok=key&&hash_hmac(key,token,hmac);cJSON_Delete(key);if(!ok)return false;
    char *user=urlencode(cfg->username);if(!user)return false;
    char cmd[768];snprintf(cmd,sizeof(cmd),"%s%s/%s/%s",strcmp(operation,"authwithtoken")==0?"":"jdev/sys/",operation,hmac,user);free(user);
    cJSON *v=reply(cmd,true);ok=v!=NULL;
    if(ok&&strcmp(operation,"refreshjwt")==0){const char *t=string(v,"token");ok=*t&&strlen(t)<1024;if(ok)snprintf(token,1024,"%s",t);}
    if(ok&&strcmp(operation,"killtoken")!=0)schedule_token_refresh(v);
    cJSON_Delete(v);return ok;
}
static bool renew_token(const lx_config_t *cfg,char token[1024]){
    if(token_command(cfg,token,"refreshjwt"))return true;
    return !s_dead&&acquire_token(cfg,token,1024);
}
static bool download_catalog(int *response_code)
{
    *response_code=0;
    phase("Loxone adatlista letöltése");
    /* LoxAPP3.json esetén enc támogatott, fenc (titkosított válasz) nem. */
    if(!send_encrypted("data/LoxAPP3.json"))return failed("Az adatlista kérése nem küldhető el.",0);
    int64_t deadline=esp_timer_get_time()+LX_TIMEOUT_MS*1000LL;
    while(!s_dead&&esp_timer_get_time()<deadline) {
        message_t m;if(xQueueReceive(s_messages,&m,pdMS_TO_TICKS(100))!=pdTRUE)continue;
        if(m.type!=0){free_message(&m);continue;}
        cJSON *catalog=lx_catalog_parse((char*)m.data,m.length);
        if(!catalog) {
            /* A hibaválasz nem katalógus: ne nyeljük el időtúllépésként. */
            cJSON *root=cJSON_ParseWithLength((char*)m.data,m.length);
            const cJSON *ll=cJSON_GetObjectItemCaseSensitive(root,"LL");
            const cJSON *code=cJSON_GetObjectItemCaseSensitive(ll,"Code");
            if(!code)code=cJSON_GetObjectItemCaseSensitive(ll,"code");
            int result=cJSON_IsNumber(code)?code->valueint:cJSON_IsString(code)?atoi(code->valuestring):0;
            *response_code=result;
            if(ll)failed(result==403?"Az adatlista hozzáférése tiltott. Ellenőrizd a felhasználói jogosultságot.":"A Miniserver adatlista helyett parancsválaszt küldött.",result);
            else if(m.length>LX_STRUCTURE_MAX)failed("Az adatlista meghaladja az 512 KiB méretkorlátot.",0);
            else if(!root)failed("A válasz nem érvényes JSON, hiányos, vagy nincs elég memória.",0);
            else if(!cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root,"controls")))failed("A válaszból hiányzik a controls adatlista.",0);
            else failed("Az állapotlista feldolgozása sikertelen (512 állapot, mélység- vagy memóriakorlát).",0);
            cJSON_Delete(root);free_message(&m);return false;
        }
        free_message(&m);
        xSemaphoreTake(s_lock,portMAX_DELAY);cJSON_Delete(s_catalog);s_catalog=catalog;xSemaphoreGive(s_lock);return true;
    }
    if(s_rx_failure==LX_RX_TOO_LARGE)return failed("A fogadott üzenet meghaladja az 513 KiB vételi korlátot.",0);
    if(s_rx_failure==LX_RX_NO_MEMORY)return failed("Nincs elég memória az adatlista fogadásához.",0);
    if(s_rx_failure==LX_RX_BAD_HEADER)return failed("Érvénytelen Loxone üzenetfejléc érkezett.",0);
    if(s_rx_failure==LX_RX_QUEUE_FULL)return failed("A fogadott üzenetek sora megtelt.",0);
    return failed(s_dead?"A kapcsolat megszakadt az adatlista fogadása közben.":"12 másodpercen belül nem érkezett adatlista.",0);
}
/* A HTTP fájlútvonal egyes Miniszervereken elérhető akkor is, ha a WS 404-et ad.
 * Csak 404-re váltunk át: a jogosultsági hibákat nem kerüljük meg. */
static char *http_body(const lx_config_t *cfg,const char *path,size_t limit,size_t *length)
{
    size_t url_size=strlen(path)+strlen(cfg->host)+32;
    char *url=malloc(url_size);if(!url){failed("Nincs elég memória a HTTP kéréshez.",0);return NULL;}
    snprintf(url,url_size,"%s://%s:%u/%s",cfg->tls?"https":"http",cfg->host,(unsigned)cfg->port,path);
    esp_http_client_config_t options={.url=url,.timeout_ms=LX_TIMEOUT_MS,
        .cert_pem=cfg->tls?cfg->ca_pem:NULL,.disable_auto_redirect=true};
    esp_http_client_handle_t http=esp_http_client_init(&options);
    if(!http){free(url);failed("A HTTP kliens nem indítható.",0);return NULL;}
    int error=esp_http_client_open(http,0);bool ok=error==ESP_OK;
    if(!ok)failed("A HTTP kapcsolat nem épült fel.",error);
    if(ok){
        int64_t size=esp_http_client_fetch_headers(http);int code=esp_http_client_get_status_code(http);
        ok=size>=0&&code==200;
        if(!ok)failed("A HTTP lekérés sikertelen (átirányítás nem követhető).",size<0?(int)size:code);
        else if((uint64_t)size>limit){ok=false;failed("A HTTP válasz meghaladja a megengedett méretet.",0);}
    }
    size_t used=0,capacity=limit<4096?limit:4096;
    char *body=ok?malloc(capacity+1):NULL;
    if(ok&&!body){ok=false;failed("Nincs elég memória a HTTP válaszhoz.",0);}
    while(ok){
        if(used==capacity){
            if(capacity==limit){
                char extra;int count=esp_http_client_read(http,&extra,1);
                if(count!=0){ok=false;failed("A HTTP válasz túl nagy vagy hiányos.",0);}break;
            }
            size_t next=capacity>limit/2?limit:capacity*2;
            char *larger=realloc(body,next+1);
            if(!larger){ok=false;failed("Nincs elég memória a HTTP válasz bővítéséhez.",0);break;}
            body=larger;capacity=next;
        }
        int count=esp_http_client_read(http,body+used,capacity-used);
        if(count<0){ok=false;failed("A HTTP válasz olvasása megszakadt.",count);break;}
        if(count==0)break;
        used+=count;
    }
    if(ok&&!esp_http_client_is_complete_data_received(http)){ok=false;failed("Hiányos HTTP válasz érkezett.",0);}
    esp_http_client_close(http);esp_http_client_cleanup(http);free(url);
    if(!ok){free(body);return NULL;}
    body[used]=0;*length=used;return body;
}
static bool download_catalog_with_fallback(const lx_config_t *cfg,const char *token)
{
    int code=0;if(download_catalog(&code))return true;
    if(code!=404||s_dead)return false;
    phase("Loxone adatlista / HTTP tokenhitelesítés");
    size_t length=0;char *body=http_body(cfg,"jdev/sys/getkey",8192,&length);
    if(!body)return false;
    cJSON *root=cJSON_ParseWithLength(body,length);free(body);
    const cJSON *ll=cJSON_GetObjectItemCaseSensitive(root,"LL");
    const cJSON *status_code=cJSON_GetObjectItemCaseSensitive(ll,"Code");
    if(!status_code)status_code=cJSON_GetObjectItemCaseSensitive(ll,"code");
    int result=cJSON_IsNumber(status_code)?status_code->valueint:cJSON_IsString(status_code)?atoi(status_code->valuestring):0;
    cJSON *key=result==200?cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(ll,"value"),true):NULL;cJSON_Delete(root);
    if(!key)return failed("A HTTP hitelesítési kulcs nem érkezett meg.",result);
    if(cJSON_IsString(key)){
        cJSON *object=cJSON_CreateObject();
        if(object){cJSON_AddStringToObject(object,"key",key->valuestring);cJSON_AddStringToObject(object,"hashAlg","SHA1");}
        cJSON_Delete(key);key=object;
    }
    char hash[65];bool ok=key&&hash_hmac(key,token,hash);cJSON_Delete(key);
    if(!ok)return failed("A HTTP tokenhitelesítés nem készíthető elő.",0);
    char *user=urlencode(cfg->username);if(!user)return failed("Nincs elég memória a felhasználónévhez.",0);
    char path[512];snprintf(path,sizeof(path),"data/LoxAPP3.json?autht=%s&user=%s",hash,user);free(user);
    phase("Loxone adatlista / HTTP letöltés");
    body=http_body(cfg,path,LX_STRUCTURE_MAX,&length);mbedtls_platform_zeroize(path,sizeof(path));
    if(!body)return false;
    cJSON *catalog=lx_catalog_parse(body,length);
    if(!catalog){
        root=cJSON_ParseWithLength(body,length);ll=cJSON_GetObjectItemCaseSensitive(root,"LL");
        status_code=cJSON_GetObjectItemCaseSensitive(ll,"Code");
        if(!status_code)status_code=cJSON_GetObjectItemCaseSensitive(ll,"code");
        result=cJSON_IsNumber(status_code)?status_code->valueint:cJSON_IsString(status_code)?atoi(status_code->valuestring):0;
        cJSON_Delete(root);free(body);
        return failed(result?"A Miniserver a HTTP adatlista kérését elutasította.":"A HTTP adatlista nem dolgozható fel (JSON, állapot- vagy memóriakorlát).",result);
    }
    free(body);
    if(s_dead){cJSON_Delete(catalog);return failed("A WebSocket kapcsolat megszakadt a HTTP letöltés közben.",0);}
    xSemaphoreTake(s_lock,portMAX_DELAY);cJSON_Delete(s_catalog);s_catalog=catalog;xSemaphoreGive(s_lock);
    return true;
}
static void close_connection(void)
{
    if(s_ws){esp_websocket_client_stop(s_ws);esp_websocket_client_destroy(s_ws);s_ws=NULL;}
    free(s_public_key);s_public_key=NULL;s_salt[0]=0;
    free(s_rx);s_rx=NULL;s_rx_len=s_rx_cap=0;s_message_type=-1;
    message_t m;while(xQueueReceive(s_messages,&m,0)==pdTRUE)free_message(&m);
    mbedtls_platform_zeroize(s_aes_key,sizeof(s_aes_key));mbedtls_platform_zeroize(s_iv,sizeof(s_iv));
}
/* A nyilvános kulcsot még a rövid WS hitelesítési határidő ELŐTT kérjük le. */
static bool fetch_public_key(const lx_config_t *cfg)
{
    phase("Miniserver elérése / nyilvános kulcs");
    char url[192];snprintf(url,sizeof(url),"%s://%s:%u/jdev/sys/getPublicKey",cfg->tls?"https":"http",cfg->host,(unsigned)cfg->port);
    esp_http_client_config_t options={.url=url,.timeout_ms=LX_TIMEOUT_MS,
        .cert_pem=cfg->tls?cfg->ca_pem:NULL,.disable_auto_redirect=true};
    esp_http_client_handle_t http=esp_http_client_init(&options);if(!http)return failed("A HTTP kliens nem indítható (memóriahiba).",0);
    char *body=calloc(1,8193);int http_result=body?esp_http_client_open(http,0):ESP_ERR_NO_MEM;
    bool ok=http_result==ESP_OK;size_t used=0;
    if(!ok)failed("Nem épült fel a HTTP kapcsolat. Ellenőrizd az IP-címet és a portot.",http_result);
    if(ok){
        int64_t headers=esp_http_client_fetch_headers(http);int http_status=esp_http_client_get_status_code(http);
        ok=headers>=0&&http_status==200;
        if(!ok)failed("A HTTP kérés sikertelen. Ellenőrizd a portot és a WS/WSS módot.",headers<0?(int)headers:http_status);
    }
    while(ok&&used<8192) {
        int n=esp_http_client_read(http,body+used,8192-used);
        if(n<0){failed("A HTTP válasz olvasása megszakadt.",n);ok=false;break;}if(n==0)break;used+=n;
    }
    if(ok&&(used==8192||!esp_http_client_is_complete_data_received(http))){failed("Hiányos vagy túl nagy nyilvánoskulcs-válasz.",0);ok=false;}
    esp_http_client_close(http);esp_http_client_cleanup(http);
    cJSON *root=ok?cJSON_Parse(body):NULL;free(body);
    const char *key=string(cJSON_GetObjectItemCaseSensitive(root,"LL"),"value");
    s_public_key=*key?strdup(key):NULL;cJSON_Delete(root);
    if(!s_public_key&&ok)failed("A HTTP válasz nem tartalmaz Loxone nyilvános kulcsot.",0);
    return s_public_key!=NULL;
}
static bool connect_server(const lx_config_t *cfg)
{
    if(!fetch_public_key(cfg))return false;
    phase("Loxone WebSocket kapcsolat felépítése");
    char uri[192];snprintf(uri,sizeof(uri),"%s://%s:%u/ws/rfc6455",cfg->tls?"wss":"ws",cfg->host,(unsigned)cfg->port);
    esp_websocket_client_config_t ws={.uri=uri,.subprotocol="remotecontrol",.disable_auto_reconnect=true,
        .buffer_size=2048,.task_stack=6144,.network_timeout_ms=LX_TIMEOUT_MS,
        .cert_pem=cfg->tls?cfg->ca_pem:NULL};
    s_rx_failure=LX_RX_OK;s_dead=false;s_ws=esp_websocket_client_init(&ws);if(!s_ws)return failed("A WebSocket kliens nem indítható (memóriahiba).",0);
    if(esp_websocket_register_events(s_ws,WEBSOCKET_EVENT_ANY,receive_event,NULL)!=ESP_OK||esp_websocket_client_start(s_ws)!=ESP_OK)return failed("A WebSocket feladat nem indítható.",0);
    int64_t until=esp_timer_get_time()+LX_TIMEOUT_MS*1000LL;
    while(!s_dead&&!esp_websocket_client_is_connected(s_ws)&&esp_timer_get_time()<until)vTaskDelay(pdMS_TO_TICKS(50));
    if(s_dead||!esp_websocket_client_is_connected(s_ws))return failed("A WebSocket nem kapcsolódott 12 másodpercen belül.",0);
    return key_exchange();
}
static void invalidate(bool configured)
{
    s_presence_session_live = false;
    app_slave_presence_service_online(false);
    app_state_set_loxone(configured,false);app_state_clear_sample();app_stats_break_continuity();
    memset(&s_values,0,sizeof(s_values));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_home_live = false;
    s_home_session++;
    memset(&s_home_values, 0, sizeof(s_home_values));
    if (s_command_pending)
        snprintf(s_command_status, sizeof(s_command_status), "%s",
                 "A kapcsolat megszakadt; a művelet eredménye nem igazolható.");
    s_command_pending = false;
    s_command_deadline = 0;
    home_command_t discarded;
    if (s_home_commands)
        while (xQueueReceive(s_home_commands, &discarded, 0) == pdTRUE) {}
    xSemaphoreGive(s_lock);
}
static bool configured(const lx_config_t *cfg)
{
    if (!cfg->host[0] || !cfg->username[0] || !cfg->password[0])
        return false;
    for (int i = 0; i < LX_FIELD_COUNT; i++)
    {
        if (lx_fields[i].required && (!s_energy_disabled || i == 6) && !cfg->map[i].uuid[0])
            return false;
    }
    return true;
}
/* A memóriabeli profil csak a sikeresen lezárt SD-tranzakció után változik. */
static void save_home_request(const lx_home_config_t *home)
{
    esp_err_t saved = app_loxone_home_store_save(home);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (saved == ESP_OK)
    {
        bool same_bell = !strcmp(s_home->intercom.bell_uuid, home->intercom.bell_uuid);
        bool intercom_changed = memcmp(&s_home->intercom, &home->intercom, sizeof(home->intercom)) != 0;
        *s_home = *home;
        if (!same_bell || !home->intercom.enabled)
            s_intercom_ringing = false;
        s_home_revision++;
        if (intercom_changed)
            s_intercom_generation++;
    }
    snprintf(s_home_status, sizeof(s_home_status), "%s",
             saved == ESP_OK ? "Otthon-beállítások elmentve." :
             "Az SD-mentés sikertelen. A korábbi Otthon-beállítások érvényesek.");
    s_home_busy = false;
    xSemaphoreGive(s_lock);
}

static void worker(void *arg)
{
    (void)arg;
    lx_config_t *active=calloc(1,sizeof(*active));request_t *request=NULL;
    if(!active){status("Nincs elegendő memória a Loxone klienshez.");vTaskDelete(NULL);return;}
    char token[1024]={0};bool testing=false;unsigned failures=0;
    for(;;) {
        uint32_t presence_revision = app_slave_presence_service_source_revision();
        if(xQueueReceive(s_requests,&request,0)==pdTRUE) {
            if (request->home)
            {
                save_home_request(request->home);
                mbedtls_platform_zeroize(request->home, sizeof(*request->home));
                free(request->home);
                free(request);
                request = NULL;
                continue;
            }
            *active=request->config;testing=!request->save;
            if(request->save) {
                if(app_loxone_store_save(active)==ESP_OK) {
                    xSemaphoreTake(s_lock,portMAX_DELAY);s_saved=*active;s_password=true;s_intercom_generation++;xSemaphoreGive(s_lock);
                    status("Beállítások elmentve. Kapcsolódás...");
                }else {status("Az SD-mentés sikertelen. A korábbi beállítások maradtak érvényben.");*active=s_saved;}
            }
            mbedtls_platform_zeroize(request,sizeof(*request));free(request);request=NULL;
            mbedtls_platform_zeroize(token,sizeof(token));failures=0;
        }else if(!testing)*active=s_saved;
        bool ready=configured(active);invalidate(ready);
        if((!ready&&!testing)||!app_wifi_is_connected()) {
            xSemaphoreTake(s_lock,portMAX_DELAY);s_busy=false;xSemaphoreGive(s_lock);
            vTaskDelay(pdMS_TO_TICKS(500));continue;
        }
        s_session_config=active;
        if(!connect_server(active)) {report_failure("A Miniserver nem érhető el, vagy a biztonságos kapcsolat nem épült fel.");goto retry;}
        status("Loxone bejelentkezés...");
        if(token[0]&&!token_command(active,token,"authwithtoken"))token[0]=0;
        if(!token[0]&&!acquire_token(active,token,sizeof(token))) {report_failure("A Loxone bejelentkezés sikertelen. Ellenőrizd a felhasználót, jelszót és jogosultságokat.");goto retry;}
        if(!download_catalog_with_fallback(active,token)) {report_failure("Az adatlista nem tölthető be, vagy meghaladja az eszköz korlátját.");goto retry;}
        if(testing) {xSemaphoreTake(s_lock,portMAX_DELAY);s_busy=false;xSemaphoreGive(s_lock);status("Sikeres kapcsolat. Az adatlista betöltve; végezd el a párosítást és mentsd el.");testing=false;(void)token_command(active,token,"killtoken");token[0]=0;close_connection();continue;}
        /* A hitelesítés és a katalógus már sikeres; a kezdő delta az ACK előtt is jöhet. */
        s_presence_session_live = true;
        app_slave_presence_service_online(true);
        cJSON *ack=reply("jdev/sps/enablebinstatusupdate",false);
        if(!ack){status("Az állapotfrissítés nem engedélyezhető.");goto retry;}cJSON_Delete(ack);
        xSemaphoreTake(s_lock,portMAX_DELAY);s_busy=false;xSemaphoreGive(s_lock);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_home_live = true;
        xSemaphoreGive(s_lock);
        app_state_set_loxone(true,true);status("Kapcsolódva. Várakozás a szükséges kezdőértékekre...");
        mark_response();int64_t heartbeat=s_last_response,last_sample=0;
        failures=0;
        while(!s_dead&&app_wifi_is_connected()&&uxQueueMessagesWaiting(s_requests)==0 &&
              app_slave_presence_service_source_revision() == presence_revision) {
            message_t m;int64_t now=esp_timer_get_time();
            if(xQueueReceive(s_messages,&m,pdMS_TO_TICKS(100))==pdTRUE) {
                mark_response();
                if(!update_values(m.type,m.data,m.length))s_dead=true;
                free_message(&m);
            }
            home_command_poll(now);
            if(now-heartbeat>=LX_KEEPALIVE_US){if(!send_plain("keepalive"))break;heartbeat=now;}
            if(now-s_last_response>=LX_KEEPALIVE_US*3)break;
            if(now>=s_token_refresh_us) {
                /* A válaszban kapott új JWT lesz a következő kapcsolódás hitelesítője. */
                if(!renew_token(active,token))break;
                status("Kapcsolódva. A token megújítva.");
            }
            if (s_energy_disabled)
            {
                if (s_values.seen[6] && (s_values.value[6] == 0 || s_values.value[6] == 1))
                    app_state_set_energy_flow_disabled(true, s_values.value[6] == 1);
                status("Loxone Otthon adatkapcsolat működik.");
            }
            if(!s_energy_disabled && now-last_sample>=5000000LL&&now-s_last_response<LX_KEEPALIVE_US*3) {
                cJSON *payload=lx_values_payload(&s_values,active);
                char *json=payload?cJSON_PrintUnformatted(payload):NULL;cJSON_Delete(payload);
                if(json) {char error[192];
                    if(app_ingest_process(APP_DATA_LOXONE,json,strlen(json),error,sizeof(error))==ESP_OK)
                        status("Loxone adatkapcsolat működik.");
                    else {app_state_clear_sample();app_stats_break_continuity();status("A párosított érték érvénytelen. Ellenőrizd a mértékegységet és az átalakítást.");}
                    free(json);
                }
                last_sample=now;
            }
        }
        status("Megszakadt a Loxone adatkapcsolat. Újracsatlakozás...");
retry:
        xSemaphoreTake(s_lock,portMAX_DELAY);s_busy=false;xSemaphoreGive(s_lock);
        invalidate(configured(&s_saved));close_connection();
        if(testing){testing=false;token[0]=0;}
        if(uxQueueMessagesWaiting(s_requests))continue;
        /* 2, 4, 8, 16, majd legfeljebb 30 s. Új beállítás azonnal megszakítja. */
        if(failures<5)failures++;
        unsigned delay=1U<<failures;if(delay>30)delay=30;
        for(unsigned i=0;i<delay*10&&!uxQueueMessagesWaiting(s_requests);i++)vTaskDelay(pdMS_TO_TICKS(100));
    }
}
esp_err_t app_loxone_start(void)
{
    app_runtime_state_t runtime;
    app_state_get_runtime(&runtime);
    s_energy_disabled = runtime.energy_flow_disabled;
    s_lock=xSemaphoreCreateMutex();s_messages=xQueueCreate(LX_QUEUE_DEPTH,sizeof(message_t));s_requests=xQueueCreate(1,sizeof(request_t*));
    s_home_commands = xQueueCreate(1, sizeof(home_command_t));
    if(!s_lock||!s_messages||!s_requests||!s_home_commands)return ESP_ERR_NO_MEM;
    s_home = calloc(1, sizeof(*s_home));
    if (!s_home)
        return ESP_ERR_NO_MEM;
    esp_err_t home_load = app_loxone_home_store_load(s_home);
    if (home_load != ESP_OK) s_home->intercom.speaker_volume = 55;
    if (home_load != ESP_OK) s_home->intercom.microphone_gain = 80;
    if (home_load != ESP_OK && home_load != ESP_ERR_NOT_FOUND)
        snprintf(s_home_status, sizeof(s_home_status), "%s",
                 "Az Otthon-profil nem tölthető be. Ellenőrizd az SD-kártyát.");
    lx_config_defaults(&s_saved);(void)app_loxone_store_load(&s_saved);s_password=s_saved.password[0]!=0;
    s_saved.energy_flow_disabled = s_energy_disabled;
    /* A GUI/touch a CPU1-en fut; a hálózati állapotfeldolgozás CPU0-n marad.
     * A rövid snapshot-lockokat mindkét magról ugyanaz a mutex védi. */
    if (xTaskCreatePinnedToCore(worker, "loxone", 12288, NULL, 3, NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    return app_intercom_start();
}
cJSON *app_loxone_web_state(void)
{
    if(!s_lock)return NULL;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    cJSON *root=lx_config_json(&s_saved);
    if(root) {
        cJSON_AddBoolToObject(root,"energy_flow_disabled",s_energy_disabled);
        cJSON_AddBoolToObject(root,"has_password",s_password);cJSON_AddBoolToObject(root,"busy",s_busy);
        cJSON_AddStringToObject(root,"status",s_status);
        cJSON *catalog=s_catalog?cJSON_Duplicate(s_catalog,true):cJSON_CreateArray();
        if(catalog)cJSON_AddItemToObject(root,"catalog",catalog);
    }
    xSemaphoreGive(s_lock);return root;
}
esp_err_t app_loxone_request(const cJSON *body,bool save)
{
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    request_t *r=calloc(1,sizeof(*r));if(!r)return ESP_ERR_NO_MEM;
    const cJSON *section = cJSON_GetObjectItemCaseSensitive(body, "section");
    const char *scope = cJSON_IsString(section) ? section->valuestring : "";
    xSemaphoreTake(s_lock,portMAX_DELAY);
    cJSON *merged = lx_config_json(&s_saved);
    bool scoped = !strcmp(scope, "connection") || !strcmp(scope, "mappings");
    bool ok = merged && (!section || scoped) && (!s_energy_disabled || scoped);
    if (ok && scoped)
    {
        if (!strcmp(scope, "connection"))
        {
            const char *keys[] = {"host", "port", "tls", "ca_pem", "username"};
            for (unsigned i = 0; ok && i < sizeof(keys) / sizeof(keys[0]); i++)
            {
                cJSON *value = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(body, keys[i]), true);
                ok = value && cJSON_ReplaceItemInObjectCaseSensitive(merged, keys[i], value);
                if (!ok) cJSON_Delete(value);
            }
        }
        else
        {
            cJSON *maps = cJSON_GetObjectItemCaseSensitive(merged, "mappings");
            const cJSON *input = cJSON_GetObjectItemCaseSensitive(body, "mappings");
            for (int i = 0; ok && i < LX_FIELD_COUNT; i++)
            {
                if (s_energy_disabled && i != 6) continue;
                cJSON *value = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(input, lx_fields[i].name), true);
                ok = value && cJSON_ReplaceItemInObjectCaseSensitive(maps, lx_fields[i].name, value);
                if (!ok) cJSON_Delete(value);
            }
        }
    }
    bool require = save && !s_energy_disabled && strcmp(scope, "connection");
    ok = ok && lx_config_parse(scoped ? merged : body, &r->config, require);
    cJSON_Delete(merged);
    r->config.energy_flow_disabled = s_energy_disabled;
    if (ok && save && !strcmp(scope, "mappings") && !r->config.map[6].uuid[0]) ok = false;
    const cJSON *password=cJSON_GetObjectItemCaseSensitive(body,"password");
    if (!strcmp(scope, "mappings"))
        snprintf(r->config.password, sizeof(r->config.password), "%s", s_saved.password);
    else if (cJSON_IsString(password) && password->valuestring[0] && strlen(password->valuestring) < LX_PASSWORD_MAX)
        snprintf(r->config.password, sizeof(r->config.password), "%s", password->valuestring);
    else if (cJSON_IsNull(password) && !strcmp(r->config.host, s_saved.host) &&
             r->config.port == s_saved.port && r->config.tls == s_saved.tls &&
             !strcmp(r->config.username, s_saved.username))
        snprintf(r->config.password, sizeof(r->config.password), "%s", s_saved.password);
    else
        ok = false;
    r->save=save;
    if(!ok||!r->config.password[0]||s_busy||s_home_busy) {xSemaphoreGive(s_lock);mbedtls_platform_zeroize(r,sizeof(*r));free(r);return ESP_ERR_INVALID_ARG;}
    s_busy=true;snprintf(s_status,sizeof(s_status),"%s",save?"Beállítások mentése...":"Kapcsolat tesztelése...");
    if(xQueueSend(s_requests,&r,0)!=pdTRUE){s_busy=false;ok=false;}
    xSemaphoreGive(s_lock);
    if(!ok){mbedtls_platform_zeroize(r,sizeof(*r));free(r);return ESP_FAIL;}
    return ESP_OK;
}

/* Külön API: a kapcsolat régi mentése nem írja felül az Otthon-profilt,
 * és ezen az úton sem jelszó, sem tetszőleges parancs nem küldhető. */
cJSON *app_loxone_home_web_state(void)
{
    if (!s_lock || !s_home)
        return NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *root = lx_home_config_json(s_home);
    if (root)
    {
        if (!cJSON_AddBoolToObject(root, "busy", s_home_busy) ||
            !cJSON_AddNumberToObject(root, "revision", s_home_revision) ||
            !cJSON_AddStringToObject(root, "status", s_home_status))
        {
            cJSON_Delete(root);
            root = NULL;
        }
    }
    xSemaphoreGive(s_lock);
    return root;
}

esp_err_t app_loxone_home_request(const cJSON *body, char error[LX_HOME_ERROR_SIZE])
{
    if (!s_lock || !s_home)
        return ESP_ERR_INVALID_STATE;
    request_t *request = calloc(1, sizeof(*request));
    lx_home_config_t *home = calloc(1, sizeof(*home));
    if (!request || !home)
    {
        free(request);
        free(home);
        return ESP_ERR_NO_MEM;
    }
    const cJSON *section = cJSON_GetObjectItemCaseSensitive(body, "section");
    const char *scope = cJSON_IsString(section) ? section->valuestring : "";
    bool scoped = !strcmp(scope, "consumers") || !strcmp(scope, "pages") || !strcmp(scope, "intercom");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    unsigned previous_revision = s_home_revision;
    cJSON *merged = scoped ? lx_home_config_json(s_home) : NULL;
    xSemaphoreGive(s_lock);
    bool valid = (!section || scoped) && (!s_energy_disabled || (scoped && strcmp(scope, "consumers")));
    if (scoped && merged)
    {
        cJSON *value = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(body, scope), true);
        valid = valid && value && cJSON_ReplaceItemInObjectCaseSensitive(merged, scope, value);
        if (!valid) cJSON_Delete(value);
    }
    valid = valid && (!scoped || merged) && body && lx_home_config_parse(scoped ? merged : body, home, error);
    cJSON_Delete(merged);
    if (!valid)
    {
        free(request);
        free(home);
        return ESP_ERR_INVALID_ARG;
    }
    request->home = home;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool credentials_ok = lx_intercom_credentials_update(&home->intercom, &s_home->intercom,
        !scoped || !strcmp(scope, "intercom") ? cJSON_GetObjectItemCaseSensitive(body, "intercom") : NULL);
    bool accepted = credentials_ok && previous_revision == s_home_revision && !s_busy && !s_home_busy &&
                    xQueueSend(s_requests, &request, 0) == pdTRUE;
    if (accepted)
    {
        s_home_busy = true;
        snprintf(s_home_status, sizeof(s_home_status), "%s", "Otthon-beállítások mentése...");
    }
    xSemaphoreGive(s_lock);
    if (!accepted)
    {
        mbedtls_platform_zeroize(home, sizeof(*home));
        free(home);
        free(request);
        snprintf(error, LX_HOME_ERROR_SIZE, "%s", credentials_ok ? "Várd meg a folyamatban lévő Loxone-műveletet." :
                 "Add meg a videó felhasználónevét és jelszavát. Új kamerához vagy felhasználóhoz új jelszó kell.");
    }
    return accepted ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static bool update_values(int type, const uint8_t *data, size_t size)
{
    if (!lx_values_update(&s_values, s_session_config, type, data, size))
        return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = !s_home || lx_home_values_update(&s_home_values, s_home, type, data, size);
    if (ok && s_home)
    {
        bool was_ringing = s_intercom_ringing;
        ok = lx_intercom_bell_update(&s_intercom_ringing, &s_home->intercom, type, data, size);
        /* Felépült beszélgetés alatt a csengő éle nem cseréli le a hívást. */
        if (was_ringing != s_intercom_ringing && !intercom_held(s_intercom_generation))
            s_intercom_generation++;
    }
    xSemaphoreGive(s_lock);
    if (ok && type == 2 && s_presence_session_live)
    {
        /* Ugyanaz a már ellenőrzött, 24 bájtos numerikus WS-delta: nincs új kapcsolat,
         * és a két BLE-olvasás közé eső 1->0 él sem veszik el. */
        for (size_t position = 0; position < size; position += 24)
        {
            char uuid[LX_UUID_LEN];
            lx_uuid_string(data + position, uuid);
            uint64_t bits = 0;
            for (unsigned byte = 0; byte < 8; ++byte)
                bits |= (uint64_t)data[position + 16 + byte] << (8 * byte);
            double value;
            memcpy(&value, &bits, sizeof(value));
            app_slave_presence_service_source(uuid, true, value);
        }
    }
    return ok;
}

bool app_loxone_home_snapshot(lx_home_config_t *config, lx_home_values_t *values,
                              unsigned *revision, bool *connected,
                              bool *command_pending, char message[LX_HOME_ERROR_SIZE])
{
    if (!s_lock || !s_home || !config || !values || !revision || !connected ||
        !command_pending || !message)
        return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *config = *s_home;
    *values = s_home_values;
    char allowed_command[96];
    for (unsigned i = 0; i < s_home->card_count; i++)
        values->command_supported[i] = lx_home_command_build(s_home, s_catalog,
            s_home->cards[i].id, s_home->cards[i].type == LX_HOME_CARD_PULSE ?
            LX_HOME_COMMAND_PULSE : LX_HOME_COMMAND_ON, allowed_command);
    *revision = s_home_revision;
    *connected = s_home_live && !s_dead &&
                 esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3;
    *command_pending = s_command_pending;
    snprintf(message, LX_HOME_ERROR_SIZE, "%s", s_command_status);
    xSemaphoreGive(s_lock);
    return true;
}

bool app_loxone_assistant_snapshot(app_assistant_consumers_t *out)
{
    if (!out || !s_lock || !s_home)
        return false;
    memset(out, 0, sizeof(*out));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->count = s_home->consumer_count;
    out->revision = s_home_revision;
    out->session = s_home_session;
    int64_t elapsed = esp_timer_get_time() - s_home_response;
    out->connected = s_home_live && !s_dead && elapsed >= 0 && elapsed < LX_KEEPALIVE_US * 3;
    for (unsigned i = 0; i < out->count && i < LX_HOME_MAX_CONSUMERS; ++i)
    {
        const lx_home_consumer_t *source = &s_home->consumers[i];
        app_assistant_consumer_t *target = &out->consumers[i];
        snprintf(target->id, sizeof(target->id), "%s", source->id);
        snprintf(target->name, sizeof(target->name), "%s", source->name);
        target->eligible = lx_home_consumer_start_kw(source, &target->required_kw);
        target->measured_kw = s_home_values.consumers[i].value;
        target->valid = s_home_values.consumers[i].valid;
    }
    xSemaphoreGive(s_lock);
    return true;
}

static esp_err_t home_command_request(const char *card_id, lx_home_command_t action,
                                       bool check_generation, unsigned generation)
{
    if (!s_lock || !s_home || !s_home_commands || !card_id || strlen(card_id) >= LX_HOME_ID_SIZE)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    char command[96];
    bool door = !strcmp(card_id, LX_INTERCOM_GATE_ID);
    bool allowed = (!check_generation || s_intercom_generation == generation) &&
                   (!door || s_intercom_ringing || intercom_held(s_intercom_generation)) &&
                   s_home_live && !s_dead && !s_command_pending && !s_home_busy &&
                   esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3 &&
                   lx_home_command_build(s_home, s_catalog, card_id, action, command);
    for (unsigned i = 0; allowed && i < s_home->card_count; i++)
    {
        if (!strcmp(s_home->cards[i].id, card_id) && s_home->cards[i].type == LX_HOME_CARD_SWITCH)
            allowed = s_home_values.cards[i].valid &&
                      s_home_values.cards[i].value == (action == LX_HOME_COMMAND_ON ? 0 : 1);
    }
    home_command_t request = {.action = action, .revision = s_home_revision, .session = s_home_session, .queued_us = esp_timer_get_time()};
    request.intercom_revision = s_intercom_generation;
    snprintf(request.id, sizeof(request.id), "%s", card_id);
    allowed = allowed && xQueueSend(s_home_commands, &request, 0) == pdTRUE;
    if (allowed)
    {
        s_command_pending = true;
        s_command_deadline = 0;
        snprintf(s_command_status, sizeof(s_command_status), "%s", "Kérés küldése...");
    }
    xSemaphoreGive(s_lock);
    /* A helyben elfogadott kapunyitás indítja a bontást: a Miniserver
     * nyugtája késhet vagy elmaradhat, a hang akkor sem maradhat nyitva. */
    if (allowed && door && action == LX_HOME_COMMAND_PULSE) {
        app_intercom_audio_gate_opened(request.intercom_revision);
#ifdef APP_INTERCOM_DISTRIBUTED
        app_intercom_control_gate(request.intercom_revision);
#endif
    }
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t app_loxone_home_command(const char *card_id, lx_home_command_t action)
{
    return home_command_request(card_id, action, false, 0);
}

/* A Slave régi gombnyomása nem használhat közben megváltozott kapunyitó
 * beállítást. A generáció és a sorba helyezés ugyanazon Loxone-zár alatt áll. */
esp_err_t app_loxone_intercom_gate_request(unsigned revision)
{
    return home_command_request(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE, true, revision);
}

/* Kizárólag a Loxone-worker küldhet a WebSocketre. A kivett parancsot soha
 * nem tesszük vissza a sorba, akkor sem, ha az eredmény a hálózati hiba miatt bizonytalan. */
static void home_command_poll(int64_t now)
{
    home_command_t request;
    if (s_home_commands && xQueueReceive(s_home_commands, &request, 0) == pdTRUE)
    {
        char command[96];
        xSemaphoreTake(s_lock, portMAX_DELAY);
        /* A kérés az üzenetfogadás várakozása közben is érkezhetett.
         * A ciklus eleji idő ilyenkor régebbi a gombnyomásnál. */
        now = esp_timer_get_time();
        bool door = !strcmp(request.id, LX_INTERCOM_GATE_ID);
        bool valid = (!door || ((s_intercom_ringing || intercom_held(s_intercom_generation)) &&
                              request.intercom_revision == s_intercom_generation)) &&
                     s_home_live && !s_dead && request.revision == s_home_revision &&
                     request.session == s_home_session && now >= request.queued_us &&
                     now - request.queued_us <= 2000000LL &&
                     lx_home_command_build(s_home, s_catalog, request.id, request.action, command);
        xSemaphoreGive(s_lock);
        cJSON *answer = valid ? reply(command, true) : NULL;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!answer)
        {
            s_command_pending = false;
            snprintf(s_command_status, sizeof(s_command_status), "%s",
                     "A művelet nem igazolható. Ellenőrizd a Loxone állapotát.");
        }
        else if (request.action == LX_HOME_COMMAND_PULSE)
        {
            s_command_pending = false;
            snprintf(s_command_status, sizeof(s_command_status), "%s", "Impulzus elküldve.");
        }
        else
        {
            s_pending_command = request;
            s_command_deadline = esp_timer_get_time() + 5000000LL;
            snprintf(s_command_status, sizeof(s_command_status), "%s", "Állapot-visszajelzésre várunk...");
        }
        xSemaphoreGive(s_lock);
        cJSON_Delete(answer);
    }
    now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_command_pending && s_command_deadline)
    {
        bool confirmed = false;
        for (unsigned i = 0; i < s_home->card_count; i++)
            if (!strcmp(s_home->cards[i].id, s_pending_command.id) && s_home_values.cards[i].valid &&
                s_home_values.cards[i].value == (s_pending_command.action == LX_HOME_COMMAND_ON ? 1 : 0))
                confirmed = true;
        if (confirmed || now >= s_command_deadline)
        {
            s_command_pending = false;
            s_command_deadline = 0;
            snprintf(s_command_status, sizeof(s_command_status), "%s", confirmed ?
                     "Loxone-állapot visszaigazolva." : "Nem érkezett megfelelő Loxone-állapot.");
        }
    }
    xSemaphoreGive(s_lock);
}

/* Kis pillanatkép, a teljes Otthon-katalógus másolása és hálózati várakozás nélkül. */
bool app_loxone_intercom_call_permitted(unsigned revision, bool active)
{
    if (!s_lock || !s_home) return false;
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) return true;
    bool allowed = s_home->intercom.enabled && s_home_live && !s_dead &&
                   esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3 &&
                   s_intercom_generation == revision && (s_intercom_ringing || active);
    xSemaphoreGive(s_lock);
    return allowed;
}

bool app_loxone_intercom_test_permitted(unsigned revision)
{
    if (!s_lock || !s_home) return false;
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) return true;
    bool allowed = !s_intercom_ringing && s_intercom_generation == revision;
    xSemaphoreGive(s_lock);
    return allowed;
}

bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out)
{
    if (!out || !s_lock || !s_home)
        return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->config = s_home->intercom;
    mbedtls_platform_zeroize(out->config.video_password, sizeof(out->config.video_password));
    out->ringing = s_intercom_ringing && out->config.enabled;
    out->connected = s_home_live && !s_dead &&
        esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3;
    out->revision = s_intercom_generation;
    out->audio_active = out->config.enabled && out->connected && app_intercom_audio_active_for(out->revision);
    out->audio_elsewhere = false;
    out->ringtone_silenced = false;
#ifdef APP_INTERCOM_DISTRIBUTED
    out->audio_elsewhere = app_intercom_control_other(out->revision);
    out->ringtone_silenced = app_intercom_control_silenced(out->revision);
    out->audio_active = out->audio_active || (out->config.enabled && out->connected && intercom_held(out->revision));
#endif
    char command[96];
    out->gate_supported = lx_home_command_build(s_home, s_catalog, LX_INTERCOM_GATE_ID,
                                                LX_HOME_COMMAND_PULSE, command);
    out->command_pending = s_command_pending;
    snprintf(out->status, sizeof(out->status), "%s", s_command_status);
    xSemaphoreGive(s_lock);
    return true;
}

bool app_loxone_intercom_credentials(unsigned revision, char username[96], char password[LX_PASSWORD_MAX])
{
    if (!s_lock || !s_home || !username || !password)
        return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (revision != s_intercom_generation) {
        xSemaphoreGive(s_lock);
        return false;
    }
    snprintf(username, 96, "%s", s_home->intercom.video_username);
    snprintf(password, LX_PASSWORD_MAX, "%s", s_home->intercom.video_password);
    bool valid = username[0] && password[0];
    xSemaphoreGive(s_lock);
    return valid;
}
