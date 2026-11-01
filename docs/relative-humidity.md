# Relatív páratartalom Loxone módban

A **Loxone beállítások → Adatpárosítás** táblázatban a **Külső hőmérséklet**
után található az opcionális **Relatív páratartalom (%)** sor. Válassz numerikus
állapotot, és állítsd be a szorzót/eltolást. Alapértékük 1 és 0; 0–1 közötti
arányból például 100-as szorzóval lesz százalék. A mentett `loxone.json`
`mappings.RelativeHumidity` objektuma a szokásos `uuid`, `scale`, `offset` mezőket
használja. Régi konfigurációval nincs szükség újrapárosításra.

Az átalakítás sorrendje: `nyers érték × szorzó + eltolás`, egészre kerekítés
(0,5-től felfelé), majd 0–100% közé korlátozás. Példák: 61,4 → 61%, 61,5 → 62%,
−2 → 0%, 105 → 100%. A megjelenítés mértékegysége `%`, tizedesjegy nélkül.
A korlátozás a kódban még lebegőpontosan, a konverzió előtt történik, így az
extrém véges értékek sem okoznak egészszám-túlcsordulást.

Párosítás nélkül és az első érvényes érték beérkezéséig **az ikon és az érték
is rejtett**. A valódi 0% látható. Nem véges/hibás opcionális értéknél a
páratartalom eltűnik, az energiaminta tovább használható; a következő érvényes
értéknél visszatér. A meglévő kapcsolatvesztési és kijelző-türelmiidő-szabályok
érvényesek. Energiaáramlás tiltás mellett az új sor is rejtett/inaktív.

## Érintett nézetek

A Rendszer nézet és a szélsebességet mutató hét SD-téma kapott ikonos
páratartalmat mindkét megjelenési módban: Élő otthon, Energiautak, Nyugodt otthon,
Energiafőkönyv, Statisztika, Nap pálya, Elektromos autó. A többi téma nem változik.
A témafrissítéshez az új ZIP-csomagokat is telepíteni kell; a régi SD-téma tovább
működik, de az új elemet nem tartalmazza. Az új csomagokhoz az új firmware kell.

A Nap pálya hőmérséklete és szélsebessége közös jobb szélre igazított.
Az időjárás megnevezése az eredeti helyén, balra igazítva marad, hogy az ikonhoz
ne érjen. A páratartalom a szél előtt, ugyanazon középvonalon látható; a teljes
sor 8 px-lel feljebb, a felső időjárási sorhoz közelebb került. A szél ikonja
6 px-re van a 72 px széles értékmezőtől; a háromjegyű „150 km/h” (71 px)
is teljesen elfér, a közös jobb szél megmarad. A dátum
és az óra bal oldali horgonya azonos. Az akku teljesítménye és töltöttsége
azonos, 20-as betűméretet és középvonalat használ; a teljesítmény előjel szerinti
színei megmaradtak.

## Slave-ek és kompatibilitás

A Master a validált egész százalékot a Slave-eknek is átadja. A P4 Slave ugyanazt
a téma-/Rendszer-megjelenítést használja, a 720×720-as 4B Slave saját időjárási
részén mutatja az adatot. A [BLE-szerződés](ble-data-seed.md#adatfolyam-és-frissesség)
szerinti v2 mintapár változatlanul 2×20 bájt; nincs új GATT-karakterisztika,
feladat vagy üzenet. Az óra v1 marad.

**Először a Slave-eket frissítsd**, utána a Mastert és a témákat, majd párosítsd
az új Loxone-adatot. Érvényes páratartalomnál a régi Slave elutasítja a v2 mintát.
Páratartalom nélkül a Master változatlan v1 mintapárt küld; az új Slave a régi
Mastertől érkező v1-et is fogadja, rejtett páratartalommal.

## Erőforrások és ellenőrzés

Az új fényes, kék cseppikon a beépített/4B nézetben natív 28×28, az SD-témákban
natív 30×30 RGBA PNG, egyenként 2 KiB alatt. A dekódolt ARGB8888 képpontadat
3136/3600 bájt, nincs nagy forráskép futásidejű betöltése. A generált eredeti
`assets/source/weather_humidity.png`; a méretező `tools/prepare_humidity_icon.py`,
a prompt és SHA-256 eredet az assetjegyzékekben szerepel.

A 96 elemű állandó témakeret megmaradt. A Nap pálya hat elválasztó-ponttal és az
új ikonnal/értékkel pontosan 96 elemes. Nincs új feladat/verembővítés. A frissítés
csak megváltozott szöveget/állapotot ír LVGL-be, és nem olvas újra SD-erőforrást.

A `tests/test_relative_humidity.py` a valódi Loxone → közös parser → BLE útvonalat,
a kezdőértéket, 0/100 határokat, kerekítést, szorzót/eltolást, hibát/helyreállást,
régi konfigurációt és hibás/kevert csomagok atomi elutasítását ellenőrzi.
A tényleges LVGL-tesztek mindkét színmódban ellenőrzik a láthatóságot és a
változatlan frissítés 0 érvénytelenítését. A fizikai panel DMA/PSRAM minimuma és
a tényleges Miniserver/szenzor-adatút külön eszközös próba tárgya.

## Kódreview és elvégzett ellenőrzések (2026-11-01)

A review a mentett konfiguráció kompatibilitását, a transzformáció/érvényesség
adatútját, az atomi BLE-csomagcserét, mindkét UI életciklusát, az SD-képkezelést,
a változatlan frissítést és az ütközésmentes elrendezést vizsgálta. A vizuális
ellenőrzés során talált két energiaasszisztens-ütközés, valamint a Rendszer nézet
programneve és dátuma közti 28 px-es átfedés javítva. A Nap pálya hosszabb
időjárás-megnevezésének eredeti pozíciója visszaállítva, így nem ér az ikonba.
Nyitott ismert kódhiba nem maradt. Az eltérő BLE mintaverzió miatti frissítési
sorrend fent
szerepel, ez a kompatibilitási változás szándékos.

| Hoston mért struktúra | Előtte | Utána |
| --- | ---: | ---: |
| `app_energy_sample_t` | 144 B | 144 B |
| `lx_config_t` | 5464 B | 5520 B |
| `lx_values_t` | 168 B | 176 B |
| 96 elemű `template_nodes` | 235008 B | 235008 B |

Ezek a méretek a Linux x64 build eredményei; a teljes firmware RAM-/flash-mérete
és a fizikai DMA-minimum ettől külön adat. Az új ikonok képpontpufferén és a
új LVGL-elemeken kívül nincs új állandó képernyőpuffer, feladat vagy verem.
A teljes regresszió szerint a legnagyobb témaképkészlet 8850960 B, a 10 MiB-os
képcache alatt marad.

Elvégzett ellenőrzések: teljes `python3 tests/run_tests.py`; tényleges Master
LVGL-regresszió 32 bites és RGB565 megjelenítéssel, AddressSanitizerrel; 100
oldal-életciklusos teszt; mind a 24 Master jelenet célzott páratartalom- és
üresjárati tesztje; 4B Slave szimulátor, valódi BLE roundtrip, láthatóság és
nulla fölösleges újrarajzolás; valódi renderből újragenerált előnézetek;
téma-ZIP validálás/csomagolás. A teszt a régi rendererrel elbukik, és szándékos
feltétel nélküli címkeírásnál az üresjárati számláló is hibát jelez.

A helyi környezet `/proc` korlátozása miatt a LeakSanitizer itt kikapcsolt,
az AddressSanitizer memóriaelérési ellenőrzése aktív; a CI saját környezetében
a meglévő `detect_leaks=1` ellenőrzés is változatlanul fut.

### Rendszer nézet fejlécének utóellenőrzése

A középső, kétsoros programnév 65 px-lel balra került. A szabadon megadható
projektcím 295 px széles, egysoros, túl hosszú szövegnél három ponttal rövidül.
A programnév és a dátum közötti rés a natív, 52 px-es postaláda-jelzés
saját helye; az adatmezők ehhez igazodnak. Így a hosszú projektcím, a
programnév, a dátum, az időjárás, a páratartalom, a szél és az óra külön,
ütközésmentes területet használ. A javítás nem hoz létre új widgetet,
képpuffert vagy periodikus feladatot.

Az új `test_builtin_header_layout` a valódi LVGL-dobozokat vizsgálja HU/EN,
világos/sötét módban, normál és maximális hosszúságú projektcímmel, −35 °C,
150 km/h és 100% páratartalom mellett. Az energiaasszisztens 300 px-es
területét és a postaláda helyét is ellenőrzi a programnévhez/dátumhoz képest.
A teljes review ezen felugró jelzések átfedését is javította, új widget és
periodikus feladat nélkül. A javítás előtti kódon az ütközési
assertion elbukott. A javított Rendszer-előnézetek valódi LVGL-renderből készülnek.
