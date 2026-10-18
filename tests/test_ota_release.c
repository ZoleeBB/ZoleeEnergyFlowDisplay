/* Valódi release-választó: nincs hálózat és nincs flash-írás a tesztben. */
#include "app_ota_release.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(app_ota_version_newer("v2.10.0", "v2.9.9"));
    assert(app_ota_version_newer("v2.0.0", "v2.0.0-rc.1"));
    assert(app_ota_version_newer("v1.0.0", "0.1.0"));
    const char *bad[] = {"v2.9.9",
                         "v2.9.8",
                         "v2.0.0",
                         "v3.0.0-beta",
                         "v03.0.0",
                         "v3.0",
                         "v3.0.0/evil",
                         "v4294967296.0.0",
                         "",
                         "3.0.0"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        assert(!app_ota_version_newer(bad[i], "v2.9.9"));
    assert(!app_ota_version_newer("v3.0.0", "not-a-version"));
    const char *good_urls[] = {APP_OTA_RELEASE_API,
                               APP_OTA_RELEASE_BASE "v3.0.0/x",
                               "https://release-assets.githubusercontent.com/x?token=abc"};
    for (unsigned i = 0; i < sizeof(good_urls) / sizeof(good_urls[0]); i++)
        assert(app_ota_download_url_allowed(good_urls[i]));
    const char *bad_urls[] = {"http://github.com/x",
                              "https://github.com.evil/x",
                              "https://github.com@evil/x",
                              "https://evil/github.com",
                              "https://github.com:443/x",
                              "//github.com/x"};
    for (unsigned i = 0; i < sizeof(bad_urls) / sizeof(bad_urls[0]); i++)
        assert(!app_ota_download_url_allowed(bad_urls[i]));
    const char *json = "{\"tag_name\":\"v3.0.0\",\"draft\":false,\"prerelease\":false,\"assets\":["
                       "{\"name\":\"ZoleeEnergyFlowDisplay-v3.0.0-ota.bin\",\"state\":\"uploaded\","
                       "\"size\":12345,\"browser_download_url\":\"" APP_OTA_RELEASE_BASE
                       "v3.0.0/ZoleeEnergyFlowDisplay-v3.0.0-ota.bin\"},"
                       "{\"name\":\"SHA256SUMS.txt\",\"state\":\"uploaded\",\"size\":300,\"browser_"
                       "download_url\":\"" APP_OTA_RELEASE_BASE "v3.0.0/SHA256SUMS.txt\"}]}";
    cJSON *root = cJSON_Parse(json);
    app_ota_release_t release;
    assert(app_ota_release_parse(root, "v2.0.0", 8 * 1024 * 1024, &release));
    assert(!strcmp(release.tag, "v3.0.0") && release.size == 12345);
    assert(!app_ota_release_parse(root, "v3.0.0", 8 * 1024 * 1024, &release));
    assert(!app_ota_release_parse(root, "v2.0.0", 10000, &release));
    cJSON_ReplaceItemInObjectCaseSensitive(root, "draft", cJSON_CreateTrue());
    assert(!app_ota_release_parse(root, "v2.0.0", 8 * 1024 * 1024, &release));
    cJSON_ReplaceItemInObjectCaseSensitive(root, "draft", cJSON_CreateFalse());
    cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
    cJSON *image = cJSON_GetArrayItem(assets, 0);
    cJSON_ReplaceItemInObjectCaseSensitive(
        image, "browser_download_url", cJSON_CreateString("https://evil/image.bin"));
    assert(!app_ota_release_parse(root, "v2.0.0", 8 * 1024 * 1024, &release));
    cJSON_Delete(root);
    root = cJSON_Parse(json);
    cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(root, "assets"), 1);
    assert(!app_ota_release_parse(root, "v2.0.0", 8 * 1024 * 1024, &release));
    cJSON_Delete(root);
    uint8_t hash[32];
    const char *sum =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  image.bin\n";
    assert(app_ota_checksum_parse(sum, "image.bin", hash));
    assert(hash[0] == 1 && hash[31] == 0xef);
    assert(!app_ota_checksum_parse(sum, "image.bin.zip", hash));
    assert(!app_ota_checksum_parse("invalid  image.bin\n", "image.bin", hash));
    char duplicate[200];
    snprintf(duplicate, sizeof(duplicate), "%s%s", sum, sum);
    assert(!app_ota_checksum_parse(duplicate, "image.bin", hash));
    puts("PASS: stable release selection, numeric versions, exact board asset, checksums and HTTPS "
         "host allowlist");
    return 0;
}
