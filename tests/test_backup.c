/* Valódi beállításcsomag-kód: export, titokkezelés, szigorú ellenőrzés,
 * tranzakciós visszaírás és a flash-slotok fejléce, ideiglenes könyvtárban. */
#include "app_backup.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];

static void write_file(const char *name, const char *text)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

static char *read_text(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    static char buffer[262144];
    size_t n = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[n] = 0;
    return buffer;
}

static char *load_sample(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = calloc(1, (size_t)size + 1);
    assert(text && fread(text, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    return text;
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    snprintf(root, sizeof(root), "%s", argv[1]);
    char templates[512];
    snprintf(templates, sizeof(templates), "%s/templates", root);
    assert(mkdir(templates, 0775) == 0);

    /* Valódi minta-config titkos mezőkkel és Otthon-beállítás. */
    char *config_sample = load_sample(argv[2]), *home_sample = load_sample(argv[3]);
    cJSON *config = cJSON_Parse(config_sample);
    assert(config);
    cJSON *wifi = cJSON_GetObjectItemCaseSensitive(config, "wifi");
    cJSON_ReplaceItemInObjectCaseSensitive(wifi, "ssid", cJSON_CreateString("home"));
    cJSON_ReplaceItemInObjectCaseSensitive(wifi, "password", cJSON_CreateString("wifi-secret"));
    char *config_text = cJSON_PrintUnformatted(config);
    write_file("config.json", config_text);
    write_file("loxone-home.json", home_sample);
    write_file("loxone.json",
               "{\"host\":\"192.168.2.10\",\"port\":80,\"tls\":false,\"ca_pem\":\"\",\"username\":\"display\","
               "\"mappings\":{\"Presence\":{\"uuid\":\"11223344-5566-7788-99aabbccddeeff00\",\"scale\":1,\"offset\":0}},"
               "\"secret\":{\"nonce\":\"00\",\"data\":\"aa\"}}");
    write_file("templates/order.txt", "builtin\n09-solar-journey\n01-live-home\n");
    write_file("templates/selection.txt", "09-solar-journey\n");

    char error[APP_BACKUP_ERROR_SIZE] = {0};
    char *public_bundle = app_backup_export(root, false, "v9.9.9", error);
    assert(public_bundle);
    assert(!strstr(public_bundle, "wifi-secret")); /* Letöltésben nincs nyílt jelszó. */
    assert(strstr(public_bundle, "\"secrets\":false") && strstr(public_bundle, "09-solar-journey"));
    assert(strstr(public_bundle, "\"secret\":{")); /* Az eszközkulccsal titkosított rész marad. */
    char *private_bundle = app_backup_export(root, true, "v9.9.9", error);
    assert(private_bundle && strstr(private_bundle, "wifi-secret"));

    unsigned files = 0;
    assert(app_backup_validate(root, public_bundle, strlen(public_bundle), &files, error) == ESP_OK);
    assert(files == 5);

    /* Visszaállítás: a null jelszó a meglévő eszköz jelszavát tartja meg. */
    write_file("templates/order.txt", "builtin\n");
    assert(app_backup_restore(root, public_bundle, strlen(public_bundle), &files, error) == ESP_OK);
    assert(files == 5);
    assert(strstr(read_text("config.json"), "wifi-secret"));
    assert(!strcmp(read_text("templates/order.txt"), "builtin\n09-solar-journey\n01-live-home\n"));

    /* Hibás rész esetén semmi sem íródik (előbb teljes ellenőrzés). */
    cJSON *bundle = cJSON_Parse(public_bundle);
    cJSON *bundle_files = cJSON_GetObjectItemCaseSensitive(bundle, "files");
    cJSON_ReplaceItemInObjectCaseSensitive(bundle_files, "templates/order.txt",
                                           cJSON_CreateString("builtin\n../evil\n"));
    char *bad = cJSON_PrintUnformatted(bundle);
    write_file("templates/order.txt", "builtin\n");
    assert(app_backup_restore(root, bad, strlen(bad), &files, error) != ESP_OK && files == 0);
    assert(strstr(error, "sablonlista"));
    assert(!strcmp(read_text("templates/order.txt"), "builtin\n"));
    free(bad);

    cJSON_ReplaceItemInObjectCaseSensitive(bundle_files, "templates/order.txt", cJSON_CreateString("builtin\n"));
    cJSON *display = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(bundle_files, "config.json"), "display");
    cJSON_ReplaceItemInObjectCaseSensitive(display, "day_brightness", cJSON_CreateNumber(400));
    bad = cJSON_PrintUnformatted(bundle);
    assert(app_backup_restore(root, bad, strlen(bad), &files, error) != ESP_OK);
    assert(strstr(error, "config.json"));
    free(bad);
    cJSON_ReplaceItemInObjectCaseSensitive(display, "day_brightness", cJSON_CreateNumber(80));

    cJSON_AddStringToObject(bundle_files, "../../etc/passwd", "x");
    bad = cJSON_PrintUnformatted(bundle);
    assert(app_backup_restore(root, bad, strlen(bad), &files, error) != ESP_OK);
    assert(strstr(error, "Ismeretlen"));
    free(bad);
    cJSON_DeleteItemFromObjectCaseSensitive(bundle_files, "../../etc/passwd");

    cJSON_ReplaceItemInObjectCaseSensitive(bundle, "format", cJSON_CreateString("other"));
    bad = cJSON_PrintUnformatted(bundle);
    assert(app_backup_validate(root, bad, strlen(bad), &files, error) != ESP_OK);
    free(bad);
    cJSON_Delete(bundle);
    assert(app_backup_validate(root, "{}", 2, &files, error) != ESP_OK);
    assert(app_backup_validate(root, NULL, 0, &files, error) != ESP_OK);

    /* Részleges csomag: csak a benne lévő fájl íródik. */
    const char *partial = "{\"format\":\"ZoleeEnergyFlowDisplay-backup\",\"version\":1,"
                          "\"files\":{\"templates/selection.txt\":\"01-live-home\\n\"}}";
    assert(app_backup_restore(root, partial, strlen(partial), &files, error) == ESP_OK && files == 1);
    assert(!strcmp(read_text("templates/selection.txt"), "01-live-home\n"));
    assert(strstr(read_text("config.json"), "wifi-secret"));

    /* Flash-slot fejléc: CRC, kapacitás és körbeforduló sorszám. */
    app_backup_slot_t slot = {.magic = APP_BACKUP_SLOT_MAGIC, .version = APP_BACKUP_VERSION,
                              .sequence = 7, .length = 1000, .payload_crc = 0x12345678,
                              .epoch_s = 1800000000};
    uint8_t header[APP_BACKUP_SLOT_HEADER];
    app_backup_slot_encode(&slot, header);
    app_backup_slot_t decoded;
    assert(app_backup_slot_decode(header, 1024 * 1024, &decoded));
    assert(decoded.sequence == 7 && decoded.length == 1000 && decoded.epoch_s == 1800000000);
    assert(!app_backup_slot_decode(header, 1000, &decoded)); /* Nem fér a slotba. */
    header[9] ^= 1;
    assert(!app_backup_slot_decode(header, 1024 * 1024, &decoded));
    app_backup_slot_t newer = slot, older = slot;
    newer.sequence = 1;
    older.sequence = 0xFFFFFFFFU;
    assert(app_backup_slot_pick(&older, true, &newer, true) == 1);
    assert(app_backup_slot_pick(&newer, true, &older, true) == 0);
    assert(app_backup_slot_pick(&newer, false, &older, true) == 1);
    assert(app_backup_slot_pick(&newer, false, &older, false) == -1);
    assert(app_backup_crc32(0, "123456789", 9) == 0xCBF43926U);

    free(public_bundle);
    free(private_bundle);
    free(config_text);
    free(config_sample);
    free(home_sample);
    cJSON_Delete(config);
    puts("PASS: settings backup export hides plaintext passwords, keeps device secrets on restore, strict all-or-nothing validation, partial restore and flash slot headers");
    return 0;
}
