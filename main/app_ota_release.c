/* A release-választás tiszta, hálózatfüggetlen szabályai. */
#include "app_ota_release.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool version(const char *text, uint32_t parts[3], bool *prerelease)
{
    if (!text || !*text || strlen(text) > 31)
        return false;
    if (*text == 'v')
        text++;
    for (unsigned i = 0; i < 3; i++)
    {
        const char *start = text;
        uint64_t value = 0;
        while (*text >= '0' && *text <= '9')
        {
            value = value * 10 + (unsigned)(*text++ - '0');
            if (value > UINT32_MAX)
                return false;
        }
        if (text == start || (text - start > 1 && *start == '0'))
            return false;
        parts[i] = (uint32_t)value;
        if (i < 2 && *text++ != '.')
            return false;
    }
    *prerelease = *text == '-';
    if (*prerelease)
    {
        if (!*++text)
            return false;
        for (; *text; text++)
        {
            if (!isalnum((unsigned char)*text) && *text != '-' && *text != '.')
                return false;
        }
    }
    return *text == 0;
}

bool app_ota_version_newer(const char *candidate, const char *current)
{
    uint32_t next[3], running[3];
    bool next_pre, running_pre;
    if (!candidate || candidate[0] != 'v' || !version(candidate, next, &next_pre) || next_pre ||
        !version(current, running, &running_pre))
        return false;
    for (unsigned i = 0; i < 3; i++)
    {
        if (next[i] != running[i])
            return next[i] > running[i];
    }
    return running_pre;
}

static const char *string(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

bool app_ota_release_parse(const cJSON *root,
                           const char *current,
                           size_t partition_size,
                           app_ota_release_t *out)
{
    const cJSON *draft = cJSON_GetObjectItemCaseSensitive(root, "draft");
    const cJSON *pre = cJSON_GetObjectItemCaseSensitive(root, "prerelease");
    const char *tag = string(root, "tag_name");
    if (!out || !cJSON_IsFalse(draft) || !cJSON_IsFalse(pre) ||
        !app_ota_version_newer(tag, current))
        return false;
    app_ota_release_t result = {0};
    snprintf(result.tag, sizeof(result.tag), "%s", tag);
    snprintf(result.filename, sizeof(result.filename), APP_OTA_ASSET_PREFIX "-%s-ota.bin", tag);
    snprintf(result.url, sizeof(result.url), APP_OTA_RELEASE_BASE "%s/%s", tag, result.filename);
    snprintf(result.checksums_url,
             sizeof(result.checksums_url),
             APP_OTA_RELEASE_BASE "%s/SHA256SUMS.txt",
             tag);
    bool image = false, checksums = false;
    const cJSON *asset;
    cJSON_ArrayForEach(asset, cJSON_GetObjectItemCaseSensitive(root, "assets"))
    {
        const char *name = string(asset, "name");
        const char *url = string(asset, "browser_download_url");
        const char *state = string(asset, "state");
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(asset, "size");
        if (!name || !url || !state || strcmp(state, "uploaded") || !cJSON_IsNumber(size) ||
            !isfinite(size->valuedouble))
            continue;
        double bytes = size->valuedouble;
        if (!strcmp(name, result.filename) && !strcmp(url, result.url))
        {
            if (image || bytes < 288 || bytes > partition_size || bytes != (uint32_t)bytes)
                return false;
            result.size = (uint32_t)bytes;
            image = true;
        }
        if (!strcmp(name, "SHA256SUMS.txt") && !strcmp(url, result.checksums_url))
        {
            if (checksums || bytes <= 0 || bytes > 16384)
                return false;
            checksums = true;
        }
    }
    if (!image || !checksums)
        return false;
    *out = result;
    return true;
}

static int hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool app_ota_checksum_parse(const char *text, const char *filename, uint8_t sha256[32])
{
    if (!text || !filename || !sha256)
        return false;
    bool found = false;
    for (const char *line = text; *line;)
    {
        const char *end = strchr(line, '\n');
        if (!end)
            end = line + strlen(line);
        size_t length = (size_t)(end - line);
        if (length && line[length - 1] == '\r')
            length--;
        if (length == 66 + strlen(filename) && line[64] == ' ' &&
            (line[65] == ' ' || line[65] == '*') && !memcmp(line + 66, filename, strlen(filename)))
        {
            if (found)
                return false;
            for (unsigned i = 0; i < 32; i++)
            {
                int a = hex(line[i * 2]), b = hex(line[i * 2 + 1]);
                if (a < 0 || b < 0)
                    return false;
                sha256[i] = (uint8_t)((a << 4) | b);
            }
            found = true;
        }
        line = *end ? end + 1 : end;
    }
    return found;
}

bool app_ota_download_url_allowed(const char *url)
{
    if (!url || strncmp(url, "https://", 8))
        return false;
    const char *host = url + 8;
    const char *end = strchr(host, '/');
    if (!end)
        return false;
    static const char *const hosts[] = {"api.github.com",
                                        "github.com",
                                        "release-assets.githubusercontent.com",
                                        "objects.githubusercontent.com"};
    for (unsigned i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++)
    {
        if ((size_t)(end - host) == strlen(hosts[i]) &&
            !strncmp(host, hosts[i], (size_t)(end - host)))
            return true;
    }
    return false;
}
