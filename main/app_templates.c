/* SD-s sablonkatalógus. Nem futtat szkriptet; minden elem és adatkapcsolat engedélylistás. */
#include "app_templates.h"
#include "app_template_raw.h"
#include "app_template_zip.h"
#include "app_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifndef APP_TEMPLATE_DESKTOP
#include "freertos/task.h"
#endif
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
#include <fcntl.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0775)
#define RMDIR(p) rmdir(p)
#endif
#ifndef APP_TEMPLATE_DESKTOP
#include "esp_heap_caps.h"
#include "esp_timer.h"
#endif

#ifdef APP_TEMPLATE_DESKTOP
#define STORAGE_TAKE() ESP_OK
#define STORAGE_GIVE() ((void)0)
#define STORAGE_TRY() true
#define STORAGE_READY() true
#else
#define STORAGE_TAKE() app_storage_lock()
#define STORAGE_GIVE() app_storage_unlock()
#define STORAGE_TRY() app_storage_try_lock()
#define STORAGE_READY() raw_storage_ready()
/*
 * Hibásra jelölt vagy majdnem tele kártyára nem írunk segédfájlt: a dekódolt
 * képek csak gyorsítások, a statisztika és a konfiguráció viszont nem. Egy
 * haldokló vagy betelt kártyát a háttérgenerálás csak tovább terhelne.
 */
static bool raw_storage_ready(void)
{
    if (!app_storage_is_ok())
        return false;
    uint64_t total = 0, free_bytes = 0;
    if (!app_storage_space(&total, &free_bytes))
        return true;
    return free_bytes >= 64ULL * 1024 * 1024;
}
#endif

static SemaphoreHandle_t lock;
static app_template_info_t catalogue[APP_TEMPLATE_MAX];
static unsigned count, generation = 1;
static char selected[33] = "builtin";
static bool save_selection;
static bool home_ready;
static app_template_info_t home_info;
static unsigned home_generation = 1;
/*
 * A teljes (PNG-dekódolással járó) ellenőrzésen már átment sablonkönyvtárak
 * ujjlenyomata. Az ujjlenyomat a könyvtár minden bejegyzésének nevéből,
 * méretéből és módosítási idejéből készül. Ha lapozáskor változatlan, a
 * könyvtár tartalma azonos a már ellenőrzöttel: a képeket nem kell másodszor
 * beolvasni és kitömöríteni, elég a ténylegesen betöltött PNG-k CRC-je.
 * Bármilyen eltérésnél (csere, törlés, új fájl, SD-csere) a korábbi teljes
 * ellenőrzés fut. A tömböt a katalógus zárja védi; a scan() újraépíti.
 */
typedef struct
{
    char id[33];
    uint64_t fingerprint;
} validated_directory_t;
static validated_directory_t validated[APP_TEMPLATE_MAX];
static unsigned validated_count;
const char *app_templates_root(void)
{
#ifdef APP_TEMPLATE_DESKTOP
    const char *env = getenv("ZOLEE_TEMPLATE_ROOT");
    if (env && *env)
        return env;
#ifdef APP_TEMPLATE_DEFAULT_ROOT
    struct stat st;
    if (!stat(APP_TEMPLATE_DEFAULT_ROOT, &st))
        return APP_TEMPLATE_DEFAULT_ROOT;
    if (stat("templates", &st) && !stat("../templates", &st))
        return "../templates";
#endif
    return "templates";
#else
    return APP_TEMPLATE_ROOT;
#endif
}
static void path_join(char out[256], const char *base, const char *name)
{
    size_t a = strlen(base), b = strlen(name);
    out[0] = 0;
    if (a + b + 2 > 256)
        return;
    memcpy(out, base, a);
    out[a] = '/';
    memcpy(out + a + 1, name, b + 1);
}
static bool error_set(char error[160], const char *message)
{
    snprintf(error, 160, "%s", message);
    return false;
}
bool app_templates_safe_id(const char *id)
{
    if (!id || !*id || strlen(id) > 32)
        return false;
    for (const char *p = id; *p; p++)
        if (!(*p >= 'a' && *p <= 'z') && !isdigit((unsigned char)*p) && *p != '-' && *p != '_')
            return false;
    return strcmp(id, "builtin") != 0;
}
bool app_templates_safe_asset(const char *path)
{
    if (!path || strlen(path) < 5 || strlen(path) > 64 || strcmp(path + strlen(path) - 4, ".png"))
        return false;
    for (const char *p = path; p < path + strlen(path) - 4; p++)
        if (!(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9') && *p != '-' && *p != '_')
            return false;
    return true;
}
#ifndef _WIN32
/*
 * Fájlpuffer az SD-ről olvasott sablonadatokhoz.
 *
 * Az SDMMC-meghajtó csak cache-sorhoz igazított PSRAM-pufferbe olvas
 * közvetlenül, több blokkos DMA-val. Igazítatlan célnál blokkonként
 * (512 bájt) külön parancsot és belső átmeneti puffert használ, ami egy
 * 1280x800-as háttér-PNG-nél több ezer SD-parancsot jelent. A 128 bájt az
 * ESP32-P4 L2 cache-sorának legnagyobb mérete, így kisebb sorméretnél is
 * megfelel. A puffer ugyanúgy PSRAM-ban van, mint az eddigi malloc() esetén,
 * és a hívók változatlanul free()-vel szabadítják fel.
 */
#define FILE_BUFFER_ALIGNMENT 128U
static unsigned char *file_buffer_alloc(size_t size)
{
#ifndef APP_TEMPLATE_DESKTOP
    unsigned char *data = heap_caps_aligned_alloc(FILE_BUFFER_ALIGNMENT, size,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data)
        return data;
#endif
    /* Foglalási hiba esetén a korábbi, igazítatlan út: lassabb, de helyes. */
    return malloc(size);
}

/*
 * Teljes fájl beolvasása egyetlen POSIX read() hívássorozattal.
 *
 * A newlib fread() a FATFS-en alapértelmezésben 128 bájtos stdio-pufferen
 * keresztül olvas (CONFIG_FATFS_VFS_FSTAT_BLKSIZE=0), így minden SD-szektor
 * külön parancs lenne. A read() a teljes kérést átadja a FATFS-nek, amely a
 * klaszterhatárig közvetlenül a hívó pufferébe olvas. A méretkorlát, a
 * lezáró nulla bájt és a hibakezelés megegyezik a korábbi változattal.
 */
static unsigned char *read_file(const char *path, size_t limit, size_t *size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uintmax_t)st.st_size > (uintmax_t)limit)
    {
        close(fd);
        return NULL;
    }
    size_t n = (size_t)st.st_size;
    unsigned char *data = file_buffer_alloc(n + 1);
    if (!data)
    {
        close(fd);
        return NULL;
    }
    size_t done = 0;
    while (done < n)
    {
        ssize_t chunk = read(fd, data + done, n - done);
        if (chunk <= 0)
            break;
        done += (size_t)chunk;
    }
    close(fd);
    if (done != n)
    {
        free(data);
        return NULL;
    }
    data[n] = 0;
    *size = n;
    return data;
}
#else
/* Windowsos szimulátor: a korábbi, hordozható stdio-út. */
static unsigned char *read_file(const char *path, size_t limit, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END))
    {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n <= 0 || (size_t)n > limit)
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    unsigned char *data = malloc((size_t)n + 1);
    if (!data)
    {
        fclose(f);
        return NULL;
    }
    bool ok = fread(data, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok)
    {
        free(data);
        return NULL;
    }
    data[n] = 0;
    *size = (size_t)n;
    return data;
}
#endif
static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
/*
 * A PNG chunk-szerkezetének bejárása: aláírás, IHDR-korlátok, minden chunk
 * CRC-je, ismeretlen kritikus chunk tiltása és a fájlvégi IEND.
 * Ha a compressed puffer meg van adva, az IDAT-tartalmat egymás után oda
 * másolja a későbbi zlib-ellenőrzéshez; NULL esetén csak a hosszt összegzi.
 * A verify_crc=false csak olyan hívónak való, amely a képet közvetlenül
 * utána az LVGL lodepng-dekóderével teljesen kitömöríti: az a chunk-CRC-t és
 * az Adler-32-t is ellenőrzi, és a hibát a hívó ugyanúgy kezeli.
 * Igazat ad, ha a szerkezet hibátlan és van tömörített képadat.
 */
static bool png_scan(const unsigned char *p,
                     size_t size,
                     unsigned *w,
                     unsigned *h,
                     unsigned *channels_out,
                     unsigned char *compressed,
                     size_t *compressed_out,
                     bool verify_crc)
{
    if (size < 45 || memcmp(p, "\211PNG\r\n\032\n", 8))
        return false;
    size_t pos = 8, compressed_size = 0;
    unsigned channels = 0;
    bool ended = false;
    while (pos + 12 <= size)
    {
        uint32_t n = be32(p + pos);
        const unsigned char *type = p + pos + 4, *data = p + pos + 8;
        if (n > size - pos - 12)
            break;
        if (verify_crc && (uint32_t)crc32(0, type, n + 4) != be32(data + n))
            break;
        if (pos == 8)
        {
            if (n != 13 || memcmp(type, "IHDR", 4))
                break;
            *w = be32(data);
            *h = be32(data + 4);
            if (!*w || !*h || *w > 1280 || *h > 800 || data[8] != 8 ||
                (data[9] != 2 && data[9] != 6) || data[10] || data[11] || data[12])
                break;
            channels = data[9] == 6 ? 4 : 3;
        }
        else if (!memcmp(type, "IDAT", 4))
        {
            if (compressed)
                memcpy(compressed + compressed_size, data, n);
            compressed_size += n;
        }
        else if (!memcmp(type, "IEND", 4))
        {
            ended = n == 0 && pos + 12 == size;
            break;
        }
        else if (!(type[0] & 32))
        {
            break;
        } /* Ismeretlen kritikus chunk. */
        pos += 12 + n;
    }
    *channels_out = channels;
    *compressed_out = compressed_size;
    return ended && channels && compressed_size;
}
/* Csak 8 bites, nem interlace RGB/RGBA PNG; CRC és teljes zlib-dekódolás ellenőrzés. */
static bool png_valid(const unsigned char *p, size_t size, unsigned *w, unsigned *h)
{
    if (size < 45 || memcmp(p, "\211PNG\r\n\032\n", 8))
        return false;
    unsigned char *compressed = malloc(size);
    if (!compressed)
        return false;
    unsigned channels = 0;
    size_t compressed_size = 0;
    bool ok = false;
    if (png_scan(p, size, w, h, &channels, compressed, &compressed_size, true))
    {
        size_t stride = (size_t)*w * channels + 1;
        uLongf expected = stride * (*h), actual = expected;
        unsigned char *decoded = malloc(expected);
        if (decoded)
        {
            ok = uncompress(decoded, &actual, compressed, compressed_size) == Z_OK &&
                 actual == expected;
            for (unsigned y = 0; ok && y < *h; y++)
                if (decoded[y * stride] > 4)
                    ok = false;
            free(decoded);
        }
    }
    free(compressed);
    return ok;
}
static bool one_of(const char *value, const char *const *list)
{
    if (!value)
        return false;
    for (int i = 0; list[i]; i++)
        if (!strcmp(value, list[i]))
            return true;
    return false;
}
static const char *str(const cJSON *o, const char *key)
{
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key));
}
static bool number(const cJSON *o, const char *key, int min, int max, bool optional)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return (!v && optional) ||
           (cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= min &&
            v->valuedouble <= max && floor(v->valuedouble) == v->valuedouble);
}
static bool color(const char *s)
{
    if (!s || strlen(s) != 7 || s[0] != '#')
        return false;
    for (int i = 1; i < 7; i++)
        if (!isxdigit((unsigned char)s[i]))
            return false;
    return true;
}
static bool duplicates(const cJSON *o)
{
    if (cJSON_IsObject(o))
        for (const cJSON *a = o->child; a; a = a->next)
            for (const cJSON *b = a->next; b; b = b->next)
                if (!strcmp(a->string, b->string))
                    return true;
    for (const cJSON *a = o->child; a; a = a->next)
        if (duplicates(a))
            return true;
    return false;
}
static bool preview_asset(const char *name)
{
    return !strcmp(name, "preview-light.png") || !strcmp(name, "preview-dark.png");
}
static bool validate_scene(const cJSON *scene)
{
    static const char *const types[] = {"box",
                                        "pulse",
                                        "solar_disc",
                                        "solar_arc",
                                        "solar_timeline",
                                        "label",
                                        "value",
                                        "image",
                                        "bar",
                                        "gauge",
                                        "flow",
                                        "chart",
                                        "weather",
                                        NULL};
    static const char *const sources[] = {"pv",
                                          "home",
                                          "grid",
                                          "battery",
                                          "soc",
                                          "battery_temp",
                                          "temperature",
                                          "wind",
                                          "clock",
                                          "date",
                                          "status",
                                          "grid_direction",
                                          "battery_direction",
                                          "production_today",
                                          "consumption_today",
                                          "import_today",
                                          "export_today",
                                          "charge_today",
                                          "discharge_today",
                                          "pv_home",
                                          "pv_export",
                                          "pv_stored",
                                          "pv_home_today",
                                          "pv_export_today",
                                          "pv_stored_today",
                                          "grid_home_today",
                                          "battery_home_today",
                                          "self_sufficiency_pct",
                                          "self_consumption_pct",
                                          "pv_home_pct",
                                          "pv_load_pct",
                                          "pv_export_pct",
                                          "pv_stored_pct",
                                          "grid_home_pct",
                                          "battery_home_pct",
                                          "purchase_day",
                                          "revenue_day",
                                          "savings_day",
                                          "net_day",
                                          "purchase_month",
                                          "revenue_month",
                                          "savings_month",
                                          "net_month",
                                          "grid_net_today",
                                          "battery_net_today",
                                          "battery_capacity",
                                          "battery_energy",
                                          "sun_azimuth",
                                          "sun_altitude",
                                          "sunrise",
                                          "sunset",
                                          "sun_direction",
                                          "weather_label",
                                          "battery_summary",
                                          "battery_diag_assessment",
                                          "battery_diag_measurement",
                                          "battery_diag_evidence",
                                          "battery_diag_capacity",
                                          "battery_diag_capacity_pct",
                                          "battery_diag_charge_pct",
                                          "battery_diag_discharge_pct",
                                          "battery_diag_charge_detail",
                                          "battery_diag_discharge_detail",
                                          "battery_diag_day_charge_pct",
                                          "battery_diag_day_discharge_pct",
                                          "battery_diag_efficiency_pct",
                                          "battery_diag_cycles",
                                          "battery_diag_c_rate",
                                          "battery_diag_soc_error",
                                          "battery_eta",

                                          "solar_savings_day",
                                          "solar_value_today",
                                          "production_yesterday_pct",
                                          "savings_yesterday_pct",
                                          "production_value_yesterday_pct",
                                          "consumption_yesterday_pct",
                                          "export_yesterday_pct",
                                          NULL};
    if (!cJSON_IsObject(scene) || !color(str(scene, "background")))
        return false;
    if (cJSON_HasObjectItem(scene, "gradient") && !color(str(scene, "gradient")))
        return false;
    const cJSON *palette = cJSON_GetObjectItemCaseSensitive(scene, "palette");
    if (palette)
    {
        const char *keys[] = {"pv", "home", "grid", "battery", NULL};
        for (int i = 0; keys[i]; i++)
            if (!color(str(palette, keys[i])))
                return false;
    }
    const cJSON *hotspot = cJSON_GetObjectItemCaseSensitive(scene, "home_hotspot");
    if (hotspot && (!cJSON_IsObject(hotspot) || !number(hotspot,"x",0,1279,false) ||
        !number(hotspot,"y",0,799,false) || !number(hotspot,"w",1,1280,false) ||
        !number(hotspot,"h",1,800,false) ||
        cJSON_GetObjectItem(hotspot,"x")->valueint + cJSON_GetObjectItem(hotspot,"w")->valueint > 1280 ||
        cJSON_GetObjectItem(hotspot,"y")->valueint + cJSON_GetObjectItem(hotspot,"h")->valueint > 800))
        return false;
    const cJSON *elements = cJSON_GetObjectItemCaseSensitive(scene, "elements");
    if (!cJSON_IsArray(elements) || cJSON_GetArraySize(elements) > 96)
        return false;
    const cJSON *e;
    cJSON_ArrayForEach(e, elements)
    {
        const char *type = str(e, "type"), *source = str(e, "source");
        const char *file = str(e, "file");
        if (file && preview_asset(file))
            return false;
        const cJSON *variant;
        cJSON_ArrayForEach(variant,
                           cJSON_GetObjectItem(e, "files")) if (cJSON_IsString(variant) &&
                                                                preview_asset(cJSON_GetStringValue(
                                                                    variant))) return false;
        if (!cJSON_IsObject(e) || !one_of(type, types) || !number(e, "x", 0, 1279, false) ||
            !number(e, "y", 0, 799, false) || !number(e, "w", 1, 1280, false) ||
            !number(e, "h", 1, 800, false))
            return false;
        if (cJSON_GetObjectItem(e, "x")->valueint + cJSON_GetObjectItem(e, "w")->valueint > 1280 ||
            cJSON_GetObjectItem(e, "y")->valueint + cJSON_GetObjectItem(e, "h")->valueint > 800)
            return false;
        if (source && !one_of(source, sources))
            return false;
        /* A két eseményfeliratnak és az óraskálának is el kell férnie. */
        if (!strcmp(type, "solar_timeline") &&
            (cJSON_GetObjectItem(e, "w")->valueint < 480 ||
             cJSON_GetObjectItem(e, "h")->valueint < 132))
            return false;
        if (cJSON_HasObjectItem(e, "shadow") && (!cJSON_IsBool(cJSON_GetObjectItem(e, "shadow")) ||
                                                 (strcmp(type, "value") && strcmp(type, "label"))))
            return false;
        if (cJSON_HasObjectItem(e, "reference_style") &&
            (!cJSON_IsBool(cJSON_GetObjectItem(e, "reference_style")) || strcmp(type, "chart")))
            return false;
        if (cJSON_HasObjectItem(e, "soc_four_bands") &&
            (!cJSON_IsBool(cJSON_GetObjectItem(e, "soc_four_bands")) || strcmp(type, "bar") ||
             !source || strcmp(source, "soc")))
            return false;
        if (cJSON_HasObjectItem(e, "foreground") &&
            (!cJSON_IsBool(cJSON_GetObjectItem(e, "foreground")) ||
             (strcmp(type, "image") && strcmp(type, "pulse"))))
            return false;
        if (cJSON_HasObjectItem(e, "text_shadow") &&
            (!cJSON_IsBool(cJSON_GetObjectItem(e, "text_shadow")) ||
             (strcmp(type, "value") && strcmp(type, "label"))))
            return false;
        if (cJSON_HasObjectItem(e, "hide_when_normal") &&
            (!cJSON_IsBool(cJSON_GetObjectItem(e, "hide_when_normal")) ||
             strcmp(type, "value") || !source || strcmp(source, "status")))
            return false;
        if (cJSON_HasObjectItem(e, "status_bubble") &&
            (!cJSON_IsBool(cJSON_GetObjectItem(e, "status_bubble")) ||
             strcmp(type, "value") || !source || strcmp(source, "status") ||
             (cJSON_IsTrue(cJSON_GetObjectItem(e, "status_bubble")) &&
             (cJSON_GetObjectItem(e, "x")->valueint < 12 ||
             cJSON_GetObjectItem(e, "y")->valueint < 8 ||
             cJSON_GetObjectItem(e, "x")->valueint + cJSON_GetObjectItem(e, "w")->valueint > 1268 ||
             cJSON_GetObjectItem(e, "y")->valueint + cJSON_GetObjectItem(e, "h")->valueint > 792))))
            return false;
        if (!number(e, "glow", 0, 24, true) || !number(e, "shadow_offset", 0, 4, true) ||
            !number(e, "shadow_opacity", 0, 255, true))
            return false;
        if ((!strcmp(type, "value") || !strcmp(type, "bar") || !strcmp(type, "gauge") ||
             !strcmp(type, "flow")) &&
            !source)
            return false;
        if (source && (!strcmp(type, "bar") || !strcmp(type, "gauge") || !strcmp(type, "flow")) &&
            (!strcmp(source, "clock") || !strcmp(source, "date") || !strcmp(source, "status") ||
             strstr(source, "direction")))
            return false;
        const char *keys[] = {"color",
                              "background",
                              "muted",
                              "positive_color",
                              "negative_color",
                              "zero_color",
                              "outline_color",
                              "shadow_color",
                              NULL};
        for (int i = 0; keys[i]; i++)
            if (cJSON_HasObjectItem(e, keys[i]) && !color(str(e, keys[i])))
                return false;
        if (!number(e, "center_x", 0, 1279, true) || !number(e, "center_y", 0, 799, true) ||
            !number(e, "radius_x", 1, 640, true) || !number(e, "radius_y", 1, 390, true))
            return false;
        if (!number(e, "animate_ms", 0, 1000, true))
            return false;
        if (!number(e, "font", 12, 96, true) || !number(e, "opacity", 0, 255, true) ||
            !number(e, "radius", 0, 200, true) || !number(e, "max", 1, 100, true))
            return false;
        if (cJSON_HasObjectItem(e, "font"))
        {
            int f = cJSON_GetObjectItem(e, "font")->valueint;
            if (f != 12 && f != 16 && f != 20 && f != 28 && f != 48 && f != 96)
                return false;
        }
        if (!strcmp(type, "label") && (!str(e, "text") || strlen(str(e, "text")) > 160))
            return false;
        if (cJSON_HasObjectItem(e, "text_en") &&
            (!str(e, "text_en") || strlen(str(e, "text_en")) > 160))
            return false;
        if (cJSON_HasObjectItem(e, "align") &&
            (!str(e, "align") ||
             (strcmp(str(e, "align"), "center") && strcmp(str(e, "align"), "left"))))
            return false;
        if (!strcmp(type, "chart") && (cJSON_GetObjectItem(e, "w")->valueint < 150 ||
                                       cJSON_GetObjectItem(e, "h")->valueint <
                                           ((str(e, "kind") && (!strcmp(str(e, "kind"), "week") ||
                                                                !strcmp(str(e, "kind"), "month")))
                                                ? 70
                                                : 100)))
            return false;
        if (cJSON_HasObjectItem(e, "font") && cJSON_GetObjectItem(e, "font")->valueint >= 48 &&
            (strcmp(type, "value") || !source || !strcmp(source, "status") ||
             strstr(source, "direction")))
            return false;
        if (!strcmp(type, "image") && !app_templates_safe_asset(str(e, "file")))
            return false;
        if (!strcmp(type, "weather") && cJSON_HasObjectItem(e, "files"))
        {
            const cJSON *files = cJSON_GetObjectItem(e, "files");
            const char *keys[] = {
                "sunny", "partly_cloudy", "cloudy", "rain", "snow", "night", NULL};
            if (!cJSON_IsObject(files) || cJSON_GetArraySize(files) != 6)
                return false;
            for (int i = 0; keys[i]; i++)
                if (!app_templates_safe_asset(str(files, keys[i])))
                    return false;
        }
        static const char *const kinds[] = {"production",
                                            "consumption",
                                            "grid",
                                            "battery",
                                            "soc",
                                            "week",
                                            "month",
                                            "solar_hourly",
                                            NULL};
        if (!strcmp(type, "chart") && !one_of(str(e, "kind"), kinds))
            return false;
        if (!strcmp(type, "flow"))
        {
            const cJSON *pts = cJSON_GetObjectItem(e, "points");
            int n = cJSON_GetArraySize(pts);
            if (!cJSON_IsArray(pts) || n < 2 || n > 8)
                return false;
            const char *curve = str(e, "curve");
            if (cJSON_HasObjectItem(e, "curve") &&
                (!curve || (strcmp(curve, "polyline") && strcmp(curve, "bezier"))))
                return false;
            if (curve && !strcmp(curve, "bezier") && n != 4 && n != 7)
                return false;
            for (int i = 0; i < n; i++)
            {
                const cJSON *pt = cJSON_GetArrayItem(pts, i);
                if (!number(pt, "x", 0, cJSON_GetObjectItem(e, "w")->valueint - 1, false) ||
                    !number(pt, "y", 0, cJSON_GetObjectItem(e, "h")->valueint - 1, false))
                    return false;
            }
        }
    }
    return true;
}
static bool home_package(const cJSON *root)
{
    const char *kind = str(root, "kind");
    return kind && !strcmp(kind, APP_HOME_RESOURCE_KIND);
}
static bool home_split_backgrounds(const cJSON *root)
{
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    return cJSON_IsNumber(version) && version->valueint >= 2;
}
static bool home_manifest(const cJSON *root)
{
    if (!str(root, "id") || strcmp(str(root, "id"), APP_HOME_RESOURCE_ID))
        return false;
    const cJSON *backgrounds = cJSON_GetObjectItemCaseSensitive(root, "consumer_backgrounds");
    if (home_split_backgrounds(root))
    {
        const char *light = str(backgrounds, "light"), *dark = str(backgrounds, "dark");
        if (!cJSON_IsObject(backgrounds) || !light || !dark ||
            strcmp(light, "consumers-light.png") || strcmp(dark, "consumers-dark.png"))
            return false;
    }
    else if (backgrounds)
        return false;
    const cJSON *icons = cJSON_GetObjectItemCaseSensitive(root, "icons");
    if (!cJSON_IsArray(icons) || cJSON_GetArraySize(icons) != APP_HOME_RESOURCE_ICONS)
        return false;
    const cJSON *icon;
    cJSON_ArrayForEach(icon, icons)
    {
        const char *id = str(icon, "id"), *file = str(icon, "file");
        if (!app_templates_safe_id(id) || !app_templates_safe_asset(file))
            return false;
        char expected[65];
        snprintf(expected, sizeof(expected), "icon-%s.png", id);
        if (strcmp(file, expected))
            return false;
        for (const cJSON *prior = icons->child; prior != icon; prior = prior->next)
            if (!strcmp(str(prior, "id"), id))
                return false;
    }
    return true;
}
static bool home_asset(const cJSON *root, const char *name, unsigned width, unsigned height)
{
    if (!strcmp(name, "background-light.png") || !strcmp(name, "background-dark.png") ||
        (home_split_backgrounds(root) && (!strcmp(name, "consumers-light.png") || !strcmp(name, "consumers-dark.png"))))
        return width == 1280 && height == 800;
    if (!strcmp(name, "loxone-logo.png"))
        return width <= 256 && height <= 256;
    const cJSON *icon;
    cJSON_ArrayForEach(icon, cJSON_GetObjectItemCaseSensitive(root, "icons"))
        if (!strcmp(str(icon, "file"), name))
            return width == 96 && height == 96;
    return false;
}

/*
 * A template.json beolvasása és teljes sémaellenőrzése a képfájlok nélkül.
 * Sikernél a hívó tulajdonába adja a dokumentumot; hibánál NULL és ugyanaz
 * a hibaüzenet, mint a teljes validálásnál.
 */
static cJSON *validate_document(const char *directory, char error[160])
{
    char path[256];
    path_join(path, directory, "template.json");
    size_t n = 0;
    unsigned char *data = read_file(path, 65536, &n);
    if (!data)
    {
        error_set(error, "Hiányzó/túl nagy template.json");
        return NULL;
    }
    /* A parser veremhasználatát a bemenettől függetlenül korlátozzuk. */
    int depth = 0;
    bool quoted = false, escaped = false, valid = true;
    for (size_t i = 0; i < n; i++)
    {
        char c = data[i];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        }
        else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[')
        {
            if (++depth > 16)
                valid = false;
        }
        else if (c == '}' || c == ']')
            depth--;
    }
    if (memchr(data, 0, n))
        valid = false;
    cJSON *root = valid ? cJSON_ParseWithLengthOpts((char *)data, n + 1, NULL, true) : NULL;
    free(data);
    if (!root || duplicates(root) || !number(root, "schema", 1, 1, false) ||
        !app_templates_safe_id(str(root, "id")) || !str(root, "name") ||
        strlen(str(root, "name")) > 80 || !number(root, "version", 1, 65535, false) ||
        !validate_scene(cJSON_GetObjectItem(root, "light")) ||
        !validate_scene(cJSON_GetObjectItem(root, "dark")))
    {
        cJSON_Delete(root);
        error_set(error, "Hibás sablonleírás / Invalid template schema");
        return NULL;
    }
    bool resources = home_package(root);
    if ((resources && !home_manifest(root)) ||
        (!resources && (cJSON_HasObjectItem(root, "kind") || !strcmp(str(root, "id"), APP_HOME_RESOURCE_ID))))
    {
        cJSON_Delete(root);
        error_set(error, "Hibás Loxone erőforráscsomag / Invalid home resources");
        return NULL;
    }
    return root;
}
bool app_templates_validate(const char *directory, cJSON **document, char error[160])
{
    cJSON *root = validate_document(directory, error);
    if (!root)
        return false;
    bool resources = home_package(root);
    bool valid = true;
    char path[256];
    size_t n = 0;
    unsigned char *data = NULL;
    DIR *dir = opendir(directory);
    if (!dir)
    {
        cJSON_Delete(root);
        return false;
    }
    struct dirent *entry;
    size_t encoded = 0, decoded = 0;
    unsigned assets = 0, previews = 0;
    while ((entry = readdir(dir)))
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !strcmp(entry->d_name, "template.json"))
            continue;
        /* Áramszünet a segédfájl írása közben: a félkész munkafájl nem
         * teheti érvénytelenné a csomagot, ezért itt takarítjuk el. */
        if (!strcmp(entry->d_name, "raw.tmp"))
        {
            path_join(path, directory, entry->d_name);
            remove(path);
            continue;
        }
        char source[66];
        if (!resources && app_template_raw_is_name(entry->d_name, source))
        {
            /* Előre dekódolt segédfájl: csak a hozzá tartozó PNG-vel együtt
             * érvényes. A tartalmát a betöltéskori CRC ellenőrzi. */
            char source_path[256];
            struct stat source_stat;
            path_join(source_path, directory, source);
            if (stat(source_path, &source_stat) || !S_ISREG(source_stat.st_mode))
            {
                valid = false;
                break;
            }
            continue;
        }
        if (!app_templates_safe_asset(entry->d_name) || ++assets > (resources ? 72U : 32U))
        {
            valid = false;
            break;
        }
        path_join(path, directory, entry->d_name);
        unsigned w = 0, h = 0;
        data = read_file(path, APP_TEMPLATE_PNG_MAX, &n);
        if (!data || !png_valid(data, n, &w, &h))
        {
            free(data);
            valid = false;
            break;
        }
        free(data);
        encoded += n;
        if (preview_asset(entry->d_name))
        {
            previews++;
            if (w > 640 || h > 400)
                valid = false;
        }
        else
        {
            if (resources && !home_asset(root, entry->d_name, w, h))
                valid = false;
            decoded += (size_t)w * h * 4;
        }
        if (encoded > APP_TEMPLATE_ENCODED_MAX || decoded > (resources ? 20U : 12U) * 1024 * 1024)
        {
            valid = false;
            break;
        }
    }
    closedir(dir);
    /* Régi SD-csomag: egyik sincs. Új előnézetek: a két üzemmód együtt érkezik. */
    if (previews == 1 || (resources && assets != (home_split_backgrounds(root) ? 69U : 67U) + previews))
        valid = false;
    const char *themes[] = {"light", "dark"};
    for (int t = 0; valid && t < 2; t++)
    {
        const cJSON *e;
        cJSON_ArrayForEach(e, cJSON_GetObjectItem(cJSON_GetObjectItem(root, themes[t]), "elements"))
        {
            if (!strcmp(str(e, "type"), "image"))
            {
                path_join(path, directory, str(e, "file"));
                struct stat st;
                if (stat(path, &st) || !S_ISREG(st.st_mode))
                    valid = false;
            }
            else if (!strcmp(str(e, "type"), "weather"))
            {
                const cJSON *file;
                cJSON_ArrayForEach(file, cJSON_GetObjectItem(e, "files"))
                {
                    path_join(path, directory, cJSON_GetStringValue(file));
                    struct stat st;
                    if (stat(path, &st) || !S_ISREG(st.st_mode))
                        valid = false;
                }
            }
        }
    }
    if (!valid)
    {
        cJSON_Delete(root);
        return error_set(error, "Hiányzó, sérült vagy túl nagy PNG / Invalid assets");
    }
    if (document)
        *document = root;
    else
        cJSON_Delete(root);
    return true;
}
static uint64_t fnv1a64(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}
/* A FATFS a readdir() által már beolvasott bejegyzést adja vissza a közvetlenül
 * utána hívott stat()-nak, így ez nem jelent bejegyzésenként külön keresést. */
static bool directory_fingerprint(const char *directory, uint64_t *out)
{
    DIR *dir = opendir(directory);
    if (!dir)
        return false;
    uint64_t hash = 14695981039346656037ULL;
    unsigned entries = 0;
    bool ok = true;
    struct dirent *entry;
    char path[256];
    while ((entry = readdir(dir)))
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        /* A generált segédfájlok nem módosítják a csomag ujjlenyomatát:
         * a keletkezésük nem jelent kártyán végzett kézi változtatást. */
        if (app_template_raw_is_name(entry->d_name, NULL) || !strcmp(entry->d_name, "raw.tmp"))
            continue;
        path_join(path, directory, entry->d_name);
        struct stat st;
        if (!path[0] || stat(path, &st) != 0)
        {
            ok = false;
            break;
        }
        int64_t size = (int64_t)st.st_size, mtime = (int64_t)st.st_mtime;
        uint32_t mode = (uint32_t)st.st_mode;
        hash = fnv1a64(hash, entry->d_name, strlen(entry->d_name) + 1);
        hash = fnv1a64(hash, &size, sizeof(size));
        hash = fnv1a64(hash, &mtime, sizeof(mtime));
        hash = fnv1a64(hash, &mode, sizeof(mode));
        entries++;
    }
    closedir(dir);
    hash = fnv1a64(hash, &entries, sizeof(entries));
    *out = hash;
    return ok;
}
static void validated_forget(const char *id)
{
    for (unsigned i = 0; i < validated_count; i++)
        if (!strcmp(validated[i].id, id))
        {
            validated[i] = validated[--validated_count];
            return;
        }
}
static void validated_remember(const char *id, uint64_t fingerprint)
{
    validated_forget(id);
    if (validated_count >= APP_TEMPLATE_MAX)
        return;
    snprintf(validated[validated_count].id, sizeof(validated[validated_count].id), "%.32s", id);
    validated[validated_count].fingerprint = fingerprint;
    validated_count++;
}
static bool validated_matches(const char *id, uint64_t fingerprint)
{
    for (unsigned i = 0; i < validated_count; i++)
        if (!strcmp(validated[i].id, id))
            return validated[i].fingerprint == fingerprint;
    return false;
}
static void remove_stage(const char *path)
{
    DIR *d = opendir(path);
    if (!d)
        return;
    struct dirent *e;
    char child[256];
    while ((e = readdir(d)))
    {
        /* A generált segédfájlok és a félkész munkafájl is a csomaghoz
         * tartozik: törléskor és cserekor ezek is eltűnnek. */
        if (strcmp(e->d_name, "template.json") && !app_templates_safe_asset(e->d_name) &&
            !app_template_raw_is_name(e->d_name, NULL) && strcmp(e->d_name, "raw.tmp"))
            continue;
        path_join(child, path, e->d_name);
        remove(child);
    }
    closedir(d);
    RMDIR(path);
}
/* Megőrzött katalógussorrend, egy sorban egy sablonazonosítóval. */
static void apply_saved_order(void)
{
    char file[256];
    path_join(file, app_templates_root(), "order.txt");
    FILE *fp = fopen(file, "rb");
    if (!fp)
    {
        path_join(file, app_templates_root(), "order.bak");
        fp = fopen(file, "rb");
    }
    if (!fp)
        return;
    char id[40];
    unsigned target = 1;
    while (target < count && fgets(id, sizeof(id), fp))
    {
        size_t n = strcspn(id, "\r\n");
        id[n] = 0;
        for (unsigned j = target; j < count; j++)
            if (!strcmp(catalogue[j].id, id))
            {
                app_template_info_t entry = catalogue[j];
                memmove(&catalogue[target + 1], &catalogue[target], (j - target) * sizeof(entry));
                catalogue[target++] = entry;
                break;
            }
    }
    fclose(fp);
}
static void scan(void)
{
    count = 1;
    validated_count = 0;
    home_ready = false;
    memset(&home_info, 0, sizeof(home_info));
    snprintf(catalogue[0].id, 33, "builtin");
    snprintf(catalogue[0].name, 81, "Rendszer");
    DIR *dir = opendir(app_templates_root());
    if (!dir)
        return;
    struct dirent *e;
    char path[256], error[160];
    while ((e = readdir(dir)))
    {
        if (!app_templates_safe_id(e->d_name))
            continue;
        path_join(path, app_templates_root(), e->d_name);
        /* Az ujjlenyomat a validálás előtti állapotot rögzíti; a tárzár
         * miatt a kettő között a könyvtár nem változhat. */
        uint64_t fingerprint = 0;
        bool fingerprinted = directory_fingerprint(path, &fingerprint);
        cJSON *doc = NULL;
        if (!app_templates_validate(path, &doc, error))
            continue;
        if (!strcmp(str(doc, "id"), e->d_name) && home_package(doc))
        {
            home_ready = true;
            snprintf(home_info.id, sizeof(home_info.id), "%s", APP_HOME_RESOURCE_ID);
            snprintf(home_info.name, sizeof(home_info.name), "%s", str(doc, "name"));
            home_info.version = cJSON_GetObjectItem(doc, "version")->valueint;
        }
        else if (count < APP_TEMPLATE_MAX && !strcmp(str(doc, "id"), e->d_name))
        {
            snprintf(catalogue[count].id, 33, "%.32s", e->d_name);
            snprintf(catalogue[count].name, 81, "%s", str(doc, "name"));
            catalogue[count].version = cJSON_GetObjectItem(doc, "version")->valueint;
            if (fingerprinted)
                validated_remember(catalogue[count].id, fingerprint);
            count++;
        }
        cJSON_Delete(doc);
    }
    closedir(dir);
    for (unsigned i = 1; i < count; i++)
        for (unsigned j = i + 1; j < count; j++)
            if (strcmp(catalogue[i].id, catalogue[j].id) > 0)
            {
                app_template_info_t t = catalogue[i];
                catalogue[i] = catalogue[j];
                catalogue[j] = t;
            }
    apply_saved_order();
    home_generation++;
}
esp_err_t app_templates_init(void)
{
    if (!lock)
        lock = xSemaphoreCreateMutex();
    if (!lock)
        return ESP_ERR_NO_MEM;
    xSemaphoreTake(lock, portMAX_DELAY);
    #ifndef APP_TEMPLATE_DESKTOP
    if (app_storage_is_cardless()) {
        memset(catalogue, 0, sizeof(catalogue));
        snprintf(catalogue[0].id, sizeof(catalogue[0].id), "builtin");
        snprintf(catalogue[0].name, sizeof(catalogue[0].name), "Rendszer / System");
        count = 1;
        snprintf(selected, sizeof(selected), "builtin");
        generation++;
        xSemaphoreGive(lock);
        return ESP_OK;
    }
    #endif
    if (STORAGE_TAKE() != ESP_OK)
    {
        xSemaphoreGive(lock);
        return ESP_FAIL;
    }
    MKDIR(app_templates_root());
    char stage[256], backup[256];
    path_join(backup, app_templates_root(), ".replace");
    /* Áramszünet közbeni sabloncsere helyreállítása. Ha az új könyvtár
     * már létezik, a régi mentés törölhető; különben a régi visszaáll. */
    char backup_json[256];
    path_join(backup_json, backup, "template.json");
    size_t backup_len = 0;
    unsigned char *backup_data = read_file(backup_json, 65536, &backup_len);
    if (backup_data)
    {
        cJSON *old = cJSON_Parse((const char *)backup_data);
        free(backup_data);
        const char *old_id = str(old, "id");
        if (app_templates_safe_id(old_id))
        {
            char old_dir[256];
            path_join(old_dir, app_templates_root(), old_id);
            struct stat st;
            if (stat(old_dir, &st) != 0)
                rename(backup, old_dir);
        }
        cJSON_Delete(old);
    }
    remove_stage(backup);
    path_join(stage, app_templates_root(), ".install");
    remove_stage(stage);
    path_join(stage, app_templates_root(), ".check");
    remove_stage(stage);
    scan();
    char path[256];
    path_join(path, app_templates_root(), "selection.txt");
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        path_join(path, app_templates_root(), "selection.bak");
        f = fopen(path, "rb");
    }
    char id[34] = {0};
    if (f)
    {
        size_t read = fread(id, 1, 33, f);
        id[read] = 0;
        fclose(f);
    }
    snprintf(selected, 33, "builtin");
    for (unsigned i = 1; i < count; i++)
        if (!strcmp(catalogue[i].id, id))
            snprintf(selected, 33, "%.32s", id);
    generation++;
    STORAGE_GIVE();
    xSemaphoreGive(lock);
    return ESP_OK;
}
unsigned app_templates_list(app_template_info_t *out, unsigned capacity, char active[33])
{
    if (!lock)
        return 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    unsigned n = count < capacity ? count : capacity;
    if (out)
        memcpy(out, catalogue, n * sizeof(*out));
    if (active)
        snprintf(active, 33, "%s", selected);
    xSemaphoreGive(lock);
    return n;
}
esp_err_t app_templates_reorder(const cJSON *ids, char error[160])
{
    if (!lock || !cJSON_IsArray(ids))
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (cJSON_GetArraySize(ids) != (int)count)
    {
        xSemaphoreGive(lock);
        error_set(error, "A teljes sablonlistát meg kell adni");
        return ESP_ERR_INVALID_ARG;
    }
    app_template_info_t ordered[APP_TEMPLATE_MAX];
    bool used[APP_TEMPLATE_MAX] = {0};
    for (unsigned i = 0; i < count; i++)
    {
        const char *id = cJSON_GetStringValue(cJSON_GetArrayItem(ids, i));
        unsigned j = 0;
        if (!id || (i == 0 && strcmp(id, "builtin")) || (i > 0 && !strcmp(id, "builtin")))
        {
            xSemaphoreGive(lock);
            error_set(error, "A Rendszer téma mindig az első");
            return ESP_ERR_INVALID_ARG;
        }
        while (j < count && (used[j] || strcmp(catalogue[j].id, id)))
            j++;
        if (j == count)
        {
            xSemaphoreGive(lock);
            error_set(error, "Ismeretlen vagy ismétlődő sablonazonosító");
            return ESP_ERR_INVALID_ARG;
        }
        ordered[i] = catalogue[j];
        used[j] = true;
    }
    if (STORAGE_TAKE() != ESP_OK)
    {
        xSemaphoreGive(lock);
        error_set(error, "SD nem elérhető");
        return ESP_FAIL;
    }
    char path[256], temp[256];
    path_join(path, app_templates_root(), "order.txt");
    path_join(temp, app_templates_root(), "order.tmp");
    FILE *fp = fopen(temp, "wb");
    bool ok = fp != NULL;
    if (fp)
    {
        for (unsigned i = 1; i < count; i++)
            if (fprintf(fp, "%s\n", ordered[i].id) < 0)
                ok = false;
        if (fflush(fp) != 0)
            ok = false;
#ifndef _WIN32
        if (fsync(fileno(fp)) != 0)
            ok = false;
#endif
        if (fclose(fp) != 0)
            ok = false;
    }
    /* FAT fájlrendszeren a rename nem írhat felül létező célfájlt. */
    char backup[256];
    path_join(backup, app_templates_root(), "order.bak");
    if (ok)
    {
        remove(backup);
        if (rename(path, backup) != 0 && errno != ENOENT)
            ok = false;
        if (ok && rename(temp, path) != 0)
        {
            rename(backup, path);
            ok = false;
        }
        if (ok)
            remove(backup);
    }
    if (!ok)
        remove(temp);
    if (ok)
    {
        memcpy(catalogue, ordered, count * sizeof(*ordered));
        generation++;
    }
    STORAGE_GIVE();
    xSemaphoreGive(lock);
    if (!ok)
        error_set(error, "A sorrend mentése sikertelen");
    return ok ? ESP_OK : ESP_FAIL;
}
static unsigned raw_cursor;
#ifndef APP_TEMPLATE_DESKTOP
static int64_t raw_pause_until_us;
#endif
/* Lapozás és telepítés után a háttérmunka néhány másodpercre félreáll. */
static void raw_postpone(void)
{
#ifndef APP_TEMPLATE_DESKTOP
    raw_pause_until_us = esp_timer_get_time() + 3000000;
#endif
}
static bool raw_postponed(void)
{
#ifndef APP_TEMPLATE_DESKTOP
    return esp_timer_get_time() < raw_pause_until_us;
#else
    return false;
#endif
}
esp_err_t app_templates_select(const char *id)
{
    if (!lock || !id)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock, portMAX_DELAY);
    esp_err_t result = ESP_ERR_NOT_FOUND;
    for (unsigned i = 0; i < count; i++)
        if (!strcmp(id, catalogue[i].id))
        {
            snprintf(selected, 33, "%s", id);
            generation++;
            save_selection = true;
            raw_postpone();
            result = ESP_OK;
            break;
        }
    xSemaphoreGive(lock);
    return result;
}
bool app_templates_step_available(int direction, bool sun_available)
{
    if (!lock || !xSemaphoreTake(lock, 0))
        return false;
    unsigned i = 0;
    for (unsigned n = 0; n < count; n++)
        if (!strcmp(selected, catalogue[n].id))
            i = n;
    /* Ha a Loxone T1 nincs bekötve, Nap pálya nem választható lapozással.
     * A már kiválasztott sablon nem vész el; csak a következő lépést szűrjük. */
    for (unsigned attempt = 0; attempt < count; attempt++)
    {
        i = direction > 0 ? (i + 1) % count : (i + count - 1) % count;
        if (sun_available || strcmp(catalogue[i].id, "09-solar-journey"))
        {
            snprintf(selected, 33, "%s", catalogue[i].id);
            generation++;
            save_selection = true;
            raw_postpone();
            break;
        }
    }
    xSemaphoreGive(lock);
    return true;
}
void app_templates_step(int direction)
{
    (void)app_templates_step_available(direction, true);
}
void app_templates_view_free(app_template_view_t *v)
{
    if (!v)
        return;
    cJSON_Delete(v->document);
    for (unsigned i = 0; i < v->asset_count; i++)
        free(v->assets[i].bytes);
    for (unsigned i = 0; i < v->raw_count; i++)
        free(v->raws[i].bytes);
    memset(v, 0, sizeof(*v));
}
bool app_templates_has_pending(unsigned applied_generation)
{
    if (!lock || !xSemaphoreTake(lock, 0))
        return false;
    bool pending = applied_generation != generation;
    xSemaphoreGive(lock);
    return pending;
}
/*
 * A kiválasztott sablon tartós mentése. Hívása a katalógus- és a tárzár alatt.
 * Atomi rename csak új fájlnál garantált; a régi választás biztonsági másolat.
 * Sikertelen írásnál a save_selection marad, a következő hívás újrapróbálja.
 */
static void persist_selection(void)
{
    char path[256], tmp[256], bak[256];
    path_join(path, app_templates_root(), "selection.txt");
    path_join(tmp, app_templates_root(), "selection.tmp");
    path_join(bak, app_templates_root(), "selection.bak");
    FILE *f = fopen(tmp, "wb");
    if (f)
    {
        bool written = fwrite(selected, 1, strlen(selected), f) == strlen(selected);
        if (fclose(f))
            written = false;
        if (written)
        {
            remove(bak);
            rename(path, bak);
            if (!rename(tmp, path))
                save_selection = false;
            else
                rename(bak, path);
        }
    }
}
/*
 * Előre dekódolt kép-segédfájlok (lásd app_template_raw.h). A jelenetekben
 * szereplő minden kép-változathoz (fájl + megjelenítési méret) tartozik egy
 * `<alap>-<w>x<h>.raw`, amelyet a telepítés készít el, és amelyet hiányzás
 * esetén a betöltés pótol. Ha egy képhez minden változat megvan, a PNG-t be
 * sem olvassuk: a témaváltásból kimarad a dekódolás és a méretezés is.
 */
#define TEMPLATE_RAW_VARIANTS 32
typedef struct
{
    char file[65];
    unsigned width, height;
} template_variant_t;

static unsigned variant_size(const cJSON *element, const char *key, unsigned fallback)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(element, key);
    if (!cJSON_IsNumber(value) || value->valuedouble < 1 || value->valuedouble > 1280)
        return fallback;
    return (unsigned)value->valuedouble;
}

/* A két jelenet összes kép-változata, ismétlődés nélkül. */
static unsigned collect_variants(const cJSON *doc, template_variant_t *out, unsigned max)
{
    unsigned total = 0;
    static const char *const scenes[] = {"light", "dark"};
    for (unsigned t = 0; t < 2; t++)
    {
        const cJSON *element;
        cJSON_ArrayForEach(element, cJSON_GetObjectItem(cJSON_GetObjectItem(doc, scenes[t]), "elements"))
        {
            const char *names[8];
            unsigned n = 0;
            const char *file = str(element, "file");
            if (file)
                names[n++] = file;
            const cJSON *item;
            cJSON_ArrayForEach(item, cJSON_GetObjectItem(element, "files"))
                if (cJSON_IsString(item) && n < 8)
                    names[n++] = item->valuestring;
            unsigned w = variant_size(element, "w", 128), h = variant_size(element, "h", 128);
            if (!w || !h || w > 1280 || h > 800)
                continue;
            for (unsigned i = 0; i < n; i++)
            {
                if (!app_templates_safe_asset(names[i]) || preview_asset(names[i]))
                    continue;
                bool known = false;
                for (unsigned j = 0; j < total; j++)
                    if (out[j].width == w && out[j].height == h && !strcmp(out[j].file, names[i]))
                        known = true;
                if (known || total >= max)
                    continue;
                snprintf(out[total].file, sizeof(out[total].file), "%.64s", names[i]);
                out[total].width = w;
                out[total].height = h;
                total++;
            }
        }
    }
    return total;
}

/* A segédfájl fejlécének beolvasása; a teljes ellenőrzés a betöltéskor fut.
 * Hordozható stdio: a Windowsos szimulátor is ezt fordítja. */
static bool raw_header(const char *path, uint8_t header[APP_TEMPLATE_RAW_HEADER], size_t *size)
{
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || (size_t)st.st_size <= APP_TEMPLATE_RAW_HEADER ||
        (size_t)st.st_size > APP_TEMPLATE_RAW_MAX)
        return false;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    bool ok = fread(header, 1, APP_TEMPLATE_RAW_HEADER, f) == APP_TEMPLATE_RAW_HEADER;
    fclose(f);
    if (ok && size)
        *size = (size_t)st.st_size;
    return ok;
}

/*
 * Asztali teszteléshez: a ZOLEE_DISABLE_RAW környezeti változóval a segédfájlok
 * készítése és használata kikapcsolható, így a két út képkockái összevethetők.
 * A firmware-be nem kerül bele.
 */
static bool raw_disabled(void)
{
#ifdef APP_TEMPLATE_DESKTOP
    return getenv("ZOLEE_DISABLE_RAW") != NULL;
#else
    return false;
#endif
}
/* Igaz, ha a segédfájl a mostani PNG-hez és mérethez készült. */
static uint32_t raw_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool raw_current(const char *directory, const template_variant_t *variant)
{
    char raw_path[320], png_path[320], name[80];
    struct stat png_stat;
    if (!app_template_raw_name(variant->file, variant->width, variant->height, name, sizeof(name)))
        return false;
    path_join(png_path, directory, variant->file);
    path_join(raw_path, directory, name);
    if (stat(png_path, &png_stat) || !S_ISREG(png_stat.st_mode))
        return false;
    uint8_t header[APP_TEMPLATE_RAW_HEADER];
    /* Csak a fejlécet olvassuk: a képpontok CRC-je a betöltéskor fut le, itt
     * feleslegesen több MiB-ot olvasnánk. */
    if (!raw_header(raw_path, header, NULL))
        return false;
    return raw_le32(header) == APP_TEMPLATE_RAW_MAGIC &&
           raw_le32(header + 8) == variant->width && raw_le32(header + 12) == variant->height &&
           raw_le32(header + 24) == (uint32_t)png_stat.st_size;
}

/* Hiányzó vagy elavult segédfájl elkészítése. Hibánál a PNG-út marad. */
static bool raw_generate(const char *directory, const template_variant_t *variant)
{
    char raw_path[320], tmp_path[320], png_path[320], name[80];
    if (!app_template_raw_name(variant->file, variant->width, variant->height, name, sizeof(name)))
        return false;
    path_join(png_path, directory, variant->file);
    path_join(raw_path, directory, name);
    path_join(tmp_path, directory, "raw.tmp");
    size_t png_size = 0;
    unsigned char *png = read_file(png_path, APP_TEMPLATE_PNG_MAX, &png_size);
    if (!png)
        return false;
    uint8_t *raw = NULL;
    size_t raw_size = 0;
    bool ok = app_template_raw_build(png, png_size, variant->width, variant->height, &raw, &raw_size);
    free(png);
    if (!ok)
        return false;
    remove(tmp_path);
    FILE *f = fopen(tmp_path, "wb");
    ok = f != NULL;
    if (f)
    {
        ok = fwrite(raw, 1, raw_size, f) == raw_size;
        if (fclose(f))
            ok = false;
    }
    free(raw);
    if (ok)
    {
        remove(raw_path);
        ok = rename(tmp_path, raw_path) == 0;
    }
    if (!ok)
        remove(tmp_path);
    return ok;
}

/*
 * Egyetlen hiányzó segédfájl elkészítése a háttérben.
 *
 * A dekódolás és a 2-3 MiB-os kiírás témánként fél percig is tarthat, ezért
 * nem a témabetöltés közben fut: ott az SD-zárat percekre elvenné a webes
 * feltöltéstől. Hívásonként egy fájl készül el, a zárakat csak addig tartjuk.
 * A lapozás és a telepítés elsőbbséget élvez: akkor ez a munka kimarad.
 */
bool app_templates_raw_step(void)
{
    if (!lock || raw_disabled() || raw_postponed() || !STORAGE_READY())
        return false;
    if (!xSemaphoreTake(lock, 0))
        return false;
    bool worked = false;
    char id[33] = {0};
    for (unsigned attempt = 0; attempt < count && !worked; attempt++)
    {
        unsigned index = (raw_cursor + attempt) % count;
        if (!index || !strcmp(catalogue[index].id, "builtin"))
            continue;
        snprintf(id, sizeof(id), "%s", catalogue[index].id);
        char directory[256], path[256];
        path_join(directory, app_templates_root(), id);
        path_join(path, directory, "template.json");
        if (!STORAGE_TRY())
            break;
        size_t size = 0;
        unsigned char *text = read_file(path, 64 * 1024, &size);
        cJSON *doc = text ? cJSON_Parse((const char *)text) : NULL;
        free(text);
        template_variant_t *variants = doc ? calloc(TEMPLATE_RAW_VARIANTS, sizeof(*variants)) : NULL;
        unsigned total = variants ? collect_variants(doc, variants, TEMPLATE_RAW_VARIANTS) : 0;
        for (unsigned i = 0; i < total && !worked; i++)
            if (!raw_current(directory, &variants[i]))
                worked = raw_generate(directory, &variants[i]);
        free(variants);
        cJSON_Delete(doc);
        STORAGE_GIVE();
        /* A következő hívás ugyanennél a témánál folytatja. */
        if (worked)
            raw_cursor = index;
    }
    if (!worked)
        raw_cursor = 0;
    xSemaphoreGive(lock);
    return worked;
}

bool app_templates_load_pending(app_template_view_t *out)
{
    if (!lock || !xSemaphoreTake(lock, 0))
        return false;
    if (out->generation == generation)
    {
        /* A téma már betöltődött: most, a megjelenítés után mentjük a
         * választást. Foglalt SD-nél a következő körben újra próbáljuk. */
#ifndef APP_TEMPLATE_DESKTOP
        bool cardless = app_storage_is_cardless();
#else
        bool cardless = false;
#endif
        if (save_selection && !cardless && STORAGE_TRY())
        {
            persist_selection();
            STORAGE_GIVE();
        }
        xSemaphoreGive(lock);
        return false;
    }
    app_template_view_t *next = calloc(1, sizeof(*next));
    if (!next)
    {
        xSemaphoreGive(lock);
        return false;
    }
    #ifndef APP_TEMPLATE_DESKTOP
    if (app_storage_is_cardless()) {
        snprintf(next->id, sizeof(next->id), "builtin");
        next->generation = generation;
        next->catalogue_count = 1;
        *out = *next;
        free(next);
        xSemaphoreGive(lock);
        return true;
    }
    #endif
    if (!STORAGE_TRY())
    {
        free(next);
        xSemaphoreGive(lock);
        return false;
    }
    snprintf(next->id, 33, "%s", selected);
    next->generation = generation;
    next->catalogue_count = count;
    for (unsigned i = 0; i < count; i++)
        if (!strcmp(selected, catalogue[i].id))
            next->catalogue_index = i;
    char directory[256], path[256], error[160];
    path_join(directory, app_templates_root(), selected);
    bool ok = !strcmp(selected, "builtin");
    /* Változatlan, már teljesen ellenőrzött könyvtárnál csak a leírót
     * validáljuk újra; a PNG-k egyszer, a betöltéskor kerülnek beolvasásra. */
    uint64_t fingerprint = 0;
    bool fingerprinted = !ok && directory_fingerprint(directory, &fingerprint);
    bool trusted = false;
    if (fingerprinted && validated_matches(selected, fingerprint))
    {
        next->document = validate_document(directory, error);
        trusted = next->document != NULL;
    }
    if (!ok && (trusted || app_templates_validate(directory, &next->document, error)))
    {
        if (!trusted && fingerprinted)
            validated_remember(selected, fingerprint);
        /* A jelenetek kép-változatai: ezekhez keresünk előre dekódolt segédfájlt. */
        template_variant_t *variants = calloc(TEMPLATE_RAW_VARIANTS, sizeof(*variants));
        unsigned variant_count = (variants && !raw_disabled())
                                     ? collect_variants(next->document, variants,
                                                        TEMPLATE_RAW_VARIANTS)
                                     : 0;
        DIR *d = opendir(directory);
        struct dirent *e;
        ok = d != NULL && (variants != NULL);
        while (ok && d && (e = readdir(d)))
        {
            if (!app_templates_safe_asset(e->d_name) || preview_asset(e->d_name))
                continue;
            if (next->asset_count >= 32)
            {
                ok = false;
                break;
            }
            app_template_asset_t *a = &next->assets[next->asset_count++];
            snprintf(a->name, 65, "%.64s", e->d_name);
            path_join(path, directory, e->d_name);
            /*
             * Ha a képhez minden használt mérethez van érvényes segédfájl, a
             * PNG-t be sem olvassuk: a témaváltásból kimarad a dekódolás és a
             * méretezés. Hiányzó segédfájlt egyszer, itt pótolunk.
             */
            bool raw_only = false;
            unsigned used = 0;
            for (unsigned i = 0; i < variant_count; i++)
                if (!strcmp(variants[i].file, a->name))
                    used++;
            if (used && next->raw_count + used <= 32)
            {
                raw_only = true;
                for (unsigned i = 0; raw_only && i < variant_count; i++)
                {
                    if (strcmp(variants[i].file, a->name))
                        continue;
                    raw_only = raw_current(directory, &variants[i]);
                }
            }
            if (raw_only)
            {
                struct stat png_stat;
                if (stat(path, &png_stat))
                {
                    ok = false;
                    break;
                }
                for (unsigned i = 0; raw_only && i < variant_count; i++)
                {
                    if (strcmp(variants[i].file, a->name))
                        continue;
                    char name[80], raw_path[320];
                    app_template_raw_t info;
                    app_template_rawimg_t *raw = &next->raws[next->raw_count];
                    if (!app_template_raw_name(a->name, variants[i].width, variants[i].height,
                                               name, sizeof(name)))
                    {
                        raw_only = false;
                        break;
                    }
                    path_join(raw_path, directory, name);
                    raw->bytes = read_file(raw_path, APP_TEMPLATE_RAW_MAX, &raw->size);
                    if (!raw->bytes ||
                        !app_template_raw_parse(raw->bytes, raw->size, variants[i].width,
                                                variants[i].height, (uint32_t)png_stat.st_size,
                                                &info))
                    {
                        free(raw->bytes);
                        raw->bytes = NULL;
                        raw_only = false;
                        break;
                    }
                    raw->width = (uint16_t)info.width;
                    raw->height = (uint16_t)info.height;
                    raw->stride = (uint16_t)info.stride;
                    raw->format = (uint8_t)info.format;
                    raw->asset = (uint8_t)(next->asset_count - 1);
                    next->raw_count++;
                }
            }
            if (raw_only)
            {
                /* A natív méret a segédfájlból is ismert. */
                a->width = next->raws[next->raw_count - 1].width;
                a->height = next->raws[next->raw_count - 1].height;
                continue;
            }
            /* Visszaesés a PNG-útra: a félkész segédfájlokat eldobjuk. */
            while (next->raw_count && next->raws[next->raw_count - 1].asset == next->asset_count - 1)
            {
                free(next->raws[next->raw_count - 1].bytes);
                memset(&next->raws[next->raw_count - 1], 0, sizeof(next->raws[0]));
                next->raw_count--;
            }
            a->bytes = read_file(path, APP_TEMPLATE_PNG_MAX, &a->size);
            if (!a->bytes || a->size < 24)
            {
                ok = false;
                break;
            }
            if (trusted)
            {
                /* Kitömörítés nélküli ellenőrzés: a sérült (bit-hibás)
                 * kártyaadatot a chunk-CRC ugyanúgy kiszűri, mint eddig. */
                unsigned width = 0, height = 0, channels = 0;
                size_t compressed = 0;
                if (!png_scan(a->bytes, a->size, &width, &height, &channels, NULL, &compressed, true))
                {
                    validated_forget(selected);
                    ok = false;
                    break;
                }
            }
            a->width = be32(a->bytes + 16);
            a->height = be32(a->bytes + 20);
        }
        free(variants);
        if (d)
            closedir(d);
    }
    if (!ok)
    {
        app_templates_view_free(next);
        snprintf(next->id, 33, "builtin");
        snprintf(selected, 33, "builtin");
        next->generation = generation;
        next->catalogue_count = count;
        save_selection = true;
    }
    /* A választás mentése (selection.txt) a következő, már betöltött témájú
     * hívásra marad: a több FAT-metaadatírás így nem késlelteti a lapozást. */
    /* A hívó a régi képeket csak az LVGL objektumok törlése után szabadíthatja fel. */
    *out = *next;
    free(next);
    STORAGE_GIVE();
    xSemaphoreGive(lock);
    return true;
}
#ifndef APP_TEMPLATE_DESKTOP
/*
 * Háttérfeladat az előre dekódolt kép-segédfájlokhoz.
 *
 * Alacsony prioritású, lassú ütemű: egyszerre egy fájl készül el, a zárakat
 * csak addig tartja, és a lapozás vagy telepítés után három másodpercig
 * félreáll. Így a webes feltöltés és a témaváltás nem várakozik rá. SD nélküli
 * Slave-en el sem indul.
 */
static void raw_task(void *argument)
{
    (void)argument;
    while (true)
    {
        bool worked = app_templates_raw_step();
        vTaskDelay(pdMS_TO_TICKS(worked ? 1500 : 60000));
    }
}

esp_err_t app_templates_raw_start(void)
{
    static TaskHandle_t task;
    if (task)
        return ESP_OK;
    /* A verem belső RAM-ban marad: a feladat SD-t ír, és a PSRAM-verem
     * indulás közben újraindulást okozott (lásd docs/errors.md). */
    return xTaskCreate(raw_task, "template_raw", 4096, NULL, 2, &task) == pdPASS ? ESP_OK
                                                                                 : ESP_ERR_NO_MEM;
}
#endif
/* Közös telepítési mag: a stage könyvtár (az SD-n, a sablongyökér alatt) vagy
 * egy ZIP kibontásával, vagy a GitHubról letöltött fájlokkal áll elő. A teljes
 * validálás itt fut, a régi téma csak sikeres csere után törlődik. */
static esp_err_t install_stage(const char *stage, char error[160])
{
    char dest[256], backup[256];
    path_join(backup, app_templates_root(), ".replace");
    cJSON *doc = NULL;
    esp_err_t result = ESP_FAIL;
    if (!app_templates_validate(stage, &doc, error))
        goto done;
    path_join(dest, app_templates_root(), str(doc, "id"));
    struct stat st;
    bool exists = stat(dest, &st) == 0;
    if (!exists && !home_package(doc) && count >= APP_TEMPLATE_MAX)
    {
        error_set(error, "A sablonlista megtelt");
        goto done;
    }
    /* A validált csomag csak most kerülhet a helyére. A régi téma teljes
     * könyvtárát megőrizzük mindaddig, amíg az új rename nem sikerült. */
    if (exists)
    {
        remove_stage(backup);
        if (rename(dest, backup))
        {
            error_set(error, "A régi sablon biztonsági mentése sikertelen");
            goto done;
        }
    }
    if (rename(stage, dest))
    {
        if (exists)
            rename(backup, dest);
        error_set(error, "SD mentési hiba; a korábbi sablon visszaállítva");
        goto done;
    }
    /* A UI régi képei memóriában élnek, a generation módosítás után
     * az aktuális téma újra beolvasódik az SD-ről. */
    scan();
    generation++;
    result = ESP_OK;
    if (exists)
        remove_stage(backup);
done:
    cJSON_Delete(doc);
    return result;
}

esp_err_t app_templates_install(const char *zip, char error[160])
{
    if (!lock)
    {
        error_set(error, "Sablonkezelő nem elérhető");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (STORAGE_TAKE() != ESP_OK)
    {
        xSemaphoreGive(lock);
        error_set(error, "SD unavailable");
        return ESP_FAIL;
    }
    char stage[256];
    path_join(stage, app_templates_root(), ".install");
    remove_stage(stage);
    MKDIR(stage);
    esp_err_t result = ESP_FAIL;
    if (app_template_unzip(zip, stage, error))
        result = install_stage(stage, error);
    remove_stage(stage);
    STORAGE_GIVE();
    xSemaphoreGive(lock);
    return result;
}

const char *app_templates_download_stage(void)
{
    static char stage[256];
    path_join(stage, app_templates_root(), ".download");
    return stage;
}

esp_err_t app_templates_install_downloaded(char error[160])
{
    if (!lock)
    {
        error_set(error, "Sablonkezelő nem elérhető");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (STORAGE_TAKE() != ESP_OK)
    {
        xSemaphoreGive(lock);
        error_set(error, "SD unavailable");
        return ESP_FAIL;
    }
    const char *stage = app_templates_download_stage();
    esp_err_t result = install_stage(stage, error);
    remove_stage(stage);
    STORAGE_GIVE();
    xSemaphoreGive(lock);
    return result;
}

void app_templates_download_reset(void)
{
    if (STORAGE_TAKE() != ESP_OK)
        return;
    const char *stage = app_templates_download_stage();
    remove_stage(stage);
    MKDIR(stage);
    STORAGE_GIVE();
}

esp_err_t app_templates_delete(const char *id, char error[160])
{
    if (!lock || !app_templates_safe_id(id))
    {
        error_set(error, "A beépített nézet nem törölhető / Invalid id");
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    char path[256];
    path_join(path, app_templates_root(), id);
    bool found = home_ready && !strcmp(id, APP_HOME_RESOURCE_ID);
    for (unsigned i = 1; i < count; i++)
        if (!strcmp(catalogue[i].id, id))
            found = true;
    if (!found)
    {
        xSemaphoreGive(lock);
        return ESP_ERR_NOT_FOUND;
    }
    if (STORAGE_TAKE() != ESP_OK)
    {
        xSemaphoreGive(lock);
        error_set(error, "SD unavailable");
        return ESP_FAIL;
    }
    if (!strcmp(selected, id))
    {
        snprintf(selected, 33, "builtin");
        generation++;
        save_selection = true;
    }
    remove_stage(path);
    struct stat st;
    bool ok = stat(path, &st) != 0;
    scan();
    generation++;
    STORAGE_GIVE();
    xSemaphoreGive(lock);
    if (!ok)
        error_set(error, "Törlési hiba / Delete failed");
    return ok ? ESP_OK : ESP_FAIL;
}

char *app_templates_manifest_json(const char *id)
{
    if (!lock || !app_templates_safe_id(id))
        return NULL;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool found = home_ready && !strcmp(id, APP_HOME_RESOURCE_ID);
    for (unsigned i = 1; i < count; i++)
        if (!strcmp(catalogue[i].id, id))
            found = true;
    char *result = NULL;
    if (found && STORAGE_TAKE() == ESP_OK)
    {
        char dir[256], path[256];
        path_join(dir, app_templates_root(), id);
        path_join(path, dir, "template.json");
        size_t size = 0;
        result = (char *)read_file(path, 65536, &size);
        STORAGE_GIVE();
    }
    xSemaphoreGive(lock);
    return result;
}

esp_err_t app_templates_probe(const char *zip, cJSON **report, char error[160])
{
    if (report)
        *report = NULL;
    if (!lock || !report)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (STORAGE_TAKE() != ESP_OK)
    {
        xSemaphoreGive(lock);
        error_set(error, "SD unavailable");
        return ESP_FAIL;
    }
    char stage[256];
    path_join(stage, app_templates_root(), ".check");
    remove_stage(stage);
    MKDIR(stage);
    cJSON *doc = NULL, *result = NULL;
    esp_err_t status = ESP_FAIL;
    if (!app_template_unzip(zip, stage, error) || !app_templates_validate(stage, &doc, error))
        goto done;
    size_t encoded = 0, decoded = 0;
    char path[256];
    DIR *dir = opendir(stage);
    struct dirent *entry;
    if (!dir)
        goto done;
    while ((entry = readdir(dir)))
    {
        if (!app_templates_safe_asset(entry->d_name))
            continue;
        path_join(path, stage, entry->d_name);
        size_t size = 0;
        unsigned char *png = read_file(path, APP_TEMPLATE_PNG_MAX, &size);
        if (!png)
        {
            closedir(dir);
            goto done;
        }
        encoded += size;
        if (!preview_asset(entry->d_name))
            decoded += (size_t)be32(png + 16) * be32(png + 20) * 4;
        free(png);
    }
    closedir(dir);
    result = cJSON_CreateObject();
    if (!result)
    {
        status = ESP_ERR_NO_MEM;
        goto done;
    }
    bool exists = home_ready && home_package(doc);
    for (unsigned i = 0; i < count; i++)
        if (!strcmp(catalogue[i].id, str(doc, "id")))
            exists = true;
    cJSON_AddBoolToObject(result, "valid", true);
    cJSON_AddBoolToObject(result, "updating", exists);
    cJSON_AddBoolToObject(result, "installable", exists || home_package(doc) || count < APP_TEMPLATE_MAX);
    cJSON_AddBoolToObject(result, "resources", home_package(doc));
    cJSON_AddStringToObject(result, "id", str(doc, "id"));
    cJSON_AddStringToObject(result, "name", str(doc, "name"));
    cJSON_AddNumberToObject(result, "version", cJSON_GetObjectItem(doc, "version")->valueint);
    cJSON_AddNumberToObject(result, "encoded_bytes", encoded);
    cJSON_AddNumberToObject(result, "decoded_image_bytes", decoded);
    cJSON_AddStringToObject(result,
                            "message",
                            exists ? "A meglévő téma frissíthető (felülírás biztonsági mentéssel)"
                            : !home_package(doc) && count >= APP_TEMPLATE_MAX ? "Sablonlista megtelt"
                                                        : "Érvényes, telepíthető sablon");
    *report = result;
    result = NULL;
    status = ESP_OK;
done:
    cJSON_Delete(result);
    cJSON_Delete(doc);
    remove_stage(stage);
    STORAGE_GIVE();
    xSemaphoreGive(lock);
    return status;
}


bool app_templates_home_info(app_template_info_t *info, unsigned *version)
{
    if (!lock || !xSemaphoreTake(lock, 0))
        return false;
    bool ready = home_ready;
    if (info)
        *info = home_info;
    if (version)
        *version = home_generation;
    xSemaphoreGive(lock);
    return ready;
}

/*
 * Az Otthon-oldal képeinek beolvasása. A 0. hely a háttér, az 1. a logó, a 2.
 * helytől jönnek az ikonok. icons_only esetén a 0. és 1. hely üres marad: a
 * hívó a már dekódolt hátteret és logót tartja meg, és csak az ikonokat cseréli.
 */
static bool home_load(const char *const *icons, unsigned icon_count, bool night,
                      bool consumers, bool icons_only, app_template_view_t *out)
{
    if (!out || icon_count > 12 || (!icons && icon_count) || !lock || !xSemaphoreTake(lock, 0))
        return false;
    if (!home_ready || !STORAGE_TRY())
    {
        xSemaphoreGive(lock);
        return false;
    }
    app_template_view_t *next = calloc(1, sizeof(*next));
    bool ok = next != NULL;
    char directory[256], path[256];
    path_join(directory, app_templates_root(), APP_HOME_RESOURCE_ID);
    unsigned start = 0;
    if (ok && icons_only)
    {
        /* Üres háttér- és logóhely: a view_free a NULL puffert kihagyja. */
        next->asset_count = 2;
        start = 2;
    }
    for (unsigned i = start; ok && i < icon_count + 2; i++)
    {
        char filename[65];
        if (i == 0)
            snprintf(filename, sizeof(filename), "%s-%s.png",
                     consumers && home_info.version >= 2 ? "consumers" : "background", night ? "dark" : "light");
        else if (i == 1)
            snprintf(filename, sizeof(filename), "loxone-logo.png");
        else
        {
            if (!app_templates_safe_id(icons[i - 2]))
            {
                ok = false;
                break;
            }
            snprintf(filename, sizeof(filename), "icon-%s.png", icons[i - 2]);
        }
        /* Az ismétlődő ikon egyetlen memóriapéldányon osztozik. */
        bool present = false;
        for (unsigned j = 0; j < next->asset_count; j++)
            if (!strcmp(next->assets[j].name, filename))
                present = true;
        if (present)
            continue;
        app_template_asset_t *asset = &next->assets[next->asset_count++];
        snprintf(asset->name, sizeof(asset->name), "%s", filename);
        path_join(path, directory, filename);
        asset->bytes = read_file(path, i < 2 ? APP_TEMPLATE_PNG_MAX : 32768, &asset->size);
        /*
         * A csomagot a scan() már teljesen (zlib-kitömörítéssel) ellenőrizte.
         * Itt csak a szerkezetet és a méretet nézzük: a kép az Otthon-oldal
         * home_decode_images() hívásában azonnal teljesen dekódolódik, a
         * lodepng a CRC-t és az Adler-32-t is ellenőrzi, és a hiba ugyanazt a
         * betöltőképes újrapróbálást váltja ki. Így egy oldalváltásnál a
         * háttér nem tömörítődik ki kétszer.
         */
        unsigned channels = 0;
        size_t compressed = 0;
        ok = asset->bytes && png_scan(asset->bytes, asset->size, &asset->width, &asset->height,
                                      &channels, NULL, &compressed, false);
        if (ok && i >= 2)
            ok = asset->width == 96 && asset->height == 96;
    }
    if (ok)
    {
        snprintf(next->id, sizeof(next->id), "%s", APP_HOME_RESOURCE_ID);
        next->generation = home_generation;
        *out = *next;
    }
    else if (next)
        app_templates_view_free(next);
    free(next);
    STORAGE_GIVE();
    xSemaphoreGive(lock);
    return ok;
}

bool app_templates_home_load_for(const char *const *icons, unsigned icon_count, bool night,
                                  bool consumers, app_template_view_t *out)
{
    return home_load(icons, icon_count, night, consumers, false, out);
}

bool app_templates_home_load_icons(const char *const *icons, unsigned icon_count,
                                    app_template_view_t *out)
{
    return home_load(icons, icon_count, false, false, true, out);
}

/* A korábbi C-hívók az Otthon-hátteret kapják; a régi SD-csomag is betölthető. */
bool app_templates_home_load(const char *const *icons, unsigned icon_count, bool night,
                              app_template_view_t *out)
{
    return app_templates_home_load_for(icons, icon_count, night, false, out);
}
