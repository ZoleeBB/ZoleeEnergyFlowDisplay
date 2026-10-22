/* A valódi kliens kézfogása és fogadása, szimulált Miniserverrel, valódi RSA/AES/HMAC-kel. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/version.h"
#include "../main/app_loxone.c"

/* A hálózati teszt a valódi jelenléttermelőt használja, RTOS/NVS nélkül. */
static app_slave_presence_table_t presence_fixture;
static bool presence_online;
void app_slave_presence_service_online(bool online)
{
    presence_online = online;
    if (!online)
        app_slave_presence_invalidate(&presence_fixture);
}
void app_slave_presence_service_source(const char *uuid, bool valid, double value)
{
    if (presence_online)
        (void)app_slave_presence_source(&presence_fixture, uuid, valid, value);
}
uint32_t app_slave_presence_service_source_revision(void) { return 0; }

typedef struct {unsigned count,head,tail,capacity;size_t item;unsigned char *data;} queue_t;
QueueHandle_t xQueueCreate(unsigned count,size_t item){queue_t *q=calloc(1,sizeof(*q));assert(q);q->capacity=count;q->item=item;q->data=calloc(count,item);assert(q->data);return q;}
int xQueueSend(QueueHandle_t handle,const void *item,TickType_t ticks){(void)ticks;queue_t *q=handle;if(q->count==q->capacity)return 0;memcpy(q->data+q->tail*q->item,item,q->item);q->tail=(q->tail+1)%q->capacity;q->count++;return 1;}
int xQueueReceive(QueueHandle_t handle,void *item,TickType_t ticks){queue_t *q=handle;if(!q->count){(void)ticks;return 0;}memcpy(item,q->data+q->head*q->item,q->item);q->head=(q->head+1)%q->capacity;q->count--;return 1;}
unsigned uxQueueMessagesWaiting(QueueHandle_t handle){return ((queue_t*)handle)->count;}
int xSemaphoreTake(SemaphoreHandle_t mutex,unsigned timeout){(void)mutex;(void)timeout;return 1;}
int xSemaphoreGive(SemaphoreHandle_t mutex){(void)mutex;return 1;}
static bool home_store_error;
esp_err_t app_loxone_home_store_save(const lx_home_config_t *home)
{
    assert(home);
    return home_store_error ? ESP_FAIL : ESP_OK;
}
static void home_requests(void)
{
    s_lock = (void *)1;
    s_home = calloc(1, sizeof(*s_home)); assert(s_home);
    s_requests = xQueueCreate(1, sizeof(request_t *));
    char error[LX_HOME_ERROR_SIZE] = {0};
    cJSON *empty = cJSON_Parse("{\"schema\":1}"); assert(empty);
    assert(app_loxone_home_request(empty, error) == ESP_OK && s_home_busy);
    assert(app_loxone_home_request(empty, error) != ESP_OK);
    request_t *request = NULL; assert(xQueueReceive(s_requests, &request, 0));
    save_home_request(request->home); free(request->home); free(request);
    assert(!s_home_busy && s_home_revision == 1);
    cJSON *state = app_loxone_home_web_state(); assert(state);
    assert(!cJSON_GetObjectItemCaseSensitive(state, "password"));
    assert(cJSON_GetObjectItemCaseSensitive(state, "revision")->valueint == 1);
    cJSON_Delete(state);
    home_store_error = true;
    assert(app_loxone_home_request(empty, error) == ESP_OK);
    assert(xQueueReceive(s_requests, &request, 0));
    save_home_request(request->home); free(request->home); free(request);
    assert(!s_home_busy && s_home_revision == 1 && strstr(s_home_status, "sikertelen"));
    s_busy = true; assert(app_loxone_home_request(empty, error) != ESP_OK); s_busy = false;
    cJSON *invalid = cJSON_Parse("{\"schema\":2}");
    assert(app_loxone_home_request(invalid, error) != ESP_OK);
    cJSON_Delete(invalid); cJSON_Delete(empty); free(s_home); s_home = NULL;
}
static int64_t clock_us;
int64_t esp_timer_get_time(void){return clock_us+=1000;}
void esp_fill_random(void *data,size_t n){FILE *f=fopen("/dev/urandom","rb");assert(f&&fread(data,1,n,f)==n);fclose(f);}
esp_err_t esp_efuse_mac_get_default(uint8_t *mac){for(int i=0;i<6;i++)mac[i]=i;return ESP_OK;}
static char *mock_http_body;
static size_t http_position;
static const char *http_active_body,*http_catalog_body;
static int http_fallback_mode,http_catalog_status=200,http_calls;
static bool http_chunked,http_advertised_oversize;
static int http_active_status;
static char http_catalog_url[768];
static int http_status=200,http_open_error;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
    http_calls++;http_active_status=http_status;http_active_body=mock_http_body;
    assert(cfg->disable_auto_redirect);
    if(strstr(cfg->url,"/jdev/sys/getPublicKey"))assert(!http_fallback_mode);
    else {
        assert(http_fallback_mode);assert(!strstr(cfg->url,"jwt-refreshed-token"));assert(!strstr(cfg->url,"secret"));
        if(strstr(cfg->url,"/jdev/sys/getkey"))http_active_body=http_fallback_mode==2?
            "{\"LL\":{\"Code\":200,\"value\":\"0102030405060708090a0b0c0d0e0f10\"}}":
            "{\"LL\":{\"Code\":200,\"value\":{\"key\":\"0102030405060708090a0b0c0d0e0f10\",\"hashAlg\":\"SHA256\"}}}";
        else {assert(strstr(cfg->url,"/data/LoxAPP3.json?autht="));snprintf(http_catalog_url,sizeof(http_catalog_url),"%s",cfg->url);http_active_body=http_catalog_body;http_active_status=http_catalog_status;}
    }
    http_position=0;return (void*)1;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){(void)h;(void)n;return http_open_error;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){(void)h;return http_advertised_oversize&&strstr(http_catalog_url,"autht=")&&http_active_body==http_catalog_body?LX_STRUCTURE_MAX+1:http_chunked?0:http_active_body?(int64_t)strlen(http_active_body):0;}
int esp_http_client_get_status_code(esp_http_client_handle_t h){(void)h;return http_active_status;}
int esp_http_client_read(esp_http_client_handle_t h,char *out,int max){(void)h;size_t remaining=strlen(http_active_body)-http_position;if(remaining>(size_t)max)remaining=max;if(remaining>7)remaining=7;memcpy(out,http_active_body+http_position,remaining);http_position+=remaining;return remaining;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){(void)h;return http_active_body&&http_position==strlen(http_active_body);}
esp_err_t esp_http_client_close(esp_http_client_handle_t h){(void)h;return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){(void)h;return ESP_OK;}
static mbedtls_pk_context server_key;
static char last_command[4096],previous_salt[17];
static int catalog_mode;
static bool reject_refresh, reject_home_command;
static unsigned home_command_count;
void app_state_set_loxone(bool configured, bool connected) { (void)configured; (void)connected; }
void app_state_clear_sample(void) {}
void app_stats_break_continuity(void) {}
static const char *catalog_reply="{\"controls\":{\"pv\":{\"name\":\"PV\",\"type\":\"InfoOnlyAnalog\",\"states\":{\"value\":\"11223344-5566-7788-99aabbccddeeff00\"}}}}";
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
    unsigned char estimated[8];memcpy(estimated,header,8);estimated[2]=0x80;
    frame(estimated,8,2,true);frame(header,8,2,true);frame((unsigned char*)json,n,1,true);
}
static void decode_url(const char *s,char *out)
{while(*s){if(*s=='%'){unsigned x;assert(sscanf(s+1,"%2x",&x)==1);*out++=x;s+=3;}else *out++=*s++;}*out=0;}
int esp_websocket_client_send_text(esp_websocket_client_handle_t h,const char *text,int len,TickType_t timeout)
{
    (void)h;(void)timeout;char decoded[4096];assert(len<(int)sizeof(decoded));
    if(strncmp(text,"jdev/sys/keyexchange/",21)==0){
        unsigned char cipher[512],plain[512];size_t n,out;
        /* A kulcscsere nyers Base64 adatot vár, nem URL-kódolt parancsot. */
        if(strchr(text+21,'%')){server_response("{\"LL\":{\"Code\":401,\"value\":\"invalid key\"}}");return len;}
        snprintf(decoded,sizeof(decoded),"%s",text+21);
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
            assert(strstr(cmd,"607d176490faff37feb2c44ae19cd3135d03270cc19245695b620246a800ea1c/display/4/"));
            server_response("{\"LL\":{\"Code\":200,\"value\":{\"token\":\"jwt-test-token\"}}}");
        }else if(strcmp(cmd,"data/LoxAPP3.json")==0){
            if(catalog_mode==1){unsigned char huge[8]={3,0,0,0,0,0,0,1};frame(huge,8,2,true);}
            else if(catalog_mode==2)s_dead=true;
            else if(catalog_mode==0)server_response(catalog_reply);
        }
        else if(strncmp(cmd,"jdev/sps/io/",12)==0) {
            assert(strstr(cmd, "jdev/sps/io/cccccccc-5566-7788-99aabbccddeeff00/") == cmd);
            home_command_count++;
            server_response(reject_home_command ? "{\"LL\":{\"Code\":403}}" : "{\"LL\":{\"Code\":200}}");
        }
        else if(strncmp(cmd,"jdev/sys/refreshjwt/",20)==0&&reject_refresh)server_response("{\"LL\":{\"Code\":401}}");
        else if(strncmp(cmd,"jdev/sys/refreshjwt/",20)==0)server_response("{\"LL\":{\"Code\":200,\"value\":{\"token\":\"jwt-refreshed-token\"}}}");
        else server_response("{\"LL\":{\"Code\":200,\"value\":\"OK\"}}");
        return len;
    }
    snprintf(last_command,sizeof(last_command),"%s",text);
    if(strncmp(text,"jdev/sys/getkey2/",17)==0)server_response("{\"LL\":{\"Code\":200,\"value\":{\"key\":\"0102030405060708090a0b0c0d0e0f10\",\"salt\":\"1234\",\"hashAlg\":\"SHA256\"}}}");
    else if(strcmp(text,"jdev/sys/getkey")==0)server_response("{\"LL\":{\"Code\":200,\"value\":{\"key\":\"0102030405060708090a0b0c0d0e0f10\",\"hashAlg\":\"SHA256\"}}}");
    else server_response("{\"LL\":{\"Code\":401,\"value\":\"denied\"}}");
    return len;
}
static void home_control_test(lx_config_t *energy)
{
    cJSON *original_catalog = s_catalog;
    const char *structure = "{\"controls\":{\"aabbccdd-5566-7788-99aabbccddeeff00\":{\"uuidAction\":\"cccccccc-5566-7788-99aabbccddeeff00\",\"type\":\"Switch\",\"isSecured\":false,\"states\":{\"active\":\"00000000-0000-0000-0000000000000000\"}}}}";
    s_catalog = lx_catalog_parse(structure, strlen(structure));
    assert(s_catalog);
    s_home = calloc(1, sizeof(*s_home)); assert(s_home);
    s_home->card_count = 1;
    lx_home_card_t *card = &s_home->cards[0];
    strcpy(card->id, "button"); card->type = LX_HOME_CARD_PULSE;
    strcpy(card->control_uuid, "aabbccdd-5566-7788-99aabbccddeeff00");
    s_home_commands = xQueueCreate(1, sizeof(home_command_t));
    s_home_live = true; s_home_busy = false; s_command_pending = false;
    s_session_config = energy; mark_response();
    /* A felhasználó a worker 100 ms-os fogadási várakozása alatt nyom gombot. */
    int64_t before_receive = esp_timer_get_time();
    clock_us += 50000;
    assert(app_loxone_home_command("button", LX_HOME_COMMAND_PULSE) == ESP_OK);
    assert(app_loxone_home_command("button", LX_HOME_COMMAND_PULSE) != ESP_OK);
    home_command_poll(before_receive);
    assert(home_command_count == 1 && !s_command_pending && strstr(s_command_status, "elküldve"));
    home_command_poll(esp_timer_get_time()); assert(home_command_count == 1);
    card->type = LX_HOME_CARD_SWITCH;
    strcpy(card->source.uuid, "00000000-0000-0000-0000000000000000"); card->source.scale = 1;
    assert(app_loxone_home_command("button", LX_HOME_COMMAND_ON) != ESP_OK);
    s_home_values.cards[0] = (lx_home_value_t){.value = 0, .valid = true};
    before_receive = esp_timer_get_time();
    clock_us += 50000;
    assert(app_loxone_home_command("button", LX_HOME_COMMAND_ON) == ESP_OK);
    home_command_poll(before_receive);
    assert(home_command_count == 2 && s_command_pending && s_home_values.cards[0].value == 0);
    uint8_t packet[24] = {0}; double on = 1; memcpy(packet + 16, &on, sizeof(on));
    assert(update_values(2, packet, sizeof(packet)));
    home_command_poll(esp_timer_get_time());
    assert(!s_command_pending && strstr(s_command_status, "visszaigazolva"));
    assert(app_loxone_home_command("button", LX_HOME_COMMAND_OFF) == ESP_OK);
    home_command_poll(esp_timer_get_time()); assert(s_command_pending);
    clock_us += 6000000; home_command_poll(esp_timer_get_time());
    assert(!s_command_pending && strstr(s_command_status, "Nem érkezett"));
    reject_home_command = true;
    assert(app_loxone_home_command("button", LX_HOME_COMMAND_OFF) == ESP_OK);
    home_command_poll(esp_timer_get_time()); assert(!s_command_pending);
    unsigned sent = home_command_count; home_command_poll(esp_timer_get_time()); assert(home_command_count == sent);
    reject_home_command = false;
    card->type = LX_HOME_CARD_PULSE;
    mark_response(); assert(app_loxone_home_command("button", LX_HOME_COMMAND_PULSE) == ESP_OK);
    invalidate(true);
    assert(!s_command_pending && !uxQueueMessagesWaiting(s_home_commands));
    s_home_live = true; mark_response(); home_command_poll(esp_timer_get_time()); assert(home_command_count == sent);
    /* A másik kapcsolatból származó, illetve túl régi kérés nem hajtható végre. */
    assert(app_loxone_home_command("button", LX_HOME_COMMAND_PULSE) == ESP_OK);
    s_home_session++; home_command_poll(esp_timer_get_time()); assert(home_command_count == sent && !s_command_pending);
    clock_us += 46000000;
    assert(app_loxone_home_command("button", LX_HOME_COMMAND_PULSE) != ESP_OK);
    /* A minimális asszisztens-pillanatkép ugyanazt a valódi WS-értéktárat olvassa. */
    s_home->consumer_count = 1;
    lx_home_consumer_t *consumer = &s_home->consumers[0];
    strcpy(consumer->id, "washer");
    strcpy(consumer->name, "Mosógép");
    strcpy(consumer->source.uuid, "00000000-0000-0000-0000000000000000");
    consumer->source.scale = 1;
    consumer->average_known = consumer->suggest_start = true;
    consumer->average_kw = 2;
    s_home_live = true;
    mark_response();
    app_assistant_consumers_t minimal;
    assert(app_loxone_assistant_snapshot(&minimal));
    assert(minimal.count == 1 && minimal.connected && minimal.consumers[0].eligible);
    assert(!minimal.consumers[0].valid);
    double idle = 0;
    memcpy(packet + 16, &idle, sizeof(idle));
    assert(update_values(2, packet, sizeof(packet)));
    assert(app_loxone_assistant_snapshot(&minimal));
    assert(minimal.consumers[0].valid && minimal.consumers[0].measured_kw == 0);
    invalidate(true);
    assert(app_loxone_assistant_snapshot(&minimal));
    assert(!minimal.connected && !minimal.consumers[0].valid);
    /* A csengetés reteszét nem törli a kapunyitás vagy a kapcsolatvesztés. */
    s_home->intercom.enabled = true;
    strcpy(s_home->intercom.bell_uuid, "00000000-0000-0000-0000000000000000");
    s_home->intercom.gate = *card;
    strcpy(s_home->intercom.gate.id, LX_INTERCOM_GATE_ID);
    s_home->intercom.gate.type = LX_HOME_CARD_PULSE;
    s_home_live = true; s_dead = false; mark_response();
    memcpy(packet + 16, &on, sizeof(on));
    assert(update_values(2, packet, sizeof(packet)));
    app_intercom_snapshot_t door;
    assert(app_loxone_intercom_snapshot(&door) && door.ringing && door.gate_supported);
    unsigned generation = door.revision;
    assert(app_loxone_home_command(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE) == ESP_OK);
    sent = home_command_count;
    home_command_poll(esp_timer_get_time());
    assert(home_command_count == sent + 1 && s_intercom_ringing && !s_command_pending);
    assert(app_loxone_home_command(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE) == ESP_OK);
    memcpy(packet + 16, &idle, sizeof(idle));
    assert(update_values(2, packet, sizeof(packet)));
    home_command_poll(esp_timer_get_time());
    assert(home_command_count == sent + 1 && !s_intercom_ringing && !s_command_pending);
    assert(app_loxone_home_command(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE) != ESP_OK);
    memcpy(packet + 16, &on, sizeof(on));
    assert(update_values(2, packet, sizeof(packet)));
    invalidate(true);
    assert(app_loxone_intercom_snapshot(&door) && door.ringing && !door.connected && door.revision != generation);
    assert(app_loxone_home_command(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE) != ESP_OK);
    s_intercom_ringing = false;
    cJSON_Delete(s_catalog); s_catalog = original_catalog;
    free(s_home); s_home = NULL; s_session_config = NULL; s_home_live = false;
}
static char *load(const char *path){FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=calloc(1,n+1);assert(s&&fread(s,1,n,f)==(size_t)n);fclose(f);return s;}
static void presence_stream_test(lx_config_t *energy)
{
    app_slave_identity_t identity = {0, {1, 2, 3, 4, 5, 6}};
    assert(app_slave_presence_register(&presence_fixture, &identity, "Jelenlét") == 0);
    app_slave_presence_profile_t profile = presence_fixture.peers[0].profile;
    memcpy(profile.presence_uuid, "00000000-0000-0000-0000000000000000", 36);
    profile.blank_at_night = true;
    assert(app_slave_presence_update(&presence_fixture, 0, &profile, NULL));
    s_session_config = energy;
    uint8_t numeric[24] = {0};
    double value = 1;
    memcpy(numeric + 16, &value, sizeof(value));
    s_presence_session_live = false;
    presence_online = false;
    mark_response();
    assert(!presence_online); /* A bejelentkezés válasza még nem jelenlét. */
    assert(update_values(2, numeric, sizeof(numeric)));
    assert(!presence_fixture.peers[0].valid);

    s_presence_session_live = true;
    mark_response();
    assert(presence_online);
    value = 0;
    memcpy(numeric + 16, &value, sizeof(value));
    assert(update_values(2, numeric, sizeof(numeric)));
    app_slave_presence_receiver_t receiver = {0};
    uint8_t wire[APP_SLAVE_PRESENCE_PACKET_SIZE];
    assert(app_slave_presence_encode(&presence_fixture.peers[0], 1, true, wire));
    assert(app_slave_presence_receive(&receiver, wire, sizeof(wire), true, 0));
    value = 1;
    memcpy(numeric + 16, &value, sizeof(value));
    assert(update_values(2, numeric, sizeof(numeric)));
    value = 0;
    memcpy(numeric + 16, &value, sizeof(value));
    assert(update_values(2, numeric, sizeof(numeric)));
    assert(presence_fixture.peers[0].counter == 1 && !presence_fixture.peers[0].active);
    assert(app_slave_presence_encode(&presence_fixture.peers[0], 2, true, wire));
    assert(app_slave_presence_receive(&receiver, wire, sizeof(wire), true, 1));
    assert(receiver.pending_pulse);
    uint32_t counter = presence_fixture.peers[0].counter;
    assert(!update_values(2, numeric, sizeof(numeric) - 1));
    assert(presence_fixture.peers[0].counter == counter);
    value = NAN;
    memcpy(numeric + 16, &value, sizeof(value));
    assert(update_values(2, numeric, sizeof(numeric)));
    assert(!presence_fixture.peers[0].valid);
    invalidate(true);
    assert(!presence_online && !s_presence_session_live);
    s_session_config = NULL;
}

int main(int argc,char **argv)
{
    home_requests();

    assert(argc==3);char *private=load(argv[1]);s_public_key=load(argv[2]);
    mbedtls_pk_init(&server_key);
#if MBEDTLS_VERSION_MAJOR >= 3
    assert(mbedtls_pk_parse_key(&server_key,(unsigned char*)private,strlen(private)+1,NULL,0,rng,NULL)==0);
#else
    assert(mbedtls_pk_parse_key(&server_key,(unsigned char*)private,strlen(private)+1,NULL,0)==0);
#endif
    free(private);s_messages=xQueueCreate(8,sizeof(message_t));s_dead=false;
    assert(key_exchange());
    lx_config_t cfg;lx_config_defaults(&cfg);strcpy(cfg.username,"display");strcpy(cfg.password,"secret]!.+");char token[1024]={0};
    assert(acquire_token(&cfg,token,sizeof(token)));assert(strcmp(token,"jwt-test-token")==0);
    assert(token_command(&cfg,token,"authwithtoken"));assert(strncmp(last_command,"authwithtoken/",14)==0);
    assert(token_command(&cfg,token,"refreshjwt"));assert(strcmp(token,"jwt-refreshed-token")==0);
    home_control_test(&cfg);
    presence_stream_test(&cfg);
    int catalog_code;assert(download_catalog(&catalog_code));assert(cJSON_IsArray(s_catalog)&&cJSON_GetArraySize(s_catalog)==1);assert(strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(s_catalog,0),"name")->valuestring,"PV")==0);assert(strcmp(last_command,"data/LoxAPP3.json")==0);
    catalog_reply="{\"LL\":{\"Code\":403,\"value\":\"denied\"}}";
    assert(!download_catalog(&catalog_code));assert(strstr(s_failure,"403"));assert(strstr(s_failure,"jogosultság"));
    catalog_reply="{\"LL\":{\"code\":\"401\",\"value\":\"denied\"}}";
    assert(!download_catalog(&catalog_code));assert(strstr(s_failure,"401"));
    catalog_reply="{\"wrong\":{}}";assert(!download_catalog(&catalog_code));assert(strstr(s_failure,"controls"));
    catalog_reply="broken JSON";assert(!download_catalog(&catalog_code));assert(strstr(s_failure,"JSON"));
    catalog_mode=1;assert(!download_catalog(&catalog_code));assert(strstr(s_failure,"513 KiB"));s_dead=false;s_rx_failure=LX_RX_OK;
    catalog_mode=2;assert(!download_catalog(&catalog_code));assert(strstr(s_failure,"megszakadt"));s_dead=false;
    catalog_mode=3;assert(!download_catalog(&catalog_code));assert(strstr(s_failure,"12 másodpercen"));catalog_mode=0;
    cJSON_Delete(s_catalog);s_catalog=NULL;
    /* Valós hibamenet: WS fájlkérés 404, külön HTTP challenge és HMAC-tokenes GET. */
    catalog_reply="{\"LL\":{\"Code\":404,\"value\":\"unknown command\"}}";
    http_fallback_mode=1;strcpy(cfg.host,"192.168.2.20");strcpy(cfg.username,"display ]!.+");
    char *large_catalog=malloc(6001);assert(large_catalog);
    strcpy(large_catalog,"{\"controls\":{\"pv\":{\"name\":\"PV\",\"states\":{\"value\":\"11223344-5566-7788-99aabbccddeeff00\"}}}}");size_t prefix=strlen(large_catalog);memset(large_catalog+prefix,' ',6000-prefix);large_catalog[6000]=0;
    http_catalog_body=large_catalog;http_chunked=true;int calls=http_calls;
    assert(download_catalog_with_fallback(&cfg,token));assert(http_calls==calls+2);assert(cJSON_GetArraySize(s_catalog)==1);
    assert(strstr(http_catalog_url,"autht=0466b02bd6a35aa5adc80e557e067e0007ec706d3fec903ca02f13b12b74f0cb&user=display%20%5D%21.%2B"));
    http_fallback_mode=2;assert(download_catalog_with_fallback(&cfg,token));assert(strstr(http_catalog_url,"autht=cd6bf8485a7b0dd4a53208c835e26e93b6618392&"));
    http_catalog_status=403;assert(!download_catalog_with_fallback(&cfg,token));assert(strstr(s_failure,"403"));
    http_catalog_status=301;assert(!download_catalog_with_fallback(&cfg,token));assert(strstr(s_failure,"301"));
    http_catalog_status=200;http_advertised_oversize=true;assert(!download_catalog_with_fallback(&cfg,token));assert(strstr(s_failure,"méret"));http_advertised_oversize=false;
    char *too_large=malloc(LX_STRUCTURE_MAX+2);assert(too_large);memset(too_large,' ',LX_STRUCTURE_MAX+1);too_large[LX_STRUCTURE_MAX+1]=0;
    http_catalog_body=too_large;assert(!download_catalog_with_fallback(&cfg,token));assert(strstr(s_failure,"túl nagy"));free(too_large);http_catalog_body=large_catalog;
    http_open_error=ESP_FAIL;assert(!download_catalog_with_fallback(&cfg,token));assert(strstr(s_failure,"HTTP kapcsolat"));http_open_error=0;
    http_catalog_body="{\"LL\":{\"Code\":401}}";assert(!download_catalog_with_fallback(&cfg,token));assert(strstr(s_failure,"401"));
    http_catalog_body="invalid";assert(!download_catalog_with_fallback(&cfg,token));assert(strstr(s_failure,"JSON"));
    catalog_reply="{\"LL\":{\"Code\":403,\"value\":\"denied\"}}";calls=http_calls;assert(!download_catalog_with_fallback(&cfg,token));assert(http_calls==calls);
    free(large_catalog);http_fallback_mode=0;http_chunked=false;strcpy(cfg.username,"display");
    cJSON_Delete(s_catalog);s_catalog=NULL;
    int64_t before_refresh=esp_timer_get_time();cJSON *expiry=cJSON_CreateObject();
    cJSON_AddNumberToObject(expiry,"validUntil",(double)time(NULL)-1230768000+600);schedule_token_refresh(expiry);cJSON_Delete(expiry);
    assert(s_token_refresh_us-before_refresh>299000000LL&&s_token_refresh_us-before_refresh<301000000LL);
    reject_refresh=true;assert(renew_token(&cfg,token));assert(!s_dead);assert(strcmp(token,"jwt-test-token")==0);reject_refresh=false;
    assert(!reply("denied",false));assert(strstr(s_failure,"401"));
    /* A teljes HTTP-előlépés darabolt válasszal, valamint konkrét hibák. */
    cJSON *key_json=cJSON_CreateObject(),*ll=cJSON_CreateObject();cJSON_AddStringToObject(ll,"value",s_public_key);cJSON_AddItemToObject(key_json,"LL",ll);
    mock_http_body=cJSON_PrintUnformatted(key_json);cJSON_Delete(key_json);free(s_public_key);s_public_key=NULL;
    strcpy(cfg.host,"192.168.2.20");assert(fetch_public_key(&cfg)&&strstr(s_public_key,"BEGIN PUBLIC KEY"));free(s_public_key);s_public_key=NULL;
    http_status=301;assert(!fetch_public_key(&cfg));assert(strstr(s_failure,"301"));report_failure("fallback");assert(strstr(s_status,"301"));
    http_status=200;http_open_error=ESP_FAIL;assert(!fetch_public_key(&cfg));assert(strstr(s_failure,"HTTP kapcsolat"));http_open_error=0;
    free(mock_http_body);mock_http_body=strdup("{}");assert(!fetch_public_key(&cfg));assert(strstr(s_failure,"nem tartalmaz"));free(mock_http_body);mock_http_body=NULL;
    /* Bináris érték két WS-fragmentumban, a driver tovább darabolja 7 bájtra. */
    unsigned char header[8]={3,2,0,0,24,0,0,0},value[24]={0};double power=7.2;memcpy(value+16,&power,8);
    frame(header,8,2,true);frame(value,11,2,false);assert(uxQueueMessagesWaiting(s_messages)==0);frame(value+11,13,0,true);
    message_t m;assert(xQueueReceive(s_messages,&m,0));assert(m.type==2&&m.length==24&&memcmp(m.data,value,24)==0);free_message(&m);
    unsigned char keep[8]={3,6};frame(keep,8,2,true);assert(xQueueReceive(s_messages,&m,0)&&m.type==6);
    /* Túlméretes fejléc, majd a sor feltorlódása kontrolláltan megszakítja a kapcsolatot. */
    unsigned char huge[8]={3,2,0,0,0,0,0,1};frame(huge,8,2,true);assert(s_dead);s_dead=false;
    for(int i=0;i<9;i++)frame(keep,8,2,true);assert(s_dead);
    puts("PASS: real RSA key exchange, SHA256/HMAC JWT acquisition, AES salt chain, token authentication/refresh, denied auth, estimated/exact headers, encrypted catalog download and explicit rejection/format/size/disconnect/timeout diagnostics, HTTP token-HMAC catalog fallback (SHA256/SHA1, chunking, redirects, bounds and no fallback on forbidden), HTTP preflight, driver/WS fragmentation, heartbeat and bounded overflow");
}
