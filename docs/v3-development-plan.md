# v3.0.0 – Loxone otthonfunkciók fejlesztési és folytatási terve

## Állapot és folytatás

- Fejlesztési ág: `feature/v3-loxone-home`.
- Kiinduló főág: `04eef87d1210356db334fa6790715521d4a31e38` (2026-10-20).
- A #15 Slave/OTA PR már egyesítve; a meglévő javítások megőrzendők.
- Cél: dokumentált, tesztelt v3.0.0; PR kizárólag az összes vállalt fejlesztés elkészülte után.
- Állapot: konfiguráció/SD/webes szerkesztő, közös erőforráscsomag, külön fogyasztói háttér, lapozható/címezhető Otthon-oldalak és korlátos vezérlés elkészült. A közös Slave érintéskapu/fényerő-runtime, a jelenlétprofil/BLE/NVS adatút és a webes Slave-beállító bekötve. A helyi energiaasszisztens és az igazolt éjszakai referencia bekötve; a 05/06 új megjelenése elfogadott és implementált. A felhasználói próba utáni F01–F10 javításcsomag ellenőrzése és a fizikai zárás folyamatban.
- Folytatáskor először olvasd végig ezt a fájlt, az AGENTS.md-t és a git diffet.
- A felhasználó bizonytalan termékdöntéseit egyesével kell megkérdezni.
- Minden checkpointnál frissítendő: kész feladatok, tesztek és korlátaik, nyitott kérdés, következő konkrét lépés.
- Nincs PR vagy release. Az implementált és szoftveresen ellenőrzött részeket a checkpointok külön jelölik; fizikai megfelelőséget nem állítunk.

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
| D14 | A Ház fogyasztói és az Otthon-kártyák külön nappali/éjszakai hátteret kapnak. A világos lábléc Loxone-logó melletti szövegét kontrasztos, egységes panel tegye olvashatóvá. |
| D15 | Az Otthon több, szerkesztőben hozzáadható és szabadon elnevezhető oldalból álljon. Oldalanként legfeljebb 12 kártya; fejlécben az oldal címe, több oldalnál alul oldalpontok. Jobbra/balra gesztus kizárólag e lapok között váltson, ne az energiatémák között. |
| D16 | Energiakör: napállás jelölése 00:00–24:00 vízszintes idővonalon, napkelte/napnyugta és aktuális idő jelölővel; a külön látványterv jóváhagyandó. |

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

- D15: több elnevezhető oldal; kezdeti erőforráskorlát 8 oldal × 12 kártya. A korábbi egyoldalas schema 1 profil automatikusan egy „Otthon” oldalra migrálódik; új tartós/API formátum schema 2.
- Oldalak hozzáadása, törlése, címadása és sorrendje a szerkesztőben; az oldalankénti kártyaszám külön ellenőrzött. Üres oldal is szerkeszthető és megnyitható.
- Az oldalváltás csak az aktív oldal legfeljebb 12 ikonját/widgetjét tartsa memóriában; a többi lap minimális numerikus állapotát megőrzi a delta adatfolyamhoz. Oldalváltás nem indíthat kártyaparancsot.
- A többoldalas konfiguráció külön, 128 KiB-ra korlátozott tárolót/API-kérést kap; hitelesítési adatok és Wi-Fi/OTA út változatlan.

- Kizárólag Master és Loxone mód.
- Kártyatípusok: érték, pillanatnyi gomb (`pulse`), On/Off kapcsoló.
- Érték: név, ikon, állapotpárosítás, mértékegység, skálázás, figyelmeztetési határok/színek.
- Gomb/kapcsoló: tényleges vezérlési objektum párosítása szükséges; az állapot UUID nem azonos automatikusan a parancs cél UUID-val.
- Pulse egyszeri érintésre egyszer küldődjön; nincs kapcsolat utáni automatikus újraküldés.
- Kapcsoló a visszaérkező Loxone-állapotot mutatja; hibás/lejárt állapotnál ne legyen hamis siker.
- Kapcsolatvesztéskor vezérlés tiltva, felhasználó számára érthető visszajelzéssel.
- Fentről lefelé húzás nyitja, jobb felső X zárja, korábbi téma visszaáll.
- Csak legalább egy kártyánál és elérhető SD-s speciális csomagnál nyitható.
- Saját színes, otthon hangulatú, jól olvasható hátterek; SD-ről és témakezelővel frissíthetően. A fogyasztói oldal külön háttérpárt használ (D14).
- Oldalanként legfeljebb 12 kártya, legfeljebb 8 oldalon (D15).
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
- [x] Korlátos konfigurációs/adatmodell, visszafelé kompatibilis parse/mentés, érzékeny adatok védelme.
- [x] Fogyasztói és kártyás webes szerkesztők; valódi JS/API-tesztek.
- [x] Speciális csomagok/assetkatalógus; pontos képméretek és SD-kezelés.
- [ ] Mindkét új oldal és navigáció, frissesség, terhelési mérések.
- [ ] Pulse/kapcsolók tényleges parancs- és állapotútja.
- [x] Energiaasszisztens szabályai és témánkénti pozíciói.
- [ ] Diagnosztika X és valós párosításjelzés.
- [x] Slave identitás/profil/jelenlét és mindkét hardver/szimulátor támogatása.
- [x] 05/06 témák jóváhagyott látványterve és elkészült eszközméretű képei.
- [x] Külső témák célzott javítása, ha kis erőforrásból elvégezhető.
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
- Távoli forráscommit: `2924f0e50b595e3df62851af03777caa763a7daa`, helyivel
  ellenőrzött fájlfa: `9aff2d4491dc63d06612b177a8047e945b008c08`.
  [Master teljes regresszió/LVGL/csomagolás/két ESP-IDF firmware](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39477055116),
  [Windows főszimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39477055055),
  [4B Linux/Windows szimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39477055041)
  minden jobja sikeres. Mindkét USB/OTA-csomag és a tényleges binárisokkal ellenőrzött
  USB-telepítő elkészült.
- Master bináris: `0x2dbf60` = 2 998 112 bájt, az előző checkpointhoz +2176 bájt;
  8 MiB app-partíción 64% szabad. 4B bináris: `0x31dee0` = 3 268 320 bájt,
  +1744 bájt; 12 MiB app-partíción 74% szabad. A statikus/tesztelt korlátok nem
  helyettesítenek futási DMA-/PSRAM-/FPS- és panelmérést. Nincs PR és nincs release.

Következő önálló technikai blokk: legfeljebb három Slave stabil, párosított
identitása; Masteren tárolt név/jelenlét-UUID/timeout/sötétítés profil; a jelenlét
minimális, hitelesített BLE-adatútja és célzott NVS-mentése. A Wi-Fi/OTA profilt
ne terjesszük ki törő módon, és ne gyengítsük az SMP/MITM/bonding ellenőrzését.
A kártyaoldalak sötét megjelenése és a 05/06 témák terve továbbra is egyeztetendő.

A következő blokk előkészített műszaki szempontjai (még nem implementáció):

- A Master az SMP által hitelesített/bondolt peer feloldott identitásához kösse
  a profilt, ne a hirdetési privát címhez vagy a Slave önbevallott azonosítójához.
  A NimBLE műveletek maradjanak a host taskban; a web csak korlátos snapshotot kapjon.
- A meglévő 102 bájtos Wi-Fi/OTA profil maradjon kompatibilis; az opcionális
  jelenlét-karakterisztika is titkosítást, MITM/bondot és 16 bájtos kulcsot kérjen.
  Régi Masterrel a biztonságos alap a kikapcsolt éjszakai sötétítés.
- Az aktív szint mellé monoton impulzusszámláló szükséges, különben két BLE-poll
  közé eső rövid 1→0 jel elveszhet. Új kapcsolat első csomagja csak bázisérték:
  régi impulzust nem szabad visszajátszani. Tartós aktív szint ébren tart,
  érvénytelen/kapcsolat nélküli adat nem hosszabbít timeoutot.
- NVS-be csak megváltozott, validált profil kerüljön, külön háttérfeladatból;
  az ismételt jelenlétminták/érintések ne írjanak flash-t és ne indítsanak újra.

## Checkpoint 2026-10-20 / 10 – lapozható Otthon és külön fogyasztói háttér

A felhasználó új kérései D14/D15-ként bekerültek a tervbe; e blokk ezeket valósítja meg.

Elkészült:

- Schema 2: legfeljebb 8, saját címmel/azonosítóval rendelkező oldal, oldalanként
  12 kártya (összesen 96). A korábbi schema 1 kártyalistája automatikusan egy
  `home` / „Otthon” oldalra kerül. Az üres lap is érvényes. A kártyaazonosítók
  minden oldalon együtt egyediek, így a parancssor oldalváltáskor sem kever célt.
- A webes szerkesztőben oldal hozzáadása, címadása, kiválasztása, törlése és
  átrendezése; minden lap saját kártyái külön megmaradnak. A háttérfrissítés és
  katalógusszűrés nem írja felül a helyi szerkesztést. Nincs HTML-ként értelmezett cím.
- A kijelző fejlécében az oldal címe; több lapnál alul aktív/inaktív pontok.
  Jobbra/balra húzás körbefordulással lapoz. A témaválasztás változatlan marad;
  X-szel a korábbi energiaelrendezés áll vissza. Gombon induló húzás sem küld parancsot.
- Egyszerre csak egy lap legfeljebb 12 widgetje/ikonja, egy háttér és logó él.
  A rejtett lapok numerikus delta állapotai megmaradnak. Natív profil 32 112 bájt,
  teljes értéktár 1824 bájt; UI-pillanatképek/értéktár PSRAM-ban. A külön SD/HTTP
  JSON-korlát 128 KiB; a hitelesítési konfiguráció és az OTA/Wi-Fi út nem nőtt.
- Fogyasztói oldal: új, külső ház-/kertnézet nappal/éjjel. Otthon: korábbi beltéri
  háttérpár. Mind a négy RGB PNG pontosan 1280×800; 64 ikon továbbra is 96×96.
  Az új képek beépített image_gen eszközzel készültek, RGB565-optimalizálással;
  nagy eredetik nincsenek a repóban, szerzői prompt/meta nem kerül az SD-csomagba.
- A v2 erőforráscsomag 70 fájl / 4 796 478 bájt; 4 háttér, 64 ikon és logó.
  Csak e speciális csomag összesített dekódolt korlátja 20 MiB; normál témák
  12 MiB korlátja, a ZIP 72-es határa és a 10 MiB LVGL-cache változatlan.
  Régi v1 csomag továbbra is betölthető közös háttérrel; külön fogyasztói képhez
  a v2 SD-csomag is frissítendő. Ez a Master USB-csomagba is bekerül.
- Fedő világos/sötét lábléc a logóval és a felirattal: a kép tónusától független
  kontraszt. A harmadik kártyasor és a lábléc nem fedik egymást.
- Háromlapos szimulátordemó (Földszint / Kert / üres Vendégszoba), ugyanazzal a UI-val.

Ellenőrzések:

- Teljes natív regresszió sikeres. C/UBSan: 8×12 körút, egyedi lap-/kártya-ID,
  per-page határ, címvalidálás, régi profil migrációja, üres oldal, atomi elutasítás,
  JSON foglalási hibák, 96 numerikus állapot és az utolsó lap parancscélja.
- Valódi SD-tároló: 96 kártyás mentés/betöltés, meglévő FAT/fsync/rename hibainjektálás;
  HTTP: hitelesítés, strict JSON, 128 KiB korlát, aszinkron mentés.
- Valódi webes JS: oldal hozzáadása/címadása/mozgatása/törlése, lapok közötti
  szerkesztésmegőrzés, 8 oldalas és 12 kártyás korlát, régi pulse profil migrációja.
- Tényleges LVGL: mindkét gesztusirány, körbefordulás, gombon induló húzás parancs
  nélkül, második lap helyes parancs-ID-ja, üres lap, címsor, aktív lap képei,
  pontok több lapnál / elrejtés egy lapnál, 12 kártya és fedő lábléc geometriája.
- C/JS csomagellenőrzés: v2 külön háttérpár, hiányzó/rossz háttér elutasítása,
  v1 kompatibilitás; normál csomag korlátja megmarad. Webgenerátor és 64 ikon ellenőrizve.
- Valódi 640×400 előnézetek a repó `assets/loxone-home/previews/` könyvtárában;
  világos/sötét és második/üres/12 kártyás lap is vizuálisan ellenőrizve.
- Távoli forráscommit: `ab2aaea253eeb68fe16843c86c81282de439c5e3`, helyivel
  ellenőrzött fájlfa: `69f6ce3fcd0270082a6ac2cda26e9e0168474942`.
  [Master regresszió/LVGL/ikonok/csomagok/két firmware](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39482302189),
  [Windows főszimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39482302204),
  [4B Linux/Windows szimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39482302150)
  minden jobja sikeres. Mindkét teljes USB/OTA-csomag és a tényleges binárisokkal
  ellenőrzött telepítő elkészült; a v2 erőforrások a Master USB-csomagban is benne vannak.
- Master: `0x2ddf00` = 3 006 208 bájt (+8096 bájt a 9. checkpointhoz), 8 MiB
  app-partíción 64% szabad. 4B: `0x31dee0` = 3 268 320 bájt, mérete változatlan;
  12 MiB app-partíción 74% szabad. Ez méretellenőrzés, nem bájtraazonossági állítás.
- A webes JS regresszió Node/DOM-illesztővel futott, tényleges Chromium-képernyőteszt
  nincs (a környezetben nincs használható böngészőbináris). A megjelenített kijelzőképek
  valódi LVGL-renderek. Futási paneles DMA-/PSRAM-/FPS-próba még szükséges.
  Nincs PR és nincs release.

Következő blokk: a 9. checkpoint végén részletezett Slave identitás/jelenlétprofil/
védett BLE/NVS adatút; energiaasszisztens és a külön egyeztetendő 05/06 tématervek.

## Checkpoint 2026-10-20 / 11 – Slave jelenlétmodell, első rész

A megszakadás után a távoli fej `250ded8040135eef6914a3fd5351dbed3d54204e`
és a 10. checkpoint ellenőrizve. A korábbi, folyamatos feltöltési engedély érvényes.
A helyi futtatókörnyezet nem érhető el; e változás ellenőrzése GitHub CI-ben történik.

Elkészült és az alábbi CI-futásokkal ellenőrizve:

- `app_slave_presence.c/.h`: három stabil, párosított identitáshoz kapcsolható,
  név/jelenlét-UUID/timeout/sötétítés profil tiszta modellje. A bemenet normalizált
  public vagy random-static identitás; privát hirdetési cím nem fogadható el.
  A modell nem végez SMP-t: a későbbi host adapter köteles a valódi
  encrypted/authenticated/bonded/16 bájtos ellenőrzés után a peer_id_addr-t átadni.
- Korlátos UTF-8 név (80 bájt), Loxone 35 karakteres állapot-UUID,
  1–86400 s timeout, alapból kikapcsolt sötétítés. Negyedik eszköz nem szorít ki
  profilt. Ismételt regisztráció nem írja át a nevet; változatlan szerkesztés
  nem kér mentést. A modell nem ír NVS-t és nem foglal heapet.
- Minden WS-delta felfutó élét megtartó impulzusszámláló, külön aktív szinttel;
  több Slave ugyanazt a forrást is használhatja. NaN/negatív/érvénytelen érték
  és adatkapcsolat-vesztés nem érvényes aktivitás.
- Új, opcionális 1006 karakterisztikához tervezett 20 bájtos wire-formátum,
  revízióval, sorszámmal és impulzusszámlálóval. Nincs benne Wi-Fi, PIN vagy
  fényerő; a régi 102 bájtos 1004 profil és SMP-ellenőrzés változatlan.
  Ez a checkpoint még nem regisztrál új GATT-karakterisztikát.
- Fogadómodell: szigorú, atomi dekódolás; ismételt/regresszív/ambiguus
  sorszám, számláló vagy profil elutasítása; modulo-2^32 átfordulás.
  Új kapcsolat/profil és 15 s csend utáni első csomag csak számlálóbázis.
  Lejárt/csatlakozás előtti impulzus nem játszódik vissza. A függő impulzusnak
  külön időbélyege van; inaktív heartbeat nem fiatalítja meg a régi eseményt.
- A meglévő közös Slave-fényerőállapotgéphez illesztő: aktív szint tart,
  rövid impulzus egyszer ébreszt, érvényes lefutó él egyszer hosszabbít;
  profilcsere, invalid adat és disconnect nem ad új timeoutot.
  A saját fényerők/napszak/PIN/inputkapu megmaradnak. A profil disconnectkor
  a memóriában megmarad; igazoltan régi Master külön legacy jelzése kikapcsolja a sötétítést.
- A modell mindkét firmware fordítási forráslistáján szerepel, de a tényleges
  BLE/Loxone/UI útba még nincs bekötve. Ez fejlesztési alap, nem kész jelenlétfunkció.
- Új valódi C/UBSan teszt a közös regresszióban: identitás és Unicode határok,
  három profil/célzás, paddingtől független változásjelzés, rövid impulzus,
  stale/replay/reconnect/wrap, hibás wire, helyi fényerő és timeout együtt.

Következő konkrét integráció: külön háttérmentő NVS-adapter és hitelesített
Master eszközlista; a host taskban peer_id_addr alapú célzás és védett 1006
olvasás/értesítés; opcionális Slave-discovery/freshness/snapshot; három jelenlét-UUID
bekötése az egyetlen meglévő Loxone WS deltaútjába; webes szerkesztő és a két
panel LVGL-zár alatti alkalmazása. Ezek továbbra is hátralévő feladatok.
Nincs PR vagy release; a 05/06 látványtervek és az energiaasszisztens is hátravan.

A 11. checkpoint első részének ellenőrzött zárása:

- Távoli forráscommit: `018df757322557bb9359268fc418f0ee67387b46`;
  teljes forrásfa: `5b02e2a3b281b6508ace1994e3893f9963f16b25`.
  Feltöltés közvetlenül a GitHub-kapcsolaton, kis szöveges kérésekkel; helyi
  munkafa/fordítás továbbra sem elérhető. A végső dokumentációs commit nem módosít kódot.
- [Master teljes regresszió/LVGL/ikonok/csomagok/két ESP-IDF firmware](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39493104910),
  [Windows főszimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39493104747),
  [4B Linux/Windows szimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39493105023)
  minden jobja sikeres, mind a végső forráscommitra.
- A teljes natív regresszióban az új C/UBSan teszt is sikeres: tábla **444 bájt**,
  receiver **48 bájt**, csomag **20 bájt**. A tényleges deltafeldolgozó 1→0
  impulzusától a közös fényerőállapotgépig futó teszt is átment.
  Külön ellenőrzött, hogy újabb inaktív heartbeat nem hosszabbítja a még UI-ra
  váró régi impulzus saját 15 másodperces érvényességét; új valódi impulzus igen.
- A napló mindkét firmware-ben igazolja az `app_slave_presence.c` fordítását.
  Master app `0x2ddf00` = **3 006 208 bájt**, 8 MiB app-partíción 64% szabad.
  4B app `0x31dee0` = **3 268 320 bájt**, 12 MiB app-partíción 74% szabad.
  A 10. checkpointtal azonos méret; ez nem bájtraazonossági állítás.
  Az éles hívások még hiányoznak, a nem hívott modulrészeket a linker elhagyhatja.
- Mindkét USB/OTA-csomag, a tényleges buildkimenetekkel futó USB-telepítőellenőrzés
  és a webgenerátor-ellenőrzés sikeres. Az ikonok/képek ebben a blokkban változatlanok.
  Fizikai paneles DMA/PSRAM/FPS-mérés vagy új jelenlét-GATT/NVS integrációs próba nincs.
- Az átviteli szerződés és a következő adapter követelményei:
  [docs/slave-presence-protocol.md](slave-presence-protocol.md).
  A host a tényleges BLE-fogadáskor őrizze meg az impulzus saját idejét;
  pusztán a legutolsó wire-csomagot továbbító UI-sor ezt nem garantálja.
  Rövid receiver-zár és LVGL→receiver zársorrend; BLE host nem vár LVGL-re.
- Nincs PR vagy release. A következő folytatás a fenti NVS/host/GATT/WS/web/UI
  bekötést végezze; a 11. checkpointot ne tekintse kész jelenlétfunkciónak.

## Checkpoint 2026-10-20 / 12 – NVS és célzott, védett BLE-olvasás

A végső CI-eredmények e checkpoint végén rögzítve.
A helyi futtatókörnyezet továbbra sem elérhető, ellenőrzés GitHub CI-ben.

Elkészült forrásként:

- Külön `ble_presence/peers` NVS-tár a Master három profiljához. Kanonikus,
  410 bájtos, verziózott és CRC32-védett blob; csak identitás/profil/revízió.
  Jelenlét, impulzusszámláló és fényerő nem tartós adat. Töltéskor minden mező,
  identitás-egyediség és kanonikus nullázás ellenőrzött; sérült adatból nincs sötétítés.
- Külön 4096 bájtos háttértask és egyhelyes értesítési sor; konfigurációs revízió
  alapján összevont mentés, hiba után legfeljebb 5 másodpercenkénti újrapróbálás.
  NVS I/O nincs az állapotzár alatt vagy BLE/LVGL callbackben. Hibastátusz és
  mentett revízió snapshotolható; változatlan profil és aktivitás nem ír flash-t.
- Publikus, korlátos szolgáltatás-API a későbbi webes szerkesztőhöz; idegen vagy
  ismeretlen identitás nem szerkeszthető, elavult revízió elutasítva. A Master
  bootonként véletlen kezdőrevíziót kap, a profilok saját revíziója tartós.
- Valódi `1006` GATT-karakterisztika: csak READ, titkosítás + MITM + 16 bájtos
  kulcs, és külön `security_authenticated` ellenőrzés minden olvasáskor.
  A profil a feloldott `peer_id_addr`-ból választódik, nem kliensnévből/slotból.
  A hitelesített peerek alapértelmezett, kikapcsolt profilja automatikusan létrejön.
- A Slave az opcionális karakterisztikát a meglévő három energia/idő feliratkozás
  megváltoztatása nélkül fedezi fel. Másodpercenkénti 20 bájtos READ; MTU=23 elég.
  Külön request-generáció védi a késői választ újrahasznált connection handle-nél is.
  4 s olvasási határidő után a késői adat nem alkalmazható, párhuzamos ATT-olvasás nincs.
- Hiányzó opcionális karakterisztika csak a hitelesített, elfogadott régi Master
  beállítása után vált legacy módba. Átmeneti hiba nem legacy jelzés.
  Hibás wire/replay nem törli a fogadó bázisát és nem frissíti az érvényességet.
- Mindkét panel a közös szolgáltatáson keresztül, a saját LVGL-zárja alatt alkalmazza
  a profilt. BLE host nem vár LVGL-zárra; az NVS-worker nem fogja meg I/O alatt
  a receiver-zárat. Az adapter a zár után friss monoton időt vesz, ezért egy korábban
  vett UI-időbélyeg nem dobhatja el a közben érkezett rövid impulzust (külön teszt).
  Saját fényerő, ismert napszak, első érintéskapu és PIN megmarad.
- HTTP/MQTT Master az opcionális karakterisztikán kikapcsolt profilt ad, a korábbi
  Wi-Fi/OTA/energia adatutat változatlanul megtartja.
- Natív tesztek: valódi NVS-worker és GATT access handler; tartós commit,
  hibainjektálás, coalescing, korrupció minden bájton, tiszta reboot, három peer célzása,
  hibás wire, régi callback, timeout, hitelesítés és legacy. A desktop demók BLE-t
  nem indítanak, ezért ott az új szolgáltatás kijelzőadaptere üres illesztő.

- Bekötött jelenlétértékek az egyetlen meglévő Loxone WebSocket 24 bájtos numerikus
  deltaútjába. Csak sikeres hitelesítés/katalógus után él; a kezdő delta az
  enablebinstatusupdate ACK előtt is megőrződik. Hibás üzenet nem táplál állapotot.
- Kapcsolati frissesség a meglévő 45 s-os Loxone heartbeat-határral; disconnect
  invalidál, változatlan állapot-UUID nem évül el önmagában. Új jelenlét-UUID-nál
  a worker a meglévő kapcsolatot újraépíti, így a teljes kezdő állapot is megérkezik.
  Ehhez nem készül második socket vagy külön parancskapcsolat.
- A natív RSA/AES/HMAC protokollteszt a tényleges WS-frissítőből a valódi
  jelenléttermelőig/fogadóig ellenőrzi a rövid 1→0 jelet, login előtti tiltást,
  NaN és hibás méret elutasítását, valamint a disconnect invalidálását.

Hátralévő bekötés: webes eszközlista/szerkesztő és mentési hibajelzés. Alapból továbbra sincs bekapcsolt
sötétítés, és a jelenlétfunkció e blokk után sem tekinthető végig elkészültnek.
Slave oldali, Master-identitáshoz kötött esetleges bootprofil-cache külön mérlegelendő;
a Master konfigurációja tartós, a fogadó profilja disconnect alatt memóriában marad.
Energiaasszisztens, 05/06 látványtervek, fizikai panelpróba és végső v3 zárás hátravan.
Nincs PR vagy release.

A 12. checkpoint ellenőrzött zárása:

- Végső forráscommit: `f33ba7fc333a8007ebbb65d67a8aa61e05c0b5a9`;
  teljes forrásfa: `11730da3099cb47f1374e20b000e3bffc3bfcf29`.
  A helyi környezet nem volt elérhető; a feltöltés kisméretű GitHub-kérésekkel,
  a natív és firmware-ellenőrzés GitHub CI-ben történt.
- A végső forráscommit összes ellenőrzése:
  [Master teljes regresszió/LVGL/ikonok/csomagok/két ESP-IDF firmware](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39497979303),
  [Windows főszimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39497979305),
  [4B Linux/Windows szimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39497979312).
- Valódi C/UBSan: a profilmodell 444 bájt, receiver 48 bájt, GATT-csomag 20 bájt,
  NVS-blob 410 bájt. A valódi háttértask commit/hibás commit/OOM/reboot/CRC minden
  bájtján sérülés/célzott GATT access próbái sikeresek. Változatlan profil és
  jelenlét nem ír NVS-t; sikertelen commit után a régi konfiguráció visszatölthető.
- Tényleges central callbackek: opcionális discovery/READ, legacy, hitelesítés,
  hibás wire, timeout és régi request-generáció ugyanazzal a connection handle-lel.
  A teljes régi energia/idő/statisztika/Wi-Fi/PIN/OTA regresszió megmaradt.
- A valódi RSA/AES/HMAC protokollteszt a közös WS-frissítőből indítja a rövid
  1→0 jelenlétet. Login előtti tiltás, NaN/hibás üzenet és disconnect ellenőrzött.
  Külön szolgáltatásteszt igazolja a 45 s-os Loxone-frissességet és azt, hogy a
  friss BLE-impulzus egy korábban vett UI-időbélyeg mellett sem veszhet el.
- A desktop demó szolgáltatásadaptere üres: a natív LVGL-regresszió a régi felület/
  inputkapu megőrzését igazolja. A valódi új szolgáltatás külön natív tesztben fut;
  a fizikai BLE/LVGL/panel integráció nem lett hardveren kipróbálva.

- A végső forráscommit **minden jobja sikeres**. A két ESP-IDF fordítás naplója
  az új szolgáltatást mindkét firmware-ben, a közös WS-bekötést a Masterben igazolja.
- Master app: `0x2e00c0` = **3 014 848 bájt**,
  a 11. checkpointhoz **+8640 bájt**; 8 MiB app-partíción 64% szabad.
  4B app: `0x31fc60` = **3 275 872 bájt**,
  **+7552 bájt**; 12 MiB app-partíción 74% szabad.
  Mindkét USB/OTA-csomag elkészült, a tényleges binárisokkal végzett USB-telepítő-
  ellenőrzés sikeres. A növekedés új funkciók kódja; bájtraazonossági állítás nincs.
- Webgenerátor-ellenőrzés és 64/64 ikonellenőrzés sikeres. Új képfájl vagy
  generált oldal kézi módosítása nem került a változásba. A végső dokumentációs
  commit kizárólag e tervet, a protokoll-leírást és a 4B fejlesztési státuszát frissíti.
- A Master külön NVS-taskja 4096 bájtos konfigurált vermet és egyhelyes értesítési
  sort használ. Ez nem teljes futási heapmérés. Fizikai BLE/panel/DMA/PSRAM/FPS-próba
  továbbra sincs, a webes beállító és HTTP-adapter még nincs elkészítve.
- Nincs PR vagy release. A jelenlétfunkció adatútja már be van kötve, de webes
  beállítás nélkül az új profilokban a sötétítés kikapcsolt marad.

A következő folytatás konkrét feladata:

1. Master/Loxone webes GET/POST adapter a meglévő hitelesítéssel és kérésfejléccel,
   korlátos JSON-nal. A `app_slave_presence_service_snapshot/update` C API már kész;
   ez még nem HTTP API. Stabil ID az `app_slave_identity_id`-ból; kizárólag már
   ismert, hitelesített identitás szerkeszthető. Elavult revízió ne írjon felül profilt.
2. Eszközlista, név, Loxone jelenlét-UUID szűrhető kiválasztása, timeout és éjszakai
   sötétítés kapcsoló; mentett revízió/hiba/pending visszajelzés. HTTP-taskból nincs
   NimBLE vagy NVS I/O. A generált webfejléceket továbbra is a generátor készítse.
3. Valódi HTTP/JS-regresszió, szerkesztésmegőrzés, három eszköz és ismételt mentés,
   a forrásváltás utáni teljes WS-frissítés integrációja. Az elavult profilok kézi
   eltávolítása/bond-kezelése, ha bekerül a felületre, külön host-parancssoron történjen;
   automatikus profil- vagy kulcskiszorítás nincs.
4. Ezután energiaasszisztens, külön egyeztetendő 05/06 látványtervek és a v3 végső,
   hardveren is ellenőrzött zárása. A korábbi tartós feltöltési engedély érvényes.


## 13. checkpoint – Slave webes eszközlista és profilbeállító

- Master/Loxone módban hitelesített GET/POST `/api/loxone/slaves`, csak a már
  párosított, ismert stabil identitásokhoz. Szigorú 2048 bájtos kéréskorlát,
  pontos mezőkészlet, UTF-8/UUID/timeout ellenőrzés és kétlépcsős revízióvédelem.
  HTTP-taskban nincs NimBLE vagy NVS I/O. Kikapcsolt seed mellett nincs szerkesztés.
- Külön Slave-szakasz a meglévő Loxone oldalon: legfeljebb három név,
  helyiség/funkció szerint szűrhető jelenlétforrás, timeout, éjszakai sötétítés.
  A kiválasztott forrás és a helyi draft katalógus-/háttérfrissítéskor megmarad.
  A beviteli DOM-elemek nem épülnek újra minden pollingnál.
- A 202 csak átvétel. A felület visszaolvasott profil és mentett revízió alapján
  jelez tartós sikert; NVS-hiba, pending és kapcsolatvesztés külön látszik.
  Elveszett POST-válasznál nincs automatikus újraküldés, csak GET-ellenőrzés.
  Célprofil külső módosítása ütközés; másik peer változása nem veszti el a draftot.
- Új valódi HTTP/model UBSan-próbák: három identitás, jogosultság/mód/fejléc,
  darabolt fogadás, korlátos szigorú JSON, teljes uint32 revízió, 409, 202 és
  mentett revízió szétválasztása, cJSON-allokációs hibasorozat. A kiadott JS
  három eszközzel és mentési/kapcsolati hibákkal ellenőrzött.
- A teljes helyi `python3 tests/run_tests.py` sikeres. A generált webfejlécet
  a generátor készítette, `--check` és `git diff --check` sikeres. Új kép nem készült.
- A helyi Chromium letöltése hibás csomagot kapott, a külön felhős böngésző nem
  éri el a helyi HTTP-szervert. Emiatt külön, verzióhoz kötött Chromium CI-job
  került be a teljes kiadott HTML/JS asztali/mobil próbájához és képernyőképeihez.
  A végső forráson ezek a CI-próbák is sikeresek; részletek alább.

Nincs automatikus profil-/bond-kiszorítás vagy webes törlés. Slave NVS-bootcache
nem készült. A fizikai BLE/panel integráció továbbra is ellenőrzendő; a desktop
BLE-adapter továbbra is üres. A következő fejlesztési blokk az energiaasszisztens,
azután a külön jóváhagyandó 05/06 látványterv és a hardveres v3-zárás. Nincs PR/release.


A 13. checkpoint ellenőrzött zárása:

- Végső forráscommit: `a327864bf37c60390cf1437dfec57a9abcfaf6a4`;
  teljes forrásfa: `b860cbb8f19d90c79832aa4d745a92e770744421`.
  A helyi fa a feltöltött Git-fával egyezik. A 12. checkpoint utáni módosítás
  kizárólag kód/web/teszt/dokumentáció; képfájl nem változott.
- A végső forráscommit összes CI-jobja sikeres:
  [Master teljes regresszió/LVGL/ikonok/csomagok/Chromium/két firmware](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39553276366),
  [Windows főszimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39553276425),
  [4B Linux/Windows szimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39553276500).
- A teljes helyi natív tesztcsomag a végső forrással ismét sikeres.
  A HTTP-próba a tényleges handlereket és modellt használja, a szolgáltatás
  snapshot/update illesztése mock; a valódi NVS-worker/BLE/WS külön meglévő
  regresszióban ellenőrzött. Ez nem fizikai HTTP/BLE/panel végponti mérés.
- A teljes kiadott HTML/JS tényleges Chromiumban: három peer, két háttérfrissítés
  alatt megmaradó DOM/fókusz/draft, 202 utáni várakozás és későbbi NVS-visszajelzés,
  szövegként megjelenő HTML-karakterek, asztali és 390 px mobil, vízszintes
  túllógás és JS-kivétel nélkül. A backend itt teszt-HTTP-szerver.
  [Asztali/mobil képernyőképek CI-artifactja](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39553276366/artifacts/12324169928)
  (7 napos megőrzés). A képernyőképek nem kézi vizuális jóváhagyás bizonyítékai.
- Külső profilváltozásnál a tiszta sor státusza is az aktuális mentett revíziót
  követi; nem marad korábbi sikerjelzés egy új függő/hibás mentés mellett.
  El sem küldött, illetve elveszett válaszú kérés külön JS-próbával ellenőrzött.
  NVS-hiba közben a már átvett profil további javítása is menthető.
- Master app: `0x2e4900` = **3 033 344 bájt**, a 12. checkpointhoz
  **+18 496 bájt**; a 8 MiB app-partíción 64% szabad. OTA SHA256:
  `fa3ecc0f899e34ae09fb7d818736683c2565c046bbe6ea355005f770f66ce1fb`.
  Az új webes funkció szándékos kód/adat növekedés, nem formázási eltérés.
- 4B app: `0x31fc60` = **3 275 872 bájt**, méreteltérés **0 bájt**;
  a 12 MiB app-partíción 74% szabad. OTA SHA256:
  `725482bc9dc358d804327759b2ef71994ad9d22a3db17561449cdeded92175c0`.
  A puszta azonos méretből nem állítunk bájtraazonosságot a korábbi binárissal.
- Mindkét USB/OTA-csomag elkészült, a tényleges binárisokkal végzett telepítő-
  ellenőrzés sikeres (normál és kézi boot dry-run). Ez nem tényleges panelre írás.
- Nincs új képfájl, új LVGL-widget, új BLE-task vagy második Loxone socket.
  HTTP-kérés legfeljebb 2048 bájt; a profilmodell és az NVS-blob változatlan.
  Teljes hardveres RAM/DMA/PSRAM/FPS/radio-mérés továbbra sem történt.
- A záró commit csak dokumentációt frissít, `[skip ci]` jelöléssel; az ellenőrzött
  firmware/web/tesztforrások változatlanok. Nincs PR vagy release.

A következő folytatás: energiaasszisztens minimális, oldaltól független
fogyasztóállapot-tára és determinisztikus szabályai; 100% SOC, valódi PV-többlet,
akkukisütésből származó export kizárása, tartósság/tartalék, bezárási-visszavonási
szabály, összevethető éjszakai történet és témánként biztonságos hely. Ezután az
05/06 tématervek külön felhasználói jóváhagyása és a fizikai v3-ellenőrzés.


## 14. checkpoint – helyi energiaasszisztens

- Master/Loxone, kizárólag szöveges javaslat; nincs automatikus kapcsolás.
  A meglévő WS-deltatárból legfeljebb 12 minimális fogyasztóbejegyzés jön,
  nyitott fogyasztói oldal vagy háttérben tartott ikon/widget nélkül.
- PV-javaslat: pontosan 100% SOC, nappali normál üzem, nincs akkukisütés,
  ismert nulla fogyasztás, engedélyezett és ismert átlagigény. A mért export és
  PV−ház−akkutöltés minimuma legalább az igény + max(20%, 100 W) legyen 120 s-ig.
  Invalid/stale/reconnect/revízió/időhiba új minősítést igényel; megszűnéskor eltűnik.
- X után 30 perc csend. Ugyanaz a fogyasztó csak igazolt, legalább 10 perces
  feltételhiány után kerül új epizódba; ismeretlen adat nem igazolja a hiányt.
  A bezárás RAM-állapot, reboot után nincs tartós csendidő.
- Éjszakai eltérés: az előző teljes helyi 00..04 óra összevetése az előző hét
  legalább három, azonos órájának mediánjával. Minden ötperces slot legalább
  270 s igazolt éjszakai mérés, legfeljebb 30 s mintapárból, monoton/falióra
  egyezéssel. Gyűjtés és lekérés is szinkronizált órához kötött. Óravisszalépés/DST-ismétlés/hiány kizár, nem lesz kitalált nulla.
  Legalább +50% ÉS +100 W órás/élő eltérés; az élő feltétel 60 s tartósságú.
  X után ugyanazon helyi éjszakán nem tér vissza.
- A meglévő v3 napfájl opcionális night_seconds/night_load_kwh mezőket kap;
  régi energiaadat megmarad, lefedettség nélküli fájl nem referencia.
  Két naphoz összesen 960 bájt új adat és legfeljebb 512 bájt referenciacache;
  nincs UI-s SD I/O, új teljes napmásolat vagy BLE-wire-változás.
- Gyári témánként a dekoratív címsáv ad helyet, külön kontrasztos panellel és
  44 px X-szel. Fontos adat/diagram/óra/időjárás/házérintés nem fedhető.
  Speciális oldalon, diagnosztikán és PIN-padnál rejtett. Külső témánál opcionális,
  geometriailag ellenőrzött assistant_area; ismeretlen területre nincs rárajzolás.
  HU/EN felirat, 1 kW alatt W, felette tizedes kW. A Rendszer alapelrendezése megmarad.
- Tényleges döntési modell: 440 bájt, fogyasztó-snapshot 1744 bájt. A modell és
  bezárása LVGL-lock alatt; a forráspillanatképek előtte készülnek. A főszimulátor
  is ezt az illesztést használja; a desktop demo nem talál ki valódi ajánlást.
- A teljes helyi natív/UBSan regresszió és a végső forrás GitHub-regressziója sikeres. Új valós statisztikai integráció/SD/reboot/
  medián/legacy/ritka minta/idő-visszalépés teszt; valódi WS->minimális snapshot teszt.
- A helyi LVGL-fordítás és render a Rendszer és mind a kilenc gyári téma mindkét
  napszakán sikeres; adatdobozokkal való átfedés, X/parancsmentesség, rejtett
  oldalak és widgetmegőrzés ellenőrzött. A minták szintetikusak. A generált képek
  kizárólag tesztkimenetek; új PNG nem kerül a repóba.
- Részletes szabályok/korlátok: [energy-assistant.md](energy-assistant.md).
  A végső forrás összes CI-ellenőrzése sikeres, a méretek alább rögzítve. Fizikai paneles próba nincs.

Nincs PR vagy release. Az energiaasszisztens utáni következő blokk a külön
jóváhagyandó 05/06 tématerv, a külső témák célzott ellenőrzése, HU/EN minták és
v3 kiadási/fizikai zárási ellenőrzések. A tartós feltöltési engedély változatlan.


A 14. checkpoint ellenőrzött zárása:

- Végső forráscommit: `28dd02397bdfd08e8d979cbe2a335f4d566c1aec`;
  teljes forrásfa: `b2d39ef697c7860da6c5dff5c8d512008df3d3d0`.
  A helyi Git-fa a feltöltött fával egyezik; a záró commit csak dokumentáció.
- A végső forrás összes CI-jobja sikeres:
  [Master regresszió/LVGL/Chromium/ikonok/csomagok/két firmware](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39563853399),
  [Windows főszimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39563853498),
  [4B Linux/Windows szimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39563853404).
- A helyi teljes natív/UBSan csomag a szinkronizáltórát-követelő javítással is
  sikeres. A valódi statisztikai integrátor a hibás/nem szinkronizált órából nem
  gyűjt referenciát; a már érvényes referencia lekérése is szinkronizált órát igényel.
  Az állapotzárat a statisztikai zár megszerzése előtt elengedjük.
- Tényleges C döntési modell: 440 bájt; minimális 12 fogyasztós snapshot 1744 bájt.
  Valódi WS-frissítő/nulla/disconnect -> snapshot; statisztikai integráció,
  NVS helyett a meglévő SD-napfájl, reboot, medián, legacy, ritka mérés,
  óra-visszalépés és nem szinkronizált óra próbái sikeresek. A közös BLE- és
  régi energia-/statisztikai regressziók megmaradtak.
- Valódi LVGL: 20 asszisztenskép (Rendszer + kilenc gyári téma, nappal/éjjel).
  Adat/óra/időjárás/diagram téglalapjaival nincs átfedés; X nem küld parancsot,
  külön oldalon/diagnosztikán/Slave módban rejtett, ismételt frissítés megtartja
  a widgetet. Hosszú név fix egy sorban marad, HU/EN és külső téma helyének
  hiánya/érvényes/hibás megadása külön ellenőrzött.
  [Teljes LVGL-képsor CI-artifactja](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39563853399/artifacts/12329017232)
  (7 napos megőrzés). A minták szintetikusak; a helyi Rendszer/Élő otthon/Nap pálya
  renderből tényleges képi ellenőrzés is történt. Ez nem hardveres FPS/panelpróba
  és nem a még el nem készült 05/06 új látványtervek jóváhagyása.
- Master app: `0x2e6f80` = **3 043 200 bájt**, a 13. checkpointhoz
  **+9856 bájt**; 8 MiB app-partíción 64% szabad. OTA SHA256:
  `291b3e325b445b545c3e037bd5e2985d8f75174eac383035b9e2a513bb140453`.
- 4B app: `0x320820` = **3 278 880 bájt**, **+3008 bájt**;
  12 MiB app-partíción 74% szabad. OTA SHA256:
  `febca37efa6926d8e8d21d8bd053051fd03e3b45bd21d496116d2ce6c5a20e5b`.
  A 4B-n az asszisztens felülete nincs bekötve; a közös statisztikai modul nőtt.
- Mindkét OTA/USB-csomag és a valós binárisokkal végzett USB dry-run ellenőrzés
  sikeres. A növekedés új funkció, nem formázás. Nincs bájtraazonossági állítás.
- Új képfájl, generált header kézi módosítása, új BLE-task, második WS vagy
  automatikus kapcsolás nincs. A 64 ikon és a 4 796 478 bájtos közös resource ZIP
  változatlan. A teljes fizikai RAM/DMA/PSRAM/FPS és Miniserver/panel próba hátravan.
- A záró commit `[skip ci]`, kizárólag leírásokat módosít. Nincs PR vagy release.

A következő folytatás a D07 szerinti külön termékegyeztetés: először az
`05-calm-home` új látványtervének iránya, majd a terv jóváhagyása, és csak utána
eszközméretű grafika/implementáció. Felvetett, MÉG NEM ELFOGADOTT irány: meleg,
otthonos nappalibelső, három nagy főadat (PV, házfogyasztás, SOC), visszafogott
napi összesítők; asszisztensnek és házérintésnek kijelölt hely. Nem készült új
05/06 kép és nem változott e két téma elrendezése. Azonosító/sorrend megőrzendő.
Ezután `06-energy-orbit` külön terv, külső témák célzott végellenőrzése,
HU/EN minták és a v3 kiadási/fizikai zárási lista. A feltöltési engedély változatlan.

## Checkpoint 2026-10-21 / 15 – jóváhagyott Nyugodt otthon új téma

A 14. checkpoint végén még felvetett 05-irányt és az új, külön generált
nappalihátteret a felhasználó elfogadta, és engedélyezte a megvalósítást.
Az `05-calm-home` v9 elkészült a jóváhagyott berendezéssel; azonosító és sorrend
megmaradt. Három nagy élő főadat, diszkrét napi összegzés, HU/EN felirat,
kijelölt asszisztenshely és külön Otthon érintőgomb. A háttér más kártyán nem
szerepel, az Otthon/fogyasztói oldalak közös erőforráscsomagja nem változik.
A részletes grafikai specifikáció, méret és ellenőrzés: [calm-home.md](calm-home.md).

- A két háttér valóban 1280×800, 8 bites RGB PNG; 871224 és 720798 bájt.
  A nagy eredetik és a régi látványterv nem kerülnek a repóba.
- A kezdeti 1,79/1,57 MB PNG-k kódolt feltöltésekor a felhasználó
  `Invalid conversation body` hibát jelzett. RGB565-egyezést megőrző csatorna-
  redukció és veszteségmentes oxipng után a két háttér blobfeltöltése sikeres.
  A kisebb méret nem csökkenti a felbontást és nem változtatja a kijelző színeit.
- Nincs új firmware-be ágyazott kép, módosított C futtatókód vagy keretemelés.
  A témát az SD-s ZIP-csomag telepítésével/frissítésével kapja meg a kijelző;
  az OTA önmagában nem írja felül a telepített sablont.
- A helyi teljes natív/UBSan regresszió sikeres. Az új tényleges LVGL-próba
  HU/EN, nappal/éjjel, 15 kW/100% mellett ellenőrzi a levágásmentes szövegeket.
  Néhány túl alacsony feliratmezőt a próba alapján javítottunk.
- A végső tömörített képekkel az ismételt teljes natív/UBSan, LVGL,
  manifest/ZIP és web-generátor ellenőrzés is sikeres. A helyi és távoli
  forrásfa egyezik: `0d6a3187d92cf3a722523c3a263640ab2a3478ea`.
- Forráscommit: `1c6314a9b2edb4054d08e894bbf89286ad81b1bb`.
  Mindhárom workflow minden jobja sikeres:
  [Master / regresszió / LVGL / Chromium / ikonok / ZIP / két firmware](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39597173636),
  [Windows főszimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39597173671),
  [4B Linux/Windows szimulátor](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39597173684).
- A teljes téma kódolt képei 1719283 bájtot, a konzervatív RGBA-dekódolás
  8847360 bájtot (8,44 MiB) igényel a meglévő 12 MiB keretből. Az LVGL
  méretezett ikonraszterei 188224 bájton maradnak; a háttér natív 1280×800.
  A telepítő ZIP 2139830 bájt. A közös Loxone-csomag 4796478 bájt, változatlan.
- Valódi nappali/éjszakai, HU/EN és szélsőértékes LVGL-képek:
  [CI-artifact](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39597173636/artifacts/12344830221)
  (7 napos megőrzés). Képi ellenőrzés helyben is történt. A minták szintetikusak.
- Master firmware: `0x2e6f80` = 3043200 bájt, **+0 bájt** a 14. checkpointhoz;
  8 MiB app-partíción 64% szabad. OTA SHA256:
  `0896e9053389f9a25a704023ae02e8e5c2c86a254f48a33ba405a39a8dc34cb4`.
- 4B firmware: `0x320820` = 3278880 bájt, **+0 bájt**;
  12 MiB app-partíción 74% szabad. OTA SHA256:
  `2fc1b2841a826a2d3bd6ddf6ed719cb5fcf08a049cf9b70d967f1bfb808b3d12`.
  Azonos méretből nincs bináris-bájtraazonossági állítás.
- Mindkét normál és kézi boot/no-stub USB dry-run a valódi buildkimenetekkel
  sikeres; a firmware- és külön OTA-artifactok feltöltése sikeres. Nincs fizikai
  flash vagy panel/FPS/PSRAM-próba. A záró `[skip ci]` commit csak dokumentáció.

Nincs PR vagy release. Következő termékegyeztetés: a `06-energy-orbit` külön
látványterve. A v3 fizikai RAM/DMA/PSRAM/FPS/Miniserver/panel próbái és a végső
kiadási ellenőrzőlista továbbra is hátravannak. A folyamatos feltöltési engedély él.

## Checkpoint 2026-10-21 / 16 – külső témák áttekintése, 06 irányjavaslat

A felhasználó a 15. checkpoint sikeres feltöltése után kérte a fejlesztési
csomag folytatását. A külső témák kis ráfordítású ellenőrzése elkészült:
[external-template-review.md](external-template-review.md).

- Nincs általános gyári ID-engedélylista; a deklaratív elemek és források,
  fájlnevek, JSON/PNG/ZIP és memóriakeretek továbbra is ellenőrzöttek.
- A meglévő gyári kompatibilitási ágak és a v3 Otthon/asszisztens kapui
  áttekintve; az opcionális hibás asszisztenshely a UI-ban rejtve marad,
  nem új telepítési követelmény. Érvényes régi csomagok támogatása megmarad.
- Friss `python3 tests/test_templates.py`: sikeres. A 15. checkpoint
  teljes LVGL/firmware/Windows/Linux CI-je változatlan futtatókódra vonatkozik.
- E checkpoint kizárólag dokumentáció; nincs új firmware-kód vagy képfájl,
  ezért nincs újrafordítás vagy új méretnövekedési állítás. Feltöltés `[skip ci]`.

A D07 előírása: „Két új vizuális terv a kedvelt Rendszer, Nap pálya és Élő otthon
alapján; generálás/implementálás előtt egyeztetés.” A 05 már elfogadott és kész;
a 06 iránya még külön felhasználói választ vár.

Javaslat az Energiakör új irányára, MÉG NEM ELFOGADVA: felülnézetes/enyhén
izometrikus, parkosított energiaudvar, középen modern családi ház, körívben
elhelyezett napelem-, akkumulátor- és hálózati pontokkal. A házhoz vezető
ívelt, színes energiautakon mozgó pontok; a megszokott import/export és
töltés/leadás irány/szín megőrzése. Nagy élő teljesítményadatok az eszközök
mellett, akkumulátornál SOC; alul diszkrét napi összesítők. Saját, új háttér:
nappal világos zöld kert, éjjel mélykék környezet és világító házablakok.
Külön biztonságos asszisztenshely, a ház érintése nyitja a fogyasztói oldalt.
Azonosító és sorrend változatlan. Nincs még generált 06 kép vagy elrendezésmódosítás.

Következő lépés: a fenti irány egyeztetése, majd egy önálló 06 látványterv
bemutatása; az elfogadott terv után a 1280×800-as nappali/éjszakai grafika és a
megvalósítás. A 05 hátteret vagy más kártya képét nem szabad újrahasználni.
Ezután v3 kiadási/fizikai zárási lista. Nincs PR vagy release;
a folyamatos repófeltöltési engedély változatlanul él.

## Checkpoint 2026-10-21 / 17 – elfogadott 06 irány, napállás-idővonalas látványterv

A felhasználó elfogadta a 16. checkpointban javasolt energiaudvaros irányt,
és kérte a nap állásának jelölését, akár 00:00–24:00 közötti vízszintes
idővonalon. Az új követelmény D16-ként bekerült a döntési táblába.

Elkészült a beépített imagegen eszközzel a külön nappali 06 látványjavaslat:
új, zöld parkosított kert kör alakú burkolattal, középen családi ház,
külön napelem-, hálózati és akkuponttal. A három ívelt energiaúton mozgó
pontok jelzik az áramlást; az adatcsoportok és napi összesítők külön felületeken.
A napi összesítők felett teljes szélességű, vízszintes napállás-idővonal:
00:00, 06:00, 12:00, 18:00, 24:00 skála, külön éjjeli/nappali szakasz,
napkelte- és napnyugtajel, az aktuális időnél napjelölő.

A mintaadatok illusztratívak: 14:30, napkelte 07:10, napnyugta 17:45;
PV 5 kW = ház 1 kW + export 2 kW + akkutöltés 2 kW, SOC 84%.
A generált kép az elrendezés látványjavaslata, nem firmware-render vagy
matematikailag garantált skála. A megvalósításban minden jelölő a tényleges
időadatból, azonos 24 órás skálán, arányosan kerül a helyére. Napkelte/
napnyugta a meglévő adatforrásból származzon; hiányzó adatnál nem lehet
kitalált időpont vagy rögzített nappali tartomány. Nem szükséges új külső API.
A jelölők nem takarhatják egymást; az élő feliratok és energiautak külön
LVGL-rétegek lesznek, nem a háttérbe égetett adatok.

A felhasználó az irányt már jóváhagyta, de ezt a konkrét új látványtervet még
nem. Következő lépés a látványterv visszajelzése, majd eszközméretű tiszta
háttérpár, rétegek, valódi 24 órás skála és HU/EN/nappal-éjjel ellenőrzés.
Az asszisztensnek és a ház érintési területének is biztonságos hely szükséges.
Az azonosító/sorrend megmarad. A nagy képi látványtervek nem kerülnek a repóba;
csak elfogadás után a méretezett, tömörített végleges háttérfájlok.
E checkpoint dokumentáció, `[skip ci]`; a futtatókód és a firmware változatlan.
Nincs PR vagy release. A tartós feltöltési engedély él.

## Checkpoint 2026-10-21 / 18 – elfogadott Energiakör és élő napállás-idővonal

A felhasználó elfogadta a 17. checkpoint konkrét látványtervét. Az
`06-energy-orbit` v13 megvalósítása elkészült: új, saját nappali/éjszakai
energiaudvar, világító éjszakai házablakok, három élő görbe energiaút, nagy
adatok, töltés/leadás, napi összesítők, külön házérintés és fejléc-asszisztens.
A háttérben nincs beégetett felirat/energiaút. Azonosító és sorrend megmaradt.
Részletek: [energy-orbit.md](energy-orbit.md).

- Új deklaratív `solar_timeline`, 00:00–24:00, mindhárom esemény ugyanazon
  időarányos skálán. A jelenlegi idő csak szinkronizált helyi órából; érvényes
  napkelte/napnyugta nélkül nincs arany sáv, kitalált esemény vagy nap/hold ikon.
  Nincs új adatforrás/task/kapcsolat/SD-kép. A meglévő widgetmezőket használja.
- Két 1280×800-as RGB565-színeket megőrző RGB háttér: 921894 és 711746 bájt.
  A nagy generált források nincsenek a repóban. A 05 és a közös Loxone képei
  változatlanok, a nem használt 06 akkukép kikerült.
- Valódi LVGL: minden helyi perc, éjfél, pontos napkelte/napnyugta,
  nap/hold-váltás, hiányzó/érvénytelen napadat és ismeretlen/nem szinkronizált
  óra, rövid nappali címkeütközés, HU/EN 15 kW és 100% próbák sikeresek.
  Tényleges animációval mindkét előjelirány és az asszisztens helye ellenőrzött.
- A natív teljes regresszió első futásán egy korábbi BLE-statisztikai CRC-próba
  tíz másodperces időhatára lejárt. Az ismételt teljes csomag sikeres;
  BLE-kód nem változott. A távoli CI-t is meg kell várni a végső záráshoz.
- Az új elem külső, nem gyári ID-jű csomagban is telepíthető, a túl keskeny/
  alacsony/kilógó változatot a C-validátor elutasítja. Régi elemek és gyári
  kompatibilitási ágak megmaradtak; Rendszer nézet nem lett áttervezve.
- A végső forrás ismételt LVGL/manifest/ZIP ellenőrzése, feltöltése, teljes CI,
  a két firmware és OTA/USB méreteinek rögzítése még folyamatban.

Az új megjelenéshez az új firmware és a 06 ZIP egyaránt szükséges, ebben a
sorrendben. A D07 két új témájának megjelenése így elfogadott és implementált;
a forrás csak a CI zárása után tekinthető ellenőrzöttnek. Nincs PR vagy release.
Ezután a teljes v3 szoftveres zárási lista és a külön fizikai
RAM/DMA/PSRAM/FPS/Miniserver/panel tesztlista következik. Feltöltési engedély él.

## Checkpoint 2026-10-21 / 19 – fizikai próba után kért javítások

A következő tételek a fejlesztési terv végére kerültek, felhasználói teszt alapján.
A feltöltési engedély él; PR és kiadás továbbra sem készül a teljes v3 lezárásáig.

- [ ] F01: a Loxone Home ZIP tartalmazzon sablonkezelő-előnézeteket.
- [ ] F02: telepített közös csomagnál kiválasztott ikon előnézete a szerkesztőben;
  hiányzó csomagnál piros, legfelül látható működési figyelmeztetés.
- [ ] F03: a kapcsoló és impulzus tényleges Loxone-vezérlése, megfelelő nyugtával
  és kapcsolóállapot-visszajelzéssel; bizonytalan kérés automatikus ismétlése nélkül.
- [ ] F04: Otthon/fogyasztók/diagnosztika bezárásakor látható betöltőképernyő.
- [ ] F05: fogyasztói nézet villogásának és hiányzó hátterének megszüntetése.
- [ ] F06: Rendszer téma energiacsillagának házérintése nyissa a fogyasztókat.
- [ ] F07: diagnosztikai Bluetooth-gomb felirata mindkét tengelyen középen.
- [ ] F08: betöltési felirat: „Adatok és grafikai elemek betöltése...” (HU/EN).
- [ ] F09: sablontörlés alatt azonnali, látható webes folyamatjelzés.
- [ ] F10: Actions-kimenetek és kiadási függőségek teljes auditja; maradjanak a
  témacsomagok, a két eszköz USB/OTA firmware-csomagja és a Windows szimulátorok.
  Fölösleges külön OTA-másolat és letölthető Linux-szimulátor ne keletkezzen.
  A regressziós ellenőrzések és a kiadási folyamat szükséges bemenetei maradjanak.

A fenti hibák javítása és ellenőrzése folyamatban; a fizikai megfigyelések
lezárásához az új firmware-rel megismételt eszközpróba is szükséges.


A 18. checkpoint távoli zárása (`f496b54a11ed7129aa2880e7b6fd5d52cef47f09`):
Master workflow `39610338353`, minden hat job sikeres; Windows fő szimulátor
`39610338619` és 4B Linux/Windows szimulátor `39610338457` szintén sikeres.
Master alkalmazás 3046288 bájt (`0x2e7b90`), 64% szabad a 8 MiB-os app-partíción;
Slave 3278880 bájt (`0x320820`), 74% szabad a 12 MiB-os partíción.
Master OTA SHA256 `6c9dac7e8abedc6034276105867ccdad56cc2d40513cd2959ae8f400b2608a67`,
Slave `1cb34e86c226ea7e2ec8726083c12465ba94db420a95831fbcf35e7c250c6b9a`.
Ez a 18. checkpoint forrására vonatkozik; a 19. checkpoint új firmware-méretei
és CI-je külön ellenőrzendők.

A 19. checkpoint implementációja: F01–F10 kódmódosítása elkészült; részletes
működés, bizonyítékok és megismétlendő fizikai próba:
[v3-field-fixes.md](v3-field-fixes.md). A teljes natív regresszió sikeres;
a végső LVGL-futtatás sikeres. Az új távoli firmware/Windows CI zárása folyamatban.
A villogás fizikai megszűnése és a valós Miniserver-vezérlés továbbra is
eszközön ellenőrzendő. Ez nem zárható le pusztán szimulátoros eredménnyel.
