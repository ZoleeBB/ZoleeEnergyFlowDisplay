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

## F12 – teljes képkockák ütemezése és visszatérő navigáció

A villogásmentes `TRIPLE_FULL` mód megmarad. A normál refresh és animáció
33 ms-os (kb. 30 FPS) keretet kap a korábbi 15/16 ms helyett; ez a kért
képkockák számát korlátozza, nem állít mért CPU-gyorsulást vagy elért FPS-t.
A nehéz görbék továbbra is 50 ms-os keretűek, a mozgás a teljes eltelt idővel
lép. A touch olvasási időzítője változatlan. Nem foglalunk új teljes panelpuffert.
A betöltőkép egyszeri, visszaigazolt flusha megelőzi az SD-/PNG-munkát.

A visszatérési regresszió valódi X-érintést, alsó szélről induló diagnosztikát,
mindkét témalapozási irányt és nem buborékoló fedőelemet próbál. Az inputkötés
nem sokszorozódhat. A 100 ciklusos memória/objektumélettartam-próba és a két
színformátumú ASan-próba megmarad. A gazdaoldali eredmény nem helyettesíti
a panelen mért inputkésést, CPU/FPS/DMA/PSRAM-idősorokat.

Az adapter 0.6.4 pontos forrásának ellenőrzése alapján a pufferre váró flush
FreeRTOS taskértesítést használ. Ezért a firmware nem hív `lv_refr_now`-t az
UI runtime másik taskjából: a saját LVGL-worker frissít, `LV_EVENT_REFR_READY`
jelzi a kész képkockát. Határidőn túli visszajelzésnél nincs képbetöltés, a
következő kör a meglévő loading oldallal próbálja újra. A headless próba a
refresh-visszajelzés kihagyását és helyreállását is injektálja, nulla idő előtti
SD-képolvasást megkövetelve. Forrás: [adapter 0.6.4](https://github.com/espressif/esp-iot-solution/tree/5d75f3f0dc499d9ed4b69284a3741187c2b75a70/components/display/tools/esp_lvgl_adapter).
