/* Execute the actual template HTTP handlers with binary ZIP/socket faults. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "app_templates.h"
#include "app_storage.h"
#include "app_config.h"
static app_config_t s_config;
static int status,interrupted;static bool authorized=true,header=true,storage_available=true;
static const unsigned char *request_bytes;static size_t request_offset;
static char *response;
static size_t chunk_bytes;static char cache_control[64];
static httpd_uri_t routes[10];static unsigned route_count;
static bool authorize(httpd_req_t *req){(void)req;if(!authorized)status=401;return authorized;}
bool app_storage_is_ok(void){return true;}
esp_err_t app_storage_lock(void){return storage_available?ESP_OK:ESP_FAIL;}
void app_storage_unlock(void){}
esp_err_t httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *r){(void)server;routes[route_count++]=*r;return ESP_OK;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){(void)r;status=atoi(s);return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;if(!strcmp(k,"Cache-Control"))snprintf(cache_control,sizeof(cache_control),"%s",v);return ESP_OK;}
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *data, int size)
{
    (void)req;
    assert(size >= 0);
    if (data && size > 0) {
        response = realloc(response, chunk_bytes + (size_t)size + 1);
        assert(response);
        memcpy(response + chunk_bytes, data, (size_t)size);
        chunk_bytes += (size_t)size;
        response[chunk_bytes] = 0;
    }
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){(void)r;free(response);response=strdup(s);assert(response);return ESP_OK;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int s,const char *text){status=s;return httpd_resp_sendstr(r,text);}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *k,char *out,size_t n){(void)r;(void)k;snprintf(out,n,"%s",header?"1":"");return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t n){if(interrupted)return -1;size_t left=r->content_len-request_offset;if(n>left)n=left;if(n>317)n=317;memcpy(out,request_bytes+request_offset,n);request_offset+=n;return (int)n;}
#include "../main/app_template_web.inc"
static void request(const char *uri,const unsigned char *bytes,size_t size){
 status=200;chunk_bytes=0;cache_control[0]=0;request_bytes=bytes;request_offset=0;free(response);response=NULL;
 httpd_req_t req={.uri=uri,.content_len=size};bool found=false;
 for(unsigned i=0;i<route_count;i++)if(!strncmp(uri,routes[i].uri,strlen(routes[i].uri)) && (uri[strlen(routes[i].uri)]==0||uri[strlen(routes[i].uri)]=='?')){routes[i].handler(&req);found=true;break;}
 assert(found);
}
int main(int argc,char **argv){
 assert(argc==4);setenv("ZOLEE_TEMPLATE_ROOT",argv[2],1);assert(app_templates_init()==ESP_OK);assert(template_register(NULL)==ESP_OK);
 FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);unsigned char *zip=malloc(size);assert(zip&&fread(zip,1,size,f)==size);fclose(f);
 storage_available=false;
 request("/api/templates/preview?id=builtin&mode=light",NULL,0);assert(status==200&&chunk_bytes==sizeof(app_builtin_preview_light));
 request("/api/templates/preview?id=builtin&mode=dark",NULL,0);assert(status==200&&chunk_bytes==sizeof(app_builtin_preview_dark));
 authorized=false;request("/api/templates/preview?id=builtin&mode=light",NULL,0);assert(status==401);authorized=true;
 storage_available=true;
 request("/templates",NULL,0);assert(status==200&&strstr(response,"canvas")&&strstr(response,"Ellenőrzés az eszközön"));
 request("/api/templates/validate",zip,size);assert(status==200);cJSON *report=cJSON_Parse(response);assert(report&&cJSON_IsTrue(cJSON_GetObjectItem(report,"valid"))&&cJSON_IsTrue(cJSON_GetObjectItem(report,"installable")));cJSON_Delete(report);
 assert(app_templates_list(NULL,16,NULL)==1);
 request("/api/templates/install",zip,size);assert(status==200&&app_templates_list(NULL,16,NULL)==2);
 request("/api/templates/validate",zip,size);assert(status==200);report=cJSON_Parse(response);
 assert(report&&cJSON_IsTrue(cJSON_GetObjectItem(report,"installable"))&&cJSON_IsTrue(cJSON_GetObjectItem(report,"updating")));cJSON_Delete(report);
 request("/api/templates/install",zip,size);assert(status==200&&app_templates_list(NULL,16,NULL)==2);
 request("/api/templates/manifest?id=test-layout",NULL,0);assert(status==200);
 report=cJSON_Parse(response);assert(report&&!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(report,"id")),"test-layout"));cJSON_Delete(report);
 request("/api/templates/preview?id=test-layout&mode=light",NULL,0);assert(status==404);
 /* A régi háttér nem helyettesítheti a hiányzó előnézetet. */
 char source_path[512],dest_path[512];unsigned char png[256];
 snprintf(source_path,sizeof(source_path),"%s/test-layout/image.png",argv[2]);
 f=fopen(source_path,"rb");assert(f);size_t png_size=fread(png,1,sizeof(png),f);fclose(f);assert(png_size>0&&png_size<sizeof(png));
 const char *names[]={"day.png","preview-light.png","preview-dark.png"};
 for(unsigned i=0;i<3;i++){
  snprintf(dest_path,sizeof(dest_path),"%s/test-layout/%s",argv[2],names[i]);
  f=fopen(dest_path,"wb");assert(f&&fwrite(png,1,png_size,f)==png_size);fclose(f);
  if(i==0){request("/api/templates/preview?id=test-layout&mode=light",NULL,0);assert(status==404&&chunk_bytes==0);}
 }
 request("/api/templates/preview?id=test-layout&mode=light&v=1",NULL,0);assert(status==200&&chunk_bytes==png_size);
 assert(!strcmp(cache_control,"private, max-age=3600"));
 request("/api/templates/preview?id=test-layout&mode=dark&v=1",NULL,0);assert(status==200&&chunk_bytes==png_size);
 authorized=false;request("/api/templates/preview?id=test-layout&mode=light",NULL,0);assert(status==401&&chunk_bytes==0);authorized=true;
 request("/api/templates/manifest?id=../invalid",NULL,0);assert(status==400);
 header=false;request("/api/templates/validate",zip,size);assert(status==403);header=true;
 authorized=false;request("/api/templates/validate",zip,size);assert(status==401);authorized=true;
 interrupted=1;request("/api/templates/validate",zip,size);assert(status==500);interrupted=0;
 request("/api/templates/validate",zip,0);assert(status==400);
 request("/api/templates/home-icon?id=washing-machine",NULL,0);assert(status==404&&chunk_bytes==0);
 char error[160];assert(app_templates_install(argv[3],error)==ESP_OK);
 request("/api/templates/preview?id=loxone-home&mode=light",NULL,0);assert(status==200&&chunk_bytes>0);
 request("/api/templates/preview?id=loxone-home&mode=dark",NULL,0);assert(status==200&&chunk_bytes>0);
 request("/api/templates/home-icon?id=washing-machine&v=3",NULL,0);assert(status==200&&chunk_bytes>0&&chunk_bytes<=32768);
 authorized=false;request("/api/templates/home-icon?id=washing-machine",NULL,0);assert(status==401&&chunk_bytes==0);authorized=true;
 request("/api/templates/home-icon?id=../washing-machine",NULL,0);assert(status==400&&chunk_bytes==0);
 request("/api/templates/home-icon?id=unknown",NULL,0);assert(status==404&&chunk_bytes==0);
 assert(app_templates_delete("loxone-home",error)==ESP_OK);
 request("/api/templates/home-icon?id=washing-machine",NULL,0);assert(status==404&&chunk_bytes==0);
 free(zip);free(response);puts("PASS: real template HTTP preview page, validation without installation, install, safe replacement, preview, auth/header and interrupted upload");return 0;
}
