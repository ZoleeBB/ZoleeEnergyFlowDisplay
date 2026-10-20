/* A valódi HTTP-kezelők: hitelesítés, korlátok, darabolás és mód szerinti elérés. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_loxone.h"
#include "esp_http_server.h"
static app_config_t s_config;
static bool authorized = true, header_ok = true, broken;
static int status_code, requested, registered;
static const char *input;
static size_t position;
static char response[1024];
static bool authorize(httpd_req_t *req) { (void)req; if (!authorized) status_code=401; return authorized; }
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s) { (void)r;(void)s;return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v) { (void)r;assert(!strcmp(k,"Cache-Control")&&!strcmp(v,"no-store"));return ESP_OK; }
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s) { (void)r;assert(sscanf(s,"%d",&status_code)==1);return ESP_OK; }
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s) { (void)r;snprintf(response,sizeof(response),"%s",s);return ESP_OK; }
esp_err_t httpd_resp_send_err(httpd_req_t *r,int code,const char *s) { status_code=code;return httpd_resp_sendstr(r,s); }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t n) { (void)r;assert(!strcmp(key,"X-Zolee-Loxone")&&n>=2);if(!header_ok)return ESP_FAIL;strcpy(out,"1");return ESP_OK; }
int httpd_req_recv(httpd_req_t *r,char *out,size_t n) { (void)r;if(broken)return -1;if(n>3)n=3;memcpy(out,input+position,n);position+=n;return (int)n; }
esp_err_t httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *route) { (void)server;assert(route->handler);registered++;return ESP_OK; }
cJSON *app_loxone_web_state(void) { return cJSON_CreateObject(); }
esp_err_t app_loxone_request(const cJSON *root,bool save) { (void)root;(void)save;return ESP_OK; }
cJSON *app_loxone_home_web_state(void) { return cJSON_Parse("{\"schema\":1,\"consumers\":[],\"cards\":[]}"); }
esp_err_t app_loxone_home_request(const cJSON *root,char error[LX_HOME_ERROR_SIZE]) {
    requested++;
    lx_home_config_t *home=calloc(1,sizeof(*home));assert(home);
    bool ok=root&&lx_home_config_parse(root,home,error);free(home);
    return ok?ESP_OK:ESP_ERR_INVALID_ARG;
}
#include "../main/app_loxone_web.inc"
static void post(const char *body)
{
    input=body;position=0;status_code=200;response[0]=0;
    httpd_req_t request={.method=HTTP_POST,.uri="/api/loxone/home",.content_len=strlen(body)};
    loxone_home_post(&request);
}
int main(void)
{
    s_config.data_recept_mode=APP_DATA_HTTP;assert(loxone_register(NULL)==ESP_OK&&!registered);
    s_config.data_recept_mode=APP_DATA_SLAVE;assert(loxone_register(NULL)==ESP_OK&&!registered);
    s_config.data_recept_mode=APP_DATA_MQTT;assert(loxone_register(NULL)==ESP_OK&&!registered);
    s_config.data_recept_mode=APP_DATA_LOXONE;assert(loxone_register(NULL)==ESP_OK&&registered==6);
    authorized=false;post("{\"schema\":1}");assert(status_code==401&&!requested);
    authorized=true;header_ok=false;post("{\"schema\":1}");assert(status_code==400&&!requested);
    header_ok=true;broken=true;post("{\"schema\":1}");assert(status_code==400&&!requested);broken=false;
    post("{\"schema\":2}");assert(status_code==400&&requested==1&&strstr(response,"schema"));
    post("{\"schema\":1} trailing");assert(status_code==400);
    post("{\"schema\":1,\"consumers\":[],\"cards\":[]}");assert(status_code==202&&strstr(response,"accepted"));
    char *large=malloc(32770);memset(large,' ',32769);large[32769]=0;int before=requested;
    post(large);assert(status_code==400&&requested==before);free(large);
    httpd_req_t request={.method=HTTP_GET,.uri="/api/loxone/home"};
    authorized=false;status_code=200;loxone_home_get(&request);assert(status_code==401);
    authorized=true;status_code=200;loxone_home_get(&request);assert(status_code==200&&strstr(response,"consumers")&&!strstr(response,"password"));
    puts("PASS: actual home HTTP handlers, MQTT/HTTP/Slave exclusion, authorization, header, partial receive, bounds, strict JSON, validation details and async 202");
}
