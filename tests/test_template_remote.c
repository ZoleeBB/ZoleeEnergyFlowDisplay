/* GitHub-témafrissítés tiszta szabályai a valódi sablonvalidátor-függvényekkel. */
#include "app_template_remote.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 2);
    char tag[32];
    assert(app_template_remote_tag("v3.2.0", "v3.3.0", tag) && !strcmp(tag, "v3.2.0"));
    /* Fejlesztői/CI build: a legutóbbi stabil kiadás. */
    assert(app_template_remote_tag("v3.2.0-ci.5", "v3.1.9", tag) && !strcmp(tag, "v3.1.9"));
    assert(app_template_remote_tag("0.1.0-dev", "v4.0.0", tag) && !strcmp(tag, "v4.0.0"));
    assert(!app_template_remote_tag("dev", "v4.0.0-rc1", tag));
    assert(!app_template_remote_tag(NULL, NULL, tag));

    cJSON *listing = cJSON_Parse("[{\"type\":\"dir\",\"name\":\"01-live-home\"},{\"type\":\"file\",\"name\":\"README.md\"},"
                                 "{\"type\":\"dir\",\"name\":\"../evil\"},{\"type\":\"dir\",\"name\":\"10-battery-diagnostics\"}]");
    char ids[4][33];
    assert(app_template_remote_ids(listing, ids, 4) == 2);
    assert(!strcmp(ids[0], "01-live-home") && !strcmp(ids[1], "10-battery-diagnostics"));
    assert(app_template_remote_ids(listing, ids, 1) == 1);
    cJSON_Delete(listing);

    /* A valódi gyári template.json minden hivatkozott képe és a két előnézet. */
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = calloc(1, (size_t)size + 1);
    assert(text && fread(text, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    cJSON *document = cJSON_Parse(text);
    free(text);
    char names[APP_TEMPLATE_REMOTE_MAX_FILES][65];
    unsigned count = 0;
    assert(app_template_remote_files(document, names, APP_TEMPLATE_REMOTE_MAX_FILES, &count));
    assert(count == 4); /* két háttér + két előnézet, ismétlés nélkül */
    bool light = false, preview = false;
    for (unsigned i = 0; i < count; i++)
    {
        light |= !strcmp(names[i], "background-day.png");
        preview |= !strcmp(names[i], "preview-dark.png");
    }
    assert(light && preview);
    assert(!app_template_remote_files(document, names, 2, &count)); /* Kapacitáskorlát. */
    cJSON *elements = cJSON_GetObjectItem(cJSON_GetObjectItem(document, "light"), "elements");
    cJSON *bad = cJSON_CreateObject();
    cJSON_AddStringToObject(bad, "type", "image");
    cJSON_AddStringToObject(bad, "file", "../../config.json");
    cJSON_AddItemToArray(elements, bad);
    assert(!app_template_remote_files(document, names, APP_TEMPLATE_REMOTE_MAX_FILES, &count));
    cJSON_Delete(document);

    char url[256];
    assert(app_template_remote_url(url, sizeof(url), "v3.2.0", "10-battery-diagnostics", "template.json"));
    assert(!strcmp(url, "https://raw.githubusercontent.com/ZoleeBB/ZoleeEnergyFlowDisplay/v3.2.0/templates/10-battery-diagnostics/template.json"));
    assert(app_template_remote_url(url, sizeof(url), "v3.2.0", "01-live-home", "day.png"));
    assert(!app_template_remote_url(url, sizeof(url), "main", "01-live-home", "day.png"));
    assert(!app_template_remote_url(url, sizeof(url), "v3.2.0", "../x", "day.png"));
    assert(!app_template_remote_url(url, sizeof(url), "v3.2.0", "01-live-home", "../config.json"));
    assert(!app_template_remote_url(url, 40, "v3.2.0", "01-live-home", "day.png"));
    assert(app_template_remote_url_allowed("https://raw.githubusercontent.com/a"));
    assert(app_template_remote_url_allowed("https://api.github.com/repos"));
    assert(!app_template_remote_url_allowed("http://raw.githubusercontent.com/a"));
    assert(!app_template_remote_url_allowed("https://raw.githubusercontent.com.evil.test/a"));
    assert(!app_template_remote_url_allowed("https://example.com/"));
    puts("PASS: GitHub template update tag choice, safe directory listing, referenced files, raw URLs and host allowlist");
    return 0;
}
