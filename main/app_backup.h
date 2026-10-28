#pragma once
/*
 * Beállítások mentése és visszaállítása.
 *
 * Egyetlen JSON-csomag tartalmazza az SD-kártya beállítási fájljait:
 * config.json, loxone.json, loxone-home.json, templates/order.txt és
 * templates/selection.txt. A statisztika, a témák képei és a csengőhangok
 * nem részei a csomagnak (ezek a kiadási ZIP-ekből újratelepíthetők).
 *
 * A letölthető csomagban a config.json jelszavai null értékűek. A Loxone- és
 * a kaputelefon-jelszó eleve az eszköz NVS-kulcsával titkosítva van a fájlban,
 * ezért az csak ugyanazon a kijelzőn használható; más eszközön a Loxone
 * oldalon újra meg kell adni.
 *
 * A modul platformfüggetlen (a hosttesztek közvetlenül futtatják): a hívó
 * adja meg a gyökérkönyvtárat, és ő felel az SD-zárolásért.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_BACKUP_FORMAT "ZoleeEnergyFlowDisplay-backup"
#define APP_BACKUP_VERSION 1
/* A csomag felső korlátja (a loxone-home.json legfeljebb 128 KiB). */
#define APP_BACKUP_MAX (256U * 1024U)
#define APP_BACKUP_ERROR_SIZE 192

/* Csomag összeállítása a root alatti fájlokból (malloc, a hívó szabadítja fel).
 * include_secrets=false esetén a config.json jelszavai null értékűek. */
char *app_backup_export(const char *root, bool include_secrets, const char *firmware,
                        char error[APP_BACKUP_ERROR_SIZE]);

/* Teljes ellenőrzés írás nélkül; a sikeresen ellenőrzött fájlok számát adja. */
esp_err_t app_backup_validate(const char *root, const char *json, size_t length,
                              unsigned *files, char error[APP_BACKUP_ERROR_SIZE]);

/* Előbb minden részt ellenőriz, csak utána ír (fájlonként tmp + bak csere).
 * A config.json null/hiányzó jelszavai a meglévő config.json értékeit kapják. */
esp_err_t app_backup_restore(const char *root, const char *json, size_t length,
                             unsigned *files, char error[APP_BACKUP_ERROR_SIZE]);

/* Belső flash-tartalék két váltakozó slottal. A fejléc és a hasznos adat
 * CRC-je együtt dönti el, melyik slot érvényes; áramszünet csak a félig írt
 * slotot ronthatja el, a másik érintetlen marad. */
#define APP_BACKUP_SLOT_MAGIC 0x314B425AU /* "ZBK1" little-endian */
#define APP_BACKUP_SLOT_HEADER 32U
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t length;
    uint32_t payload_crc;
    int64_t epoch_s;
    uint32_t header_crc;
} app_backup_slot_t;

uint32_t app_backup_crc32(uint32_t crc, const void *data, size_t length);
void app_backup_slot_encode(const app_backup_slot_t *slot, uint8_t out[APP_BACKUP_SLOT_HEADER]);
/* Csak formailag ellenőrzött fejlécet ad vissza (a hasznos adat CRC-jét a hívó nézi). */
bool app_backup_slot_decode(const uint8_t in[APP_BACKUP_SLOT_HEADER], uint32_t capacity,
                            app_backup_slot_t *slot);
/* A két (érvényesnek talált) slot közül a frissebbet választja; -1: nincs. */
int app_backup_slot_pick(const app_backup_slot_t *first, bool first_valid,
                         const app_backup_slot_t *second, bool second_valid);

/* --- Firmware-oldali tárolás (app_backup_flash.c, csak ESP-IDF) --- */
typedef struct
{
    bool available;
    uint32_t sequence, length;
    int64_t epoch_s;
} app_backup_flash_info_t;

esp_err_t app_backup_flash_info(app_backup_flash_info_t *out);
/* A legutóbbi érvényes tartalék (malloc, nullával lezárva). */
esp_err_t app_backup_flash_load(char **json, size_t *length);
/* Csak eltérő tartalomnál ír; *written jelzi, történt-e írás. */
esp_err_t app_backup_flash_save(const char *json, size_t length, bool *written);
/* Háttérbeli szinkron indítása (bootkor és beállításmentés után). */
esp_err_t app_backup_flash_start(void);
void app_backup_flash_request(void);
/* Bootkor, a config.json hiányában: visszaállítás a belső tartalékból. */
esp_err_t app_backup_flash_restore_sd(unsigned *files, char error[APP_BACKUP_ERROR_SIZE]);
