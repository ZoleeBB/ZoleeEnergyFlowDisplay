#pragma once

/*
 * Előre dekódolt sablonképek ("segédfájl") az SD-n.
 *
 * A kijelzőn a témaváltás ideje nagyrészt két helyen keletkezik: a nagy
 * hátterek PNG-dekódolásában (az első képkockában) és a méretezett ikonok
 * felépítésében (LVGL pillanatkép). Mindkettő ugyanazt a kész képpontsort
 * állítja elő minden egyes belépéskor. Telepítéskor egyszer elkészítjük és az
 * SD-re mentjük, utána a firmware dekódolás és méretezés nélkül, közvetlenül
 * használja.
 *
 * A fájl neve `<alap>-<szélesség>x<magasság>.raw`, a PNG mellett. Tartalma egy
 * 32 bájtos fejléc és a képpontok: teljesen átlátszatlan képnél RGB565,
 * egyébként RGB565A8 (LVGL elrendezés: színsík, utána az alfa sík). A fejléc
 * tartalmazza a forrás PNG méretét és a képpontok CRC32-jét: a csomag
 * cseréjekor a régi segédfájl érvénytelen, sérült kártyaadatnál pedig ugyanúgy
 * a Rendszer nézetre esünk vissza, mint a PNG chunk-CRC hibájánál.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_TEMPLATE_RAW_MAGIC 0x3157525AU /* "ZRW1" */
#define APP_TEMPLATE_RAW_HEADER 32U
#define APP_TEMPLATE_RAW_RGB565 1U
#define APP_TEMPLATE_RAW_RGB565A8 2U
/* Egy segédfájl felső korlátja: 1280x800 RGB565A8 = 3 MiB. */
#define APP_TEMPLATE_RAW_MAX (3U * 1024U * 1024U + APP_TEMPLATE_RAW_HEADER)

typedef struct
{
    unsigned format; /* APP_TEMPLATE_RAW_RGB565 vagy RGB565A8. */
    unsigned width, height;
    unsigned stride;          /* A színsík sorhossza bájtban (2 * szélesség). */
    const uint8_t *pixels;    /* A fejléc utáni képpontok. */
    size_t pixels_size;       /* A képpontok mérete bájtban. */
    uint32_t source_size;     /* A forrás PNG mérete. */
    uint32_t data_crc;        /* A képpontok CRC32-je. */
} app_template_raw_t;

/* Segédfájl neve a PNG nevéből és a megjelenítési méretből. */
bool app_template_raw_name(const char *png_name, unsigned width, unsigned height,
                           char *out, size_t out_size);
/* Igaz, ha a fájlnév segédfájl-névnek látszik (`<alap>-<w>x<h>.raw`). */
bool app_template_raw_is_name(const char *name, char base[66]);

/*
 * PNG kibontása és szükség esetén méretezése a megadott megjelenítési méretre,
 * majd a kész segédfájl összeállítása. A visszaadott puffert a hívó szabadítja
 * fel. Csak 8 bites, nem interlace RGB/RGBA PNG-t fogad el.
 */
bool app_template_raw_build(const unsigned char *png, size_t png_size, unsigned width,
                            unsigned height, uint8_t **out, size_t *out_size);

/* Beolvasott segédfájl ellenőrzése: fejléc, méretek, CRC és hossz. */
bool app_template_raw_parse(const uint8_t *data, size_t size, unsigned width, unsigned height,
                            uint32_t source_size, app_template_raw_t *out);
