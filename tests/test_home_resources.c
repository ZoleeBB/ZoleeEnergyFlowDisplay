/* Valódi SD-csomagkezelő: v4 keretek, visszafelé kompatibilitás és csak látható képek. */
#include "app_templates.h"
#include "app_template_zip.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const app_template_asset_t *asset(const app_template_view_t *view, const char *name)
{
    for (unsigned i = 0; i < view->asset_count; i++)
        if (!strcmp(view->assets[i].name, name))
            return &view->assets[i];
    return NULL;
}

static cJSON *cases_read(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size > 0 && size < 65536 && !fseek(file, 0, SEEK_SET));
    char *body = calloc(1, (size_t)size + 1);
    assert(body && fread(body, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    cJSON *result = cJSON_Parse(body);
    free(body);
    assert(cJSON_IsArray(result));
    return result;
}

static void old_pack_load(unsigned version)
{
    const char *icons[] = {"thermostat-irc", "blinds-shading", "gate-double", "dimmer", "audio-player", "washing-machine"};
    app_template_view_t *view = calloc(1, sizeof(*view));
    assert(view);
    assert(app_templates_home_load_page(icons, 6, false, true, false, true, view));
    assert(!strcmp(view->assets[0].name, version == 1 ? "background-light.png" : "consumers-light.png"));
    assert(asset(view, "icon-washing-machine.png"));
    const app_template_asset_t *blind = asset(view, "icon-blinds-shading.png");
    assert(blind && blind->bytes && blind->width == 96 && blind->height == 96);
    /* Az IRC régi, eltérő geometriájú általános termosztátképre sem eshet vissza. */
    assert(!asset(view, "icon-thermostat-irc.png") && !asset(view, "icon-thermostat.png"));
    assert(!asset(view, "icon-gate-double.png") && !asset(view, "icon-dimmer.png"));
    assert(!asset(view, "icon-audio-player.png") && !asset(view, "audio-player-day.png"));
    assert(view->asset_count == 4);
    app_templates_view_free(view);
    assert(app_templates_home_load_page(icons, 6, true, false, true, true, view));
    assert(view->asset_count == 4 && !view->assets[0].bytes && !view->assets[1].bytes);
    assert(asset(view, "icon-blinds-shading.png") && !asset(view, "audio-player-night.png"));
    app_templates_view_free(view);
    free(view);
}

static void declared_file_failure(const char *root)
{
    char original[512], held[512];
    snprintf(original, sizeof(original), "%s/loxone-home/icon-washing-machine.png", root);
    snprintf(held, sizeof(held), "%s/loxone-home/icon-washing-machine.held", root);
    app_template_view_t *view = calloc(1, sizeof(*view));
    assert(view);
    const char *icons[] = {"washing-machine"};
    assert(!rename(original, held));
    assert(!app_templates_home_load_icons(icons, 1, view) && view->asset_count == 0);
    assert(!rename(held, original));
    /* A futásidejű strukturális hiba nem válhat hiányzó opcionális ikonná.
     * A chunk-CRC-t ezen az úton az azonnal utána futó LVGL-dekóder ellenőrzi. */
    FILE *file = fopen(original, "r+b");
    assert(file);
    int first = fgetc(file);
    assert(first != EOF && !fseek(file, 0, SEEK_SET) && fputc(0, file) != EOF && !fclose(file));
    assert(!app_templates_home_load_icons(icons, 1, view) && view->asset_count == 0);
    file = fopen(original, "r+b");
    assert(file && fputc(first, file) != EOF && !fclose(file));
    assert(app_templates_home_load_icons(icons, 1, view));
    app_templates_view_free(view);
    free(view);
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    setenv("ZOLEE_TEMPLATE_ROOT", argv[2], 1);
    assert(app_templates_init() == ESP_OK);
    unsigned generation = 0;
    assert(!app_templates_home_info(NULL, &generation));
    char error[160];
    assert(app_templates_install(argv[1], error) == ESP_OK);
    app_template_info_t info;
    assert(app_templates_home_info(&info, &generation) && info.version == 4);
    assert(!strcmp(info.id, "loxone-home"));
    assert(app_templates_list(NULL, APP_TEMPLATE_MAX, NULL) == 1);
    assert(app_templates_select("loxone-home") != ESP_OK);
    app_template_view_t *view = calloc(1, sizeof(*view));
    assert(view);
    const char *icons[] = {"washing-machine", "refrigerator", "washing-machine"};
    assert(app_templates_home_load(icons, 3, false, view));
    assert(view->asset_count == 4 && view->generation == generation);
    assert(view->assets[0].width == 1280 && view->assets[0].height == 800);
    assert(!strcmp(view->assets[2].name, "icon-washing-machine.png"));
    app_templates_view_free(view);
    assert(app_templates_home_load(icons, 3, true, view));
    assert(!strcmp(view->assets[0].name, "background-dark.png"));
    app_templates_view_free(view);
    assert(app_templates_home_load_for(icons, 3, false, true, view));
    assert(!strcmp(view->assets[0].name, "consumers-light.png") && view->asset_count == 4);
    app_templates_view_free(view);
    assert(app_templates_home_load_for(icons, 3, true, true, view));
    assert(!strcmp(view->assets[0].name, "consumers-dark.png"));
    app_templates_view_free(view);
    /* Kártyalapozás: a korábbi háttér/logó marad, csak a kért ikonok és skin jönnek. */
    assert(app_templates_home_load_icons(icons, 3, view));
    assert(view->asset_count == 4 && view->generation == generation);
    assert(!view->assets[0].bytes && !view->assets[1].bytes && !view->assets[0].name[0]);
    assert(!strcmp(view->assets[2].name, "icon-washing-machine.png") && view->assets[2].width == 96);
    assert(!strcmp(view->assets[3].name, "icon-refrigerator.png"));
    app_templates_view_free(view);
    assert(app_templates_home_load_icons(NULL, 0, view) && view->asset_count == 2);
    app_templates_view_free(view);
    assert(app_templates_home_load_page(icons, 3, false, false, false, true, view));
    const app_template_asset_t *skin = asset(view, "audio-player-day.png");
    assert(skin && skin->width == 292 && skin->height == 200 && skin->bytes);
    assert(!asset(view, "audio-player-night.png") && view->asset_count == 5);
    app_templates_view_free(view);
    assert(app_templates_home_load_page(icons, 3, true, false, true, true, view));
    skin = asset(view, "audio-player-night.png");
    assert(skin && skin->width == 292 && skin->height == 200 && skin->bytes);
    assert(!view->assets[0].bytes && !view->assets[1].bytes && view->asset_count == 5);
    app_templates_view_free(view);
    assert(app_templates_home_load_page(icons, 3, false, false, true, false, view));
    assert(!asset(view, "audio-player-day.png") && view->asset_count == 4);
    app_templates_view_free(view);
    const char *bad[] = {"../unsafe"};
    assert(!app_templates_home_load_icons(bad, 1, view) && !view->asset_count);
    assert(!app_templates_home_load(bad, 1, false, view) && !view->asset_count);
    declared_file_failure(argv[2]);

    cJSON *cases = cases_read(argv[3]);
    const cJSON *test;
    unsigned index = 0;
    cJSON_ArrayForEach(test, cases)
    {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(test, "name"));
        const char *path = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(test, "path"));
        const char *operation = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(test, "operation"));
        bool expected = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(test, "valid"));
        assert(name && path && operation);
        bool actual;
        if (!strcmp(operation, "unzip"))
        {
            char stage[512];
            snprintf(stage, sizeof(stage), "%s/unzip-%u", argv[2], index);
            assert(!mkdir(stage, 0755));
            actual = app_template_unzip(path, stage, error);
        }
        else
        {
            app_template_info_t before;
            unsigned before_generation;
            assert(app_templates_home_info(&before, &before_generation));
            cJSON *report = NULL;
            actual = app_templates_probe(path, &report, error) == ESP_OK;
            if (actual != expected)
            {
                fprintf(stderr, "FAIL %s: expected=%d actual=%d; %s\n", name, expected, actual, error);
                cJSON_Delete(report);
                abort();
            }
            if (actual)
            {
                assert(report);
                assert(cJSON_GetObjectItemCaseSensitive(report, "encoded_bytes")->valuedouble ==
                       cJSON_GetObjectItemCaseSensitive(test, "encoded")->valuedouble);
                assert(cJSON_GetObjectItemCaseSensitive(report, "decoded_image_bytes")->valuedouble ==
                       cJSON_GetObjectItemCaseSensitive(test, "decoded")->valuedouble);
            }
            cJSON_Delete(report);
            if (!strcmp(operation, "install"))
            {
                bool installed = app_templates_install(path, error) == ESP_OK;
                assert(installed == actual);
                if (installed)
                {
                    unsigned expected_version = (unsigned)cJSON_GetObjectItemCaseSensitive(test, "version")->valueint;
                    assert(app_templates_home_info(&info, NULL) && info.version == expected_version);
                    if (expected_version < 4)
                        old_pack_load(expected_version);
                }
            }
            if (strcmp(operation, "install") || !actual)
            {
                unsigned unchanged;
                assert(app_templates_home_info(&info, &unchanged));
                assert(unchanged == before_generation && info.version == before.version);
            }
        }
        if (actual != expected)
        {
            fprintf(stderr, "FAIL %s: expected=%d actual=%d; %s\n", name, expected, actual, error);
            abort();
        }
        index++;
    }
    cJSON_Delete(cases);

    /* Webes frissítés közben a már előtöltött nézet memóriája változatlanul a UI-é. */
    assert(app_templates_install(argv[1], error) == ESP_OK);
    assert(app_templates_home_info(NULL, &generation));
    assert(app_templates_home_load_page(icons, 3, false, false, false, true, view));
    size_t size = view->assets[2].size;
    unsigned char first = view->assets[2].bytes[0];
    assert(app_templates_install(argv[1], error) == ESP_OK);
    unsigned next;
    assert(app_templates_home_info(NULL, &next) && next > generation);
    assert(view->assets[2].size == size && view->assets[2].bytes[0] == first);
    assert(asset(view, "audio-player-day.png"));
    app_templates_view_free(view);
    free(view);
    assert(app_templates_delete("loxone-home", error) == ESP_OK && !app_templates_home_info(NULL, NULL));
    assert(app_templates_list(NULL, APP_TEMPLATE_MAX, NULL) == 1);
    printf("PASS: actual v4 home resource installer, %u boundary/corruption/legacy cases, active skin ownership, old-pack fallback and declared-file failures\n", index);
}
