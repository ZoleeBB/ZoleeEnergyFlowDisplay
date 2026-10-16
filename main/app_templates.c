/* SD-s sablonkatalógus. Nem futtat szkriptet; minden elem és adatkapcsolat engedélylistás. */
#include "app_templates.h"
#include "app_template_zip.h"
#include "app_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <ctype.h>
#include "app_template_dir.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p,0775)
#define RMDIR(p) rmdir(p)
#endif

#ifdef APP_TEMPLATE_DESKTOP
#define STORAGE_TAKE() ESP_OK
#define STORAGE_GIVE() ((void)0)
#define STORAGE_TRY() true
#else
#define STORAGE_TAKE() app_storage_lock()
#define STORAGE_GIVE() app_storage_unlock()
#define STORAGE_TRY() app_storage_try_lock()
#endif

static SemaphoreHandle_t lock;
static app_template_info_t catalogue[APP_TEMPLATE_MAX];
static unsigned count,generation=1;
static char selected[33]="builtin";
static bool save_selection;
const char *app_templates_root(void){
#ifdef APP_TEMPLATE_DESKTOP
    const char *env=getenv("ZOLEE_TEMPLATE_ROOT");if(env&&*env)return env;
#ifdef APP_TEMPLATE_DEFAULT_ROOT
    struct stat st;
    if(!stat(APP_TEMPLATE_DEFAULT_ROOT,&st))return APP_TEMPLATE_DEFAULT_ROOT;
    if(stat("templates",&st)&&!stat("../templates",&st))return "../templates";
#endif
    return "templates";
#else
    return APP_TEMPLATE_ROOT;
#endif
}
static void path_join(char out[256],const char *base,const char *name){
    size_t a=strlen(base),b=strlen(name);out[0]=0;
    if(a+b+2>256)return;
    memcpy(out,base,a);out[a]='/';memcpy(out+a+1,name,b+1);
}
static bool error_set(char error[160],const char *message){snprintf(error,160,"%s",message);return false;}
bool app_templates_safe_id(const char *id){
    if(!id||!*id||strlen(id)>32)return false;
    for(const char *p=id;*p;p++)if(!(*p>='a'&&*p<='z')&&!isdigit((unsigned char)*p)&&*p!='-'&&*p!='_')return false;
    return strcmp(id,"builtin")!=0;
}
bool app_templates_safe_asset(const char *path){
    if(!path||strlen(path)<5||strlen(path)>64||strcmp(path+strlen(path)-4,".png"))return false;
    for(const char *p=path;p<path+strlen(path)-4;p++)if(!(*p>='a'&&*p<='z')&&!(*p>='0'&&*p<='9')&&*p!='-'&&*p!='_')return false;
    return true;
}
static unsigned char *read_file(const char *path,size_t limit,size_t *size){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);
    if(n<=0||(size_t)n>limit){fclose(f);return NULL;}rewind(f);
    unsigned char *data=malloc((size_t)n+1);if(!data){fclose(f);return NULL;}
    bool ok=fread(data,1,(size_t)n,f)==(size_t)n;fclose(f);
    if(!ok){free(data);return NULL;}data[n]=0;*size=(size_t)n;return data;
}
static uint32_t be32(const unsigned char *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
/* Csak 8 bites, nem interlace RGB/RGBA PNG; CRC és teljes zlib-dekódolás ellenőrzés. */
static bool png_valid(const unsigned char *p,size_t size,unsigned *w,unsigned *h){
    if(size<45||memcmp(p,"\211PNG\r\n\032\n",8))return false;
    size_t pos=8,compressed_size=0;unsigned channels=0;bool ended=false;
    unsigned char *compressed=malloc(size);if(!compressed)return false;
    while(pos+12<=size){
        uint32_t n=be32(p+pos);const unsigned char *type=p+pos+4,*data=p+pos+8;
        if(n>size-pos-12 || (uint32_t)crc32(0,type,n+4)!=be32(data+n))break;
        if(pos==8){
            if(n!=13||memcmp(type,"IHDR",4))break;
            *w=be32(data);*h=be32(data+4);
            if(!*w||!*h||*w>1280||*h>800||data[8]!=8||(data[9]!=2&&data[9]!=6)||data[10]||data[11]||data[12])break;
            channels=data[9]==6?4:3;
        }else if(!memcmp(type,"IDAT",4)){
            memcpy(compressed+compressed_size,data,n);compressed_size+=n;
        }else if(!memcmp(type,"IEND",4)){
            ended=n==0&&pos+12==size;break;
        }else if(!(type[0]&32)){break;} /* Ismeretlen kritikus chunk. */
        pos+=12+n;
    }
    bool ok=false;
    if(ended&&channels&&compressed_size){
        size_t stride=(size_t)*w*channels+1;uLongf expected=stride * (*h),actual=expected;
        unsigned char *decoded=malloc(expected);
        if(decoded){
            ok=uncompress(decoded,&actual,compressed,compressed_size)==Z_OK&&actual==expected;
            for(unsigned y=0;ok&&y<*h;y++)if(decoded[y*stride]>4)ok=false;
            free(decoded);
        }
    }
    free(compressed);return ok;
}
static bool one_of(const char *value,const char *const *list){if(!value)return false;for(int i=0;list[i];i++)if(!strcmp(value,list[i]))return true;return false;}
static const char *str(const cJSON *o,const char *key){return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,key));}
static bool number(const cJSON *o,const char *key,int min,int max,bool optional){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    return (!v&&optional)||(cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=min&&v->valuedouble<=max&&floor(v->valuedouble)==v->valuedouble);
}
static bool color(const char *s){if(!s||strlen(s)!=7||s[0]!='#')return false;for(int i=1;i<7;i++)if(!isxdigit((unsigned char)s[i]))return false;return true;}
static bool duplicates(const cJSON *o){
    if(cJSON_IsObject(o))for(const cJSON *a=o->child;a;a=a->next)for(const cJSON *b=a->next;b;b=b->next)if(!strcmp(a->string,b->string))return true;
    for(const cJSON *a=o->child;a;a=a->next)if(duplicates(a))return true;return false;
}
static bool preview_asset(const char *name){
    return !strcmp(name,"preview-light.png")||!strcmp(name,"preview-dark.png");
}
static bool validate_scene(const cJSON *scene){
    static const char *const types[]={"box","pulse","solar_disc","solar_arc","label","value","image","bar","gauge","flow","chart","weather",NULL};
    static const char *const sources[]={"pv","home","grid","battery","soc","battery_temp","temperature","wind","clock","date","status","grid_direction","battery_direction","production_today","consumption_today","import_today","export_today","charge_today","discharge_today","pv_home","pv_export","pv_stored","pv_home_today","pv_export_today","pv_stored_today","grid_home_today","battery_home_today","self_sufficiency_pct","self_consumption_pct","pv_home_pct","pv_load_pct","pv_export_pct","pv_stored_pct","grid_home_pct","battery_home_pct","purchase_day","revenue_day","savings_day","net_day","purchase_month","revenue_month","savings_month","net_month","grid_net_today","battery_net_today","battery_capacity","battery_energy","sun_azimuth","sun_altitude","sunrise","sunset","sun_direction","weather_label","battery_summary","solar_savings_day","solar_value_today","production_yesterday_pct","savings_yesterday_pct","production_value_yesterday_pct","consumption_yesterday_pct","export_yesterday_pct",NULL};
    if(!cJSON_IsObject(scene)||!color(str(scene,"background")))return false;
    if(cJSON_HasObjectItem(scene,"gradient")&&!color(str(scene,"gradient")))return false;
    const cJSON *palette=cJSON_GetObjectItemCaseSensitive(scene,"palette");
    if(palette){
        const char *keys[]={"pv","home","grid","battery",NULL};
        for(int i=0;keys[i];i++)if(!color(str(palette,keys[i])))return false;
    }
    const cJSON *elements=cJSON_GetObjectItemCaseSensitive(scene,"elements");
    if(!cJSON_IsArray(elements)||cJSON_GetArraySize(elements)>96)return false;
    const cJSON *e;
    cJSON_ArrayForEach(e,elements){
        const char *type=str(e,"type"),*source=str(e,"source");
        const char *file=str(e,"file");if(file&&preview_asset(file))return false;
        const cJSON *variant;cJSON_ArrayForEach(variant,cJSON_GetObjectItem(e,"files"))
            if(cJSON_IsString(variant)&&preview_asset(cJSON_GetStringValue(variant)))return false;
        if(!cJSON_IsObject(e)||!one_of(type,types)||!number(e,"x",0,1279,false)||!number(e,"y",0,799,false)||
           !number(e,"w",1,1280,false)||!number(e,"h",1,800,false))return false;
        if(cJSON_GetObjectItem(e,"x")->valueint+cJSON_GetObjectItem(e,"w")->valueint>1280 ||
           cJSON_GetObjectItem(e,"y")->valueint+cJSON_GetObjectItem(e,"h")->valueint>800)return false;
        if(source&&!one_of(source,sources))return false;
        if(cJSON_HasObjectItem(e,"shadow")&&(!cJSON_IsBool(cJSON_GetObjectItem(e,"shadow"))||
           (strcmp(type,"value")&&strcmp(type,"label"))))return false;
        if(cJSON_HasObjectItem(e,"reference_style")&&(!cJSON_IsBool(cJSON_GetObjectItem(e,"reference_style"))||strcmp(type,"chart")))return false;
        if(cJSON_HasObjectItem(e,"soc_four_bands")&&(!cJSON_IsBool(cJSON_GetObjectItem(e,"soc_four_bands"))||strcmp(type,"bar")||!source||strcmp(source,"soc")))return false;
        if(cJSON_HasObjectItem(e,"foreground")&&(!cJSON_IsBool(cJSON_GetObjectItem(e,"foreground"))||(strcmp(type,"image")&&strcmp(type,"pulse"))))return false;
        if(cJSON_HasObjectItem(e,"text_shadow")&&(!cJSON_IsBool(cJSON_GetObjectItem(e,"text_shadow"))||
           (strcmp(type,"value")&&strcmp(type,"label"))))return false;
        if(!number(e,"glow",0,24,true)||!number(e,"shadow_offset",0,4,true)||!number(e,"shadow_opacity",0,255,true))return false;
        if((!strcmp(type,"value")||!strcmp(type,"bar")||!strcmp(type,"gauge")||!strcmp(type,"flow"))&&!source)return false;
        if(source&&(!strcmp(type,"bar")||!strcmp(type,"gauge")||!strcmp(type,"flow"))&&
           (!strcmp(source,"clock")||!strcmp(source,"date")||!strcmp(source,"status")||strstr(source,"direction")))return false;
        const char *keys[]={"color","background","muted","positive_color","negative_color","zero_color","outline_color","shadow_color",NULL};
        for(int i=0;keys[i];i++)if(cJSON_HasObjectItem(e,keys[i])&&!color(str(e,keys[i])))return false;
        if(!number(e,"center_x",0,1279,true)||!number(e,"center_y",0,799,true)||!number(e,"radius_x",1,640,true)||!number(e,"radius_y",1,390,true))return false;
        if(!number(e,"animate_ms",0,1000,true))return false;
        if(!number(e,"font",12,96,true)||!number(e,"opacity",0,255,true)||!number(e,"radius",0,200,true)||!number(e,"max",1,100,true))return false;
        if(cJSON_HasObjectItem(e,"font")){int f=cJSON_GetObjectItem(e,"font")->valueint;if(f!=12&&f!=16&&f!=20&&f!=28&&f!=48&&f!=96)return false;}
        if(!strcmp(type,"label")&&(!str(e,"text")||strlen(str(e,"text"))>160))return false;
        if(cJSON_HasObjectItem(e,"text_en")&&(!str(e,"text_en")||strlen(str(e,"text_en"))>160))return false;
        if(cJSON_HasObjectItem(e,"align")&&(!str(e,"align")||(strcmp(str(e,"align"),"center")&&strcmp(str(e,"align"),"left"))))return false;
        if(!strcmp(type,"chart")&&
           (cJSON_GetObjectItem(e,"w")->valueint<150||cJSON_GetObjectItem(e,"h")->valueint<((str(e,"kind")&&(!strcmp(str(e,"kind"),"week")||!strcmp(str(e,"kind"),"month")))?70:100)))return false;
        if(cJSON_HasObjectItem(e,"font")&&cJSON_GetObjectItem(e,"font")->valueint>=48&&
           (strcmp(type,"value")||!source||!strcmp(source,"status")||strstr(source,"direction")))return false;
        if(!strcmp(type,"image")&&!app_templates_safe_asset(str(e,"file")))return false;
        if(!strcmp(type,"weather")&&cJSON_HasObjectItem(e,"files")){
            const cJSON *files=cJSON_GetObjectItem(e,"files");
            const char *keys[]={"sunny","partly_cloudy","cloudy","rain","snow","night",NULL};
            if(!cJSON_IsObject(files)||cJSON_GetArraySize(files)!=6)return false;
            for(int i=0;keys[i];i++)if(!app_templates_safe_asset(str(files,keys[i])))return false;
        }
        static const char *const kinds[]={"production","consumption","grid","battery","soc","week","month","solar_hourly",NULL};
        if(!strcmp(type,"chart")&&!one_of(str(e,"kind"),kinds))return false;
        if(!strcmp(type,"flow")){
            const cJSON *pts=cJSON_GetObjectItem(e,"points");int n=cJSON_GetArraySize(pts);
            if(!cJSON_IsArray(pts)||n<2||n>8)return false;
            const char *curve=str(e,"curve");
            if(cJSON_HasObjectItem(e,"curve")&&(!curve||(strcmp(curve,"polyline")&&strcmp(curve,"bezier"))))return false;
            if(curve&&!strcmp(curve,"bezier")&&n!=4&&n!=7)return false;
            for(int i=0;i<n;i++){
                const cJSON *pt=cJSON_GetArrayItem(pts,i);
                if(!number(pt,"x",0,cJSON_GetObjectItem(e,"w")->valueint-1,false)||!number(pt,"y",0,cJSON_GetObjectItem(e,"h")->valueint-1,false))return false;
            }
        }
    }
    return true;
}
bool app_templates_validate(const char *directory,cJSON **document,char error[160]){
    char path[256];path_join(path,directory,"template.json");size_t n=0;
    unsigned char *data=read_file(path,65536,&n);if(!data)return error_set(error,"Hiányzó/túl nagy template.json");
    /* A parser veremhasználatát a bemenettől függetlenül korlátozzuk. */
    int depth=0;bool quoted=false,escaped=false,valid=true;
    for(size_t i=0;i<n;i++){char c=data[i];if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}else if(c=='"')quoted=true;else if(c=='{'||c=='['){if(++depth>16)valid=false;}else if(c=='}'||c==']')depth--;}
    if(memchr(data,0,n))valid=false;
    cJSON *root=valid?cJSON_ParseWithLengthOpts((char*)data,n+1,NULL,true):NULL;free(data);
    if(!root||duplicates(root)||!number(root,"schema",1,1,false)||!app_templates_safe_id(str(root,"id"))||!str(root,"name")||strlen(str(root,"name"))>80||
       !number(root,"version",1,65535,false)||!validate_scene(cJSON_GetObjectItem(root,"light"))||!validate_scene(cJSON_GetObjectItem(root,"dark"))){
        cJSON_Delete(root);return error_set(error,"Hibás sablonleírás / Invalid template schema");
    }
    DIR *dir=opendir(directory);if(!dir){cJSON_Delete(root);return false;}
    struct dirent *entry;size_t encoded=0,decoded=0;unsigned assets=0,previews=0;
    while((entry=readdir(dir))){
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,"..")||!strcmp(entry->d_name,"template.json"))continue;
        if(!app_templates_safe_asset(entry->d_name)||++assets>32){valid=false;break;}
        path_join(path,directory,entry->d_name);unsigned w=0,h=0;
        data=read_file(path,APP_TEMPLATE_PNG_MAX,&n);
        if(!data||!png_valid(data,n,&w,&h)){free(data);valid=false;break;}free(data);
        encoded+=n;
        if(preview_asset(entry->d_name)){
            previews++;if(w>640||h>400)valid=false;
        }else decoded+=(size_t)w*h*4;
        if(encoded>APP_TEMPLATE_ENCODED_MAX||decoded>12*1024*1024){valid=false;break;}
    }
    closedir(dir);
    /* Régi SD-csomag: egyik sincs. Új előnézetek: a két üzemmód együtt érkezik. */
    if(previews==1)valid=false;
    const char *themes[]={"light","dark"};
    for(int t=0;valid&&t<2;t++){
        const cJSON *e; cJSON_ArrayForEach(e,cJSON_GetObjectItem(cJSON_GetObjectItem(root,themes[t]),"elements")){
            if(!strcmp(str(e,"type"),"image")){
                path_join(path,directory,str(e,"file"));struct stat st;
                if(stat(path,&st)||!S_ISREG(st.st_mode))valid=false;
            }else if(!strcmp(str(e,"type"),"weather")){
                const cJSON *file;cJSON_ArrayForEach(file,cJSON_GetObjectItem(e,"files")){
                    path_join(path,directory,cJSON_GetStringValue(file));struct stat st;
                    if(stat(path,&st)||!S_ISREG(st.st_mode))valid=false;
                }
            }
        }
    }
    if(!valid){cJSON_Delete(root);return error_set(error,"Hiányzó, sérült vagy túl nagy PNG / Invalid assets");}
    if(document)*document=root;else cJSON_Delete(root);return true;
}
static void remove_stage(const char *path){
    DIR *d=opendir(path);if(!d)return;struct dirent *e;char child[256];
    while((e=readdir(d))){if(strcmp(e->d_name,"template.json")&&!app_templates_safe_asset(e->d_name))continue;path_join(child,path,e->d_name);remove(child);}
    closedir(d);RMDIR(path);
}
/* Megőrzött katalógussorrend, egy sorban egy sablonazonosítóval. */
static void apply_saved_order(void){
    char file[256];path_join(file,app_templates_root(),"order.txt");
    FILE *fp=fopen(file,"rb");
    if(!fp){path_join(file,app_templates_root(),"order.bak");fp=fopen(file,"rb");}
    if(!fp)return;
    char id[40];unsigned target=1;
    while(target<count && fgets(id,sizeof(id),fp)){
        size_t n=strcspn(id,"\r\n");id[n]=0;
        for(unsigned j=target;j<count;j++)if(!strcmp(catalogue[j].id,id)){
            app_template_info_t entry=catalogue[j];
            memmove(&catalogue[target+1],&catalogue[target],(j-target)*sizeof(entry));
            catalogue[target++]=entry;break;
        }
    }
    fclose(fp);
}
static void scan(void){
    count=1;snprintf(catalogue[0].id,33,"builtin");snprintf(catalogue[0].name,81,"Rendszer");
    DIR *dir=opendir(app_templates_root());if(!dir)return;struct dirent *e;char path[256],error[160];
    while(count<APP_TEMPLATE_MAX&&(e=readdir(dir))){
        if(!app_templates_safe_id(e->d_name))continue;path_join(path,app_templates_root(),e->d_name);cJSON *doc=NULL;
        if(!app_templates_validate(path,&doc,error))continue;
        if(!strcmp(str(doc,"id"),e->d_name)){
            snprintf(catalogue[count].id,33,"%.32s",e->d_name);snprintf(catalogue[count].name,81,"%s",str(doc,"name"));
            catalogue[count].version=cJSON_GetObjectItem(doc,"version")->valueint;count++;
        }
        cJSON_Delete(doc);
    }
    closedir(dir);
    for(unsigned i=1;i<count;i++)for(unsigned j=i+1;j<count;j++)if(strcmp(catalogue[i].id,catalogue[j].id)>0){app_template_info_t t=catalogue[i];catalogue[i]=catalogue[j];catalogue[j]=t;}
    apply_saved_order();
}
esp_err_t app_templates_init(void){
    if(!lock)lock=xSemaphoreCreateMutex();if(!lock)return ESP_ERR_NO_MEM;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(STORAGE_TAKE()!=ESP_OK){xSemaphoreGive(lock);return ESP_FAIL;}
    MKDIR(app_templates_root());char stage[256],backup[256];
    path_join(backup,app_templates_root(),".replace");
    /* Áramszünet közbeni sabloncsere helyreállítása. Ha az új könyvtár
     * már létezik, a régi mentés törölhető; különben a régi visszaáll. */
    char backup_json[256];path_join(backup_json,backup,"template.json");size_t backup_len=0;
    unsigned char *backup_data=read_file(backup_json,65536,&backup_len);
    if(backup_data){
        cJSON *old=cJSON_Parse((const char*)backup_data);free(backup_data);
        const char *old_id=str(old,"id");
        if(app_templates_safe_id(old_id)){
            char old_dir[256];path_join(old_dir,app_templates_root(),old_id);
            struct stat st;
            if(stat(old_dir,&st)!=0)rename(backup,old_dir);
        }
        cJSON_Delete(old);
    }
    remove_stage(backup);
    path_join(stage,app_templates_root(),".install");remove_stage(stage);
    path_join(stage,app_templates_root(),".check");remove_stage(stage);
    scan();char path[256];path_join(path,app_templates_root(),"selection.txt");FILE *f=fopen(path,"rb");
    if(!f){path_join(path,app_templates_root(),"selection.bak");f=fopen(path,"rb");}
    char id[34]={0};if(f){size_t read=fread(id,1,33,f);id[read]=0;fclose(f);}
    snprintf(selected,33,"builtin");for(unsigned i=1;i<count;i++)if(!strcmp(catalogue[i].id,id))snprintf(selected,33,"%.32s",id);
    generation++;STORAGE_GIVE();xSemaphoreGive(lock);return ESP_OK;
}
unsigned app_templates_list(app_template_info_t *out,unsigned capacity,char active[33]){
    if(!lock)return 0;xSemaphoreTake(lock,portMAX_DELAY);unsigned n=count<capacity?count:capacity;
    if(out)memcpy(out,catalogue,n*sizeof(*out));if(active)snprintf(active,33,"%s",selected);xSemaphoreGive(lock);return n;
}
esp_err_t app_templates_reorder(const cJSON *ids,char error[160]){
    if(!lock || !cJSON_IsArray(ids))return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(cJSON_GetArraySize(ids)!=(int)count){xSemaphoreGive(lock);error_set(error,"A teljes sablonlistát meg kell adni");return ESP_ERR_INVALID_ARG;}
    app_template_info_t ordered[APP_TEMPLATE_MAX];bool used[APP_TEMPLATE_MAX]={0};
    for(unsigned i=0;i<count;i++){
        const char *id=cJSON_GetStringValue(cJSON_GetArrayItem(ids,i));unsigned j=0;
        if(!id || (i==0&&strcmp(id,"builtin")) || (i>0&&!strcmp(id,"builtin"))){
            xSemaphoreGive(lock);error_set(error,"A Rendszer téma mindig az első");return ESP_ERR_INVALID_ARG;
        }
        while(j<count && (used[j]||strcmp(catalogue[j].id,id)))j++;
        if(j==count){xSemaphoreGive(lock);error_set(error,"Ismeretlen vagy ismétlődő sablonazonosító");return ESP_ERR_INVALID_ARG;}
        ordered[i]=catalogue[j];used[j]=true;
    }
    if(STORAGE_TAKE()!=ESP_OK){xSemaphoreGive(lock);error_set(error,"SD nem elérhető");return ESP_FAIL;}
    char path[256],temp[256];path_join(path,app_templates_root(),"order.txt");
    path_join(temp,app_templates_root(),"order.tmp");
    FILE *fp=fopen(temp,"wb");bool ok=fp!=NULL;
    if(fp){
        for(unsigned i=1;i<count;i++)if(fprintf(fp,"%s\n",ordered[i].id)<0)ok=false;
        if(fflush(fp)!=0)ok=false;
#ifndef _WIN32
        if(fsync(fileno(fp))!=0)ok=false;
#endif
        if(fclose(fp)!=0)ok=false;
    }
    /* FAT fájlrendszeren a rename nem írhat felül létező célfájlt. */
    char backup[256];path_join(backup,app_templates_root(),"order.bak");
    if(ok){
        remove(backup);
        if(rename(path,backup)!=0 && errno!=ENOENT)ok=false;
        if(ok && rename(temp,path)!=0){rename(backup,path);ok=false;}
        if(ok)remove(backup);
    }
    if(!ok)remove(temp);
    if(ok){memcpy(catalogue,ordered,count*sizeof(*ordered));generation++;}
    STORAGE_GIVE();xSemaphoreGive(lock);
    if(!ok)error_set(error,"A sorrend mentése sikertelen");
    return ok?ESP_OK:ESP_FAIL;
}
esp_err_t app_templates_select(const char *id){
    if(!lock||!id)return ESP_ERR_INVALID_STATE;xSemaphoreTake(lock,portMAX_DELAY);esp_err_t result=ESP_ERR_NOT_FOUND;
    for(unsigned i=0;i<count;i++)if(!strcmp(id,catalogue[i].id)){snprintf(selected,33,"%s",id);generation++;save_selection=true;result=ESP_OK;break;}
    xSemaphoreGive(lock);return result;
}
void app_templates_step_available(int direction,bool sun_available){
    if(!lock||!xSemaphoreTake(lock,0))return;
    unsigned i=0;
    for(unsigned n=0;n<count;n++)if(!strcmp(selected,catalogue[n].id))i=n;
    /* Ha a Loxone T1 nincs bekötve, Nap pálya nem választható lapozással.
     * A már kiválasztott sablon nem vész el; csak a következő lépést szűrjük. */
    for(unsigned attempt=0;attempt<count;attempt++){
        i=direction>0?(i+1)%count:(i+count-1)%count;
        if(sun_available||strcmp(catalogue[i].id,"09-solar-journey")){
            snprintf(selected,33,"%s",catalogue[i].id);generation++;save_selection=true;
            break;
        }
    }
    xSemaphoreGive(lock);
}
void app_templates_step(int direction){app_templates_step_available(direction,true);}
void app_templates_view_free(app_template_view_t *v){if(!v)return;cJSON_Delete(v->document);for(unsigned i=0;i<v->asset_count;i++)free(v->assets[i].bytes);memset(v,0,sizeof(*v));}
bool app_templates_has_pending(unsigned applied_generation){
    if(!lock||!xSemaphoreTake(lock,0))return false;
    bool pending=applied_generation!=generation;
    xSemaphoreGive(lock);return pending;
}
bool app_templates_load_pending(app_template_view_t *out){
    if(!lock||!xSemaphoreTake(lock,0))return false;
    if(out->generation==generation){xSemaphoreGive(lock);return false;}
    app_template_view_t *next=calloc(1,sizeof(*next));if(!next){xSemaphoreGive(lock);return false;}
    if(!STORAGE_TRY()){free(next);xSemaphoreGive(lock);return false;}
    snprintf(next->id,33,"%s",selected);next->generation=generation;
    next->catalogue_count=count;for(unsigned i=0;i<count;i++)if(!strcmp(selected,catalogue[i].id))next->catalogue_index=i;
    char directory[256],path[256],error[160];path_join(directory,app_templates_root(),selected);
    bool ok=!strcmp(selected,"builtin");
    if(!ok&&app_templates_validate(directory,&next->document,error)){
        DIR *d=opendir(directory);struct dirent *e;ok=d!=NULL;
        while(d&&(e=readdir(d))){if(!app_templates_safe_asset(e->d_name)||preview_asset(e->d_name))continue;
            if(next->asset_count>=32){ok=false;break;}
            app_template_asset_t *a=&next->assets[next->asset_count++];snprintf(a->name,65,"%.64s",e->d_name);
            path_join(path,directory,e->d_name);a->bytes=read_file(path,APP_TEMPLATE_PNG_MAX,&a->size);
            if(!a->bytes||a->size<24){ok=false;break;}a->width=be32(a->bytes+16);a->height=be32(a->bytes+20);
        }if(d)closedir(d);
    }
    if(!ok){app_templates_view_free(next);snprintf(next->id,33,"builtin");snprintf(selected,33,"builtin");next->generation=generation;next->catalogue_count=count;save_selection=true;}
    /* Atomi rename csak új fájlnál garantált; a régi választás biztonsági másolat. */
    if(save_selection){
        char tmp[256],bak[256];path_join(path,app_templates_root(),"selection.txt");path_join(tmp,app_templates_root(),"selection.tmp");path_join(bak,app_templates_root(),"selection.bak");
        FILE *f=fopen(tmp,"wb");if(f){bool written=fwrite(selected,1,strlen(selected),f)==strlen(selected);if(fclose(f))written=false;
            if(written){remove(bak);rename(path,bak);if(!rename(tmp,path))save_selection=false;else rename(bak,path);}}
    }
    /* A hívó a régi képeket csak az LVGL objektumok törlése után szabadíthatja fel. */
    *out=*next;free(next);STORAGE_GIVE();xSemaphoreGive(lock);return true;
}
esp_err_t app_templates_install(const char *zip,char error[160]){
    if(!lock){error_set(error,"Sablonkezelő nem elérhető");return ESP_ERR_INVALID_STATE;}xSemaphoreTake(lock,portMAX_DELAY);
    if(STORAGE_TAKE()!=ESP_OK){xSemaphoreGive(lock);error_set(error,"SD unavailable");return ESP_FAIL;}
    char stage[256],dest[256],backup[256];
    path_join(stage,app_templates_root(),".install");remove_stage(stage);MKDIR(stage);
    path_join(backup,app_templates_root(),".replace");
    cJSON *doc=NULL;esp_err_t result=ESP_FAIL;
    if(!app_template_unzip(zip,stage,error)||!app_templates_validate(stage,&doc,error))goto done;
    path_join(dest,app_templates_root(),str(doc,"id"));struct stat st;
    bool exists=stat(dest,&st)==0;
    if(!exists&&count>=APP_TEMPLATE_MAX){
        error_set(error,"A sablonlista megtelt");goto done;
    }
    /* A validált ZIP csak most kerülhet a helyére. A régi téma teljes
     * könyvtárát megőrizzük mindaddig, amíg az új rename nem sikerült. */
    if(exists){
        remove_stage(backup);
        if(rename(dest,backup)){
            error_set(error,"A régi sablon biztonsági mentése sikertelen");goto done;
        }
    }
    if(rename(stage,dest)){
        if(exists)rename(backup,dest);
        error_set(error,"SD mentési hiba; a korábbi sablon visszaállítva");
        goto done;
    }
    /* A UI régi képei memóriában élnek, a generation módosítás után
     * az aktuális téma újra beolvasódik az SD-ről. */
    scan();generation++;result=ESP_OK;
    if(exists)remove_stage(backup);
done:cJSON_Delete(doc);remove_stage(stage);STORAGE_GIVE();xSemaphoreGive(lock);return result;
}
esp_err_t app_templates_delete(const char *id,char error[160]){
    if(!lock||!app_templates_safe_id(id)){error_set(error,"A beépített nézet nem törölhető / Invalid id");return ESP_ERR_INVALID_ARG;}
    xSemaphoreTake(lock,portMAX_DELAY);char path[256];path_join(path,app_templates_root(),id);
    bool found=false;for(unsigned i=1;i<count;i++)if(!strcmp(catalogue[i].id,id))found=true;
    if(!found){xSemaphoreGive(lock);return ESP_ERR_NOT_FOUND;}
    if(STORAGE_TAKE()!=ESP_OK){xSemaphoreGive(lock);error_set(error,"SD unavailable");return ESP_FAIL;}
    if(!strcmp(selected,id)){snprintf(selected,33,"builtin");generation++;save_selection=true;}
    remove_stage(path);struct stat st;bool ok=stat(path,&st)!=0;scan();
    generation++;STORAGE_GIVE();xSemaphoreGive(lock);if(!ok)error_set(error,"Törlési hiba / Delete failed");return ok?ESP_OK:ESP_FAIL;
}

char *app_templates_manifest_json(const char *id){
    if(!lock||!app_templates_safe_id(id))return NULL;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool found=false;
    for(unsigned i=1;i<count;i++)if(!strcmp(catalogue[i].id,id))found=true;
    char *result=NULL;
    if(found&&STORAGE_TAKE()==ESP_OK){
        char dir[256],path[256];path_join(dir,app_templates_root(),id);
        path_join(path,dir,"template.json");
        size_t size=0;result=(char *)read_file(path,65536,&size);
        STORAGE_GIVE();
    }
    xSemaphoreGive(lock);
    return result;
}

esp_err_t app_templates_probe(const char *zip,cJSON **report,char error[160]){
    if(report)*report=NULL;
    if(!lock||!report)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(STORAGE_TAKE()!=ESP_OK){xSemaphoreGive(lock);error_set(error,"SD unavailable");return ESP_FAIL;}
    char stage[256];path_join(stage,app_templates_root(),".check");remove_stage(stage);MKDIR(stage);
    cJSON *doc=NULL,*result=NULL;esp_err_t status=ESP_FAIL;
    if(!app_template_unzip(zip,stage,error)||!app_templates_validate(stage,&doc,error))goto done;
    size_t encoded=0,decoded=0;char path[256];DIR *dir=opendir(stage);struct dirent *entry;
    if(!dir)goto done;
    while((entry=readdir(dir))){
        if(!app_templates_safe_asset(entry->d_name))continue;
        path_join(path,stage,entry->d_name);size_t size=0;unsigned char *png=read_file(path,APP_TEMPLATE_PNG_MAX,&size);
        if(!png){closedir(dir);goto done;}
        encoded+=size;if(!preview_asset(entry->d_name))decoded+=(size_t)be32(png+16)*be32(png+20)*4;free(png);
    }
    closedir(dir);result=cJSON_CreateObject();if(!result){status=ESP_ERR_NO_MEM;goto done;}
    bool exists=false;for(unsigned i=0;i<count;i++)if(!strcmp(catalogue[i].id,str(doc,"id")))exists=true;
    cJSON_AddBoolToObject(result,"valid",true);
    cJSON_AddBoolToObject(result,"updating",exists);
    cJSON_AddBoolToObject(result,"installable",exists||count<APP_TEMPLATE_MAX);
    cJSON_AddStringToObject(result,"id",str(doc,"id"));cJSON_AddStringToObject(result,"name",str(doc,"name"));
    cJSON_AddNumberToObject(result,"version",cJSON_GetObjectItem(doc,"version")->valueint);
    cJSON_AddNumberToObject(result,"encoded_bytes",encoded);cJSON_AddNumberToObject(result,"decoded_image_bytes",decoded);
    cJSON_AddStringToObject(result,"message",exists?"A meglévő téma frissíthető (felülírás biztonsági mentéssel)":count>=APP_TEMPLATE_MAX?"Sablonlista megtelt":"Érvényes, telepíthető sablon");
    *report=result;result=NULL;status=ESP_OK;
done:
    cJSON_Delete(result);cJSON_Delete(doc);remove_stage(stage);STORAGE_GIVE();xSemaphoreGive(lock);return status;
}
