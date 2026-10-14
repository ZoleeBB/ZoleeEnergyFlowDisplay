#include "app_template_page.h"
/* Az OTA szerver része: ugyanaz a hitelesítés, nincs külön vagy nyilvános admin végpont. */
static bool template_request_authorized(httpd_req_t *req,bool write){
    if(!authorize(req))return false;
    if(write){char header[8]={0};
        if(httpd_req_get_hdr_value_str(req,"X-Zolee-Template",header,sizeof(header))!=ESP_OK||strcmp(header,"1")){
            httpd_resp_set_status(req,"403 Forbidden");httpd_resp_sendstr(req,"Hiányzik a sablonkezelő kérésazonosító.");return false;
        }
    }return true;
}
static esp_err_t template_page(httpd_req_t *req){
    if(!template_request_authorized(req,false))return ESP_OK;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,template_manager_page);
}
static esp_err_t template_list_get(httpd_req_t *req){
    if(!template_request_authorized(req,false))return ESP_OK;
    app_template_info_t list[APP_TEMPLATE_MAX];char active[33]="builtin";unsigned count=app_templates_list(list,APP_TEMPLATE_MAX,active);
    cJSON *root=cJSON_CreateObject();if(!root)return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root,"active",active);cJSON *array=cJSON_AddArrayToObject(root,"templates");
    for(unsigned i=0;i<count;i++){cJSON *item=cJSON_CreateObject();cJSON_AddStringToObject(item,"id",list[i].id);cJSON_AddStringToObject(item,"name",list[i].name);cJSON_AddItemToArray(array,item);}
    char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!json)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");esp_err_t result=httpd_resp_sendstr(req,json);free(json);return result;
}
/* Csak a meglévő katalógusban szereplő, engedélyezett sablonok érhetők el.
 * Az id az URI query-ből jön, de nem használunk általános fájlelérési API-t. */
static bool template_query_id(const httpd_req_t *req,char id[33]){
    const char *q=strchr(req->uri,'?');
    if(!q||strncmp(q,"?id=",4))return false;
    const char *first=q+4,*last=strchr(first,'&');
    size_t n=last?(size_t)(last-first):strlen(first);
    if(n==0||n>32)return false;
    memcpy(id,first,n);id[n]=0;
    return app_templates_safe_id(id);
}
static esp_err_t template_manifest_get(httpd_req_t *req){
    if(!template_request_authorized(req,false))return ESP_OK;
    char id[33];if(!template_query_id(req,id))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Érvénytelen sablonazonosító");
    char *json=app_templates_manifest_json(id);
    if(!json)return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Sablon nem található");
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_sendstr(req,json);free(json);return result;
}
static esp_err_t template_preview_get(httpd_req_t *req){
    if(!template_request_authorized(req,false))return ESP_OK;
    char id[33];if(!template_query_id(req,id))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Érvénytelen sablonazonosító");
    /* A speciális előnézet kis fájl: nem szolgálunk ki tetszőleges,
     * akár több megabájtos képet közvetlenül SD-ről. */
    const char *file=strstr(req->uri,"&mode=dark")?"preview-dark.png":"preview-light.png";
    char *manifest=app_templates_manifest_json(id);
    if(!manifest)return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Sablon nem található");
    free(manifest);
    if(app_storage_lock()!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SD nem elérhető");
    char path[256];snprintf(path,sizeof(path),"%s/%s/%s",app_templates_root(),id,file);
    FILE *f=fopen(path,"rb");
    if(!f){app_storage_unlock();return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Nincs külön előnézeti kép");}
    bool valid=fseek(f,0,SEEK_END)==0;long size=valid?ftell(f):0;
    if(!valid||size<=0||size>512*1024||fseek(f,0,SEEK_SET)!=0){
        fclose(f);app_storage_unlock();
        return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Nincs megfelelő előnézeti kép");
    }
    httpd_resp_set_type(req,"image/png");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    char data[1024];esp_err_t result=ESP_OK;size_t n;
    while((n=fread(data,1,sizeof(data),f))>0){
        if(httpd_resp_send_chunk(req,data,n)!=ESP_OK){result=ESP_FAIL;break;}
    }
    if(ferror(f))result=ESP_FAIL;
    fclose(f);app_storage_unlock();
    return result==ESP_OK?httpd_resp_send_chunk(req,NULL,0):result;
}
static esp_err_t template_change(httpd_req_t *req){
    if(!template_request_authorized(req,true))return ESP_OK;
    if(req->content_len<1||req->content_len>32)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Érvénytelen sablonazonosító");
    char id[33]={0},error[160]="Sablon nem található.";size_t received=0;
    while(received<req->content_len){int n=httpd_req_recv(req,id+received,req->content_len-received);if(n<=0)return httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Kérés időtúllépés");received+=n;}
    if(strlen(id)!=received)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Érvénytelen sablonazonosító");
    esp_err_t result=!strcmp(req->uri,"/api/templates/delete")?app_templates_delete(id,error):app_templates_select(id);
    if(result!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,error);
    return httpd_resp_sendstr(req,"Sikeres");
}
static esp_err_t template_upload(httpd_req_t *req){
    if(!template_request_authorized(req,true))return ESP_OK;
    if(req->content_len<22||req->content_len>APP_TEMPLATE_ZIP_MAX)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"A ZIP legfeljebb 8 MiB lehet");
    if(!app_storage_is_ok())return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Az SD-kártya nem érhető el");
    if(app_storage_lock()!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Az SD-kártya nem érhető el");
    char path[256];snprintf(path,sizeof(path),"%s/.upload.zip",app_templates_root());FILE *f=fopen(path,"wb");
    if(!f){app_storage_unlock();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SD-kártya írási hiba");}
    char buffer[2048],error[160]="Telepítés sikertelen";size_t left=req->content_len;bool ok=true;
    while(left){int n=httpd_req_recv(req,buffer,left>sizeof(buffer)?sizeof(buffer):left);if(n<=0||fwrite(buffer,1,n,f)!=(size_t)n){ok=false;break;}left-=n;}
    if(fclose(f))ok=false;
    app_storage_unlock();
    bool probe=!strcmp(req->uri,"/api/templates/validate");cJSON *report=NULL;
    esp_err_t result=ok?(probe?app_templates_probe(path,&report,error):app_templates_install(path,error)):ESP_FAIL;
    if(app_storage_lock()==ESP_OK){remove(path);app_storage_unlock();}
    if(!ok)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"A ZIP feltöltése megszakadt; nem történt telepítés");
    if(result!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,error);
    if(probe){
        char *json=cJSON_PrintUnformatted(report);cJSON_Delete(report);if(!json)return ESP_ERR_NO_MEM;
        httpd_resp_set_type(req,"application/json");esp_err_t sent=httpd_resp_sendstr(req,json);free(json);return sent;
    }
    return httpd_resp_sendstr(req,"A sablon telepítése vagy frissítése sikeres");
}
static esp_err_t template_register(httpd_handle_t server){
    const httpd_uri_t routes[]={
        {.uri="/templates",.method=HTTP_GET,.handler=template_page},
        {.uri="/api/templates",.method=HTTP_GET,.handler=template_list_get},
        {.uri="/api/templates/manifest",.method=HTTP_GET,.handler=template_manifest_get},
        {.uri="/api/templates/preview",.method=HTTP_GET,.handler=template_preview_get},
        {.uri="/api/templates/select",.method=HTTP_POST,.handler=template_change},
        {.uri="/api/templates/delete",.method=HTTP_POST,.handler=template_change},
        {.uri="/api/templates/validate",.method=HTTP_POST,.handler=template_upload},
        {.uri="/api/templates/install",.method=HTTP_POST,.handler=template_upload}};
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++){esp_err_t err=httpd_register_uri_handler(server,&routes[i]);if(err!=ESP_OK)return err;}
    return ESP_OK;
}
