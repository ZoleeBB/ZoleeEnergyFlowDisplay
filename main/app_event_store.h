#pragma once

/*
 * A RAM eseménynapló kötegelt mentése az SD-kártyára.
 *
 * Cél, hogy újraindítás után is látszódjon, mi történt előtte: a webes felület
 * ma csak a RAM körkörös pufferét mutatja, az pedig minden indulásnál üres.
 *
 * A mentés sosem akadhat össze a kijelzővel vagy a webes feltöltéssel:
 * - a bejegyzés rögzítése (app_events_add) változatlanul csak RAM-ot ír,
 *   SD-műveletet nem végez, tehát bármelyik feladatból hívható;
 * - a kiírás kötegelt, és kizárólag a háttér-karbantartó feladatból fut;
 * - a tárzárat csak nem blokkoló módon kéri: ha a kártyán más dolgozik
 *   (témabetöltés, telepítés, statisztikamentés), a kiírás kimarad, és a
 *   következő körben újrapróbálja. Az események addig a RAM-ban várnak.
 *
 * A fájl naponta fordul: az aktuális nap mellett az előző nap naplója marad
 * meg, a régebbi törlődik. A napi fájl mérete is korlátos, így hibavihar sem
 * tudja teleírni a kártyát.
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* A naplófájlok helye; a tesztek felülírhatják. */
#ifndef APP_EVENT_STORE_DIR
#define APP_EVENT_STORE_DIR "/sdcard/logs"
#endif

/* Legfeljebb ekkora egy napi fájl; a határ átlépésekor nap közben is fordul. */
#ifndef APP_EVENT_STORE_MAX_BYTES
#define APP_EVENT_STORE_MAX_BYTES (512U * 1024U)
#endif

/* Két kiírás között legalább ennyi idő telik el, ha nincs sok új esemény. */
#define APP_EVENT_STORE_INTERVAL_US (30LL * 1000 * 1000)
/* Ennyi új eseménynél nem várunk az időzítésre. */
#define APP_EVENT_STORE_BATCH 8U

/*
 * Egy karbantartó lépés. `now_us` monoton óra (esp_timer), `wall` a fali idő.
 * Nem blokkol: foglalt kártyánál azonnal visszatér. Igaz, ha írt is.
 */
bool app_event_store_step(int64_t now_us, time_t wall);
