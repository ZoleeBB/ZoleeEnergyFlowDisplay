# v3.0.0 – Loxone otthonfunkciók fejlesztési és folytatási terve

## Állapot és folytatás

- Fejlesztési ág: `feature/v3-loxone-home`.
- Kiinduló főág: `04eef87d1210356db334fa6790715521d4a31e38` (2026-10-20).
- A #15 Slave/OTA PR már egyesítve; a meglévő javítások megőrzendők.
- Cél: dokumentált, tesztelt v3.0.0; PR kizárólag az összes vállalt fejlesztés elkészülte után.
- Állapot: követelmények egyeztetése folyamatban; konfiguráció/SD/webes szerkesztő, közös erőforráscsomag, két élő Master-oldal és korlátos vezérlés elkészült. Slave-jelenlét runtime, energiaasszisztens és tématervek hátravan.
- Folytatáskor először olvasd végig ezt a fájlt, az AGENTS.md-t és a git diffet.
- A felhasználó bizonytalan termékdöntéseit egyesével kell megkérdezni.
- Minden checkpointnál frissítendő: kész feladatok, tesztek és korlátaik, nyitott kérdés, következő konkrét lépés.
- Még nincs PR, nincs release és nincs késznek nyilvánított új funkció.

## Elfogadott döntések – felhasználói válaszok

| Azonosító | Döntés |
| --- | --- |
| D01 | Minden új Loxone-funkció csak `DataReceptMode=loxone` módban érhető el. MQTT/HTTP mellé nem épül külön Loxone-kapcsolat. |
| D02 | Fogyasztói és szabad kártyás oldal kizárólag MASTER kijelzőn. |
| D03 | A „Most érdemes…” energiaasszisztens is kizárólag MASTER kijelzőn. |
| D04 | Fogyasztónként „Indítás javasolható” kapcsoló; alapértelmezetten kikapcsolva. |
| D05 | Akku töltése elsődleges. Fogyasztóindítási javaslat csak 100% SOC elérésekor és tartós, elegendő napelemes többletnél. Nem 80%-os beállítható célérték! |
| D06 | Éjjel elsötétített Slave jelenlétjelre felébred, majd az utolsó jel után a beállított timeout végén újra elsötétedik. |
| D07 | Nyugodt otthon és Energiakör teljes újratervezése; régi azonosító és sorrend megmarad. Látványtervek külön egyeztetendők. |
| D08 | Minden új fogyasztói ikon 96×96 px. Master teljes képernyős háttér 1280×800 px; esetleges Slave háttér 720×720 px. Nagy generálási forrásképek ne kerüljenek a repóba. |
| D09 | Jól tagolt forrás, magyar kommentek; firmware/RAM/PSRAM korlátok és a már működő adatfogadás/animáció védelme. |
| D10 | Slave nappal folyamatosan a saját beállított nappali fényerején világít. Éjjel az engedélyezett elsötétítés mellett sötét; jelenlétre a saját beállított éjszakai fényerejére ébred, timeout után újra sötét. |
| D11 | A sötét Slave érintésre is ébred az éjszakai fényerőre, ugyanazzal az időkorláttal. Az első érintés csak ébreszt: a teljes érintéssorozatot (felengedés is) el kell nyelni, nem indíthat kattintást vagy gesztust. |
| D12 | A világos fogyasztói oldal látványterve elfogadva: 4×3, legfeljebb 12 kártya. Hiányos sor vízszintesen középre igazítva, a két szélén azonos üres hellyel; 7 elemnél felül 4, alul 3 középre. |
| D13 | A világos Otthon oldal látványterve elfogadva (értékek, kapcsolók és indítógombok). A felhasználó sokféle ikont kér a közös készletbe, a fogyasztói ikonokon túl is. |

## Követelmények – fogyasztói oldal

- Loxone konfigurációs oldalon fogyasztói szakasz: több szerkeszthető és törölhető elem.
- Mezők: ikon, név, opcionális átlagos üzemi teljesítmény, Loxone állapot UUID, skálázó; belső egység kW.
- „Indítás javasolható” kapcsoló (D04). Átlagos teljesítmény hiányakor ne találjunk ki indítási igényt.
- Egységes, valósághű, átlátszó 96×96 PNG-készlet minimum: hűtő, mosógép, szárító, mosogatógép, sütő, főzőlap, beltéri/dual/trial/mobil klíma, autótöltő, UPS, páramentesítő, TV, szellőztető, ventilátor, elszívó, borhűtő, fűtőtest, lámpa, hőszivattyú, bojler, padlófűtés.
- További indokolt ikonok javaslata: fagyasztó, kávéfőző, vízforraló, mikrohullámú sütő, porszívó/robotporszívó, számítógép, NAS, keringetőszivattyú, medenceszivattyú, törölközőszárító, általános konnektor.
- Minden Master témában a ház ikon érintése nyitja. Illusztrált háttérbe épített háznál megfelelő érintési terület kell.
- Nyitás feltétele: Loxone mód, Master, működő SD, érvényes speciális csomag és legalább egy konfigurált fogyasztó.
- X a jobb felső sarokban; visszatérés pontosan a korábban megnyitott témához.
- Kártyák: ikon, név, pillanatnyi teljesítmény; ezen az oldalon 1 kW alatt W. 0 valós mérés, hiányzó adat külön állapot.
- Nulla teljesítmény: valódi szürke/fekete-fehér ikon (egy áttetsző szürke réteg önmagában nem színtelenít).
- Csak aktív oldalhoz szükséges képek/widgetek maradjanak betöltve; nincs második Loxone kapcsolat.
- Fogyasztói élő részletek aktív oldalon frissüljenek. Az asszisztens minimális állapotigényét ehhez külön meg kell tervezni.
- Háttér és ikonok SD-s speciális témacsomagban, a meglévő témakezelőn át frissíthetők.
- Visszafogott, helyi Loxone-logó. Forrás: https://upload.wikimedia.org/wikipedia/commons/c/c6/Logo-Loxone-green-Web.png
- Elfogadott 12 fogyasztós keret (4×3); hiányos utolsó sor középre igazítva (D12). A teljes sorok és a kártyák mérete nem változik az elemszámmal. Végleges elrendezés valódi LVGL-renderrel is ellenőrizendő.

## Követelmények – energiaasszisztens

- Loxone mód és Master (D01/D03).
- Nincs automatikus kapcsolás; érthető, rövid szöveges javaslat.
- Csak javasolhatóra jelölt, nem már működő fogyasztó, ismert átlagos üzemi teljesítménnyel.
- Akku 100%, tartós napelemes többlet, fogyasztót fedező tartalék. Akutöltés elsőbbsége (D05).
- A „napelemes többlet” ne legyen tévesen akkuból exportált energia; forrásállapotok és előjelek vizsgálandók.
- Ne javasoljuk ugyanazt állandóan; minimumtartósság, visszavonás és bezárási újramegjelenési szabály tervezendő.
- Másik funkció: szokásosnál magasabb éjszakai alapfogyasztás, csak elegendő, összevethető történeti lefedettséggel.
- X-szel bezárható; aktualitás megszűnésekor automatikusan eltűnik.
- Témánként külön biztonságos hely; ne takarjon fontos adatot. Speciális oldalakon ne fedje a vezérlőket.
- Nem kell felhő/AI-szolgáltatás: helyi, determinisztikus szabályok.

## Követelmények – szabad Loxone-kártyák

- Kizárólag Master és Loxone mód.
- Kártyatípusok: érték, pillanatnyi gomb (`pulse`), On/Off kapcsoló.
- Érték: név, ikon, állapotpárosítás, mértékegység, skálázás, figyelmeztetési határok/színek.
- Gomb/kapcsoló: tényleges vezérlési objektum párosítása szükséges; az állapot UUID nem azonos automatikusan a parancs cél UUID-val.
- Pulse egyszeri érintésre egyszer küldődjön; nincs kapcsolat utáni automatikus újraküldés.
- Kapcsoló a visszaérkező Loxone-állapotot mutatja; hibás/lejárt állapotnál ne legyen hamis siker.
- Kapcsolatvesztéskor vezérlés tiltva, felhasználó számára érthető visszajelzéssel.
- Fentről lefelé húzás nyitja, jobb felső X zárja, korábbi téma visszaáll.
- Csak legalább egy kártyánál és elérhető SD-s speciális csomagnál nyitható.
- Saját színes, otthon hangulatú, jól olvasható hátterek; SD-ről és témakezelővel frissíthetően.
- Javasolt felső határ 12 kártya; a részletek a látványtervnél ellenőrzendők.
- Biztonságos, engedélyezett konkrét műveletek; nincs önkényes felhasználói URL/parancs végrehajtás.

## Követelmények – diagnosztika és gesztusok

- Diagnosztika bezárása jobb felső X-szel; a lefelé húzás ne zárja.
- Meglévő felfelé húzásos diagnosztikanyitás megőrizhető; lefelé húzás az új kártyás oldalé.
- Párosítás engedélyezett ablaka alatt a kapcsolódó mező háttérszíne `#0082FC`, előtte kis Bluetooth-ikon.
- Az állapot a tényleges párosítási ablakot kövesse (lejárat, bezárás, hiba is).
- Ikon referencia: https://icon-icons.com/icon/bluetooth/130975 ; helyi, megfelelő méretű/forrásjogú erőforrás kell.
- Gesztusok, X, PIN-pad és kártyagombok ne indítsanak egymásra eseményt.

## Követelmények – témák és külső csomagok

- `05-calm-home` és `06-energy-orbit` azonosító, sorrend és mentett választás megőrzése.
- Két új vizuális terv a kedvelt Rendszer, Nap pálya és Élő otthon alapján; generálás/implementálás előtt egyeztetés.
- A Rendszer témán csak a kért új elérési pontok/asszisztens jelenjen meg; ne tervezzük át az alap megjelenést.
- Külső tématámogatás csak kis ráfordítás esetén. Már van `docs/template-authoring.md`, C-validátor és ZIP-ellenőrzés.
- Ellenőrizni kell az ID-engedélylistát, renderer-specifikus megkötéseket, fájl- és dekódolt memóriakorlátokat, hibaüzeneteket.
- Nem épül önkényes kódot futtató pluginrendszer; dokumentált deklaratív elemekből készülő csomagok a cél.
- A 32 PNG/csomag korlát a nagy ikonkészlethez nem elég. Közös, verziózott erőforráscsomag vagy külön speciális csomagszabály szükséges, korlátlan általános megemelés helyett.
- Saját témáknál a ház érintési területének és az asszisztens helyének dokumentált megadása megtervezendő.
- Korábbi csomagok visszafelé kompatibilitása megőrzendő.

## Követelmények – Slave profilok és jelenlét

- Párosításhoz kötött, tartós logikai azonosító; név szerkeszthető a Master Loxone-oldalán.
- Rádiós privát cím helyett stabil eszközidentitásra épüljön.
- Slave-enként jelenlétjel-párosítás, timeout és éjszakai elsötétítés kapcsoló.
- Impulzusos jelenlét újraindítja az időzítőt; tartós aktív jel ne eredményezzen téves elsötétítést.
- D06: éjszakai jelenlét felébreszt; utolsó jel/aktív állapot vége után timeouttal ismét sötét.
- D10: nappal a jelenlét hiánya nem sötétít; folyamatos saját nappali fényerő. Éjjel jelenlétre saját éjszakai fényerő, timeout után teljes elsötétítés. A Slave saját fényerőértékeit nem írhatja felül a Master pillanatnyi kijelzőfényereje.
- D11: érintés is ébreszt; az ébresztő érintés csak ébreszt, ugyanazzal a timeouttal.
- Kapcsolatvesztésnél érvénytelen jelenlét nem hosszabbíthatja meg az ébrenlétet; a korábbi időzítő lejárhat. Friss éjszakai adat nélkül az utolsó ismert napszak megőrzése és az indulás/PIN-pad láthatósága külön integrációs ellenőrzés.
- Szükséges profil/időzítés minimális állapota BLE-n; új kártyák vezérlése nem kerül a Slave-re.
- PIN/MITM/bonding és a Wi-Fi/OTA profil meglévő védelme nem gyengülhet.
- Visszafelé kompatibilitás és régi Master/Slave viselkedés dokumentálandó.
- Master módban legfeljebb három meglévő peer kerete vizsgálandó; nem emeljük automatikusan a kapcsolatszámot.

## Megvalósítási sorrend és elfogadási pontok

- [x] Főág és #15 állapota ellenőrizve.
- [x] AGENTS.md és meglévő témaszerzői dokumentáció elolvasva.
- [x] Új ág megnyitva a friss főágból.
- [ ] Releváns források, tesztek, kiadási workflow részletes felmérése.
- [ ] Következő termékdöntések egyesével tisztázva; a választ e fájlban rögzíteni.
- [ ] Korlátos konfigurációs/adatmodell, visszafelé kompatibilis parse/mentés, érzékeny adatok védelme.
- [ ] Fogyasztói és kártyás webes szerkesztők; valódi JS/API-tesztek.
- [ ] Speciális csomagok/assetkatalógus; pontos képméretek és SD-kezelés.
- [ ] Mindkét új oldal és navigáció, frissesség, terhelési mérések.
- [ ] Pulse/kapcsolók tényleges parancs- és állapotútja.
- [ ] Energiaasszisztens szabályai és témánkénti pozíciói.
- [ ] Diagnosztika X és valós párosításjelzés.
- [ ] Slave identitás/profil/jelenlét és mindkét hardver/szimulátor támogatása.
- [ ] 05/06 témák jóváhagyott látványterve és elkészült eszközméretű képei.
- [ ] Külső témák célzott javítása, ha kis erőforrásból elvégezhető.
- [ ] HU/EN dokumentáció és feliratok, konfigurációs minták.
- [ ] `python3 tests/run_tests.py`, releváns szimulátor/LVGL render, Master és Slave ESP-IDF build.
- [ ] Firmware/OTA, belső RAM/DMA és PSRAM keretek ellenőrzése.
- [ ] Hardveres próbákhoz külön lista; szimulációt nem tekintünk fizikai megfelelésnek.
- [ ] v3.0.0 kiadási cél beállítása a tényleges release-folyamat szerint, automatikus köztes kiadás nélkül.
- [ ] Csak minden vállalt pont kész állapotánál PR. Egyesítés/publikálás nincs előre elvégezve.

## Checkpoint 2026-10-20 / 1

Elkészült: kezdeti követelménylista és elfogadott D01–D09 döntések, új ág.
Ellenőrzés: read-only repómeta/PR/dokumentáció felmérés; funkcionális kód még nem módosult.
Ismert kockázatok: belső RAM/DMA szűk keret, 32 képes csomagkorlát, állapot- és parancs-UUID különbsége,
aktív oldalhoz kötött fogyasztói adatok versus asszisztens állapotigénye.
Következő lépés: releváns C/JS/kiadási források és tesztkörnyezet beolvasása; első önálló modell/validátor megvalósításának előkészítése.

## Checkpoint 2026-10-20 / 2

Elkészült:

- `app_loxone_home_model.h/.c`: külön, jelszómentes, kezdetben 12 fogyasztót és
  12 érték/pulse/kapcsolókártyát leíró modell, atomi parse és JSON-szerializálás.
- Opcionális üzemi teljesítmény, alapból tiltott indításjavaslat, külön állapot-
  és vezérlési UUID, skálázás és figyelmeztetési beállítások validálása.
- Loxone katalógus kiegészítése `control_uuid` mezővel; nem változott meg a régi
  `uuid` jelentése. Érvénytelen objektumkulcs nem használható parancscélként.
- A Master fordítási listájában az új modell; a Slave firmware listája változatlan.
- [Fejlesztői formátumleírás](v3-home-config.md) és `tests/test_loxone_home.py/.c`;
  az új teszt bekerült a teljes regressziós teszt belépési pontjába.

Ellenőrzés:

- Új modell célzott tesztje: sikeres (UBSan, 8192 bájtos stack-frame korlát,
  teljes 12+12 profil, hibás bemenetek, atomi elutasítás, calloc-hiba és minden
  szerializálási JSON-foglalási pont hibainjektálása, szivárgásszámláló).
- Profilméret a natív tesztkörnyezetben: 6248 bájt, parse ideiglenes tár heapen.
  Ez nem a paneles memóriahasználat mérése.
- Meglévő Loxone C-modellteszt: sikeres.
- Meglévő valódi Loxone webes JS-teszt: sikeres.
- Beágyazott oldalak generátor-ellenőrzése és git diff whitespace: sikeres.
- A módosítás előtti teljes regresszió az első öt BLE/boot tesztcsoporton átment,
  majd hiányzó `mbedtls/version.h` miatt megállt. ESP-IDF nincs a PATH-on.
  Teljes firmware-build, LVGL-render és paneles próba ebben a checkpointban nem volt.

Forrásfelmérés:

- `app_templates_safe_id` nem csak saját témaneveket enged; a meglévő teszt
  egy `test-layout` nevű saját csomag telepítését is ellenőrzi. Nem kell új általános
  témamotor. A célzott kompatibilitási és hibaüzenet-vizsgálat még hátravan.
- A release kiválasztás jelenleg PR-számhoz kötött v1/v2 kivételt tartalmaz.
  A v3 kiválasztását a záráskor kell a valós végső PR-hoz kötni vagy megfelelően
  szűkített szabállyal megoldani; a workflow nem lett előre átállítva.
- A munkakönyvtárban észlelt, nem követett `assets/source/.weather_cloud.png.4Dadnb`
  nem e fejlesztés módosítása; nem commitolandó, nem törlendő mellékesen.

Még nincs: tartós home-konfigurációmentés, webes szerkesztő, élő állapottár,
parancsküldés, speciális képernyő vagy új grafika. A modell önmagában nem kész funkció.

Az itt korábban nyitott nappali/éjszakai viselkedést a felhasználó a 3. checkpointban eldöntötte (D10).
Ezzel párhuzamos következő technikai lépés: modellek SD-s atomi mentése és a meglévő
Loxone webes konfigurációba illesztés, a régi beállítások/jelszavak megőrzésével.


## Checkpoint 2026-10-20 / 3

Felhasználó által elfogadva (D10): nappal folyamatos világítás a Slave saját nappali
fényerején; éjjel elsötétítés, jelenlétre a saját éjszakai fényerőre ébredés,
majd timeout után újra elsötétítés. Az éjszakai elsötétítés külön eszközkapcsolója megmarad.
Ebben a checkpointban csak a specifikáció változott, futási kód nem.
Az itt nyitott érintéses ébresztést a felhasználó elfogadta (D11, 4. checkpoint).

## Checkpoint 2026-10-20 / 4

Felhasználó által elfogadva: D11, érintéses ébresztés, csak ébresztő első érintéssel.

Elkészült:

- `app_slave_display.h/.c`: közös, platformfüggetlen nappali/éjszakai/jelenléti
  állapotgép. Saját nappali és éjszakai fényerő, kapcsolható éjszakai elsötétítés,
  impulzus/tartós jel, monoton timeout, kapcsolatvesztési érvénytelenítés.
- Érintésnél a teljes első érintés lenyelésének jelzése a felengedéssel együtt;
  a következő rendes érintés továbbítható. Hosszú nyomás alatt nem alszik el.
- A modul nem foglal memóriát, nem ír NVS-t. Natív tesztben az állapot 32 bájt.
- A közös forrás mindkét firmware fordítási listájában szerepel.
- A 4B firmware tényleges, korábban fix 70/25%-os fényerőválasztása most a
  saját `config.display.day_brightness/night_brightness` értékeit használja.
- A három meglévő ellenőrző workflow a v3 fejlesztési ág push eseményeire is fut.
  Így a PR-nyitásig is ellenőrizhető a két firmware és a szimulátor. A kiadási
  workflow triggerei változatlanok: fejlesztési push nem készít release-t.

Ellenőrzés:

- `python3 tests/test_slave_display.py`: sikeres, valódi C és UBSan. Nappali
  folyamatos világítás, éjszakai sötétítés, impulzusok és tartós jelenlét,
  timeout határpillanat, első érintés/felengedés, második kattintás, kapcsolatvesztés,
  nappal/éjjel váltás, profilvalidálás, hibás időbélyegsorrend és int64 túlcsordulás.
- `python3 tests/test_loxone_home.py`: továbbra is sikeres.
- `git diff --check`: sikeres.
- A korábbi mbedTLS/ESP-IDF környezetkorlát változatlan. Teljes firmware-build
  és paneles érintési próba itt még nem készült.

Még nincs bekötve: BLE egyedi identitás és jelenlétprofil, védett profilátvitel,
NVS-mentés, LVGL inputkapu és időzített PWM-ébresztés. Ezért a közös állapotgép
elkészülte még nem jelenti a teljes éjszakai funkció üzemkész állapotát.
A 4B saját fényerőértékeinek használata viszont már a meglévő runtime út része.

Következő terméklépés: a fogyasztói oldal látványtervének egyeztetése; 4×3 kártyás,
96×96 ikonokkal és 1280×800 felbontásra tervezett, olvasható otthonhangulatú elrendezés.
Következő technikai lépés: a home-konfiguráció atomi SD-mentése és webes szerkesztője;
Slave oldalon stabil identitás/profil és az inputkapu bekötése mindkét kijelzőn.

## Checkpoint 2026-10-20 / 5

Felhasználó által elfogadva (D12): a fogyasztói oldal világos látványterve,
legfeljebb 12 kártyával, hiányos utolsó sor középre igazítva. Példája: 7 elem =
4 az első sorban, 3 középen a másodikban, egyenlő üres bal/jobb széllel.
A látványterv példaértékeket mutat; önmagában nem telepíthető háttér vagy kész UI.

Elkészült:

- `app_loxone_home_layout.h`: a valódi megjelenítő által használandó közös,
  memóriát nem foglaló kártyageometria, egységes mérettel és sorközökkel.
- A meglévő natív teszt minden 1–12 közötti elemszám minden sorát ellenőrzi:
  szimmetrikus margók, állandó méret, hézagok, képernyőhatárok és a konkrét
  hétkártyás eset. Hibás elemszám/index nem írja felül a kimenetet.
- A fejlesztői formátumleírás tartalmazza az elfogadott elrendezési szabályt.

Ellenőrzés:

- `python3 tests/test_loxone_home.py`: sikeres az új geometriai tesztekkel is.
- `git diff --check`: sikeres.
- Az előző kódcheckpoint (`cf6700d21aeecf0cdba64d792f09f8a418f7936e`) CI-je:
  [Master teljes regresszió, valódi LVGL render, csomagolás és firmware-build](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39440931504)
  minden jobja sikeres; [Windows-szimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39440931695)
  és [4B Slave ellenőrzés](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39440931483)
  szintén sikeres. Ez nem a még el nem készült v3 UI hardveres igazolása.

Következő terméklépés: a szabad Loxone-kártyás oldal látványterve ugyanebben a
vizuális világban; a világos fogyasztói tervre már nem kell ismét jóváhagyást kérni.
További grafikai egyeztetés: sötét változat és a 05/06 témák külön tervei.
Technikai feladatok továbbra is nyitva: SD-mentés/webes szerkesztő és a tényleges
LVGL-oldalak, runtime állapotok/vezérlés, Slave BLE-profil és inputkapu.

## Checkpoint 2026-10-20 / 6 – teljes ikonkészlet elkészült

Elfogadva (D13): a világos Otthon kártyás oldal terve. A felhasználó bőséges,
sokféle ikonkészletet kér. Mind a 64 megtervezett ikon elkészült:
háztartási fogyasztók, klíma/fűtés, technika, érzékelők, világítás, kapuk,
árnyékolók, biztonság, kert és jelenetek.

Elkészült:

- `assets/loxone-home/catalog.json`: stabil ikonkulcs, magyar/angol név, kategória,
  tárgyleírás, pontos generálási prompt, runtime-útvonal, eredeti és végleges SHA256.
- `assets/loxone-home/icons/`: 64 különálló, 96×96 RGBA PNG valódi átlátszósággal.
  A nagy generált eredetik nem kerültek a repóba. Beépített `image_gen` készült;
  az import kizárólag méretre alakít, nem festi át és nem távolítja el a hátteret.
- Teljes PNG-méret: 682 017 bájt (666 KiB); egy ikon 5 149–14 536 bájt.
- `assets/loxone-home/README.md`: használat, teljességellenőrzés, importfolyamat,
  memóriaigény és a még hátralévő integráció világos elhatárolása.
- `tools/import_home_icons.py`: RGBA/alfa és méret ellenőrzése, import,
  ellenőrzőösszeg és eredet rögzítése. `--check` kész részhalmazhoz;
  `--complete` a teljes katalógushoz.
- A Master workflow külön `home-icons` jobja minden jövőbeli futáskor ellenőrzi
  a készlet teljességét, méretét, színmódját, átlátszóságát és ellenőrzőösszegét.

Ellenőrzés:

- `python3 tools/import_home_icons.py --complete`: 64/64 sikeres.
- Mind a 64 valódi 96×96 PNG vizuálisan ellenőrizve világos és sötét alapon,
  csoportonként. Nincsenek csak nagy méretben ellenőrzött ikonok.
- A korábbi C/UBSan modelltesztek eredménye változatlan; e checkpointban futási
  C-kód nem változott. A képek még nem firmware-be ágyazott erőforrások.
- A korábbi célhardver-build eredménye nem igazolja a még hiányzó v3 UI működését.

Folytatás:

1. A kész ikonokat ne generáld újra. Igazságforrás a katalógus és a `--complete`.
2. A 64 ikon közös, korlátos SD-erőforráscsomagba csomagolása következik.
   A jelenlegi 32 PNG/csomag korlátot ne emeld korlátlanul; célzott speciális
   csomagszabály és tömör runtime-katalógus kell. A promptokat tartalmazó
   szerzői katalógus ne töltődjön a firmware memóriájába.
3. A fogyasztói és Otthon oldal világos látványterve már elfogadva. Következő
   grafika: tényleges, szöveg/kártya nélküli 1280×800 hátterek, helyi Loxone-logó;
   majd sötét változat és a 05/06 témák külön egyeztetendő tervei.
4. Technikai nyitott feladat: atomi SD-konfigurációmentés, webes szerkesztők,
   aktív oldali állapotok, tényleges LVGL-oldalak és Loxone-vezérlés. A képkészlet
   önmagában még nem választható ki a jelenlegi webes felületen.
5. Slave: stabil identitás/profil, BLE, inputkapu és a jelenléti állapotgép
   runtime bekötése továbbra is nyitott. Nincs PR és nincs release.

## Checkpoint 2026-10-20 / 7 – folytatás új beszélgetésben

Előzmény visszaállítva: a „v3 Loxone kártyák - 1” beszélgetés félbeszakadt,
a helyi munkapéldány és a távoli `feature/v3-loxone-home` ág azonos `f9f10c7`
checkpointon állt. A D01–D13 döntések változatlanok; az ikonok nem készültek újra.

Elkészült:

- Külön, jelszómentes `/sdcard/loxone-home.json` tároló, 32 KiB korláttal,
  tmp/bak tranzakcióval, fsync és validált boot-helyreállítással. A kapcsolat
  titkosított `loxone.json` fájlja változatlan marad.
- Háttérfeladatba sorolt mentés; az élő konfiguráció és revízió csak sikeres
  SD-tranzakció után változik. Foglalt állapot, konkrét validálási hiba és SD-hiba
  külön visszajelezve. A JSON mélysége már a cJSON előtt korlátozott.
- GET/POST `/api/loxone/home`, a meglévő webhitelesítéssel és kérésfejléccel,
  csak Loxone módban. HTTP/MQTT/Slave nem regisztrálja.
- A meglévő Loxone weboldalon külön 12 fogyasztós és 12 Otthon-kártyás szerkesztő:
  64 ikon, név, állapotpárosítás, skálázás, átlagteljesítmény, javaslatkapcsoló;
  érték/pulse/switch, külön vezérlési UUID, egység, tizedesek és figyelmeztetések.
- Külön helyiség-/funkciószűrők; szűréskor a mentett párosítás megmarad.
  Háttérfrissítés nem írja felül a helyi szerkesztést. Azonos elemkulcs a két
  oldalon külön DOM-névtérben él. Mentéskor nincs kapcsolatjelszó a kérésben.
- A firmware-be csak a tömör id/hu/en ikonlista kerül; a promptokat tartalmazó
  szerzői katalógus továbbra sem kerül a készülék memóriájába.

Ellenőrzés:

- Valódi C/UBSan tárolóteszt: teljes 12+12 körút, hiányzó/sérült fájl, backup,
  fsync és mindkét rename hibája, reboot a csere közben, sérült főfájllal
  megszakadt újabb mentés, változatlan hitelesítési fájl.
- Tényleges C HTTP-kezelők: hitelesítés, fejléc, méretkorlát, darabolt/megszakadt
  fogadás, hibás séma, záró szemét, mód szerinti elérés, 202 háttérmentés.
- Tényleges webes JS: 64 ikon, fogyasztóátalakítás, pulse/switch külön cél,
  szűrés, háttérfrissítés, szerkesztésmegőrzés, foglalt és hibás mentés.
- A Loxone protokollteszt a profilkérelmek sorba állítását és a sikeres/hibás
  háttérmentés revízióját is ellenőrzi.
- A teljes natív regresszió a korábbi környezetkorlát feloldásával futtatható:
  `CPATH=/workspace/scratch/85d386cc142b/test-deps
  LIBRARY_PATH=/workspace/scratch/85d386cc142b/test-deps python3 tests/run_tests.py`.
  Ez a helyi függőségútvonal, nem a projekt új előfeltétele; CI továbbra is a
  rendszeres libmbedtls-dev csomagot használja.

A webes szerkesztő már menthető konfigurációt ad. A kijelzőn lévő két új oldal,
SD-erőforráscsomag, élő értéktár és vezérlés, asszisztens, Slave-profil/BLE/inputkapu,
05/06 új témák továbbra is külön integrációs feladatok; ezek még nem kész funkciók.


A 7. checkpoint zárása:

- A teljes `tests/run_tests.py` regresszió **sikeres** a felsorolt helyi
  függőségútvonalakkal; a meglévő OTA/BLE/energiatéma/Loxone tesztek is átmentek.
- `tools/prepare_web.py --check` és `git diff --check` sikeres.
- A webes teszt a firmware-ből minimálisan szerializált pulse kártya újbóli
  betöltését és értékké alakítását, illetve a 12-es korlátot/törlést is ellenőrzi.
- Valódi Chromium-próba előkészítve, de a környezetben nincs telepített böngészőbináris;
  ezért képernyőkép vagy valós böngészős megfelelés nincs állítva.
- Helyi implementációcommit: `df3df8a`; a következő checkpoint-dokumentációs
  commit is helyben marad, amíg a távoli feltöltésre engedély nem érkezik.
- A `git push origin feature/v3-loxone-home` műveletet az automatikus
  jóváhagyásellenőrzés elutasította: a távoli cél és a módosított fájlok feltöltésére
  szóló felhatalmazást nem tekintette igazoltnak. A távoli fej `f9f10c7` maradt.
  Nem történt kerülőutas connectoros feltöltés. A pontos célra felhasználói
  megerősítés szükséges a folytatáskor; azt követően ezen az ágon push,
  majd Master/Slave/szimulátor CI ellenőrzése a következő lépés.
- Teljes ESP-IDF nincs helyben, ezért az új kód firmware-/OTA-mérete és hardveres
  viselkedése még CI-/panelpróbát igényel. Nem készül PR vagy release ebben a fázisban.

Következő önálló technikai blokk: célzott SD-erőforráscsomag a 64 ikonhoz,
minimális élőállapot-tár a meglévő Loxone-adatfolyamon, majd az elfogadott két
kijelzőoldal (házérintés/lefelé húzás, X és pontos téma-visszatérés).
A még hátralévő termékegyeztetések: sötét oldaltervek és a 05/06 témák külön terve.

## Checkpoint 2026-10-20 / 8 – erőforráscsomag, élő oldalak és vezérlés

A felhasználó 2026-10-20-án folyamatos engedélyt adott a repóba történő feltöltésre.
A 7. checkpoint `ef5e92bed468aa467ce8f79f40f5a109ebe59c02` alatt a távoli ágon van;
a GitHub-kapcsolaton keresztüli feltöltés fájlfája bájtra egyezett a helyivel.
Master, Slave és Windows-szimulátor CI-je sikeres. A helyi két korábbi commitot
`checkpoint/local-home-editors-b3f0804` ág őrzi; a fejlesztési ág az egyező távoli
fához lett igazítva. Újabb feltöltéshez ne kérj ismét felhatalmazást.

Elkészült:

- Közös `loxone-home` SD-erőforráscsomag a meglévő témakezelő ZIP telepítésével,
  ellenőrzésével, frissítésével és törlésével. Nem lapozható/aktiválható téma,
  nem foglal helyet a témák sorrendjében. A weben külön erőforrásként látszik.
- Célzott `kind=loxone-home-resources`, 64 egyedi, 96×96 ikon, két pontos
  1280×800 háttér és helyi logó. Normál téma továbbra is 32 PNG / 34 ZIP-bejegyzés;
  a speciális, külön validált csomag kemény felső korlátja 72 bejegyzés.
- Determinisztikus csomagoló; szerzői promptok és nagy generálási eredetik nélkül.
  Kész csomag az RGB565-optimalizálás után 2 639 955 bájt (újrakészítéskor ellenőrizendő). Csak a kiválasztott
  oldal legfeljebb 12 ikonját, egy hátterét és logóját tölti elő; az ismétlődő ikon
  csak egyszer szerepel a memóriában. Az előző energiaoldal képei felszabadulnak.
- Nappali háttér a jóváhagyott szoba rekonstrukciójából, éjszakai változat azonos
  berendezéssel. A felhasználó még nem hagyta jóvá a sötét oldal végső megjelenését.
  A háttér csak kép; a kártyák, feliratok, gombok, értékek és ikonok valódi LVGL-elemek.
- A két Master-oldal: házérintés / lefelé húzás, jobb felső X, 4×3 rögzített geometria,
  hiányos sor középen. Bezáráskor pontosan az előző témát tölti vissza; nem lapoz.
- A gyári témák `home_hotspot` mezővel rendelkeznek. Régi SD-s gyári témákhoz
  kompatibilis érintési terület és ház-/otthonérték-felismerés is van. Saját,
  illusztrált csomagnál az explicit `home_hotspot` a támogatott szerzői felület.
- A Loxone delta adatfolyam 24 számot tart meg (nem képeket/widgeteket), hogy egy
  régóta változatlan mérés megnyitáskor se vesszen el. Értéktár 400 bájt a natív
  környezetben, képességjelzőkkel együtt. Külön hálózati kapcsolat nincs.
- Nulla mérés és hiányzó/hibás érték külön állapot; 1 kW alatt W. A 0 fogyasztású
  ikon valódi luminanciaalapú szürke ARGB-változatot kap, változatlan alfával.
- Korlátos pulse/On/Off parancsút a már hitelesített WebSocketen, kizárólag
  konfigurált, támogatott és nem védett objektumokra. Switch állapot az adott
  objektum `active` állapota kell legyen. Kezdetben Switch; pulse célként még
  Pushbutton és TimedSwitch. Más típus vagy vizuális jelszó nem kerül megkerülésre.
- Állapot nélküli Pushbutton is szerepelhet a katalógusban, külön parancscélként;
  üres mérési UUID-ja nem jelenik meg a mérési listában. A szülő védettsége öröklődik.
- Parancssor mélysége 1, érvényessége 2 másodperc. A revízió és kapcsolatazonosító
  ellenőrzött. Pulse egyszer küldődik, reconnect után nem ismétlődik. Kapcsoló
  200 nyugtától még nem vált: tényleges állapotot vár legfeljebb 5 másodpercig.
- Kapcsolatvesztés/lejárt kapcsolat/hiányzó érték/nem támogatott cél esetén vezérlés
  tiltva. A webes szerkesztő is csak a támogatott, nem védett célokat kínálja fel.
- Diagnosztika X-szel zárható, lefelé húzás nem zárja. Nyitott párosítási ablak
  tényleges státuszához kötött `#0082FC` háttér és helyi betűkészletes Bluetooth-jel.
- HU/EN kijelzőfeliratok, magyar tizedesvessző; a felhasználó nevei/egységei változatlanok.
- Windows-szimulátorban külön `--home-demo` mód és ugyanazon UI API-t helyettesítő
  demóadatok; a valódi hálózati út tesztje nem e fixture-t használja.

Ellenőrzések:

- Új natív C/UBSan értéktár/parancsépítő teszt: előjelek, skálázás, nulla, NaN,
  formahibás tábla atomi elutasítása, típus/UUID/állapot korlátok és örökölt védelem.
- Tényleges RSA/AES/HMAC protokollteszt: parancs egyszeri titkosított küldése,
  állapot-visszajelzés, nyugta érték nélkül, timeout, tiltás, sorürítés reconnectnél,
  másik munkamenetből származó kérés és elöregedett kapcsolat.
- Tényleges C és böngészős JS csomagteszt: 64 ikon, hiányzó/felesleges fájl,
  normál csomag változatlan korlátja, SD telepítés/frissítés/törlés, nem választható
  erőforrás, aktív képek korlátja és deduplikálása, régi nézet tulajdonjoga.
- A teljes natív regresszió sikeres. Az utolsó UI-simítás után célzott LVGL-render
  újra ellenőrizendő és az eredmény e checkpoint végén rögzítendő.
- Helyben működő valódi LVGL 9.5.0 build:
  `/workspace/scratch/77c8587e08fc/build-deps/cmake/data/bin/cmake`,
  forrás `/workspace/scratch/77c8587e08fc/lvgl`, build `.../preview-build`.
  Futtatáskor `ZOLEE_TEMPLATE_ROOT` egy másolt témakönyvtár;
  `ZOLEE_HOME_RESOURCE_ZIP` a csomagolóval készített `loxone-home.zip` teljes útja.
- Nincs fizikai panelpróba. Az új checkpoint tényleges firmware/OTA mérete és
  Windows-buildje a friss távoli CI után állítható; a 7. checkpoint sikere nem
  tekinthető az új megjelenítők hardveres igazolásának.

Következő feladatok (továbbra sem kész v3):

1. Sötét oldalterv egyeztetése a tényleges render alapján; 05/06 új témák külön tervei.
2. Slave stabil identitás/profil, védett BLE-átvitel, NVS, jelenlét/érintés/PWM runtime.
   A korábbi közös állapotgép és a saját nappali/éjszakai fényerők megőrzendők.
3. Energiaasszisztens: csak Master/Loxone, 100% SOC + tartós tényleges PV-többlet,
   opt-in fogyasztók és összevethető éjszakai alapfogyasztás. Automatikus kapcsolás nincs.
4. Új firmware méret/PSRAM/DMA ellenőrzés, hardveres próbalista és végső HU/EN dokumentáció.
5. v3.0.0 végső kiadási szabály és PR csak az összes vállalt pont elkészülte után.

A 8. checkpoint helyi zárása:

- Az utolsó natív LVGL-futtatás sikeres: tényleges házérintés, X, majd rögtön lefelé
  húzás; négy világos/sötét oldalkép, valódi szürke ikon, parancsvárakozás,
  kapcsolatvesztés, képfelszabadítás és azonos energiaelrendezésre visszatérés.
- Ellenőrzött 640×400 renderképek: `assets/loxone-home/previews/` négy PNG-je.
  Ezek a tényleges LVGL-render kicsinyítései, nem generált UI-mockupok.
- A kártyák erősebb hátteret, a kapcsolók visszajelzésvezérelt fehér kapcsológombot,
  a számok HU tizedesvesszőt kaptak. Megszűnt a lábléc okozta túlgördülés.
- Az X eseménye már felengedett érintéskor jön; nem állít be új wait-release
  állapotot, így a következő gesztust nem nyeli el. Gesztus közben továbbra is
  a rendes gesztuskezelő várja meg a felengedést.
- Az új csomag a Master teljes USB-csomagjának SD-könyvtárába is bekerül.
  A csomagolás/regresszió célzott tesztje ezt és a 64 ikont ellenőrzi.
- A teljes natív regresszió, új csomagtesztek, valódi Loxone protokollteszt,
  webes JS, USB/OTA-csomagolási teszt és a natív LVGL-build/render sikeres.
- Távoli CI: a mostani új forráshoz a feltöltés után új futás szükséges. Addig
  nincs hardveres firmware-méret/FPS-állítás, nincs PR és nincs release.

Feltöltési javítás az „Invalid conversation body” hiba után:

- Minden ikon 96×96, mindkét háttér 1280×800, a négy valódi LVGL-előnézet 640×400.
  Nem került nagy generálási eredeti a feltöltendő fájlok közé.
- A panel `CONFIG_BSP_LCD_COLOR_FORMAT_RGB565=y` beállításának megfelelően a két
  háttér alsó 3/2/3 színbitét elhagytuk. Mind a 2×1 024 000 képponton ellenőrzött
  az eredeti és az optimalizált kép RGB565-értékeinek egyezése; a felbontás változatlan.
  Világos: 1 934 717 → 1 067 208 bájt; sötét: 1 677 635 → 879 208 bájt.
- A csomag 4 305 565 → 2 639 955 bájt; a tényleges C/JS csomagvalidálás és
  SD-telepítés/frissítés/törlés tesztje az optimalizált képekkel is sikeres.
- A hat képfájl külön GitHub-blobként sikeresen feltöltve; a forrásfák kisebb
  kérésekben kerülnek fel. A hiba pontos szerveroldali oka nem bizonyított.

A feltöltés lezárása: `e62c0c2e34024f3dfedbb8e851e34e6e8e3d8648` a távoli ágon;
a teljes fájlfa helyivel ellenőrzött SHA-ja `e6434ad9a5e330910587491e71f874f081093d3d`.
Mind a hat kép és mind az 55 szövegfájl sikeresen felkerült, kisméretű kérésekben.
Az `e62c0c2` összes távoli ellenőrzése sikeres:
[Master regresszió/LVGL/ikonok/csomagolás/két firmware](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39475070978),
[Windows főszimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39475070936),
[4B Linux/Windows szimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39475070882).
Master app: `0x2db6e0` bájt, 8 MiB app-partíción 64% szabad; 4B app:
`0x31d810` bájt, 12 MiB app-partíción 74% szabad. Mindkét USB/OTA-csomag és
a valódi buildkimenetekkel ellenőrzött telepítő elkészült. Futási heap/FPS/panelmérés nincs.

## Checkpoint 2026-10-20 / 9 – Slave érintéskapu és runtime

Elkészült:

- `app_slave_display_input.c/.h`: a BSP eredeti LVGL-read callbackjét egyszer hívó,
  a nyers eseményt még a widgetek előtt szűrő kapu. A teljes ébresztő érintés,
  mozgás és felengedés elnyelődik; a következő érintés használható. Nem írja át
  a driver/user adatmutatókat, nem foglal saját heapet. Input törlésekor lekapcsol;
  explicit leválasztáskor visszaállítja az eredeti olvasót. Minden állapota LVGL-zár alatt.
- Közös boot/PIN láthatósági felülbírálás: a saját nappali/éjszakai fényerő
  megmarad, nincs kitalált jelenlét és nincs profilfrissítés miatti ébresztés.
  A már lenyelt érintés közben felugró PIN sem kap téves felengedést/kattintást.
- A fő panel Slave runtime-ja és a 4B runtime a közös állapotgépet használja,
  közvetlen PWM-célértékkel. Csak friss, csatlakoztatott és nem lejárt minta
  változtat napszakot. Kapcsolatvesztéskor megmarad az utolsó ismert éjszaka.
- Bekötött LVGL-inputkapu mindkét hardveren; érintőkapu nélkül nem sötétülhet
  felébreszthetetlenül a kijelző. A 4B boot közbeni láthatósági jel atomikus.
- A Slave saját fényerőit nem írja felül a Master mérési csomagjának fényereje.
  Alapból továbbra is `blank_at_night=false`: a védett jelenlétprofil még hátravan.

Ellenőrzések:

- Teljes `tests/run_tests.py` regresszió sikeres; modellállapot továbbra is 32 bájt.
- Natív LVGL 9.5 build és tényleges 1280×800 UI-regresszió sikeres: első érintés,
  gesztus, felengedés, második kattintás, PIN, az input törlése/leválasztása.
- Ugyanez a valós kaputeszt a 720×720 4B szimulátorban is sikeres; a meglévő
  energia/PIN/diagnosztikai képek és tesztek megmaradtak. Windows CI-ben is fut.
- A tényleges főpaneles Slave runtime helper PWM-adaptere ellenőrzött: saját
  83/17%, sötét 0%, PIN-láthatóság, lejárt/kapcsolat nélküli éjszaka megőrzése,
  friss nappali visszatérés és hiányzó érintő melletti láthatóság.
- Webgenerátor-ellenőrzés, 64/64 ikonkatalógus és whitespace-ellenőrzés sikeres.
- E checkpoint ESP-IDF/Windows CI-je a következő feltöltés után ellenőrzendő.
  A szimulátor nem hardveres panelpróba, nincs PR és nincs release.

Következő önálló technikai blokk: legfeljebb három Slave stabil, párosított
identitása; Masteren tárolt név/jelenlét-UUID/timeout/sötétítés profil; a jelenlét
minimális, hitelesített BLE-adatútja és célzott NVS-mentése. A Wi-Fi/OTA profilt
ne terjesszük ki törő módon, és ne gyengítsük az SMP/MITM/bonding ellenőrzését.
A kártyaoldalak sötét megjelenése és a 05/06 témák terve továbbra is egyeztetendő.
