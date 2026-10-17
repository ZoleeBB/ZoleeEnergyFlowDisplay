/* A valódi kliens kézfogása és fogadása, szimulált Miniserverrel, valódi RSA/AES/HMAC-kel. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/version.h"
#include "../main/app_loxone.c"

typedef struct {unsigned count,head,tail,capacity;size_t item;unsigned char *data;} queue_t;
QueueHandle_t xQueueCreate(unsigned count,size_t item){queue_t *q=calloc(1,sizeof(*q));assert(q);q->capacity=count;q->item=item;q->data=calloc(count,item);assert(q->data);return q;}
int xQueueSend(QueueHandle_t handle,const void *item,TickType_t ticks){(void)ticks;queue_t *q=handle;if(q->count==q->capacity)return 0;memcpy(q->data+q->tail*q->item,item,q->item);q->tail=(q->tail+1)%q->capacity;q->count++;return 1;}
int xQueueReceive(QueueHandle_t handle,void *item,TickType_t ticks){queue_t *q=handle;if(!q->count){(void)ticks;return 0;}memcpy(item,q->data+q->head*q->item,q->item);q->head=(q->head+1)%q->capacity;q->count--;return 1;}
unsigned uxQueueMessagesWaiting(QueueHandle_t handle){return ((queue_t*)handle)->count;}
static int64_t clock_us;
int64_t esp_timer_get_time(void){return clock_us+=1000;}
void esp_fill_random(void *data,size_t n){FILE *f=fopen("/dev/urandom","rb");assert(f&&fread(data,1,n,f)==n);fclose(f);}
esp_err_t esp_efuse_mac_get_default(uint8_t *mac){for(int i=0;i<6;i++)mac[i]=i;return ESP_OK;}
static mbedtls_pk_context server_key;
static char last_command[4096],previous_salt[17];
static void frame(const unsigned char *data,size_t length,int opcode,bool fin)
{
    /* Több driver-chunk és valódi WS-fragmentáció külön-külön is szerepel. */
    size_t pos=0;while(pos<length){size_t n=length-pos;if(n>7)n=7;
        esp_websocket_event_data_t e={.data_ptr=(char*)data+pos,.data_len=(int)n,.payload_len=(int)length,.payload_offset=(int)pos,.op_code=opcode,.fin=fin};
        receive_event(NULL,NULL,WEBSOCKET_EVENT_DATA,&e);pos+=n;}
}
static void server_response(const char *json)
{
    unsigned char header[8]={3,0,0,0};size_t n=strlen(json);for(int i=0;i<4;i++)header[4+i]=n>>(8*i);
    frame(header,8,2,true);frame((unsigned char*)json,n,1,true);
}
static void decode_url(const char *s,char *out)
{while(*s){if(*s=='%'){unsigned x;assert(sscanf(s+1,"%2x",&x)==1);*out++=x;s+=3;}else *out++=*s++;}*out=0;}
int esp_websocket_client_send_text(esp_websocket_client_handle_t h,const char *text,int len,TickType_t timeout)
{
    (void)h;(void)timeout;char decoded[4096];assert(len<(int)sizeof(decoded));
    if(strncmp(text,"jdev/sys/keyexchange/",21)==0){
        unsigned char cipher[512],plain[512];size_t n,out;decode_url(text+21,decoded);
        assert(mbedtls_base64_decode(cipher,sizeof(cipher),&n,(unsigned char*)decoded,strlen(decoded))==0);
        assert(mbedtls_pk_decrypt(&server_key,cipher,n,plain,&out,sizeof(plain),rng,NULL)==0);plain[out]=0;
        char k[65],iv[33],expected[100];hex(s_aes_key,32,k);hex(s_iv,16,iv);snprintf(expected,sizeof(expected),"%s:%s",k,iv);assert(strcmp((char*)plain,expected)==0);
        server_response("{\"LL\":{\"Code\":\"200\",\"value\":\"OK\"}}");return len;
    }
    if(strncmp(text,"jdev/sys/enc/",13)==0){
        unsigned char cipher[4096],plain[4096],iv[16];size_t n;decode_url(text+13,decoded);
        assert(mbedtls_base64_decode(cipher,sizeof(cipher),&n,(unsigned char*)decoded,strlen(decoded))==0);
        mbedtls_aes_context aes;mbedtls_aes_init(&aes);memcpy(iv,s_iv,16);
        assert(mbedtls_aes_setkey_dec(&aes,s_aes_key,256)==0);assert(mbedtls_aes_crypt_cbc(&aes,MBEDTLS_AES_DECRYPT,n,iv,cipher,plain)==0);mbedtls_aes_free(&aes);plain[n]=0;
        char *cmd=(char*)plain;
        if(previous_salt[0]){assert(strncmp(cmd,"nextSalt/",9)==0);cmd+=9;assert(strncmp(cmd,previous_salt,16)==0);assert(cmd[16]=='/');cmd+=17;}
        else {assert(strncmp(cmd,"salt/",5)==0);cmd+=5;}
        memcpy(previous_salt,cmd,16);previous_salt[16]=0;assert(cmd[16]=='/');cmd+=17;
        snprintf(last_command,sizeof(last_command),"%s",cmd);
        if(strncmp(cmd,"jdev/sys/getjwt/",16)==0){
            assert(strstr(cmd,"e55d6e884eb8904ec9c0e207670b3777716763d0aa594a4a5fb045c5ed5140e4/display/4/"));
            server_response("{\"LL\":{\"Code\":200,\"value\":{\"token\":\"jwt-test-token\"}}}");
        }else if(strncmp(cmd,"jdev/sys/refreshjwt/",20)==0)server_response("{\"LL\":{\"Code\":200,\"value\":{\"token\":\"jwt-refreshed-token\"}}}");
        else server_response("{\"LL\":{\"Code\":200,\"value\":\"OK\"}}");
        return len;
    }
    snprintf(last_command,sizeof(last_command),"%s",text);
    if(strncmp(text,"jdev/sys/getkey2/",17)==0)server_response("{\"LL\":{\"Code\":200,\"value\":{\"key\":\"0102030405060708090a0b0c0d0e0f10\",\"salt\":\"1234\",\"hashAlg\":\"SHA256\"}}}");
    else if(strcmp(text,"jdev/sys/getkey")==0)server_response("{\"LL\":{\"Code\":200,\"value\":{\"key\":\"0102030405060708090a0b0c0d0e0f10\",\"hashAlg\":\"SHA256\"}}}");
    else server_response("{\"LL\":{\"Code\":401,\"value\":\"denied\"}}");
    return len;
}
static char *load(const char *path){FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=calloc(1,n+1);assert(s&&fread(s,1,n,f)==(size_t)n);fclose(f);return s;}
int main(int argc,char **argv)
{
    assert(argc==3);char *private=load(argv[1]);s_public_key=load(argv[2]);
    mbedtls_pk_init(&server_key);
#if MBEDTLS_VERSION_MAJOR >= 3
    assert(mbedtls_pk_parse_key(&server_key,(unsigned char*)private,strlen(private)+1,NULL,0,rng,NULL)==0);
#else
    assert(mbedtls_pk_parse_key(&server_key,(unsigned char*)private,strlen(private)+1,NULL,0)==0);
#endif
    free(private);s_messages=xQueueCreate(8,sizeof(message_t));s_dead=false;
    assert(key_exchange());
    lx_config_t cfg;lx_config_defaults(&cfg);strcpy(cfg.username,"display");strcpy(cfg.password,"secret");char token[1024]={0};
    assert(acquire_token(&cfg,token,sizeof(token)));assert(strcmp(token,"jwt-test-token")==0);
    assert(token_command(&cfg,token,"authwithtoken"));assert(strncmp(last_command,"authwithtoken/",14)==0);
    assert(token_command(&cfg,token,"refreshjwt"));assert(strcmp(token,"jwt-refreshed-token")==0);
    assert(!reply("denied",false));
    /* Bináris érték két WS-fragmentumban, a driver tovább darabolja 7 bájtra. */
    unsigned char header[8]={3,2,0,0,24,0,0,0},value[24]={0};double power=7.2;memcpy(value+16,&power,8);
    frame(header,8,2,true);frame(value,11,2,false);assert(uxQueueMessagesWaiting(s_messages)==0);frame(value+11,13,0,true);
    message_t m;assert(xQueueReceive(s_messages,&m,0));assert(m.type==2&&m.length==24&&memcmp(m.data,value,24)==0);free_message(&m);
    unsigned char keep[8]={3,6};frame(keep,8,2,true);assert(xQueueReceive(s_messages,&m,0)&&m.type==6);
    /* Túlméretes fejléc, majd a sor feltorlódása kontrolláltan megszakítja a kapcsolatot. */
    unsigned char huge[8]={3,2,0,0,0,0,0,1};frame(huge,8,2,true);assert(s_dead);s_dead=false;
    for(int i=0;i<9;i++)frame(keep,8,2,true);assert(s_dead);
    puts("PASS: real RSA key exchange, SHA256/HMAC JWT acquisition, AES salt chain, token authentication/refresh, denied auth, driver/WS fragmentation, heartbeat and bounded overflow");
}
