# v3.0.0 – Loxone otthonfunkciók fejlesztési és folytatási terve

## Állapot és folytatás

- Fejlesztési ág: `feature/v3-loxone-home`.
- Kiinduló főág: `04eef87d1210356db334fa6790715521d4a31e38` (2026-10-20).
- A #15 Slave/OTA PR már egyesítve; a meglévő javítások megőrzendők.
- Cél: dokumentált, tesztelt v3.0.0; PR kizárólag az összes vállalt fejlesztés elkészülte után.
- Állapot: követelmények egyeztetése folyamatban; első konfigurációs modell és célzott teszt elkészült, UI/runtime integráció még nincs.
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
