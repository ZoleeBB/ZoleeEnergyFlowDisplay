/* Runs the real HTTP handler, validator, state and statistics with socket faults. */
#include "app_http.h"
#include "app_ingest.h"
static httpd_uri_t route;
static const char *request_body, *request_type;
static size_t body_offset;
static int status_code, recv_fault, chunk_size;
static int64_t read_delay;
static bool allow_post;
esp_err_t httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *r){(void)server;route=*r;return ESP_OK;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){(void)r;status_code=atoi(s);return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *t){(void)r;assert(!strcmp(t,"application/json"));return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;if(!strcmp(k,"Allow")){assert(!strcmp(v,"POST"));allow_post=true;}return ESP_OK;}
esp_err_t httpd_resp_send(httpd_req_t *r,const char *b,int n){(void)r;(void)n;cJSON *json=cJSON_Parse(b);assert(json);cJSON_Delete(json);return ESP_OK;}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *k){(void)r;assert(!strcmp(k,"Content-Type"));return strlen(request_type);}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *k,char *out,size_t n){(void)r;(void)k;snprintf(out,n,"%s",request_type);return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t n){
    (void)r;fake_us+=read_delay;if(recv_fault)return recv_fault;
    if(chunk_size && n>(size_t)chunk_size)n=(size_t)chunk_size;
    size_t left=strlen(request_body)-body_offset;if(n>left)n=left;
    memcpy(out,request_body+body_offset,n);body_offset+=n;return (int)n;
}
static void request(int method,const char *body,size_t length,const char *type,int expected){
    request_body=body;request_type=type;body_offset=0;status_code=200;allow_post=false;
    httpd_req_t req={.method=method,.content_len=length};
    esp_err_t e=route.handler(&req);assert(status_code==expected);
    assert((e==ESP_OK)==(expected==200));if(expected==405)assert(allow_post);
}
static void test_http(void){
    reset(instant("2026-10-05",12,0,0));app_state_init();
    assert(app_http_register(NULL)==ESP_OK);assert(route.method==HTTP_ANY);assert(!strcmp(route.uri,"/api/energy"));
    request(HTTP_POST,payload,strlen(payload),"application/json",409);
    request(HTTP_GET,"",0,"",405);
    app_state_set_data_mode(APP_DATA_HTTP);
    request(HTTP_POST,payload,strlen(payload),"application/json",503);
    app_state_set_config_ok(true);app_state_set_time_synced(true);app_state_set_wifi_connected(true);app_state_set_sd_ok(true);
    app_state_set_http_ready(true);
    app_runtime_state_t rt;app_state_get_runtime(&rt);
    assert(!rt.mqtt_connected && app_state_ota_ready(&rt) && !rt.has_valid_sample);
    assert(!app_state_data_timed_out(&rt,fake_us+29999999,30));
    assert(app_state_data_timed_out(&rt,fake_us+30000000,30));
    request(HTTP_POST,payload,strlen(payload),"application/json",503); /* ingestion not initialized */
    assert(app_ingest_init(APP_DATA_HTTP)==ESP_OK);
    request(HTTP_POST,"",0,"application/json",400);
    request(HTTP_POST,"",4097,"application/json",413);
    request(HTTP_POST,payload,strlen(payload),"text/plain",415);
    request(HTTP_POST,payload,strlen(payload),"application/jsonp",415);
    recv_fault=HTTPD_SOCK_ERR_TIMEOUT;request(HTTP_POST,payload,strlen(payload),"application/json",408);recv_fault=0;
    chunk_size=7;read_delay=1000000;request(HTTP_POST,payload,strlen(payload),"application/json",408);read_delay=0;
    request(HTTP_POST,"{",5,"application/json",400);
    request(HTTP_POST,"{}",2,"application/json",400);
    app_state_get_runtime(&rt);assert(!rt.has_valid_sample);
    request(HTTP_POST,payload,strlen(payload),"Application/JSON; charset=utf-8",200);
    app_energy_sample_t s;assert(app_state_get_sample(&s));near(s.power_bat_kw,-2);
    int64_t accepted=s.received_monotonic_us;
    char error[160];assert(app_ingest_process(APP_DATA_MQTT,payload,strlen(payload),error,sizeof(error))==ESP_ERR_INVALID_STATE);
    cJSON *d=cJSON_Parse(payload);cJSON_AddNumberToObject(d,"powergrid",4);char *bad=cJSON_PrintUnformatted(d);
    fake_us+=30000000;request(HTTP_POST,bad,strlen(bad),"application/json",400);cJSON_free(bad);cJSON_Delete(d);
    app_state_get_runtime(&rt);assert(rt.last_valid_packet_monotonic_us==accepted);assert(app_state_data_timed_out(&rt,fake_us,30));
    /* Recovery with unchanged values must still renew the heartbeat. */
    request(HTTP_POST,payload,strlen(payload),"application/json",200);
    app_state_get_runtime(&rt);assert(!app_state_data_timed_out(&rt,fake_us,30));
    /* Exactly 4096 bytes accepted, no arbitrary smaller buffer limit. */
    char *full=malloc(4097);memset(full,' ',4096);memcpy(full,payload,strlen(payload));full[4096]=0;
    request(HTTP_POST,full,4096,"application/json",200);free(full);
    app_state_set_wifi_connected(false);app_state_get_runtime(&rt);assert(!app_state_ota_ready(&rt));
    app_state_set_wifi_connected(true);app_state_set_http_ready(false);app_state_get_runtime(&rt);assert(!app_state_ota_ready(&rt));
    app_state_set_data_mode(APP_DATA_MQTT);app_state_set_mqtt_connected(true);app_state_get_runtime(&rt);assert(app_state_ota_ready(&rt));
    puts("PASS: real HTTP handler (200/400/405/408/409/413/415/503), fragments, shared ingestion, timeout recovery and OTA readiness");tests++;
}
static void test_mode_config(void){
    const char *cases[]={
        "{\"wifi\":{\"ssid\":\"test\"},\"mqtt\":{\"broker\":\"x\"}}",
        "{\"DataReceptMode\":\"HtTp\",\"wifi\":{\"ssid\":\"test\"},\"mqtt\":false}",
        "{\"DataReceptMode\":\"http\",\"wifi\":{\"ssid\":\"test\"},\"http\":{\"data_timeout_seconds\":5}}",
        "{\"DataReceptMode\":\"http\",\"wifi\":{\"ssid\":\"test\"},\"http\":{\"data_timeout_seconds\":3600}}",
        "{\"DataReceptMode\":\"http\",\"wifi\":{\"ssid\":\"test\"},\"http\":{\"data_timeout_seconds\":4}}",
        "{\"DataReceptMode\":\"http\",\"wifi\":{\"ssid\":\"test\"},\"http\":{\"data_timeout_seconds\":3601}}",
        "{\"DataReceptMode\":\"http\",\"wifi\":{\"ssid\":\"test\"},\"http\":false}",
        "{\"DataReceptMode\":\"mqtt\",\"wifi\":{\"ssid\":\"test\"}}",
        "{\"DataReceptMode\":\"invalid\",\"wifi\":{\"ssid\":\"test\"}}",
        "{\"DataReceptMode\":1,\"wifi\":{\"ssid\":\"test\"}}"
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);i++){
        const char *path=APP_SD_MOUNT_POINT "/mode.json";FILE *f=fopen(path,"wb");assert(f);fputs(cases[i],f);fclose(f);
        char error[256];esp_err_t e=app_config_load_file(path,&cfg,error,sizeof(error));assert((e==ESP_OK)==(i<4));
        if(i==0)assert(cfg.data_recept_mode==APP_DATA_MQTT && app_config_data_timeout(&cfg)==30);
        if(i==1)assert(cfg.data_recept_mode==APP_DATA_HTTP && app_config_data_timeout(&cfg)==30);
        if(i==2)assert(app_config_data_timeout(&cfg)==5);
        if(i==3)assert(app_config_data_timeout(&cfg)==3600);
    }tests++;
}
