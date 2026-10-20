#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

#define APP_TEMPLATE_MAX 16
#define APP_TEMPLATE_ZIP_MAX (8U * 1024U * 1024U)
/* 1280x800 fotóháttér: egy RGB PNG 2 MiB felett is lehet, a teljes
 * dekódolt képmemória korlátja ettől függetlenül 12 MiB marad. */
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
    uint8_t *bytes;
    size_t size;
    unsigned width, height;
} app_template_asset_t;
/* A UI tulajdona; a webszál soha nem módosítja ezt a pillanatképet. */
typedef struct
{
    char id[33];
    cJSON *document;
    app_template_asset_t assets[32];
    unsigned asset_count;
    unsigned generation, catalogue_count, catalogue_index;
} app_template_view_t;

esp_err_t app_templates_init(void);
unsigned app_templates_list(app_template_info_t *out, unsigned capacity, char active[33]);
esp_err_t app_templates_select(const char *id);
/* Sorrend: az első elem kötelezően builtin, minden telepített id pontosan egyszer. */
esp_err_t app_templates_reorder(const cJSON *ids, char error[160]);
void app_templates_step(int direction);
/* Ugyanaz a lapozás, de napadat nélkül a Nap pálya sablont átugorja. */
void app_templates_step_available(int direction, bool sun_available);
esp_err_t app_templates_delete(const char *id, char error[160]);
/* ZIP ellenőrzése telepítés és katalógusmódosítás nélkül. */
esp_err_t app_templates_probe(const char *zip_path, cJSON **report, char error[160]);
esp_err_t app_templates_install(const char *zip_path, char error[160]);
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
