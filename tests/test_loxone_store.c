/* A tényleges AES-GCM és SD-mentő, valódi kriptóval és fájlrendszer-hibákkal. */
#include "app_loxone_store.h"
#include "app_storage.h"
#include "nvs.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned char key[32];static bool have_key,sd_bad;
static int rename_fail,renames,fsync_fail;
void esp_fill_random(void *out,size_t n){FILE *f=fopen("/dev/urandom","rb");assert(f);assert(fread(out,1,n,f)==n);fclose(f);}
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h){(void)ns;(void)mode;*h=1;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *name,void *out,size_t *n){(void)h;(void)name;if(!have_key)return ESP_ERR_NVS_NOT_FOUND;assert(*n==32);memcpy(out,key,32);return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *name,const void *in,size_t n){(void)h;(void)name;assert(n==32);memcpy(key,in,32);have_key=true;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t app_storage_lock(void){return sd_bad?ESP_FAIL:ESP_OK;}
void app_storage_unlock(void){}
int __real_rename(const char *,const char *);
int __wrap_rename(const char *a,const char *b){if(++renames==rename_fail){errno=EIO;return -1;}return __real_rename(a,b);}
int __real_fsync(int);
int __wrap_fsync(int fd){if(fsync_fail){errno=EIO;return -1;}return __real_fsync(fd);}
static cJSON *read_file(void){FILE *f=fopen(APP_SD_MOUNT_POINT "/loxone.json","rb");assert(f);char buf[16385]={0};size_t n=fread(buf,1,sizeof(buf)-1,f);assert(n);fclose(f);cJSON *o=cJSON_Parse(buf);assert(o);return o;}
static void write_file(cJSON *o){char *body=cJSON_PrintUnformatted(o);assert(body);FILE *f=fopen(APP_SD_MOUNT_POINT "/loxone.json","wb");assert(f);assert(fwrite(body,1,strlen(body),f)==strlen(body));fclose(f);free(body);}
int main(void)
{
    lx_config_t cfg,out;lx_config_defaults(&cfg);strcpy(cfg.host,"192.168.2.10");strcpy(cfg.username,"kijelzo");strcpy(cfg.password,"Secret-Test-Password");
    strcpy(cfg.map[0].uuid,"11223344-5566-7788-99aabbccddeeff00");
    for(int i=LX_SUN_FIRST;i<LX_FIELD_COUNT;i++){strcpy(cfg.map[i].uuid,"11223344-5566-7788-99aabbccddeeff00");cfg.map[i].scale=0.5;cfg.map[i].offset=i;}
    assert(app_loxone_store_save(&cfg)==ESP_OK);assert(have_key);
    cJSON *first=read_file();char *serialized=cJSON_PrintUnformatted(first);assert(!strstr(serialized,cfg.password));free(serialized);
    assert(app_loxone_store_load(&out)==ESP_OK&&strcmp(out.password,cfg.password)==0);
    for(int i=LX_SUN_FIRST;i<LX_FIELD_COUNT;i++){assert(strcmp(out.map[i].uuid,cfg.map[i].uuid)==0);assert(out.map[i].scale==0.5&&out.map[i].offset==i);}
    assert(app_loxone_store_save(&cfg)==ESP_OK);cJSON *second=read_file();
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(first,"secret"),"nonce")->valuestring,
                  cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(second,"secret"),"nonce")->valuestring)!=0);
    /* Másik készülék/NVS-törlés: a párosítás megmarad, jelszó nem használható. */
    unsigned char old_key[32];memcpy(old_key,key,32);memset(key,0x7a,32);
    assert(app_loxone_store_load(&out)==ESP_OK&&!out.password[0]&&strcmp(out.map[0].uuid,cfg.map[0].uuid)==0);memcpy(key,old_key,32);
    /* Hitelesített metadata és titkosítvány rongálása nem adhat jelszót. */
    cJSON_ReplaceItemInObject(second,"host",cJSON_CreateString("192.168.2.11"));write_file(second);
    assert(app_loxone_store_load(&out)==ESP_OK&&!out.password[0]);
    write_file(first);cJSON *cipher=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(first,"secret"),"cipher");cipher->valuestring[0]=cipher->valuestring[0]=='a'?'b':'a';write_file(first);
    assert(app_loxone_store_load(&out)==ESP_OK&&!out.password[0]);cJSON_Delete(first);cJSON_Delete(second);
    assert(app_loxone_store_save(&cfg)==ESP_OK);
    lx_config_t next=cfg;strcpy(next.password,"replacement-secret");
    renames=0;rename_fail=2;assert(app_loxone_store_save(&next)!=ESP_OK);rename_fail=0;
    assert(app_loxone_store_load(&out)==ESP_OK&&strcmp(out.password,cfg.password)==0);
    fsync_fail=1;assert(app_loxone_store_save(&next)!=ESP_OK);fsync_fail=0;
    assert(app_loxone_store_load(&out)==ESP_OK&&strcmp(out.password,cfg.password)==0);
    assert(rename(APP_SD_MOUNT_POINT "/loxone.json",APP_SD_MOUNT_POINT "/loxone.json.bak")==0);
    assert(app_loxone_store_load(&out)==ESP_OK&&strcmp(out.password,cfg.password)==0);
    sd_bad=true;assert(app_loxone_store_save(&next)!=ESP_OK);
    puts("PASS: real AES-GCM, random nonce, no plaintext, device key mismatch, tamper rejection, fsync/rename failures and boot recovery");
}
