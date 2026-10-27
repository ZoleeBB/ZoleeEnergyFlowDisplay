/* Beállításcsomag: összeállítás, szigorú ellenőrzés és tranzakciós visszaírás.
 * Platformfüggetlen; az SD-zárolás és a flash-tárolás a hívó dolga. */
#include "app_backup.h"
#include "app_config.h"
#include "app_loxone_model.h"
#include "app_loxone_home_model.h"
#include "cJSON.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A csomagban ismert fájlok. A kulcs egyben a gyökérhez viszonyított útvonal. */
typedef enum
{
    PART_CONFIG,
    PART_LOXONE,
    PART_HOME,
    PART_ORDER,
    PART_SELECTION,
    PART_COUNT
} part_t;

static const struct
{
    const char *key;
    bool json;
    size_t limit;
} parts[PART_COUNT] = {
    {"config.json", true, 16384},
    {"loxone.json", true, 16384},
    {"loxone-home.json", true, 128U * 1024U},
    {"templates/order.txt", false, 2048},
    {"templates/selection.txt", false, 256},
};

static void set_error(char error[APP_BACKUP_ERROR_SIZE], const char *format, const char *detail)
{
    if (error)
        snprintf(error, APP_BACKUP_ERROR_SIZE, format, detail ? detail : "");
}

static void path_for(char *out, size_t size, const char *root, part_t part)
{
    snprintf(out, size, "%s/%s", root, parts[part].key);
}

/* Kis fájl teljes beolvasása POSIX read()-del (nincs 128 bájtos stdio-puffer). */
static char *read_all(const char *path, size_t limit, size_t *size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (unsigned long long)st.st_size > (unsigned long long)limit)
    {
        close(fd);
        return NULL;
    }
    size_t n = (size_t)st.st_size, done = 0;
    char *data = malloc(n + 1);
    if (!data)
    {
        close(fd);
        return NULL;
    }
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
    if (size)
        *size = n;
    return data;
}

/* Tranzakciós fájlcsere FAT-on: tmp írás + fsync, a régi .bak-ra, az új a helyére. */
static bool write_atomic(const char *path, const char *data, size_t length)
{
    char tmp[256], bak[256];
    snprintf(tmp, sizeof(tmp), "%s.rstmp", path);
    snprintf(bak, sizeof(bak), "%s.rsbak", path);
    FILE *file = fopen(tmp, "wb");
    if (!file)
        return false;
    bool ok = fwrite(data, 1, length, file) == length && fflush(file) == 0 &&
              fsync(fileno(file)) == 0;
    if (fclose(file) != 0)
        ok = false;
    if (!ok)
    {
        remove(tmp);
        return false;
    }
    remove(bak);
    bool had_old = access(path, F_OK) == 0;
    if (had_old && rename(path, bak) != 0)
    {
        remove(tmp);
        return false;
    }
    if (rename(tmp, path) != 0)
    {
        if (had_old)
            (void)rename(bak, path);
        remove(tmp);
        return false;
    }
    remove(bak);
    return true;
}

/* Sablonazonosító-lista (order.txt / selection.txt): soronként egy azonosító. */
static bool valid_id_lines(const char *text, size_t limit, unsigned max_lines)
{
    if (!text || strlen(text) > limit)
        return false;
    unsigned lines = 0;
    const char *p = text;
    while (*p)
    {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n && p[n - 1] == '\r')
            n--;
        if (n)
        {
            if (n > 32 || ++lines > max_lines)
                return false;
            for (size_t i = 0; i < n; i++)
            {
                char c = p[i];
                if (!((c >= 'a' && c <= 'z') || isdigit((unsigned char)c) || c == '-' || c == '_'))
                    return false;
            }
        }
        p += n;
        if (*p == '\r')
            p++;
        if (*p == '\n')
            p++;
    }
    return true;
}

/* A config.json jelszavai: exportnál null, visszaállításkor a meglévő érték. */
static const char *secret_sections[] = {"wifi", "mqtt", "ota"};

static void hide_secrets(cJSON *config)
{
    for (unsigned i = 0; i < sizeof(secret_sections) / sizeof(secret_sections[0]); i++)
    {
        cJSON *section = cJSON_GetObjectItemCaseSensitive(config, secret_sections[i]);
        if (cJSON_IsObject(section) && cJSON_HasObjectItem(section, "password"))
            cJSON_ReplaceItemInObjectCaseSensitive(section, "password", cJSON_CreateNull());
    }
}

static bool keep_secrets(cJSON *config, const cJSON *current)
{
    for (unsigned i = 0; i < sizeof(secret_sections) / sizeof(secret_sections[0]); i++)
    {
        cJSON *section = cJSON_GetObjectItemCaseSensitive(config, secret_sections[i]);
        if (!cJSON_IsObject(section))
            continue;
        cJSON *password = cJSON_GetObjectItemCaseSensitive(section, "password");
        if (password && !cJSON_IsNull(password))
            continue;
        const cJSON *old = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(current, secret_sections[i]), "password");
        cJSON *value = cJSON_CreateString(cJSON_IsString(old) ? old->valuestring : "");
        if (!value)
            return false;
        if (password)
            cJSON_ReplaceItemInObjectCaseSensitive(section, "password", value);
        else
            cJSON_AddItemToObject(section, "password", value);
    }
    return true;
}

char *app_backup_export(const char *root, bool include_secrets, const char *firmware,
                        char error[APP_BACKUP_ERROR_SIZE])
{
    cJSON *bundle = cJSON_CreateObject(), *files = cJSON_CreateObject();
    if (!bundle || !files)
    {
        cJSON_Delete(bundle);
        cJSON_Delete(files);
        set_error(error, "Nincs elegendő memória.%s", NULL);
        return NULL;
    }
    cJSON_AddStringToObject(bundle, "format", APP_BACKUP_FORMAT);
    cJSON_AddNumberToObject(bundle, "version", APP_BACKUP_VERSION);
    cJSON_AddStringToObject(bundle, "firmware", firmware ? firmware : "");
    cJSON_AddBoolToObject(bundle, "secrets", include_secrets);
    cJSON_AddItemToObject(bundle, "files", files);
    bool ok = true;
    for (int part = 0; ok && part < PART_COUNT; part++)
    {
        char path[256];
        path_for(path, sizeof(path), root, (part_t)part);
        size_t size = 0;
        char *data = read_all(path, parts[part].limit, &size);
        if (!data)
            continue; /* A hiányzó fájl (pl. Loxone nélküli mód) egyszerűen kimarad. */
        cJSON *item = NULL;
        if (parts[part].json)
        {
            item = cJSON_ParseWithLength(data, size);
            if (cJSON_IsObject(item) && part == PART_CONFIG && !include_secrets)
                hide_secrets(item);
            if (!cJSON_IsObject(item))
            {
                cJSON_Delete(item);
                item = NULL;
                set_error(error, "Hibás JSON az SD-kártyán: %s", parts[part].key);
                ok = false;
            }
        }
        else
            item = cJSON_CreateString(data);
        free(data);
        if (ok && (!item || !cJSON_AddItemToObject(files, parts[part].key, item)))
        {
            cJSON_Delete(item);
            set_error(error, "Nincs elegendő memória.%s", NULL);
            ok = false;
        }
    }
    if (ok && !cJSON_GetArraySize(files))
    {
        set_error(error, "Nincs menthető beállítás az SD-kártyán.%s", NULL);
        ok = false;
    }
    char *text = ok ? cJSON_PrintUnformatted(bundle) : NULL;
    cJSON_Delete(bundle);
    if (ok && !text)
        set_error(error, "Nincs elegendő memória.%s", NULL);
    if (text && strlen(text) > APP_BACKUP_MAX)
    {
        free(text);
        text = NULL;
        set_error(error, "A mentés meghaladja a %s korlátot.", "256 KiB");
    }
    return text;
}

/* Egy rész ellenőrzése és végleges szövegének előállítása (malloc). */
static char *prepare_part(const char *root, part_t part, const cJSON *item,
                          char error[APP_BACKUP_ERROR_SIZE])
{
    const char *key = parts[part].key;
    if (!parts[part].json)
    {
        if (!cJSON_IsString(item) ||
            !valid_id_lines(item->valuestring, parts[part].limit, part == PART_ORDER ? 32 : 1))
        {
            set_error(error, "Érvénytelen sablonlista: %s", key);
            return NULL;
        }
        return strdup(item->valuestring);
    }
    if (!cJSON_IsObject(item))
    {
        set_error(error, "A(z) %s nem JSON-objektum.", key);
        return NULL;
    }
    cJSON *copy = cJSON_Duplicate(item, true);
    char *text = NULL;
    bool ok = copy != NULL;
    if (ok && part == PART_CONFIG)
    {
        char path[256];
        path_for(path, sizeof(path), root, PART_CONFIG);
        char *current_text = read_all(path, parts[PART_CONFIG].limit, NULL);
        cJSON *current = current_text ? cJSON_Parse(current_text) : NULL;
        free(current_text);
        ok = keep_secrets(copy, current);
        cJSON_Delete(current);
    }
    if (ok)
        text = cJSON_PrintUnformatted(copy);
    cJSON_Delete(copy);
    if (!text || strlen(text) > parts[part].limit)
    {
        free(text);
        set_error(error, "Túl nagy vagy nem menthető rész: %s", key);
        return NULL;
    }
    if (part == PART_CONFIG)
    {
        /* Ugyanaz a validátor, amelyet bootkor és a webes mentésnél is használunk. */
        char tmp[256], message[256] = {0};
        snprintf(tmp, sizeof(tmp), "%s/config.json.rscheck", root);
        FILE *file = fopen(tmp, "wb");
        ok = file && fwrite(text, 1, strlen(text), file) == strlen(text);
        if (file && fclose(file) != 0)
            ok = false;
        app_config_t *config = ok ? calloc(1, sizeof(*config)) : NULL;
        ok = config && app_config_load_file(tmp, config, message, sizeof(message)) == ESP_OK;
        free(config);
        remove(tmp);
        if (!ok)
        {
            set_error(error, "config.json: %s", message[0] ? message : "érvénytelen beállítás");
            free(text);
            return NULL;
        }
    }
    else if (part == PART_LOXONE)
    {
        lx_config_t *config = calloc(1, sizeof(*config));
        cJSON *parsed = cJSON_Parse(text);
        ok = config && parsed && lx_config_parse(parsed, config, false);
        cJSON_Delete(parsed);
        free(config);
        if (!ok)
        {
            set_error(error, "loxone.json: érvénytelen Loxone-beállítás.%s", NULL);
            free(text);
            return NULL;
        }
    }
    else if (part == PART_HOME)
    {
        lx_home_config_t *config = calloc(1, sizeof(*config));
        char message[LX_HOME_ERROR_SIZE] = {0};
        cJSON *parsed = lx_home_document_parse(text, strlen(text));
        ok = config && parsed && lx_home_config_parse(parsed, config, message);
        cJSON_Delete(parsed);
        free(config);
        if (!ok)
        {
            set_error(error, "loxone-home.json: %s", message[0] ? message : "érvénytelen Otthon-beállítás");
            free(text);
            return NULL;
        }
    }
    return text;
}

/* Közös ellenőrző mag: siker esetén a texts tömb a végleges fájltartalmakat tartja. */
static esp_err_t prepare_bundle(const char *root, const char *json, size_t length,
                                char *texts[PART_COUNT], unsigned *count,
                                char error[APP_BACKUP_ERROR_SIZE])
{
    memset(texts, 0, sizeof(char *) * PART_COUNT);
    *count = 0;
    if (!json || length < 2 || length > APP_BACKUP_MAX)
    {
        set_error(error, "A mentés mérete érvénytelen (legfeljebb %s).", "256 KiB");
        return ESP_ERR_INVALID_SIZE;
    }
    cJSON *bundle = cJSON_ParseWithLength(json, length);
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(bundle, "format");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(bundle, "version");
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(bundle, "files");
    if (!cJSON_IsObject(bundle) || !cJSON_IsString(format) ||
        strcmp(format->valuestring, APP_BACKUP_FORMAT) || !cJSON_IsNumber(version) ||
        version->valuedouble != APP_BACKUP_VERSION || !cJSON_IsObject(files))
    {
        cJSON_Delete(bundle);
        set_error(error, "Ez nem Zolee Energy Flow Display beállításmentés (vagy újabb formátum).%s", NULL);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_OK;
    const cJSON *item;
    cJSON_ArrayForEach(item, files)
    {
        int part = -1;
        for (int i = 0; i < PART_COUNT; i++)
            if (item->string && !strcmp(item->string, parts[i].key))
                part = i;
        if (part < 0 || texts[part])
        {
            set_error(error, "Ismeretlen vagy ismétlődő fájl a mentésben: %s", item->string);
            result = ESP_ERR_INVALID_ARG;
            break;
        }
        texts[part] = prepare_part(root, (part_t)part, item, error);
        if (!texts[part])
        {
            result = ESP_ERR_INVALID_ARG;
            break;
        }
        (*count)++;
    }
    cJSON_Delete(bundle);
    if (result == ESP_OK && !*count)
    {
        set_error(error, "A mentés nem tartalmaz visszaállítható fájlt.%s", NULL);
        result = ESP_ERR_INVALID_ARG;
    }
    if (result != ESP_OK)
        for (int i = 0; i < PART_COUNT; i++)
        {
            free(texts[i]);
            texts[i] = NULL;
        }
    return result;
}

esp_err_t app_backup_validate(const char *root, const char *json, size_t length,
                              unsigned *files, char error[APP_BACKUP_ERROR_SIZE])
{
    char *texts[PART_COUNT];
    unsigned count = 0;
    esp_err_t result = prepare_bundle(root, json, length, texts, &count, error);
    for (int i = 0; i < PART_COUNT; i++)
        free(texts[i]);
    if (files)
        *files = count;
    return result;
}

esp_err_t app_backup_restore(const char *root, const char *json, size_t length,
                             unsigned *files, char error[APP_BACKUP_ERROR_SIZE])
{
    char *texts[PART_COUNT];
    unsigned count = 0;
    esp_err_t result = prepare_bundle(root, json, length, texts, &count, error);
    if (result == ESP_OK)
    {
        char directory[256];
        snprintf(directory, sizeof(directory), "%s/templates", root);
        if ((texts[PART_ORDER] || texts[PART_SELECTION]) && access(directory, F_OK) != 0)
            (void)mkdir(directory, 0775);
        for (int i = 0; i < PART_COUNT; i++)
        {
            if (!texts[i])
                continue;
            char path[256];
            path_for(path, sizeof(path), root, (part_t)i);
            if (!write_atomic(path, texts[i], strlen(texts[i])))
            {
                set_error(error, "Az SD-írás sikertelen: %s", parts[i].key);
                result = ESP_FAIL;
                break;
            }
        }
    }
    for (int i = 0; i < PART_COUNT; i++)
        free(texts[i]);
    if (files)
        *files = result == ESP_OK ? count : 0;
    return result;
}

uint32_t app_backup_crc32(uint32_t crc, const void *data, size_t length)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (length--)
    {
        crc ^= *p++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static void put32(uint8_t *p, uint32_t value)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void app_backup_slot_encode(const app_backup_slot_t *slot, uint8_t out[APP_BACKUP_SLOT_HEADER])
{
    memset(out, 0, APP_BACKUP_SLOT_HEADER);
    put32(out, slot->magic);
    put32(out + 4, slot->version);
    put32(out + 8, slot->sequence);
    put32(out + 12, slot->length);
    put32(out + 16, slot->payload_crc);
    put32(out + 20, (uint32_t)((uint64_t)slot->epoch_s & 0xFFFFFFFFU));
    put32(out + 24, (uint32_t)((uint64_t)slot->epoch_s >> 32));
    put32(out + 28, app_backup_crc32(0, out, 28));
}

bool app_backup_slot_decode(const uint8_t in[APP_BACKUP_SLOT_HEADER], uint32_t capacity,
                            app_backup_slot_t *slot)
{
    app_backup_slot_t value = {
        .magic = get32(in),
        .version = get32(in + 4),
        .sequence = get32(in + 8),
        .length = get32(in + 12),
        .payload_crc = get32(in + 16),
        .epoch_s = (int64_t)((uint64_t)get32(in + 20) | (uint64_t)get32(in + 24) << 32),
        .header_crc = get32(in + 28),
    };
    if (value.magic != APP_BACKUP_SLOT_MAGIC || value.version != APP_BACKUP_VERSION ||
        value.header_crc != app_backup_crc32(0, in, 28) || !value.length ||
        value.length > APP_BACKUP_MAX || value.length > capacity - APP_BACKUP_SLOT_HEADER)
        return false;
    *slot = value;
    return true;
}

int app_backup_slot_pick(const app_backup_slot_t *first, bool first_valid,
                         const app_backup_slot_t *second, bool second_valid)
{
    if (first_valid && second_valid)
        /* Körbeforduló sorszám: a „később” az, amelyik fél számtéren belül előrébb jár. */
        return (int32_t)(second->sequence - first->sequence) > 0 ? 1 : 0;
    if (first_valid)
        return 0;
    if (second_valid)
        return 1;
    return -1;
}
