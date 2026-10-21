# ESP32-P4 memória és képfrissítés

A célpanel a Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1, 32 MB PSRAM-mal. A firmware ESP-IDF 5.5.4, LVGL 9.5.0, esp_lvgl_adapter 0.6.4 és ESP-Hosted 1.4.7 verziókra épül. A változtatások nem módosítják a C6 társprocesszor firmware-ét, az SDIO streaming protokollt vagy a flash-partíciókat.

## Kutatási alap és alkalmazott javítások

A [Waveshare hivatalos példatárának](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X) `08_lvgl_demo_v9`, `09_video_lcd_display` és `10_mp4_player` példái PSRAM-ot és elkülönített grafikus/videó adatútvonalakat használnak. A kamera és MP4 példák képpuffert adnak át a kijelzőnek; ez nem ugyanaz a terhelés, mint PNG-dekódolás, méretezés, átlátszó rétegek és sok LVGL objektum újrarajzolása. A kameraminta beállított 50 FPS-e nem a jelen firmware mért sebessége.

Az [Espressif GUI útmutatója](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/advanced-development/lcd-application-note/gui-optimization-summary.html) a dekódolási költség és a képkezelés optimalizálását javasolja. A témaképekből az LVGL saját rasterizálója belépéskor a tényleges megjelenítési méretű, alfa csatornás képet készít. Az animáció közben ez már 1:1 méretű kép; az időjárási variánsok első használatukkor készülnek el és újrahasználhatók. A gyorsítótár maximum 64 variáns / 6 MiB. Hibánál vagy a korlát túllépésekor az eredeti PNG-méretezési út működik. Témaváltáskor először törlődnek a régi objektumok, majd a cache-bejegyzések és a képpufferek; az SD-n tárolt PNG-k megmaradnak. A változatlan képforrás beállítását kihagyjuk, mert az LVGL `set_src` művelete újrarajzolást kérne.

A 96 elemű sablon-geometriai tábla CPU által használt adat, nem DMA-puffer. Az ESP-IDF [külső BSS támogatásával](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/api-guides/external-ram.html) PSRAM-ba kerül. A desktop mérésben mérete 85 248 bájt; a célplatform pontos mérete a firmware linker-térképéből ellenőrizhető. A 32 KiB belső memória-tartalék megmarad, nem növeljük a korábban indulási abortot okozó 64 KiB-ra.

Az ESP-Hosted [#144 hibajegye](https://github.com/espressif/esp-hosted-mcu/issues/144) ugyanezt az `sdio_rx_get_buffer` és TX foglalási hibát tárgyalja: az összes szabad heap mellett a DMA-képes memória fragmentációja is számít. A driver buildbeli másolatában egyetlen, statikus, 64 bájtra igazított 1 KiB belső DMA-ablak fogadja a streaming adatot; a teljes stream és a csomagmásolatok PSRAM-ban maradnak. Így a 3–6 KiB-os és nagyobb RX burstökhöz nem kell új összefüggő belső DMA-blokkot találni. A [CMD53 FIFO protokoll](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/protocols/esp_sdio_slave_protocol.html) szerint a cím az adott részolvasás tényleges adathosszát kódolja; a végső blokk kitöltését nem másoljuk a payloadba. PSRAM-foglalási hibánál a FIFO kiürül, az adatot eldobjuk, a következő stream fogadható. Az RX/TX sorok egyenként 8 eleműek a korábbi 20 helyett; ez csökkenti az egyidejű memóriaterhelést, nagy hálózati burstnél visszanyomást/eldobást okozhat.

A PPA gyorsítás továbbra is tiltott. Az adapter 0.6.4 PPA hang workaroundja és az IDF 5.5.4 driverének ellenőrzése alapján a bekapcsolás nem önálló konfigurációváltás: a korábbi hardveres elakadás és CPU/DMA cache-vonal ütközés miatt külön fizikai validálást igényel. A worker tickje és a watchdog védelme megmarad.

## v3 Master és Home-képek

A 10.1-es Master a meglévő három panelpuffert `TRIPLE_FULL` módban használja,
CPU-forgatással, részleges framebuffer-javítás nélkül. A felhasználó 2026-10-21-én
igazolta a villogás megszűnését és a fogyasztói háttér megjelenését. Ez nem mért
FPS-adat. A 4B nem forgatott kijelzőjének részleges módját nem változtattuk meg.

Home-belépéskor az előző jelenet globális képcache-e is felszabadul. Az összes
aktív kép dekódolása még a kész oldal megjelenítése előtt történik, külön
birtokolt pufferbe. A 1280×800 RGB565 háttér 2 048 000 bájt. Legfeljebb 12
külön színes és 12 szürke 96×96 ARGB ikon, valamint a 167×167 logó együtt a
háttérrel 3 044 292 bájt dekódolt képadatot jelent, plusz a struktúrák és a
kódolt PNG-k. A dekódolás átmeneti memóriája ettől külön számít. A többi oldal
képei nem élnek vele párhuzamosan. Hibánál a teljes előtöltés visszavonódik.

A valódi LVGL-próba 32 biten és RGB565 felületen 100–100 ciklust, összesen
800 Home/fogyasztói oldalt renderel. A háttér képpontjait, a sérült háttér/ikon
visszavonását, állandó widget-/animációszámot és pufferfelszabadítást ellenőrzi;
a CI ASan-ja a memóriahozzáférést és szivárgást is vizsgálja.

## Ellenőrzés és hardveres mérés

A regressziós tesztek a tényleges RX patch kódblokkjait fordítják, majd 3072, 5632, 5633, 7 és 32768 bájtos burstöt, blokkvégi kitöltést, foglalási hibát, FIFO-ürítést és helyreállást vizsgálnak. A raster-cache előtti és utáni 16 teljes render összevetésében 11 kép képpontra azonos, az öt másikban a csatornánkénti eltérés maximum 1–2 színérték (alfa-kerekítés). A valódi LVGL preview mind a 18 témaváltozatot, időjárási variánsokat és ismételt lapozást ellenőrzi. Ezek nem mérnek fizikai panel-FPS-t vagy tartós Wi-Fi terhelést.

A `/diagnostics` és `/api/diagnostics` az összes szabad memória mellett a szabad belső RAM-ot (`free_internal_kib`) és a legnagyobb szabad belső DMA-blokkot (`largest_dma_block_bytes`) is mutatja. Hardveren ezek értékeit érdemes összevetni indulás után és legalább 100 témaváltás, böngészős oldalfrissítések és adatküldés után. Az összes memória magas értéke önmagában nem bizonyítja a DMA-foglalások működését. Animációs FPS és késleltetés csak a készüléken végzett méréssel állítható.

A csomagban tárolt `preview-light.png` és `preview-dark.png` csak a webes
sablonkezelő képe. A runtime előtöltő kihagyja őket, nem foglalnak LVGL
asset-/raster-cache-t. A web a 480×300-as képeket közvetlenül, lusta betöltéssel
és verzióhoz kötött gyorsítótárazással mutatja; régi csomagnál nincs háttérletöltés
vagy második, közelítő rajzoló.

## Futásidejű terhelési adatok (v2.0.0)

A kijelző és a webes diagnosztika magonként jelzi a CPU becsült foglaltságát. Az ESP-timer mikrosekundumos, 64 bites FreeRTOS futásidő-számlálóiból, az idle idő különbségével, legalább két másodperces ablakban számolunk; az első ablak előtt nincs százalék. Ez feladatütemezési foglaltság, nem panel-FPS vagy külön Wi-Fi társprocesszor-terhelés. Nem listázzuk az összes taskot, és nem kérünk stack-vízjelet. A számlálás kis többletmunkát jelent minden taskváltásnál; pontos hardveres összehasonlítás szükséges.

A belső RAM és PSRAM adatai az allocator által kezelt teljes és szabad heapet jelentik; a sáv a foglalt arányt mutatja. A belső RAM-nál a legnagyobb DMA-blokk is megmarad. Az SD telítettségét a háttérben legfeljebb percenként frissítjük; a diagnosztika csak a gyorsítótárból olvas, LVGL-zár alatt nincs FAT-vizsgálat. SD-hibánál a sáv rejtett. A kezdeti FAT-szabadhelyszámítás nagy kártyán időigényes lehet, de nem a megjelenítőtask végzi.

## F12/F13 – visszatérő és indulás utáni navigáció

A villogásmentes `TRIPLE_FULL` mód és a Home elődekódolt képei megmaradnak.
Az F12 33 ms-os animációja és kész frame utáni további 33 ms-os várakozása
eszközön akadozást okozott; ezt az F13 visszavonja. A refresh ismét 15 ms,
az animáció 16 ms periódusú, további frame-végi várakozás nélkül. A nehéz
görbék 50 ms-os kerete és a teljes eltelt idővel számolt mozgás megmarad.
Ez kért ütemezés, nem eszközön mért FPS vagy CPU-gyorsulás. Új puffer nincs.

A touch-adapter IRQ-k között az utolsó koordinátát ismétli. Az LVGL alap
3 px/minta sebességküszöbe minden ilyen mintánál törli a húzás összegét,
ezért a lassú vagy szaggatott mintavételű húzás elvész. A navigációhoz a
sebességküszöb nulla, az 50 px-es elmozdulásküszöb megmarad. A mozdulat
nem követel egyenletes mintasebességet, a kis remegés nem lapoz. Az
inputkötést egyszer végezzük, az animációs tick már nem járja be a callbackeket.

Az új regresszió Otthon-nyitás nélkül mind a tíz témán balra/jobbra/felfelé
húz, 8 px-es lépésekkel és minden lépés között ismételt koordinátával.
A javítás előtti firmware UI-kódja már a beépített téma első húzásán elbukott.
A próba ellenőrzi a 15 ms utáni redraw-t, illetve 90 ms-os flush után az
esedékes rajzolás és inputolvasás folytatódását külön várakozás nélkül.
A valódi X utáni visszatérési és a 100 ciklusos memória-/objektumélettartam-
próba, valamint mindkét színformátum ASan-ellenőrzése megmarad.

Az adapter 0.6.4 pufferre váró flusha FreeRTOS taskértesítést használ.
Ezért a firmware nem hív `lv_refr_now`-t az UI runtime másik taskjából:
a saját LVGL-worker frissít, `LV_EVENT_REFR_READY` jelzi a kész képkockát.
Határidőn túli visszajelzésnél nincs képbetöltés; a következő kör a meglévő
loading oldallal próbálkozik. A kihagyott/helyreálló visszajelzés próbája
nulla idő előtti SD-képolvasást követel. Forrás: [adapter 0.6.4](https://github.com/espressif/esp-iot-solution/tree/5d75f3f0dc499d9ed4b69284a3741187c2b75a70/components/display/tools/esp_lvgl_adapter).

Az F12 eszközös visszajelzése sikertelen volt. Az F13 javulását és a tartós
CPU/FPS/DMA/PSRAM-adatokat újra fizikai panelen kell visszamérni; a host
ellenőrzés ezt nem helyettesíti.
