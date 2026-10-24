#include "app_web_upload.inc"
#include "app_support_web.h"
#include "app_template_page.h"
#include "app_builtin_previews.h"
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
    return app_web_send_page(req,template_manager_page,true);
}
static esp_err_t template_list_get(httpd_req_t *req){
    if(!template_request_authorized(req,false))return ESP_OK;
    app_template_info_t list[APP_TEMPLATE_MAX];char active[33]="builtin";unsigned count=app_templates_list(list,APP_TEMPLATE_MAX,active);
    cJSON *root=cJSON_CreateObject();if(!root)return ESP_ERR_NO_MEM;
    bool disabled = s_config.energy_flow_disabled && s_config.data_recept_mode == APP_DATA_LOXONE;
    cJSON_AddBoolToObject(root, "energy_flow_disabled", disabled);
    if (disabled) count = 0;
    cJSON_AddStringToObject(root,"active",disabled ? APP_HOME_RESOURCE_ID : active);cJSON *array=cJSON_AddArrayToObject(root,"templates");
    for(unsigned i=0;i<count;i++){cJSON *item=cJSON_CreateObject();cJSON_AddStringToObject(item,"id",list[i].id);cJSON_AddStringToObject(item,"name",list[i].name);cJSON_AddNumberToObject(item,"version",list[i].version);cJSON_AddItemToArray(array,item);}
    app_template_info_t resource;
    cJSON *resources=cJSON_AddArrayToObject(root,"resources");
    bool installed = app_templates_home_info(&resource,NULL);
    if (disabled && !installed)
        resource = (app_template_info_t){.id = APP_HOME_RESOURCE_ID, .name = "Loxone Otthon"};
    if(resources&&(installed || disabled)){
        cJSON *item=cJSON_CreateObject();
        if(item){cJSON_AddBoolToObject(item,"installed",installed);cJSON_AddStringToObject(item,"id",resource.id);cJSON_AddStringToObject(item,"name",resource.name);cJSON_AddNumberToObject(item,"version",resource.version);cJSON_AddItemToArray(resources,item);}
    }
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
    return !strcmp(id,"builtin")||app_templates_safe_id(id);
}
static esp_err_t template_manifest_get(httpd_req_t *req){
    if(!template_request_authorized(req,false))return ESP_OK;
    char id[33];if(!template_query_id(req,id))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Érvénytelen sablonazonosító");
    if (s_config.energy_flow_disabled && strcmp(id, APP_HOME_RESOURCE_ID))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Az Energiaáramlás tiltás aktív.");
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
    /* Kizárólag a mentett LVGL-előnézet: régi csomagnál 404 és üres keret.
     * Nincs nagy háttérkép vagy pontatlan böngészős rajz tartalékként. */
    if (s_config.energy_flow_disabled && strcmp(id, APP_HOME_RESOURCE_ID))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Az Energiaáramlás tiltás aktív.");
    bool dark=strstr(req->uri,"&mode=dark")!=NULL;
    if(!strcmp(id,"builtin")){
        const unsigned char *png=dark?app_builtin_preview_dark:app_builtin_preview_light;
        size_t size=dark?sizeof(app_builtin_preview_dark):sizeof(app_builtin_preview_light);
        httpd_resp_set_type(req,"image/png");
        httpd_resp_set_hdr(req,"Cache-Control","private, no-cache");
        esp_err_t result=httpd_resp_send_chunk(req,(const char *)png,size);
        return result==ESP_OK?httpd_resp_send_chunk(req,NULL,0):result;
    }
    const char *filename=dark?"preview-dark.png":"preview-light.png";
    app_template_info_t list[APP_TEMPLATE_MAX];unsigned count=app_templates_list(list,APP_TEMPLATE_MAX,NULL);
    bool known=false;for(unsigned i=1;i<count;i++)if(!strcmp(list[i].id,id))known=true;
    if(!strcmp(id,APP_HOME_RESOURCE_ID))known=app_templates_home_info(NULL,NULL);
    if(!known)return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Sablon nem található");
    if(app_storage_lock()!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SD nem elérhető");
    char path[256];FILE *f=NULL;long size=0;
    snprintf(path,sizeof(path),"%s/%s/%s",app_templates_root(),id,filename);
    f=fopen(path,"rb");
    if(f && (fseek(f,0,SEEK_END)!=0 || (size=ftell(f))<=0 ||
             size>APP_TEMPLATE_PNG_MAX || fseek(f,0,SEEK_SET)!=0)){
        fclose(f);f=NULL;
    }
    if(!f){
        app_storage_unlock();
        return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Nincs előnézeti kép");
    }
    httpd_resp_set_type(req,"image/png");
    httpd_resp_set_hdr(req,"Cache-Control","private, max-age=3600");
    char data[1024];esp_err_t result=ESP_OK;size_t n;
    while((n=fread(data,1,sizeof(data),f))>0){
        if(httpd_resp_send_chunk(req,data,n)!=ESP_OK){result=ESP_FAIL;break;}
    }
    if(ferror(f))result=ESP_FAIL;
    fclose(f);app_storage_unlock();
    return result==ESP_OK?httpd_resp_send_chunk(req,NULL,0):result;
}
/* A telepített csomag kiválasztott 96×96-os ikonja, általános fájlböngészés nélkül. */
static esp_err_t template_home_icon_get(httpd_req_t *req)
{
    if (!template_request_authorized(req, false))
        return ESP_OK;
    char id[33];
    if (!template_query_id(req, id) || !strcmp(id, "builtin"))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Érvénytelen ikonazonosító");
    if (!app_templates_home_info(NULL, NULL))
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "A Loxone Otthon csomag nincs telepítve");
    if (app_storage_lock() != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD nem elérhető");
    char path[256];
    snprintf(path, sizeof(path), "%s/loxone-home/icon-%s.png", app_templates_root(), id);
    FILE *file = fopen(path, "rb");
    long size = 0;
    if (file && (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 ||
                 size > 32768 || fseek(file, 0, SEEK_SET)))
    {
        fclose(file);
        file = NULL;
    }
    if (!file)
    {
        app_storage_unlock();
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Ikon nem található");
    }
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "private, no-cache");
    char buffer[1024];
    size_t count;
    esp_err_t result = ESP_OK;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        result = httpd_resp_send_chunk(req, buffer, count);
        if (result != ESP_OK)
            break;
    }
    if (ferror(file)) result = ESP_FAIL;
    fclose(file);
    app_storage_unlock();
    return result == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : result;
}
static esp_err_t template_change(httpd_req_t *req){
    if(!template_request_authorized(req,true))return ESP_OK;
    if(req->content_len<1||req->content_len>32)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Érvénytelen sablonazonosító");
    char id[33]={0},error[160]="Sablon nem található.";size_t received=0;
    while(received<req->content_len){int n=httpd_req_recv(req,id+received,req->content_len-received);if(n<=0)return httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Kérés időtúllépés");received+=n;}
    if(strlen(id)!=received)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Érvénytelen sablonazonosító");
    if (s_config.energy_flow_disabled)
    {
        if (strcmp(id, APP_HOME_RESOURCE_ID))
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Az Energiaáramlás tiltás aktív; csak a Loxone Otthon érhető el.");
        if (!strcmp(req->uri, "/api/templates/select"))
            return httpd_resp_sendstr(req, "A Loxone Otthon az alapnézet.");
    }
    esp_err_t result=!strcmp(req->uri,"/api/templates/delete")?app_templates_delete(id,error):app_templates_select(id);
    if(result!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,error);
    return httpd_resp_sendstr(req,"Sikeres");
}
static esp_err_t template_upload(httpd_req_t *req){
    if(!template_request_authorized(req,true))return ESP_OK;
    if(req->content_len<22||req->content_len>APP_TEMPLATE_ZIP_MAX)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"A ZIP legfeljebb 8 MiB lehet");
    char path[256], error[160]="Telepítés sikertelen";
    snprintf(path,sizeof(path),"%s/.upload.zip",app_templates_root());
    bool ok=receive_sd_zip(req,path,error);
    bool probe=!strcmp(req->uri,"/api/templates/validate");cJSON *report=NULL;
    if (ok && s_config.energy_flow_disabled)
    {
        cJSON *inspection = NULL;
        esp_err_t checked = app_templates_probe(path, &inspection, error);
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(inspection, "id");
        bool allowed = checked == ESP_OK && cJSON_IsString(id) && !strcmp(id->valuestring, APP_HOME_RESOURCE_ID);
        cJSON_Delete(inspection);
        if (!allowed)
        {
            if (app_storage_lock() == ESP_OK) { remove(path); app_storage_unlock(); }
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ebben a módban csak a Loxone Otthon erőforráscsomag telepíthető.");
        }
    }
    esp_err_t result=ok?(probe?app_templates_probe(path,&report,error):app_templates_install(path,error)):ESP_FAIL;
    if(app_storage_lock()==ESP_OK){remove(path);app_storage_unlock();}
    if(!ok)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,error);
    if(result!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,error);
    if(probe){
        char *json=cJSON_PrintUnformatted(report);cJSON_Delete(report);if(!json)return ESP_ERR_NO_MEM;
        httpd_resp_set_type(req,"application/json");esp_err_t sent=httpd_resp_sendstr(req,json);free(json);return sent;
    }
    return httpd_resp_sendstr(req,"A sablon telepítése vagy frissítése sikeres");
}
static esp_err_t template_reorder_post(httpd_req_t *req){
    if(!template_request_authorized(req,true))return ESP_OK;
    if (s_config.energy_flow_disabled)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Az Energiaáramlás tiltás aktív.");
    if(req->content_len<2||req->content_len>2048)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Hibás sorrend");
    char input[2049];size_t received=0;
    while(received<(size_t)req->content_len){
        int n=httpd_req_recv(req,input+received,req->content_len-received);
        if(n<=0)return httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Kérés időtúllépés");
        received+=(size_t)n;
    }
    input[received]=0;
    cJSON *ids=cJSON_Parse(input);char error[160]="Érvénytelen sorrend";
    esp_err_t result=app_templates_reorder(ids,error);cJSON_Delete(ids);
    if(result!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,error);
    return httpd_resp_sendstr(req,"Sorrend mentve");
}
static esp_err_t template_register(httpd_handle_t server){
    const httpd_uri_t routes[]={
        {.uri="/templates",.method=HTTP_GET,.handler=template_page},
        {.uri="/api/templates",.method=HTTP_GET,.handler=template_list_get},
        {.uri="/api/templates/manifest",.method=HTTP_GET,.handler=template_manifest_get},
        {.uri="/api/templates/preview",.method=HTTP_GET,.handler=template_preview_get},
        {.uri="/api/templates/home-icon",.method=HTTP_GET,.handler=template_home_icon_get},
        {.uri="/api/templates/select",.method=HTTP_POST,.handler=template_change},
        {.uri="/api/templates/reorder",.method=HTTP_POST,.handler=template_reorder_post},
        {.uri="/api/templates/delete",.method=HTTP_POST,.handler=template_change},
        {.uri="/api/templates/validate",.method=HTTP_POST,.handler=template_upload},
        {.uri="/api/templates/install",.method=HTTP_POST,.handler=template_upload}};
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++){esp_err_t err=httpd_register_uri_handler(server,&routes[i]);if(err!=ESP_OK)return err;}
    return ESP_OK;
}
