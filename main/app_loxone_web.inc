/* Az app_web.c közös hitelesítésével védett, kizárólag Loxone módban regisztrált útvonalak. */
#include "app_loxone_page.h"
static esp_err_t loxone_page(httpd_req_t *req)
{
    if(!authorize(req))return ESP_OK;
    httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,app_loxone_page);
}
static esp_err_t loxone_get(httpd_req_t *req)
{
    if(!authorize(req))return ESP_OK;
    cJSON *state=app_loxone_web_state();char *json=state?cJSON_PrintUnformatted(state):NULL;cJSON_Delete(state);
    if(!json)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"A Loxone kliens nem érhető el.");
    httpd_resp_set_type(req,"application/json; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t e=httpd_resp_sendstr(req,json);free(json);return e;
}
static esp_err_t loxone_post(httpd_req_t *req)
{
    if(!authorize(req))return ESP_OK;
    char header[8];
    if(httpd_req_get_hdr_value_str(req,"X-Zolee-Loxone",header,sizeof(header))!=ESP_OK||strcmp(header,"1")!=0)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Hiányzó kérésazonosító.");
    if(req->content_len<2||req->content_len>16384)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Érvénytelen kérésméret.");
    char *body=calloc(1,req->content_len+1);if(!body)return ESP_ERR_NO_MEM;
    size_t pos=0;while(pos<req->content_len) {
        int n=httpd_req_recv(req,body+pos,req->content_len-pos);
        if(n<=0){memset(body,0,req->content_len);free(body);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"A kérés megszakadt.");}pos+=n;
    }
    cJSON *root=cJSON_ParseWithLength(body,req->content_len);memset(body,0,req->content_len);free(body);
    bool save=strcmp(req->uri,"/api/loxone/save")==0;
    esp_err_t e=app_loxone_request(root,save);
    cJSON *password=cJSON_GetObjectItemCaseSensitive(root,"password");
    if(cJSON_IsString(password))memset(password->valuestring,0,strlen(password->valuestring));
    cJSON_Delete(root);
    if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ellenőrizd a kapcsolat adatait, a jelszót és a kötelező párosításokat. WSS-hez CA-tanúsítvány is kell. Várd meg a folyamatban lévő műveletet.");
    httpd_resp_set_status(req,"202 Accepted");httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,"{\"accepted\":true}");
}
/* A külön, korlátos profil végpontja ugyanazt a hitelesítést használja. */
static esp_err_t loxone_home_get(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    cJSON *root = app_loxone_home_web_state();
    char *body = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!body)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Az Otthon-profil nem érhető el.");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, body);
    free(body);
    return result;
}

static esp_err_t loxone_home_post(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    char header[8];
    if (httpd_req_get_hdr_value_str(req, "X-Zolee-Loxone", header, sizeof(header)) != ESP_OK ||
        strcmp(header, "1") != 0 || req->content_len < 2 || req->content_len > 32768)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Érvénytelen Otthon-kérés.");
    char *body = calloc(1, req->content_len + 1);
    if (!body)
        return ESP_ERR_NO_MEM;
    size_t position = 0;
    while (position < req->content_len)
    {
        int count = httpd_req_recv(req, body + position, req->content_len - position);
        if (count <= 0)
        {
            free(body);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "A kérés megszakadt.");
        }
        position += (size_t)count;
    }
    cJSON *root = lx_home_document_parse(body, req->content_len);
    free(body);
    char error[LX_HOME_ERROR_SIZE] = "Az Otthon-profil nem fogadható el.";
    esp_err_t result = app_loxone_home_request(root, error);
    cJSON_Delete(root);
    if (result != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"accepted\":true}");
}

static esp_err_t loxone_register(httpd_handle_t server)
{
    if(s_config.data_recept_mode!=APP_DATA_LOXONE)return ESP_OK;
    const httpd_uri_t routes[]={
        {.uri="/loxone",.method=HTTP_GET,.handler=loxone_page},
        {.uri="/api/loxone",.method=HTTP_GET,.handler=loxone_get},
        {.uri="/api/loxone/test",.method=HTTP_POST,.handler=loxone_post},
        {.uri="/api/loxone/save",.method=HTTP_POST,.handler=loxone_post},
        {.uri="/api/loxone/home",.method=HTTP_GET,.handler=loxone_home_get},
        {.uri="/api/loxone/home",.method=HTTP_POST,.handler=loxone_home_post}};
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++) {
        esp_err_t e=httpd_register_uri_handler(server,&routes[i]);if(e!=ESP_OK)return e;
    }return ESP_OK;
}
