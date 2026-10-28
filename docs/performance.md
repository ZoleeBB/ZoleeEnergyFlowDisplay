# ESP32-P4 memória és képfrissítés

A célpanel a Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1, 32 MB PSRAM-mal. A firmware ESP-IDF 5.5.4, LVGL 9.5.0, esp_lvgl_adapter 0.6.4 és ESP-Hosted 1.4.7 verziókra épül. A változtatások nem módosítják a C6 társprocesszor firmware-ét, az SDIO streaming protokollt vagy a flash-partíciókat.

## Kutatási alap és alkalmazott javítások

A [Waveshare hivatalos példatárának](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X) `08_lvgl_demo_v9`, `09_video_lcd_display` és `10_mp4_player` példái PSRAM-ot és elkülönített grafikus/videó adatútvonalakat használnak. A kamera és MP4 példák képpuffert adnak át a kijelzőnek; ez nem ugyanaz a terhelés, mint PNG-dekódolás, méretezés, átlátszó rétegek és sok LVGL objektum újrarajzolása. A kameraminta beállított 50 FPS-e nem a jelen firmware mért sebessége.

Az [Espressif GUI útmutatója](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/advanced-development/lcd-application-note/gui-optimization-summary.html) a dekódolási költség és a képkezelés optimalizálását javasolja. A témaképekből az LVGL saját rasterizálója belépéskor a tényleges megjelenítési méretű, alfa csatornás képet készít. Az animáció közben ez már 1:1 méretű kép; az időjárási variánsok első használatukkor készülnek el és újrahasználhatók. A gyorsítótár maximum 64 variáns / 6 MiB. Hibánál vagy a korlát túllépésekor az eredeti PNG-méretezési út működik. Témaváltáskor először törlődnek a régi objektumok, majd a cache-bejegyzések és a képpufferek; az SD-n tárolt PNG-k megmaradnak. A változatlan képforrás beállítását kihagyjuk, mert az LVGL `set_src` művelete újrarajzolást kérne.

A beépített témák háttere natív 1280×800 px. Az Energiautak, Műszerfal és Energiafőkönyv téma korábban 1024×640-es háttérrel érkezett, ezért belépéskor teljes képernyős `lv_snapshot` méretezés futott: a desktop mérésben (RGB565, `ZOLEE_THEME_SWITCH_BENCH=1`) ez 43–75 ms építési időt jelentett a többi téma ~4 ms-ával szemben. A natív képekkel a teljes témaváltás 73→51 ms (Energiautak), 92→41 ms (Műszerfal) és 50→49 ms (Energiafőkönyv); a kép a felső 797 sorban képpontra azonos a korábbival, csak az alsó két sor szélmintavételezése tér el. A natív háttereket a `ZOLEE_EXPORT_SCALED_BACKGROUNDS=<mappa>` preview-mód és a `tools/prepare_native_backgrounds.py` állítja elő a futásidejű raszterből. Saját témához is natív méretű hátteret ajánlunk.

### Előre dekódolt kép-segédfájlok

A panelen mért fázisidők szerint a témaváltás két nagy tétele a felépítés
(a méretezett ikonokhoz készülő LVGL-pillanatképek) és az első képkocka
(a háttér PNG-dekódolása): a Nap pálya témánál 7,7 s + 2,4 s. Mindkettő
ugyanazt a képpontsort állítja elő minden belépéskor, ezért egyszer, a
telepítéskor készítjük el, és az SD-re mentjük a PNG mellé.

- A jelenetek minden kép-változatához (fájl + megjelenítési méret) tartozik egy
  `<alap>-<szélesség>x<magasság>.raw` fájl: 32 bájtos fejléc és a képpontok.
  Átlátszatlan képnél RGB565, egyébként RGB565A8 (LVGL elrendezés: színsík,
  utána az alfa sík). Részletek: `main/app_template_raw.h`.
- A fejléc a forrás PNG méretét és a képpontok CRC32-jét őrzi: csomagcsere után
  a régi segédfájl érvénytelen, sérült kártyaadatnál pedig ugyanúgy a Rendszer
  nézetre esünk vissza, mint a PNG chunk-CRC hibájánál.
- Ha egy képhez minden használt mérethez van érvényes segédfájl, a PNG-t be sem
  olvassuk: a témaváltásból kimarad a dekódolás és a méretezés is, a dekódolt
  háttér pedig 4 MiB helyett 2–3 MiB PSRAM-ot foglal.
- A segédfájlokat külön, alacsony prioritású háttérfeladat készíti el:
  hívásonként egy fájl, a katalógus- és SD-zárat csak addig tartva, lapozás és
  telepítés után néhány másodperc szünettel. Témabetöltés közben nem fut, mert
  ott a dekódolás és a 2–3 MiB-os kiírás témánként fél percre elvenné az
  SD-kártyát a webes feltöltéstől. Sikertelen írásnál minden a megszokott
  PNG-úton fut tovább, a következő körben újra próbálkozik.
- A segédfájlok nem kerülnek a sablon ZIP-be, az ujjlenyomatba és az előnézeti
  képekbe; a csomag törlésekor a könyvtárral együtt eltűnnek.
- Az ikonok átméretezése a telepítéskori bilineáris szűrővel készül, ezért az
  ikonok képpontjai néhány egységgel eltérhetnek a korábbi LVGL-skálázástól; a
  natív méretű háttér képpontra ugyanaz marad. A preview mindkét utat
  lefuttatja és összeveti (`ZOLEE_DISABLE_RAW=1` kapcsolja ki az asztali
  összehasonlításhoz).

### Témaváltás fázisidői a valódi panelen

Az asztali mérés (`ZOLEE_THEME_SWITCH_BENCH=1`) a gyors SSD és a desktop CPU
miatt nem mutatja meg a tényleges SD- és dekódolási időt, ezért minden
témaváltás végén a kijelző egyetlen összegző sort ír a soros naplóba és a webes
eseménynaplóba (Kezdőlap → eseménynapló):

```
Témaváltás 09-solar-journey: kép 95, SD 420, csere 12, felépítés 60, 1. képkocka 380, összesen 967 ms
```

- **kép**: a betöltőképernyő megjelenítése, benne a kötelező 85 ms várakozás.
- **SD**: a csomag beolvasása és ellenőrzése (`app_templates_load_pending`).
- **csere**: a régi téma objektumainak és képeinek felszabadítása.
- **felépítés**: az LVGL objektumfa összeállítása (`template_build`).
- **1. képkocka**: a felépítés vége és az első kész frissítés között eltelt idő;
  **ebben fut a háttér PNG-dekódolása**, ezért tipikusan ez a legnagyobb tétel.
- **összesen**: a lapozástól az első kész képkockáig.

A mérés néhány időbélyeg és váltásonként egy naplósor; a 200 ms-os frissítési
körben nincs többletmunka. A további gyorsítás (például előre dekódolt RGB565
háttér az SD-n) csak ezekkel a számokkal együtt érdemes.

A témaváltás betöltőképernyője mögött a régi téma egyetlen objektuma sem látszik,
ezért a dekódolt képeit és a raszter-gyorsítótárat már a betöltőképocka után
elengedjük. Enélkül az SD-olvasás és az új háttér dekódolása a régi téma 4–10 MiB-os
dekódolt képei mellé kért PSRAM-ot: gyors oda-vissza lapozásnál elfogyhatott a hely,
a háttér dekódolása minden képkockán újra elbukott, és a kijelző használhatatlanul
lelassult. Ha a betöltőképernyő mögött hat másodpercig semmi nem halad (foglalt SD,
elmaradó képkocka-visszajelzés), a felépítés eldobja a félkész állapotot és tiszta
lappal újrakezd; enélkül a kijelző a pörgő körön maradt, ahol a húzásos navigáció
sem él.

Nappali/éjszakai váltáskor az előző változat dekódolt képei (háttérképenként 1280×800 ARGB8888 = 4 MiB) elfoglalnák az új háttér dekódolásához szükséges PSRAM-ot, és sikertelen dekódolásnál a háttér elmaradt. A felépítés ezért a törölt objektumok után, az új jelenet előtt üríti a kép- és raszter-gyorsítótárat; az SD-ről beolvasott PNG-bájtok megmaradnak, nincs újraolvasás. A preview minden témánál ellenőrzi, hogy a váltás után a jelenet képei a saját változatukra mutatnak, és a teljes képernyős háttér valóban dekódolt képpontokat ad.

A 96 elemű sablon-geometriai tábla CPU által használt adat, nem DMA-puffer. Az ESP-IDF [külső BSS támogatásával](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/api-guides/external-ram.html) PSRAM-ba kerül. A desktop mérésben mérete 234 240 bájt; a célplatform pontos mérete a firmware linker-térképéből ellenőrizhető. A 32 KiB belső memória-tartalék megmarad, nem növeljük a korábban indulási abortot okozó 64 KiB-ra.

Az ESP-Hosted [#144 hibajegye](https://github.com/espressif/esp-hosted-mcu/issues/144) ugyanezt az `sdio_rx_get_buffer` és TX foglalási hibát tárgyalja: az összes szabad heap mellett a DMA-képes memória fragmentációja is számít. A driver buildbeli másolatában egyetlen, statikus, 64 bájtra igazított 1 KiB belső DMA-ablak fogadja a streaming adatot; a teljes stream és a csomagmásolatok PSRAM-ban maradnak. Így a 3–6 KiB-os és nagyobb RX burstökhöz nem kell új összefüggő belső DMA-blokkot találni. A [CMD53 FIFO protokoll](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/protocols/esp_sdio_slave_protocol.html) szerint a cím az adott részolvasás tényleges adathosszát kódolja; a végső blokk kitöltését nem másoljuk a payloadba. PSRAM-foglalási hibánál a FIFO kiürül, az adatot eldobjuk, a következő stream fogadható. Az RX/TX sorok egyenként 8 eleműek a korábbi 20 helyett; ez csökkenti az egyidejű memóriaterhelést, nagy hálózati burstnél visszanyomást/eldobást okozhat.

A PPA gyorsítás továbbra is tiltott. Az adapter 0.6.4 PPA hang workaroundja és az IDF 5.5.4 driverének ellenőrzése alapján a bekapcsolás nem önálló konfigurációváltás: a korábbi hardveres elakadás és CPU/DMA cache-vonal ütközés miatt külön fizikai validálást igényel. A worker tickje és a watchdog védelme megmarad.

## v3 Master és Home-képek

A 10.1-es Master az F14-től újra `TRIPLE_PARTIAL` módban dolgozik, a panel
meglévő három pufferével és egy 80 000 bájtos PSRAM-részpufferrel. Az összes
részlet után kész képet ad át, CPU-forgatással és CPU-cache-visszaírással.
Az F11 teljes újrarajzolása megszüntette a villogást, de az F12/F13 után a
felhasználó erős akadozást és ritka érintésfelismerést jelzett. A 4B nem
forgatott kijelzőjének részleges útja változatlan.

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

A `/diagnostics` és `/api/diagnostics` az összes szabad memória mellett a szabad belső RAM-ot (`free_internal_kib`), a bekapcsolás óta mért legkisebb szabad belső RAM-ot (`min_free_internal_kib`) és a legnagyobb szabad belső DMA-blokkot (`largest_dma_block_bytes`) is mutatja. A legkisebb érték azért kell, mert egy pillanatnyi mélypont (például egy TLS-kapcsolat felépítése) a 3 másodpercenkénti pillanatfelvételből nem látszik. Hardveren ezek értékeit érdemes összevetni indulás után és legalább 100 témaváltás, böngészős oldalfrissítések és adatküldés után. Az összes memória magas értéke önmagában nem bizonyítja a DMA-foglalások működését. Animációs FPS és késleltetés csak a készüléken végzett méréssel állítható.

A csomagban tárolt `preview-light.png` és `preview-dark.png` csak a webes
sablonkezelő képe. A runtime előtöltő kihagyja őket, nem foglalnak LVGL
asset-/raster-cache-t. A web a 480×300-as képeket közvetlenül, lusta betöltéssel
és verzióhoz kötött gyorsítótárazással mutatja; régi csomagnál nincs háttérletöltés
vagy második, közelítő rajzoló.

## Futásidejű terhelési adatok (v2.0.0)

A kijelző és a webes diagnosztika magonként jelzi a CPU becsült foglaltságát. Az ESP-timer mikrosekundumos, 64 bites FreeRTOS futásidő-számlálóiból, az idle idő különbségével, legalább két másodperces ablakban számolunk; az első ablak előtt nincs százalék. Ez feladatütemezési foglaltság, nem panel-FPS vagy külön Wi-Fi társprocesszor-terhelés. Nem listázzuk az összes taskot, és nem kérünk stack-vízjelet. A számlálás kis többletmunkát jelent minden taskváltásnál; pontos hardveres összehasonlítás szükséges.

A belső RAM és PSRAM adatai az allocator által kezelt teljes és szabad heapet jelentik; a sáv a foglalt arányt mutatja. A belső RAM-nál a legnagyobb DMA-blokk is megmarad. Az SD telítettségét a háttérben legfeljebb percenként frissítjük; a diagnosztika csak a gyorsítótárból olvas, LVGL-zár alatt nincs FAT-vizsgálat. SD-hibánál a sáv rejtett. A kezdeti FAT-szabadhelyszámítás nagy kártyán időigényes lehet, de nem a megjelenítőtask végzi.

## F12/F13 – visszatérő és indulás utáni navigáció

Az F12/F13 a `TRIPLE_FULL` módot megtartotta; ezt az F14 alább részleges
rajzolásra cseréli. A Home elődekódolt képei megmaradnak.
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

## F14 – összevetés a mainnel, részleges rajzolás és két CPU-mag

A `main` (`04eef87`) és az F11 előtti `150fde5` részleges frissítést használt,
a normál animáció 16 ms-os volt. Az F11 `adfa395` ettől `TRIPLE_FULL` módra
tért át: minden animációs frame az egész 1280×800-as jelenetet újrarajzolta
és CPU-val elforgatta. Az F13 csak az időzítést állította vissza; a teljes
rajzolás költsége megmaradt. A felhasználó F13 után is kb. tízből egy húzást,
akadozást, 82% belső RAM-ot, 55% PSRAM-ot, 28–52% CPU0-t és kb. 1% CPU1-et
jelzett. Ezek visszajelzett pillanatértékek, nem saját műszeres mérés.

Az F14 a main részleges LVGL-rajzolásához tér vissza, a régi diff-pufferjavítás
helyett egyszerű CPU-tulajdonlással: az első flush előtt a legutóbb leadott
kész képből egyszeri `memcpy` készít alapot a szabad hátsó pufferbe. A CPU
csak a módosult részleteket forgatja rá; az utolsó flush után egyszeri C2M
write-back, majd teljes kész kép átadása következik. A kijelzett/leadott
forráspufferre nem írunk, nincs DMA-javítás vagy M2C invalidálás. Az 50 soros
PSRAM-részpuffer 80 000 bájt; új teljes képpuffer nincs. A PPA tiltott marad.
A patch csak az adapter buildbeli másolatát érinti, ismeretlen forrásnál leáll.

A GUI/touch adapterfeladat explicit CPU1-en fut, a Loxone worker CPU0-n.
A két mag megosztja az állapot mutexeit; nincs második LVGL-feladat vagy
párhuzamos LVGL-objektumkezelés. Az energiaadatok öt másodpercenként jutnak
az ingestbe; a Home és Slave-jelenlét a meglévő WS-deltát dolgozza fel,
nem nyit új kapcsolatot. Az [adapter útmutatója](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html)
és az [IDF feladatütemezési leírása](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/api-reference/system/freertos_idf.html)
is támogat maghoz rendelt feladatokat. A magváltás önmagában nem csökkenti
a rajzolási munkát; azt a részleges frissítés csökkenti.

A korábbi desktop teszt DIRECT módja nem fedte le a firmware FULL módjának
költségét. Most az alap LVGL-próba a firmware-rel azonos 40 000 pixeles
részpufferrel fut. Azonos jelenetek és 30 animációs tick mellett külön FULL
összehasonlítás is készül; a flusholt pixelek száma mérhető, nem panel-FPS.
A buildelt adapter valódi CPU-forgatófüggvényével 480 puffer-újrahasználati
frame, átfedő/párnázott részletek, teljes jelenetcserék, RGB565/RGB888,
90°/270°, sikertelen submit és hiányzó szabad puffer vizsgálata fut.
A kész frame előtt nincs panelátadás; a régi puffer teljesen érintetlen.
A hiányzó alapmásolatot szándékosan visszaállító hibás változat elbukik.
A felhasználó az F14 (`b0a62d9`) után igazolta az akadásmentes animációt, a működő érintést és Loxone-oldalakat. Számszerű új CPU/FPS/inputkésés-mérést nem közölt.

Azonos RGB565 jelenet, 30 animációs tick, előzetesen befejezett belépési áttűnés:

| Téma | FULL mód flusholt pixelei | PARTIAL mód flusholt pixelei |
| --- | ---: | ---: |
| Rendszer | 30 720 000 | 3 633 786 |
| Élő otthon | 30 720 000 | 314 511 |
| Energiakör | 10 240 000 | 18 698 |
| Nap pálya | 30 720 000 | 171 850 |

A Nyugodt otthon statikus jelenete változatlan adatokkal egyik módban sem
kér új frame-et. A táblázat az LVGL által újrarajzolt/forgatásra leadott pixelt
számolja; a baseline 2 048 000 bájtos másolatát, cache-visszaírást, panelátadást
és hálózati munkát nem. Ez ezért nem azonos a teljes CPU- vagy memóriaforgalom
csökkenésével. A készüléken elért FPS/inputkésés továbbra is visszamérendő.

## Gyorsabb témaváltás – SD-olvasás és ismételt validálás

A balra/jobbra lapozás eddig minden váltáskor kétszer olvasta be a sablon
PNG-it: először a teljes validálás (az előnézetekkel együtt, CRC és teljes
zlib-kitömörítés), majd a betöltés. A `read_file()` `fread()`-et használt; a
FATFS `CONFIG_FATFS_VFS_FSTAT_BLKSIZE=0` beállításával a newlib 128 bájtos
stdio-puffere miatt minden 512 bájtos szektor külön SD-parancs volt, és az
igazítatlan PSRAM-cél miatt az SDMMC-meghajtó blokkonként, belső átmeneti
pufferen át olvasott (ESP-IDF 5.5.4 `sdmmc_read_sectors`).

A javítás a működést és a megjelenített képpontokat nem változtatja:

- A `read_file()` POSIX `open`/`fstat`/`read` hívással, 128 bájtra igazított
  PSRAM-pufferbe olvas. A FATFS így klaszterenként egy több blokkos DMA-
  átvitelt kér. Foglalási hibánál a korábbi `malloc()` puffer marad.
- A boot és minden telepítés/törlés utáni `scan()` a teljesen validált
  sablonkönyvtárak ujjlenyomatát (név, méret, módosítási idő) megjegyzi.
  Változatlan könyvtárnál a lapozás a `template.json` teljes sémaellenőrzését
  futtatja, a betöltött PNG-ket pedig egyszer olvassa be, chunk-CRC-vel
  ellenőrizve, ismételt kitömörítés nélkül. Bármilyen eltérésnél (új, törölt,
  cserélt fájl, SD-csere) a korábbi teljes ellenőrzés fut; hibánál a
  Rendszer nézet marad, mint eddig.
- A `selection.txt` mentése a téma betöltése utáni, változatlan generációjú
  `app_templates_load_pending()` hívásra kerül (kb. 200 ms-mal később), így a
  FAT-metaadatírások nem késleltetik a lapozást. Sikertelen írást a következő
  kör újrapróbál.
- Az elfogadott gesztus bináris szemaforral azonnal felébreszti a runtime
  taskot, nem kell kivárni a ciklus végi 200 ms-ot.

Két további, a felületet lassító felesleges ellenőrzés is kikerült:

- Az Otthon-oldalak betöltése (`app_templates_home_load_for`) minden
  oldalváltáskor teljesen kitömörítette a háttér- és ikon-PNG-ket egy
  ideiglenes pufferbe, majd a `home_decode_images()` az LVGL lodepng-
  dekóderével még egyszer. A csomagot a `scan()` már teljesen ellenőrizte, a
  lodepng a chunk-CRC-t és az Adler-32-t is vizsgálja, és dekódolási hibánál
  ugyanaz a betöltőképes újrapróbálás fut (ezt a preview sérült háttér/ikon
  tesztje lefedi). A betöltés ezért csak a PNG-szerkezetet és a méretet nézi.
- A runtime task 200 ms-onként, zárt diagnosztikai nézet mellett is lekérte a
  teljes diagnosztikai pillanatképet: heap-statisztikát (a legnagyobb szabad
  DMA-blokk a heap bejárásával) és a Wi-Fi RSSI-t, ami az `esp_hosted` miatt
  SDIO-s RPC a C6 felé. Most csak a megnyitott nézet kéri le, változatlan
  tartalommal. A CPU-terhelés első értéke megnyitás után legfeljebb 2 s-ig a
  legutóbbi mintavétel óta eltelt idő átlaga.

Az Otthon-kártyák közötti lapozás eddig minden lapnál újra beolvasta és
dekódolta a háttérképet (kb. 0,9–1,1 MB PNG, 1280×800-as ARGB8888 dekódolás és
RGB565-átalakítás) és a logót, pedig a háttér csak a módtól (kártyák vagy
fogyasztók), a napszaktól és az erőforráscsomag generációjától függ. Ha ezek
változatlanok, a dekódolt háttér- és logópuffer megmarad, és az új
`app_templates_home_load_icons()` csak az adott lap ikonjait olvassa be. A
betöltőkép és a megjelenített képpontok változatlanok. Bármilyen hibánál
(sérült ikon, foglalt SD, közben cserélt csomag) a teljes oldal felszabadul, és
a következő próbálkozás a hátteret is teljesen újratölti.

A `tests/preview` `ZOLEE_THEME_SWITCH_BENCH=1` módja a valódi UI-kódon méri
a betöltőkép, az előkészítés, a nézetépítés, az első kép és az áttűnés idejét,
és a végső kép ellenőrzőösszegét is kiírja. Hoston (RGB565, 33 váltás) az
előkészítés ideje kb. 580 ms-ról 57 ms-ra csökkent; mind a 11 téma végső
képe és a preview mind a 143 mentett képe bájtra azonos (a később
hozzáadott, csak változáskor rajzoló frissítésnél lásd a lenti összevetési
módszert). A host lemezcache
miatt ez az SD-olvasás javulását nem tartalmazza; a sablononként beolvasott
adat kb. 48 MB-ról 22 MB-ra (a tíz gyári témán összesen), a szektoronkénti
parancsok helyett klaszterenkénti átvitelre változik. A készüléken elért
váltási időt fizikai panelen kell visszamérni.

## Csak változáskor rajzoló frissítés – Master és 4B Slave

Az LVGL 9.5-ben az `lv_label_set_text()`, az `lv_image_set_src()` és az
`lv_obj_set_style_*()` színbeállítók azonos értéknél is érvénytelenítik az
objektumot. A pozíció-, méret-, flag-, állapot- és `lv_bar_set_value()` beállítók
már maguk kihagyják a változatlan értéket. Periodikus frissítésben ezért minden
szöveget, képforrást és színt a `*_if_changed` segédekkel vagy az
`lv_image_get_src()` összevetésével kell írni.

- **4B Slave (`firmware/slave-waveshare-4b/ui`)**: a 33 ms-os `refresh`
  változatlan adat mellett is kb. 40 területet érvénytelenített képkockánként
  (négy érték-, akku-, hőmérséklet-, szél-, óra-, üzenet- és diagnosztikai felirat
  kétszeres, árnyékolt rajzolással, az időjárásikon, a gyűrűk, utak és pontok
  színe). Most nulla; a mozgó energiapontok és a valódi változások ugyanúgy
  frissülnek. A kaputelefon-fedőréteg (állapot, gombfeliratok, gombszín) a
  hívás alatt sem rajzol újra feleslegesen, így nem versenyez a videókép
  frissítésével.
- **Master sablonok**: a 200 ms-os `template_update` a nap/hold korong
  árnyékszínét, a műszerek (03) ívszínét és a beépített időjárásikont is csak
  változáskor írja.

Ellenőrzés: a 4B szimulátor 30, a Slave kaputelefon-preview 10 üresjárati
frissítésen át számolja az `LV_EVENT_INVALIDATE_AREA` eseményeket (elvárt: 0).
A régi kód mindkét teszten megbukik.

**Képpont-összevetés részleges frissítésnél.** Az LVGL szoftveres renderelője a
részletekben rajzolt átméretezett képeknél és élsimított íveknél 1–2 (ritkán
több) egységnyi eltérést adhat a teljes újrarajzoláshoz képest; ez a régi kódban
is megvolt. Ha egy változtatás csökkenti a felesleges érvénytelenítést, a mentett
képek ezért néhány képpontban eltérhetnek, holott az UI-állapot azonos. Ilyenkor
a mentés előtt teljes újrarajzolással (`lv_obj_invalidate(lv_screen_active())`,
`lv_refr_now()`) kell összevetni: a 143 Master- és 9 Slave-kép így bájtra
azonos volt a változtatás előtt és után. A részleges és a teljes rajzolás
közötti eltérés az érintett képeken csökkent.

## Felhasználói élmény bővítések – terhelési szerződés

A `feature/ux-improvements` funkciói a meglévő teljesítményszabályokat követik:

- **Akku hátralévő idő:** mintánként egy exponenciális simítás (néhány
  lebegőpontos művelet), memória- és SD-hozzáférés nélkül; a felirat csak
  változáskor íródik.
- **Postaláda-jelvény:** egyetlen, natív méretű (52×52) beágyazott PNG, egyszer
  dekódolva; a 200 ms-os kör csak a láthatóságot/helyet írja változáskor.
- **Jelenlétvezérlés:** a közös, allokációmentes fényerő-állapotgép; a Loxone
  jeleket egy kis, zár alatti pillanatkép adja (nincs új kapcsolat vagy kérés).
- **4B Mai összesítő:** a 45 KiB-os statisztika-pillanatkép PSRAM-ban, csak
  látható nézetnél, legfeljebb másodpercenként; üresjáratban 0 érvénytelenítés
  (szimulátorteszt).
- **Belső beállítás-tartalék:** csak tartalomváltozáskor ír flash-t, alacsony
  prioritású feladatban, 15 s-os összevonással; a témaváltás nem indít írást.
- **GitHub-témafrissítés:** csak kérésre, saját feladatban; a nagy PNG-k PSRAM-ba
  töltődnek, az SD-zár csak a rövid írások idejére foglalt.
- **Webfelület:** a közös stíluslap oldalanként beágyazva (nincs plusz HTTP-kérés),
  a firmware-ben kb. 3,5 KiB oldalanként.
