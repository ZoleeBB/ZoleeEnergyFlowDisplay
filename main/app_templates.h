#pragma once
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

#define APP_TEMPLATE_MAX 16
#define APP_TEMPLATE_EV_ID "11-electric-car"
#define APP_TEMPLATE_ZIP_MAX (8U * 1024U * 1024U)
/* 1280x800 fotóháttér: egy RGB PNG 2 MiB felett is lehet, a teljes
 * dekódolt összes kép normál témánál 12 MiB, speciális Otthon-csomagnál 20 MiB.
 * A UI-cache változatlan; a speciális csomagból csak az aktív oldal töltődik be. */
#define APP_TEMPLATE_PNG_MAX (5U * 1024U * 1024U / 2U)
#define APP_TEMPLATE_ENCODED_MAX (5U * 1024U * 1024U)
#define APP_TEMPLATE_ROOT APP_SD_MOUNT_POINT "/templates"
typedef struct
{
    char id[33], name[81];
    unsigned version;
} app_template_info_t;
typedef struct
{
    char name[65];
    uint8_t *bytes;   /* A PNG tartalma; NULL, ha csak segédfájlokat töltöttünk be. */
    size_t size;
    unsigned width, height;
} app_template_asset_t;
/*
 * Előre dekódolt (és a megjelenítési méretre hozott) kép az SD-ről: a
 * témaváltáskor sem PNG-dekódolás, sem LVGL-méretezés nem kell hozzá.
 * Lásd app_template_raw.h.
 */
typedef struct
{
    uint8_t *bytes;        /* A teljes segédfájl (fejléccel). */
    size_t size;
    uint16_t width, height, stride;
    uint8_t asset;         /* Index az assets tömbben. */
    uint8_t format;        /* APP_TEMPLATE_RAW_RGB565 vagy RGB565A8. */
} app_template_rawimg_t;
/* A UI tulajdona; a webszál soha nem módosítja ezt a pillanatképet. */
typedef struct
{
    char id[33];
    cJSON *document;
    app_template_asset_t assets[32];
    app_template_rawimg_t raws[32];
    unsigned asset_count, raw_count;
    unsigned generation, catalogue_count, catalogue_index;
} app_template_view_t;

esp_err_t app_templates_init(void);
/* Az autós téma csak közvetlen Loxone módban aktiválható. */
void app_templates_set_loxone_mode(bool enabled);
/*
 * Egyetlen hiányzó, előre dekódolt kép-segédfájl elkészítése (app_template_raw.h).
 * Háttérfeladatból hívandó: a lapozás, a telepítés és a webes feltöltés
 * elsőbbséget élvez, ilyenkor hamissal tér vissza. Igaz: készült egy fájl,
 * rövidesen újra hívható.
 */
bool app_templates_raw_step(void);
/* A fenti lépéseket futtató, alacsony prioritású háttérfeladat indítása. */
esp_err_t app_templates_raw_start(void);
unsigned app_templates_list(app_template_info_t *out, unsigned capacity, char active[33]);
esp_err_t app_templates_select(const char *id);
/* Sorrend: az első elem kötelezően builtin, minden telepített id pontosan egyszer. */
esp_err_t app_templates_reorder(const cJSON *ids, char error[160]);
void app_templates_step(int direction);
/* Ugyanaz a lapozás, de napadat nélkül a Nap pálya sablont átugorja.
 * Hamis érték: a katalóguszár foglalt volt, a kérést a hívó ismételje meg. */
bool app_templates_step_available(int direction, bool sun_available);
esp_err_t app_templates_delete(const char *id, char error[160]);
/* ZIP ellenőrzése telepítés és katalógusmódosítás nélkül. */
esp_err_t app_templates_probe(const char *zip_path, cJSON **report, char error[160]);
esp_err_t app_templates_install(const char *zip_path, char error[160]);
/* GitHubról letöltött téma: a fájlok a download stage-be kerülnek (az SD-n a
 * sablongyökér alatt), majd ugyanaz a validálás és atomi csere fut, mint ZIP-nél. */
const char *app_templates_download_stage(void);
/*
 * Nagy puffer kiírása SD-re rögzített, belső DMA-pufferen keresztül.
 * Az SD-vezérlő DMA-ja a PSRAM-ot nem éri el, ezért a közvetlen fwrite minden
 * blokkhoz futásidejű átmeneti puffert kérne a szűk, DMA-képes keretből
 * („sdmmc_write_sectors: not enough mem”). Igaz, ha a teljes tartalom kiment.
 */
bool app_templates_sd_write(FILE *file, const void *data, size_t size);
void app_templates_download_reset(void);
esp_err_t app_templates_install_downloaded(char error[160]);
/* Visszaadja a telepített téma JSON-ját (malloc); a hívó szabadítja fel. */
char *app_templates_manifest_json(const char *id);
/* SD I/O: kizárólag az LVGL lock előtt, a runtime szálból hívható. */
/* Lock-only query: no SD I/O, safe before showing the loading frame. */
bool app_templates_has_pending(unsigned applied_generation);
bool app_templates_load_pending(app_template_view_t *out);
void app_templates_view_free(app_template_view_t *view);
bool app_templates_validate(const char *directory, cJSON **document, char error[160]);
bool app_templates_safe_id(const char *id);
bool app_templates_safe_asset(const char *path);
const char *app_templates_root(void);

/* Külön, nem lapozható erőforráscsomag a két Loxone Master-oldalhoz.
 * Az általános témák 32 képes korlátja nem változik. */
#define APP_HOME_RESOURCE_ID "loxone-home"
#define APP_HOME_RESOURCE_KIND "loxone-home-resources"
#define APP_HOME_RESOURCE_ICONS 64U
bool app_templates_home_info(app_template_info_t *info, unsigned *generation);
bool app_templates_home_load(const char *const *icons, unsigned count, bool night,
                              app_template_view_t *out);

/* A v2 erőforráscsomagból külön fogyasztói háttér; v1 esetén közös fallback. */
bool app_templates_home_load_for(const char *const *icons, unsigned icon_count, bool night,
                                  bool consumers, app_template_view_t *out);
/* Csak az ikonok (2. helytől); a 0. és 1. hely üres. Kártyalapozáshoz, amikor a
 * már dekódolt háttér és logó változatlan. A generation alapján ellenőrizhető,
 * hogy közben nem cserélődött-e az erőforráscsomag. */
bool app_templates_home_load_icons(const char *const *icons, unsigned icon_count,
                                    app_template_view_t *out);
