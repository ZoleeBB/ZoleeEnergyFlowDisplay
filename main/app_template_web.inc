/* Az OTA szerver része: ugyanaz a hitelesítés, nincs külön vagy nyilvános admin végpont. */
static bool template_request_authorized(httpd_req_t *req,bool write){
    if(!authorize(req))return false;
    if(write){char header[8]={0};
        if(httpd_req_get_hdr_value_str(req,"X-Zolee-Template",header,sizeof(header))!=ESP_OK||strcmp(header,"1")){
            httpd_resp_set_status(req,"403 Forbidden");httpd_resp_sendstr(req,"Missing template request header");return false;
        }
    }return true;
}
static esp_err_t template_page(httpd_req_t *req){
    if(!template_request_authorized(req,false))return ESP_OK;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,
        "<!doctype html><html lang='hu'><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
        "<title>Zolee – Sablonok</title><style>body{font:16px system-ui;max-width:860px;margin:40px auto;padding:20px;background:#f3f6f7;color:#14202a}"
        "article{background:white;padding:20px;margin:12px 0;border-radius:14px}button{padding:10px;margin:6px;border:0;border-radius:6px;cursor:pointer}"
        "#message{white-space:pre-wrap}h1{font-size:28px}</style><a href='/'>← Eszköz / Device</a><h1>Sablonkezelő / Templates</h1>"
        "<p>A világos/sötét témát a NightMode vezérli. Lapozás csak kézzel.</p>"
        "<article><input type='file' id='zip' accept='.zip'><button id='upload'>ZIP feltöltése / Upload</button>"
        "<p>Legfeljebb 8 MiB. A ZIP gyökerében template.json és PNG képek. Létező sablon nem írható felül.</p></article>"
        "<p id='message' role='status'></p><main id='list'></main><script>"
        "const message=document.getElementById('message'),list=document.getElementById('list');"
        "async function action(path,body){message.textContent='Feldolgozás / Processing…';try{let r=await fetch(path,{method:'POST',headers:{'X-Zolee-Template':'1'},body});"
        "let text=await r.text();message.textContent=text;if(r.ok)await refresh();}catch(e){message.textContent='Kapcsolati hiba / Connection failed';}}"
        "async function refresh(){let r=await fetch('/api/templates',{cache:'no-store'});if(!r.ok)throw Error(r.status);let data=await r.json();list.replaceChildren();"
        "for(let t of data.templates){let a=document.createElement('article'),title=document.createElement('strong');title.textContent=t.name+(t.id===data.active?' ✓':'');a.append(title);"
        "let b=document.createElement('button');b.textContent='Aktiválás / Activate';b.onclick=()=>action('/api/templates/select',t.id);a.append(b);"
        "if(t.id!=='builtin'){let d=document.createElement('button');d.textContent='Törlés / Delete';d.onclick=()=>{if(confirm('Sablon törlése? / Delete template?'))action('/api/templates/delete',t.id)};a.append(d);}list.append(a);}}"
        "document.getElementById('upload').onclick=()=>{let f=document.getElementById('zip').files[0];if(!f)return;if(f.size>8388608){message.textContent='Túl nagy ZIP / ZIP too large';return;}action('/api/templates/install',f);};"
        "refresh().catch(()=>message.textContent='A lista nem olvasható / Cannot load templates');</script></html>");
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
static esp_err_t template_change(httpd_req_t *req){
    if(!template_request_authorized(req,true))return ESP_OK;
    if(req->content_len<1||req->content_len>32)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid id");
    char id[33]={0},error[160]="Sablon nem található / Template not found";size_t received=0;
    while(received<req->content_len){int n=httpd_req_recv(req,id+received,req->content_len-received);if(n<=0)return httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Request timeout");received+=n;}
    if(strlen(id)!=received)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid id");
    esp_err_t result=!strcmp(req->uri,"/api/templates/delete")?app_templates_delete(id,error):app_templates_select(id);
    if(result!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,error);
    return httpd_resp_sendstr(req,"Sikeres / OK");
}
static esp_err_t template_upload(httpd_req_t *req){
    if(!template_request_authorized(req,true))return ESP_OK;
    if(req->content_len<22||req->content_len>APP_TEMPLATE_ZIP_MAX)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ZIP size limit: 8 MiB");
    if(!app_storage_is_ok())return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SD unavailable");
    if(app_storage_lock()!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SD unavailable");
    char path[256];snprintf(path,sizeof(path),"%s/.upload.zip",app_templates_root());FILE *f=fopen(path,"wb");
    if(!f){app_storage_unlock();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SD write failed");}
    char buffer[2048],error[160]="Install failed";size_t left=req->content_len;bool ok=true;
    while(left){int n=httpd_req_recv(req,buffer,left>sizeof(buffer)?sizeof(buffer):left);if(n<=0||fwrite(buffer,1,n,f)!=(size_t)n){ok=false;break;}left-=n;}
    if(fclose(f))ok=false;
    app_storage_unlock();
    esp_err_t result=ok?app_templates_install(path,error):ESP_FAIL;
    if(app_storage_lock()==ESP_OK){remove(path);app_storage_unlock();}
    if(!ok)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"ZIP upload interrupted; nothing installed");
    if(result!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,error);
    return httpd_resp_sendstr(req,"Sablon ellenőrizve és telepítve / Template validated and installed");
}
static esp_err_t template_register(httpd_handle_t server){
    const httpd_uri_t routes[]={
        {.uri="/templates",.method=HTTP_GET,.handler=template_page},
        {.uri="/api/templates",.method=HTTP_GET,.handler=template_list_get},
        {.uri="/api/templates/select",.method=HTTP_POST,.handler=template_change},
        {.uri="/api/templates/delete",.method=HTTP_POST,.handler=template_change},
        {.uri="/api/templates/install",.method=HTTP_POST,.handler=template_upload}};
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++){esp_err_t err=httpd_register_uri_handler(server,&routes[i]);if(err!=ESP_OK)return err;}
    return ESP_OK;
}
