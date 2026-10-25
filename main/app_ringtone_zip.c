/* A hangcsomag saját kibontója; a témák főági ZIP-kezelőjét nem módosítja. */
#include "app_ringtone_zip.h"
#include "app_ringtone_wav.h"
#include "app_templates.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static unsigned read16(const unsigned char *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

static uint32_t read32(const unsigned char *p)
{
    return read16(p) | ((uint32_t)read16(p + 2) << 16);
}

static bool skipped(cJSON *report, const char *name, const char *reason, char error[160])
{
    cJSON *item = cJSON_CreateObject();
    if (!item || !cJSON_AddStringToObject(item, "name", name)
        || !cJSON_AddStringToObject(item, "reason", reason)) {
        cJSON_Delete(item);
        snprintf(error, 160, "%s", "Nincs memória a kihagyott fájlok jelentéséhez.");
        return false;
    }
    if (!cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(report, "failed"), item)) {
        cJSON_Delete(item);
        snprintf(error, 160, "%s", "A hibajelentés nem bővíthető.");
        return false;
    }
    return true;
}

/* A központi fejléc érvényes határai után minden elemhiba helyben marad.
 * A fájlt csak CRC- és PCM-ellenőrzés után nyitjuk meg írásra. */
static bool extract(FILE *zip, const unsigned char h[46], const char *name,
                    uint32_t cd, const char *directory, char reason[160])
{
    uint8_t *input = NULL, *output = NULL;
    FILE *file = NULL;
    bool ok = false;
    const char *stage = "ZIP fejléc/típus/méret";
    unsigned flags = read16(h + 8), method = read16(h + 10), n = read16(h + 28);
    uint32_t compressed = read32(h + 20), size = read32(h + 24), offset = read32(h + 42);
    unsigned type = (read32(h + 38) >> 16) & 0170000;
    unsigned char local[30];
    char local_name[65], path[256] = "";
    errno = 0;
    if ((flags & ~(0x800 | 8 | 6)) || (method != 0 && method != 8) || read16(h + 34)
        || (type && type != 0100000) || !size || size > APP_RINGTONE_MAX
        || compressed > APP_TEMPLATE_ZIP_MAX || offset >= cd)
        goto done;
    stage = "ZIP helyi fejléc";
    if (fseek(zip, offset, SEEK_SET) || fread(local, 1, 30, zip) != 30
        || read32(local) != 0x04034b50 || read16(local + 6) != flags
        || read16(local + 8) != method || read16(local + 26) != n
        || fread(local_name, 1, n, zip) != n || memcmp(local_name, name, n))
        goto done;
    uint64_t start = (uint64_t)offset + 30 + n + read16(local + 28);
    if (start + compressed > cd)
        goto done;
    stage = "ZIP memóriafoglalás";
    input = malloc(compressed ? compressed : 1);
    output = malloc(size);
    if (!input || !output)
        goto done;
    stage = "ZIP adatolvasás";
    if (fseek(zip, (long)start, SEEK_SET) || fread(input, 1, compressed, zip) != compressed)
        goto done;
    stage = "ZIP kicsomagolás";
    if (method == 0) {
        if (compressed != size)
            goto done;
        memcpy(output, input, size);
    } else {
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
    stage = "ZIP CRC-ellenőrzés";
    if ((uint32_t)crc32(0, output, size) != read32(h + 16))
        goto done;
    stage = "PCM WAV-ellenőrzés (16 bit, mono, 16 kHz, legfeljebb 60 s)";
    size_t pcm_offset, pcm_size;
    if (!app_ringtone_wav(output, size, &pcm_offset, &pcm_size))
        goto done;
    stage = "SD fájlmegnyitás";
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    file = fopen(path, "wb");
    if (!file)
        goto done;
    /* A főági témákhoz hasonló, pufferelt stdio, külön fsync nélkül. */
    stage = "SD fájlírás/lezárás";
    ok = fwrite(output, 1, size, file) == size;
    if (fclose(file))
        ok = false;
    file = NULL;
done:
    if (file)
        fclose(file);
    if (!ok) {
        snprintf(reason, 160, "%s (errno=%d)", stage, errno);
        if (path[0])
            remove(path);
    }
    free(input);
    free(output);
    clearerr(zip);
    return ok;
}

bool app_ringtone_unpack(const char *archive, const char *directory,
                         cJSON *report, char error[160])
{
    FILE *zip = fopen(archive, "rb");
    if (!zip) {
        snprintf(error, 160, "ZIP megnyitása sikertelen (errno=%d).", errno);
        return false;
    }
    bool ok = false;
    unsigned installed = 0;
    unsigned char *tail = NULL;
    char (*names)[65] = NULL;
    snprintf(error, 160, "%s", "Hibás ZIP központi könyvtár vagy archívumméret.");
    if (fseek(zip, 0, SEEK_END))
        goto done;
    long length = ftell(zip);
    if (length < 22 || length > APP_TEMPLATE_ZIP_MAX)
        goto done;
    size_t tail_size = length < 65557 ? (size_t)length : 65557;
    tail = malloc(tail_size);
    if (!tail || fseek(zip, length - (long)tail_size, SEEK_SET)
        || fread(tail, 1, tail_size, zip) != tail_size)
        goto done;
    long end = -1;
    for (long i = (long)tail_size - 22; i >= 0; --i) {
        if (read32(tail + i) == 0x06054b50 && i + 22 + read16(tail + i + 20) == (long)tail_size) {
            end = i;
            break;
        }
    }
    if (end < 0)
        goto done;
    const unsigned char *e = tail + end;
    unsigned entries = read16(e + 10);
    uint32_t cd = read32(e + 16), cd_size = read32(e + 12);
    if (read16(e + 4) || read16(e + 6) || read16(e + 8) != entries || !entries || entries > 72
        || (uint64_t)cd + cd_size != (uint64_t)(length - (long)tail_size + end))
        goto done;
    names = calloc(entries, sizeof(*names));
    if (!names)
        goto done;
    uint64_t next = cd, total = 0;
    for (unsigned i = 0; i < entries; ++i) {
        unsigned char h[46];
        if (next + sizeof(h) > (uint64_t)cd + cd_size || fseek(zip, (long)next, SEEK_SET)
            || fread(h, 1, sizeof(h), zip) != sizeof(h) || read32(h) != 0x02014b50)
            goto done;
        unsigned n = read16(h + 28);
        next += 46U + n + read16(h + 30) + read16(h + 32);
        if (next > (uint64_t)cd + cd_size)
            goto done;
        if (!n || n > 64) {
            if (n && fread(names[i], 1, 64, zip) != 64)
                goto done;
            if (!skipped(report, n ? names[i] : "(üres fájlnév)",
                         "A fájlnév 1–64 bájt lehet; a hosszabb név eleje látható.", error))
                goto done;
            continue;
        }
        if (fread(names[i], 1, n, zip) != n)
            goto done;
        names[i][n] = 0;
        const char *name = names[i];
        if (strlen(name) != n || !app_ringtone_name(name)) {
            /* A manifest nem szükséges a fájlonkénti telepítéshez. */
            if ((strlen(name) != n || strcmp(name, "ringtone.json"))
                && !skipped(report, name, "Nem támogatott vagy nem biztonságos hangfájlnév.", error))
                goto done;
            continue;
        }
        bool duplicate = false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(names[j], name))
                duplicate = true;
        if (duplicate) {
            if (!skipped(report, name, "Ismétlődő fájlnév a ZIP-ben.", error))
                goto done;
            continue;
        }
        uint32_t size = read32(h + 24);
        if (installed >= 32 || total + size > APP_TEMPLATE_ZIP_MAX) {
            if (!skipped(report, name, "Legfeljebb 32 hang és 8 MiB kibontott adat telepíthető.", error))
                goto done;
            continue;
        }
        char reason[160];
        if (extract(zip, h, name, cd, directory, reason)) {
            ++installed;
            total += size;
        } else if (!skipped(report, name, reason, error)) {
            goto done;
        }
    }
    ok = next == (uint64_t)cd + cd_size;
    if (ok)
        error[0] = 0;
done:
    if (!cJSON_AddNumberToObject(report, "installed", installed)) {
        snprintf(error, 160, "%s", "Nincs memória a telepített fájlok jelentéséhez.");
        ok = false;
    }
    free(names);
    free(tail);
    fclose(zip);
    return ok;
}
