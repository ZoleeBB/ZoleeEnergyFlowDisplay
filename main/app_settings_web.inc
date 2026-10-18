/*
 * A webes beállítási felület ugyanazt a hitelesítést használja, mint az OTA.
 * A jelszavakat a GET soha nem szolgáltatja ki; a JSON null változatlanul
 * hagyja, a szöveg újraírja, az üres szöveg törli a jelszót.
 */
#include "app_settings_page.h"
#include "app_boot_config.h"

static cJSON *settings_json(const app_config_t *v)
{
    cJSON *root=cJSON_CreateObject();
    if(!root)return NULL;
    cJSON_AddBoolToObject(root,"cardless_slave",v->cardless_slave);
    cJSON_AddNumberToObject(root,"DataSeedEnabled",v->data_seed_enabled?1:0);
    cJSON_AddStringToObject(root,"DataSeedPin",v->data_seed_pin);
    cJSON_AddStringToObject(root,"DataReceptMode",v->data_recept_mode==APP_DATA_SLAVE?"slave":v->data_recept_mode==APP_DATA_LOXONE?"loxone":v->data_recept_mode==APP_DATA_HTTP?"http":"mqtt");
#define OBJ(id) cJSON *id=cJSON_AddObjectToObject(root,#id)
#define STR(o,k,val) cJSON_AddStringToObject(o,k,val)
#define NUM(o,k,val) cJSON_AddNumberToObject(o,k,val)
    OBJ(project);
    STR(project,"title",v->project.title);STR(project,"language",v->project.language);STR(project,"currency",v->project.currency);
    OBJ(wifi);
    STR(wifi,"ssid",v->wifi.ssid);cJSON_AddNullToObject(wifi,"password");NUM(wifi,"reconnect_seconds",v->wifi.reconnect_seconds);
    cJSON *network=cJSON_AddObjectToObject(wifi,"network");
    NUM(network,"use_static_ip",v->wifi.network.use_static_ip?1:0);
    STR(network,"ip",v->wifi.network.ip);STR(network,"gateway",v->wifi.network.gateway);
    STR(network,"subnet",v->wifi.network.subnet);STR(network,"dns",v->wifi.network.dns);
    OBJ(time);
    STR(time,"ntp_server",v->time.ntp_server);STR(time,"timezone",v->time.timezone);
    NUM(time,"auto_dst",v->time.auto_dst?1:0);NUM(time,"utc_offset_minutes",v->time.utc_offset_minutes);
    NUM(time,"startup_retry_seconds",v->time.startup_retry_seconds);
    NUM(time,"resync_hours",v->time.resync_hours);
    NUM(time,"background_retry_minutes",v->time.background_retry_minutes);
    OBJ(mqtt);
    STR(mqtt,"broker",v->mqtt.broker);NUM(mqtt,"port",v->mqtt.port);
    STR(mqtt,"topic",v->mqtt.topic);STR(mqtt,"username",v->mqtt.username);
    cJSON_AddNullToObject(mqtt,"password");
    NUM(mqtt,"data_timeout_seconds",v->mqtt.data_timeout_seconds);
    NUM(mqtt,"reconnect_seconds",v->mqtt.reconnect_seconds);
    OBJ(http);NUM(http,"data_timeout_seconds",v->http.data_timeout_seconds);
    OBJ(display);NUM(display,"day_brightness",v->display.day_brightness);
    NUM(display,"night_brightness",v->display.night_brightness);
    NUM(display,"rotation_degrees",v->display.rotation_degrees);
    OBJ(statistics);
    NUM(statistics,"save_interval_minutes",v->statistics.save_interval_minutes);
    NUM(statistics,"max_integration_gap_seconds",v->statistics.max_integration_gap_seconds);
    OBJ(energy);
    NUM(energy,"battery_capacity_kwh",v->energy.battery_capacity_kwh);
    NUM(energy,"electricity_purchase_price",v->energy.electricity_purchase_price);
    NUM(energy,"feed_in_price",v->energy.feed_in_price);
    NUM(energy,"co2_factor_kg_per_kwh",v->energy.co2_factor_kg_per_kwh);
    NUM(energy,"tree_absorption_kg_per_year",v->energy.tree_absorption_kg_per_year);
    OBJ(ota);
    NUM(ota,"auto_update",v->ota.auto_update?1:0);
    STR(ota,"username",v->ota.username);cJSON_AddNullToObject(ota,"password");
#undef OBJ
#undef STR
#undef NUM
    return root;
}
static esp_err_t settings_response(httpd_req_t *req,int status,const char *message,bool restart,const app_config_t *cfg)
{
    cJSON *root=cJSON_CreateObject();if(!root)return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(root,"ok",status<400);
    cJSON_AddBoolToObject(root,"restart",restart);
    cJSON_AddStringToObject(root,"message",message?message:"");
    if(cfg){cJSON *json=settings_json(cfg);if(json)cJSON_AddItemToObject(root,"config",json);}
    char *body=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(!body)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    if(status==400)httpd_resp_set_status(req,"400 Bad Request");
    else if(status>=500)httpd_resp_set_status(req,"500 Internal Server Error");
    esp_err_t result=httpd_resp_send(req,body,HTTPD_RESP_USE_STRLEN);free(body);
    return result;
}
static esp_err_t settings_page(httpd_req_t *req)
{
    if(!authorize(req))return ESP_OK;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,app_settings_page);
}
static esp_err_t settings_get(httpd_req_t *req)
{
    if(!authorize(req))return ESP_OK;
    cJSON *root=settings_json(&s_config);if(!root)return ESP_ERR_NO_MEM;
    char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(!json)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_send(req,json,HTTPD_RESP_USE_STRLEN);free(json);
    return result;
}
/* Null esetén a webes formon nem megadott titkos értéket változatlanul hagyjuk. */
static void keep_secret(cJSON *root,const char *section,const char *previous)
{
    cJSON *object=cJSON_GetObjectItemCaseSensitive(root,section);
    if(!cJSON_IsObject(object))return;
    cJSON *password=cJSON_GetObjectItemCaseSensitive(object,"password");
    if(cJSON_IsNull(password))
        cJSON_ReplaceItemInObjectCaseSensitive(object,"password",cJSON_CreateString(previous));
}
static esp_err_t settings_post_locked(httpd_req_t *req)
{
    if(!authorize(req))return ESP_OK;
    char header[8]={0};
    if(httpd_req_get_hdr_value_str(req,"X-Zolee-Settings",header,sizeof(header))!=ESP_OK||
       strcmp(header,"1")!=0)
        return settings_response(req,400,"Hiányzik a beállításkezelő kérésazonosítója.",false,NULL);
    if(req->content_len<2||req->content_len>16384)
        return settings_response(req,400,"Érvénytelen konfigurációméret.",false,NULL);
    int length=req->content_len;
    char *body=malloc((size_t)length+1);
    if(!body)return settings_response(req,500,"Nincs elegendő memória.",false,NULL);
    int received=0;int64_t deadline=esp_timer_get_time()+30000000LL;
    while(received<length){
        int n=httpd_req_recv(req,body+received,length-received);
        if(n==HTTPD_SOCK_ERR_TIMEOUT && esp_timer_get_time()<deadline)continue;
        if(n<=0){free(body);return settings_response(req,400,"A konfiguráció feltöltése megszakadt.",false,NULL);}
        received+=n;
    }
    body[received]=0;
    cJSON *root=cJSON_ParseWithLength(body,(size_t)received);
    free(body);
    if(!cJSON_IsObject(root)){cJSON_Delete(root);return settings_response(req,400,"Érvénytelen JSON konfiguráció.",false,NULL);}
    keep_secret(root,"wifi",s_config.wifi.password);
    keep_secret(root,"mqtt",s_config.mqtt.password);
    keep_secret(root,"ota",s_config.ota.password);
    char *data=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(!data)return settings_response(req,500,"Nincs elegendő memória.",false,NULL);
    app_config_t next;char error[256]={0};
    if (s_config.cardless_slave) {
        esp_err_t result = app_boot_config_save(data, strlen(data), &next, error, sizeof(error));
        free(data);
        if (result != ESP_OK) return settings_response(req,400,error[0]?error:"A belső mentés sikertelen.",false,NULL);
        goto apply_settings;
    }
    const char *path=APP_SD_MOUNT_POINT "/config.json";
    const char *tmp=APP_SD_MOUNT_POINT "/config.json.webtmp";
    const char *bak=APP_SD_MOUNT_POINT "/config.json.webbak";
    if(!app_storage_is_ok()||app_storage_lock()!=ESP_OK){free(data);return settings_response(req,500,"Az SD-kártya nem érhető el.",false,NULL);}
    FILE *f=fopen(tmp,"wb");
    size_t bytes=strlen(data);bool ok=f && fwrite(data,1,bytes,f)==bytes;
    free(data);
    if(ok)ok=fflush(f)==0 && fsync(fileno(f))==0;
    if(f && fclose(f)!=0)ok=false;
    if(!ok){
        if(f)remove(tmp);
        app_storage_unlock();
        return settings_response(req,500,"Nem sikerült az SD-kártyára írni.",false,NULL);
    }
    if(app_config_load_file(tmp,&next,error,sizeof(error))!=ESP_OK){
        remove(tmp);app_storage_unlock();
        return settings_response(req,400,error[0]?error:"Hibás konfiguráció.",false,NULL);
    }
    /*
     * FAT-on a meglévő célfájlt nem mindig írja felül a rename. Ezért
     * először mentjük a régit, majd aktiváljuk az újat. Bootkor a backup
     * helyreállítható váratlan tápvesztés esetén.
     */
    remove(bak);
    if(rename(path,bak)!=0){
        remove(tmp);app_storage_unlock();
        return settings_response(req,500,"A régi konfiguráció nem menthető.",false,NULL);
    }
    if(rename(tmp,path)!=0){
        (void)rename(bak,path);remove(tmp);app_storage_unlock();
        return settings_response(req,500,"A konfiguráció nem aktiválható.",false,NULL);
    }
    remove(bak);
    app_storage_unlock();

apply_settings:
    ;
    app_config_t old_cmp=s_config,new_cmp=next;
    memset(&old_cmp.statistics,0,sizeof(old_cmp.statistics));
    memset(&new_cmp.statistics,0,sizeof(new_cmp.statistics));
    bool restart=memcmp(&old_cmp,&new_cmp,sizeof(old_cmp))!=0;
    s_config=next;
    if(restart){
        if(app_stats_save_now()!=ESP_OK)
            return settings_response(req,500,"A konfiguráció mentve, de a statisztika mentése sikertelen. Kézi újraindítás szükséges.",false,NULL);
        if(xTaskCreate(reboot_task,"settings_restart",2048,NULL,8,NULL)!=pdPASS)
            return settings_response(req,500,"Beállítások mentve, de az automatikus újraindítás nem indítható.",false,NULL);
        s_restart_pending = true;
    }else{
        app_stats_update_config(&next.statistics);
    }
    app_events_add(APP_EVENT_INFO,restart?
        "Webes beállítások mentve; újraindítás.":"Webes statisztikai beállítások alkalmazva.");
    return settings_response(req,200,restart?"Sikeres mentés, újraindítás következik.":"Beállítások mentve és alkalmazva.",restart,&s_config);
}
static esp_err_t settings_post(httpd_req_t *req)
{
    return exclusive_request(req, settings_post_locked);
}

static esp_err_t pairing_open_post(httpd_req_t *req)
{
    if (!authorize(req)) return ESP_OK;
    char header[8] = {0};
    if (!s_config.data_seed_enabled ||
        httpd_req_get_hdr_value_str(req, "X-Zolee-Settings", header, sizeof(header)) != ESP_OK ||
        strcmp(header, "1")) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Csak master módban engedélyezhető.");
    }
    app_ble_pairing_open();
    return httpd_resp_sendstr(req, "Párosítás engedélyezve 2 percre.");
}

static esp_err_t settings_register(httpd_handle_t server)
{
    const httpd_uri_t routes[]={
        {.uri="/settings",.method=HTTP_GET,.handler=settings_page},
        {.uri="/api/settings",.method=HTTP_GET,.handler=settings_get},
        {.uri="/api/settings",.method=HTTP_POST,.handler=settings_post},
        {.uri="/api/ble/pairing",.method=HTTP_POST,.handler=pairing_open_post}};
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++){
        esp_err_t result=httpd_register_uri_handler(server,&routes[i]);
        if(result!=ESP_OK)return result;
    }
    return ESP_OK;
}
