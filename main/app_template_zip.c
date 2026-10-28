/*
 * Feltöltött sablon-ZIP ellenőrzése és biztonságos kibontása.
 *
 * Az archívumban csak engedélyezett nevek és korlátozott méretű elemek
 * szerepelhetnek. Kibontás előtt a fejléceket, az útvonalakat, a tömörítést,
 * valamint a kicsomagolt állományok CRC-értékét is ellenőrizzük.
 */
#include "app_template_zip.h"
#include "app_templates.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

/* A ZIP-formátum többyte-os mezői little-endian sorrendben vannak. */
static unsigned u16(const unsigned char *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

static uint32_t u32(const unsigned char *p)
{
    return u16(p) | ((uint32_t)u16(p + 2) << 16);
}

static bool fail(char *error, const char *message)
{
    snprintf(error, 160, "%s", message);
    return false;
}

bool app_template_unzip(const char *archive, const char *directory, char error[160])
{
    FILE *f = fopen(archive, "rb");
    if (!f)
        return fail(error, "ZIP nem olvasható / Cannot read ZIP");

    bool ok = false;
    unsigned char *tail = NULL, *input = NULL, *output = NULL;
    char names[72][65];
    unsigned entries = 0;
    size_t total = 0;

    /* A központi könyvtárat az archívum végén található leíró alapján keressük. */
    if (fseek(f, 0, SEEK_END))
        goto done;
    long length = ftell(f);
    if (length < 22 || length > APP_TEMPLATE_ZIP_MAX)
        goto done;

    size_t tail_size = length < 65557 ? (size_t)length : 65557;
    tail = malloc(tail_size);
    if (!tail)
        goto done;
    if (fseek(f, length - (long)tail_size, SEEK_SET) || fread(tail, 1, tail_size, f) != tail_size)
        goto done;

    long end = -1;
    for (long i = (long)tail_size - 22; i >= 0; i--)
        if (u32(tail + i) == 0x06054b50 && i + 22 + u16(tail + i + 20) == (long)tail_size)
        {
            end = i;
            break;
        }
    if (end < 0)
        goto done;

    const unsigned char *e = tail + end;
    entries = u16(e + 10);
    uint32_t cd = u32(e + 16), cd_size = u32(e + 12);
    if (u16(e + 4) || u16(e + 6) || u16(e + 8) != entries || !entries || entries > 72 ||
        (uint64_t)cd + cd_size != (uint64_t)(length - (long)tail_size + end))
        goto done;

    long next = cd;
    for (unsigned i = 0; i < entries; i++)
    {
        unsigned char h[46], local[30];
        if (fseek(f, next, SEEK_SET) || fread(h, 1, 46, f) != 46 || u32(h) != 0x02014b50)
            goto done;

        unsigned flags = u16(h + 8), method = u16(h + 10), n = u16(h + 28), extra = u16(h + 30),
                 comment = u16(h + 32);
        uint32_t compressed = u32(h + 20), size = u32(h + 24), offset = u32(h + 42),
                 crc = u32(h + 16);
        unsigned unix_type = (u32(h + 38) >> 16) & 0170000;

        /* Nem követünk könyvtárbejárást, symlinket vagy ismeretlen ZIP-funkciót. */
        if ((flags & ~(0x800 | 8 | 6)) || (method != 0 && method != 8) || u16(h + 34) ||
            (unix_type && unix_type != 0100000) || !n || n > 64 || size > APP_TEMPLATE_PNG_MAX ||
            !size || compressed > APP_TEMPLATE_ZIP_MAX || (total += size) > APP_TEMPLATE_ZIP_MAX)
            goto done;

        if (fread(names[i], 1, n, f) != n)
            goto done;
        names[i][n] = 0;
        if (strlen(names[i]) != n ||
            (strcmp(names[i], "template.json") && !app_templates_safe_asset(names[i])))
            goto done;
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(names[i], names[j]))
                goto done;

        next += 46 + n + extra + comment;
        if (next > (long)((uint64_t)cd + cd_size))
            goto done;
        if (fseek(f, offset, SEEK_SET) || fread(local, 1, 30, f) != 30 ||
            u32(local) != 0x04034b50 || u16(local + 6) != flags || u16(local + 8) != method ||
            u16(local + 26) != n)
            goto done;

        char local_name[65];
        if (fread(local_name, 1, n, f) != n || memcmp(local_name, names[i], n))
            goto done;

        uint64_t data_offset = (uint64_t)offset + 30 + n + u16(local + 28);
        if (data_offset + compressed > cd || offset >= cd)
            goto done;

        /* Egyetlen elemhez tartozó bemeneti/kimeneti puffer él egyszerre. */
        input = malloc(compressed ? compressed : 1);
        output = malloc(size);
        if (!input || !output || fseek(f, (long)data_offset, SEEK_SET) ||
            fread(input, 1, compressed, f) != compressed)
            goto done;

        if (method == 0)
        {
            if (compressed != size)
                goto done;
            memcpy(output, input, size);
        }
        else
        {
            z_stream z = {0};
            z.next_in = input;
            z.avail_in = compressed;
            z.next_out = output;
            z.avail_out = size;
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
                goto done;
            int result = inflate(&z, Z_FINISH);
            bool valid = result == Z_STREAM_END && z.total_out == size && z.total_in == compressed;
            inflateEnd(&z);
            if (!valid)
                goto done;
        }

        if ((uint32_t)crc32(0, output, size) != crc)
            goto done;

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
        FILE *out = fopen(path, "wb");
        if (!out)
            goto done;
        bool written = app_templates_sd_write(out, output, size);
        if (fclose(out))
            written = false;
        if (!written)
            goto done;

        free(input);
        input = NULL;
        free(output);
        output = NULL;
    }

    ok = next == (long)((uint64_t)cd + cd_size);
    /* A nagyobb fájlszám kizárólag a fenntartott, külön validált home-csomagé.
     * Normál témának ugyanúgy legfeljebb 34 ZIP-bejegyzése lehet. */
    if (ok && entries > 34)
    {
        char manifest[256];
        snprintf(manifest, sizeof(manifest), "%s/template.json", directory);
        FILE *json_file = fopen(manifest, "rb");
        char *body = calloc(1, 65537);
        size_t size = json_file && body ? fread(body, 1, 65536, json_file) : 0;
        unsigned depth = 0;
        bool valid_json = size > 0 && size < 65536 && !memchr(body, 0, size);
        bool quoted = false, escaped = false;
        for (size_t i = 0; valid_json && i < size; i++)
        {
            char ch = body[i];
            if (quoted)
            {
                if (escaped) escaped = false;
                else if (ch == '\\') escaped = true;
                else if (ch == '"') quoted = false;
            }
            else if (ch == '"') quoted = true;
            else if (ch == '{' || ch == '[') valid_json = ++depth <= 16;
            else if (ch == '}' || ch == ']')
            {
                if (!depth) valid_json = false;
                else depth--;
            }
        }
        cJSON *root = valid_json && !depth && !quoted ?
                      cJSON_ParseWithLengthOpts(body, size + 1, NULL, true) : NULL;
        const char *kind = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "kind"));
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "id"));
        ok = kind && id && !strcmp(kind, APP_HOME_RESOURCE_KIND) && !strcmp(id, APP_HOME_RESOURCE_ID);
        cJSON_Delete(root);
        free(body);
        if (json_file)
            fclose(json_file);
    }

done:
    /* Minden hibás ágon az összes puffer és a megnyitott ZIP felszabadul. */
    free(input);
    free(output);
    free(tail);
    fclose(f);
    if (!ok)
        fail(error, "Hibás vagy nem támogatott ZIP: útvonal, méret, CRC vagy tömörítés.");
    return ok;
}
