/* A GitHub-alapú témafrissítés tiszta, hálózatfüggetlen szabályai. */
#include "app_template_remote.h"
#include "app_ota_release.h"
#include "app_templates.h"
#include <stdio.h>
#include <string.h>

static bool stable_tag(const char *tag)
{
    /* Az OTA-val azonos szabály: csak „v” előtagú, előzetes jelölés nélküli
     * vMAJOR.MINOR.PATCH (a v0.0.0 fölötti bármely ilyen verzió megfelel). */
    return tag && strlen(tag) < 32 &&
           (app_ota_version_newer(tag, "v0.0.0") || !strcmp(tag, "v0.0.0"));
}

bool app_template_remote_tag(const char *running, const char *latest, char out[32])
{
    const char *tag = stable_tag(running) ? running : stable_tag(latest) ? latest : NULL;
    if (!tag)
        return false;
    snprintf(out, 32, "%s", tag);
    return true;
}

unsigned app_template_remote_ids(const cJSON *contents, char ids[][33], unsigned capacity)
{
    unsigned count = 0;
    const cJSON *entry;
    if (!cJSON_IsArray(contents))
        return 0;
    cJSON_ArrayForEach(entry, contents)
    {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(entry, "type");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        if (count >= capacity || !cJSON_IsString(type) || strcmp(type->valuestring, "dir") ||
            !cJSON_IsString(name) || !app_templates_safe_id(name->valuestring))
            continue;
        snprintf(ids[count++], 33, "%s", name->valuestring);
    }
    return count;
}

static bool add_file(const char *name, char names[][65], unsigned capacity, unsigned *count)
{
    if (!app_templates_safe_asset(name))
        return false;
    for (unsigned i = 0; i < *count; i++)
        if (!strcmp(names[i], name))
            return true;
    if (*count >= capacity)
        return false;
    snprintf(names[(*count)++], 65, "%s", name);
    return true;
}

bool app_template_remote_files(const cJSON *document, char names[][65], unsigned capacity,
                               unsigned *count)
{
    *count = 0;
    if (!cJSON_IsObject(document))
        return false;
    static const char *const scenes[] = {"light", "dark"};
    for (unsigned s = 0; s < 2; s++)
    {
        const cJSON *elements = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(document, scenes[s]), "elements");
        const cJSON *element;
        cJSON_ArrayForEach(element, elements)
        {
            const cJSON *file = cJSON_GetObjectItemCaseSensitive(element, "file");
            if (cJSON_IsString(file) && !add_file(file->valuestring, names, capacity, count))
                return false;
            const cJSON *variant;
            cJSON_ArrayForEach(variant, cJSON_GetObjectItemCaseSensitive(element, "files"))
                if (cJSON_IsString(variant) && !add_file(variant->valuestring, names, capacity, count))
                    return false;
        }
    }
    return add_file("preview-light.png", names, capacity, count) &&
           add_file("preview-dark.png", names, capacity, count);
}

bool app_template_remote_url(char *out, size_t size, const char *tag, const char *id,
                             const char *file)
{
    if (!stable_tag(tag) || !app_templates_safe_id(id) ||
        (strcmp(file, "template.json") && !app_templates_safe_asset(file)))
        return false;
    int length = snprintf(out, size, APP_TEMPLATE_REMOTE_RAW "%s/templates/%s/%s", tag, id, file);
    return length > 0 && (size_t)length < size;
}

bool app_template_remote_url_allowed(const char *url)
{
    if (!url || strncmp(url, "https://", 8))
        return false;
    const char *host = url + 8, *end = strchr(host, '/');
    if (!end)
        return false;
    static const char *const hosts[] = {"api.github.com", "raw.githubusercontent.com"};
    for (unsigned i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++)
        if ((size_t)(end - host) == strlen(hosts[i]) && !strncmp(host, hosts[i], (size_t)(end - host)))
            return true;
    return false;
}
