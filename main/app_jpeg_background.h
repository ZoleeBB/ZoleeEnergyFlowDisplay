#pragma once

/*
 * Kísérleti, kapcsolóval kikapcsolható JPEG-háttér (#17).
 *
 * Az ESP32-P4 hardveres JPEG-motorja az átlátszatlan, teljes képernyős
 * témahátteret a lodepng-nél lényegesen gyorsabban bontja ki, és a kész kép
 * RGB565-ben feleannyi PSRAM-ot foglal, mint az ARGB8888 PNG. A sablon
 * mappájában a PNG mellett opcionálisan szerepelhet azonos nevű `.jpg`; a
 * firmware csak akkor használja, ha a `display.jpeg_backgrounds` be van
 * kapcsolva, a kép mérete megegyezik a PNG-ével, és a dekódolás sikerül.
 * Bármelyik feltétel hiányában a megszokott PNG-út fut, ezért a funkció a
 * beállítás visszakapcsolásával teljesen visszavonható.
 *
 * A motort a kaputelefon videója is használja. Saját, rövid életű
 * dekóderpéldányt nyitunk: ha a hardver foglalt, egyszerűen a PNG marad.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *pixels;  /* RGB565 little-endian képpontok (free()-vel szabadítandó). */
    size_t size;      /* A puffer tényleges mérete bájtban. */
    unsigned width;   /* Látható szélesség. */
    unsigned height;  /* Látható magasság. */
    unsigned stride;  /* Sorhossz bájtban; a hardver 16 képpontra tölthet ki. */
} app_jpeg_image_t;

/*
 * JPEG kibontása RGB565-be. Igaz érték mellett az `out` felszabadítandó
 * puffert tartalmaz. Hamis érték minden hibát jelent (nem támogatott
 * platform, foglalt motor, eltérő méret, memóriahiány); ilyenkor a hívó a
 * PNG-úton dolgozik tovább.
 */
bool app_jpeg_background_decode(const uint8_t *data, size_t size, unsigned expected_width,
                                unsigned expected_height, app_jpeg_image_t *out);
void app_jpeg_background_free(app_jpeg_image_t *image);
