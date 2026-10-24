# Automatikus firmware-release

A master/Slave, PIN-es párosítás, SD nélküli Rendszer nézet és éjféli OTA
fejlesztésének átadási állapota, automatizált bizonyítékai és eszközös próbái:
[master/Slave lezárási jegyzet](master-slave-handover.md). A kiadások a
10,1 colos P4 mellett a [720×720-as Waveshare 4B Slave](slave-waveshare-4b.md)
külön firmware-ét is tartalmazzák. A két alkalmazás nem cserélhető fel.

## Workflow-k és események

| Actions-név | Fájl | Esemény és feladat |
| --- | --- | --- |
| Zolee \| CI - PR és kézi ellenőrzés | `.github/workflows/ci-checks.yml` | Mainre nyitott PR, PR-frissítés vagy kézi indítás; teljes ellenőrzés és tesztcsomagok |
| Zolee \| Automatikus kiadás - main | `.github/workflows/release-main.yml` | Main push; ellenőrzés és egyetlen buildút. Igazolt PR-merge esetén verziózott kiadás, közvetlen pushnál csak CI |
| Zolee \| Kézi kiadás - firmware | `.github/workflows/release-manual.yml` | Kézzel publikált GitHub release; a tag pontos forrásából készít mellékleteket |
| Zolee \| Közös build - tesztek és csomagok | `.github/workflows/ci-shared.yml` | Meghívott `workflow_call`; önmagában nem indul pushra vagy PR-re |

A jobok neve külön jelöli a Master 10.1 és Slave 4B célt,
a firmware-t, a Windows/Linux szimulátort és az ellenőrzés típusát.
A korábbi futások megőrzik a régi nevüket; az új nevek a módosítást tartalmazó
forrásra érvényesek, a fő Actions-lista a mainre egyesítés után frissül.

Feature pushra nincs második, párhuzamos ellenőrzés: a nyitott PR új commitja
pontosan egy CI-t indít. PR nélküli ágat a CI **Run workflow** gombjával lehet
ellenőrizni. A régi önálló `simulator.yml` és `slave-4b.yml` megszűnt; a tesztjeik
változatlanul a közös folyamatban futnak. A legújabb PR-frissítés megszakítja
az elavult PR-futást. Mainen a teljes folyamat soros, `queue: max` mellett;
legfeljebb 100 várakozó merge fér a sorba, nincs megszakított kiadás.

Először a natív C/UBSan, JS, Chromium desktop/mobil, ikon-, sablon-, Master
LVGL/ASan 32 bit/RGB565 és a két platformos Slave-szimulátor teszt fut.
A költséges ESP-IDF firmware-build csak mindegyik sikeres eredménye után indul.
A 100–100 teljes Home-életciklus memóriahozzáférési és szivárgásellenőrzést is kap.
A rögzített actionlint a workflow-szintaxist, külön szerződésteszt az eseményeket,
a függőségi gráfot, jogosultságokat és artifact-átadást ellenőrzi.

## Pontosan négy letölthető csomag

| Artifact | Tartalom | Felhasználás |
| --- | --- | --- |
| `zolee-firmware-<commit>` | Mindkét hardver OTA-ja és teljes USB ZIP-je, két build-info, közös SHA256SUMS | OTA, USB és a két kiadási publikáló |
| `zolee-templates-<commit>` | Kilenc téma és a közös `loxone-home.zip`, előnézetekkel | Master SD-s telepítés/frissítés |
| `zolee-simulator-master-windows-<commit>` | Fő kijelző Windows EXE és az összes szükséges SD-erőforrás | Helyi kipróbálás |
| `zolee-simulator-slave-4b-windows-<commit>` | 4B Slave Windows EXE | Helyi kipróbálás |

A commit minden névben a checkoutolt, tesztelt forrás SHA-ja. PR-nél ez a GitHub
ellenőrző merge-commitja; mainen a tényleges main commit, kézi release-nél a tag
commitja. A firmware build-info ugyanezt az eredetet őrzi. A témák és szimulátorok
újrafuttatása azonos nevű ideiglenes artifactot cserél, új másolatot nem készít.
A firmware-artifact felülírása tiltott: félbeszakadt publikáláskor az eredeti
bájtokat kell újra felhasználni a **Re-run failed jobs** útján. A release Assets
fájljait sem írja felül és nem törli.

Az Actions az artifactot ZIP-ként tölti le. Webes frissítéshez a megfelelő
`-ota.bin` fájlt válaszd az eszköz `/update` oldalán. A két hardver binárisa nem
cserélhető fel. Külön OTA-artifact, PDB, nagy renderképcsomag és Linux-szimulátor-
letöltés nincs; a Linuxos tesztek megmaradtak. A régi futások története megmarad.

## Automatikus kiadás PR-merge után

A **#20 kaputelefonos PR** egyszeri célverziója **v3.1.0**, a szokásos
patch-növelés helyett. A szabály kizárólag a saját `feature/loxone-intercom`
ág igazolt main-merge-jére érvényes. Összevonás nélküli lezárás nem ad ki
verziót. Meglévő stabil v3.1.0 vagy újabb verziónál ütközési hibával leáll;
a korábbi saját foglalást újrafuttatáskor megőrzi. Más PR marad patch-kiadás.

A kaputelefonos PR összevonása előtt az aktuális commit teljes Actions-futása
legyen sikeres, beleértve a Master/Slave firmware-t, a méret-/OTA-ellenőrzést,
a böngészős és LVGL/szimulátoros teszteket. A videót, kétirányú hangot és az
5 másodperces bontást a felhasználó 2026-10-24-én készüléken visszaigazolta.
A végső felirattisztítás a médiakezelést nem módosítja. Az összevonás utáni
main-futás külön építi és teszteli a v3.1.0 csomagokat, majd publikál;
a PR jóváhagyása önmagában még nem indít kiadást.

A v3 fejlesztés egyszeri célverziója **v3.0.0**. Kizárólag a
`ZoleeBB/ZoleeEnergyFlowDisplay` saját `feature/v3-loxone-home` ágából igazoltan
mainre összevont PR kapja. A kapcsolódó PR-t a pontos main commithoz kérjük le,
majd ellenőrizzük az összevonást, a merge SHA-t, a célrepót és az eredeti saját
forráságat. Azonos nevű forkág vagy PR-cím nem emelhet főverziót. A korábbi
félbeszakadt kiadás saját foglalt verzióját az újrafuttatás megtartja.
Meglévő stabil v3.0.0 vagy újabb verziót a szabály nem ír felül.

A main push egyetlen workflow-t indít. A sikeres tesztek után a verzióválasztó
eldönti, hogy igazolt merge vagy közvetlen main push történt. Utóbbinál
`v0.0.0-ci` tesztcsomag készül, publikálás nélkül. Merge-nél a kiválasztott
verzióval egyszer fordul mindkét firmware. Minden ellenőrzés, mindkét build és
az USB-telepítő dry-run sikere kell a publikáláshoz. Nincs külön main CI-build
és ugyanarra a változásra újabb automatikus release-build.

Normál merge-nél a számszerűen legnagyobb stabil tag/release patch-verziója nő:
`v1.2.3 → v1.2.4`. Prerelease és `v0.0.0-ci` nem alapverzió. A régi `Prod` tag
numerikus release-címe is számít; előzmény nélkül `v0.0.1` következik. A korábbi
#9/#12 egyszeri v1/v2 szabályok megmaradtak; a v3 után a szokásos patch-növelés
folytatódik. Fejlesztési feltöltés és PR-nyitás nem készít taget vagy release-t.

Írási jogosultság csak a publikáló jobnak jár. Előbb annotált tagben rögzíti a PR
és commit azonosságát, draftot készít, feltölti az összes ellenőrzött fájlt,
majd publikál. A GITHUB_TOKEN-nel létrehozott release nem indít újabb release
workflow-t; a folyamat közvetlenül hívja a közös buildet és publikálót, PAT nélkül.
A korábbi `pull_request_target: closed` indítás helyét az ellenőrzött main push
vette át. Az új megoldás nem futtatja írási tokennel a nyitott PR kódját.

Sikertelen teszt/build esetén nincs új tag vagy release. Feltöltési hiba esetén
a draft és a foglalt verzió marad; **Re-run failed jobs** ugyanazokat az elkészült
csomagokat töltheti fel. A már publikált merge újrafuttatása nem épít új firmware-t
ugyanarra a verzióra és nem publikál új kiadást. Ütköző tag vagy eltérő meglévő
release-fájl hibát ad; semmi nem kerül más commitra vagy más bájtokra átírva.

## Kézi release publikálása

A `.github/workflows/release-manual.yml` a GitHub **Publish release** eseményére indul, normál release és prerelease esetén is. A tag által jelölt commitot fordítja ESP-IDF 5.5.4-gyel ESP32-P4 célra. A release-tagben levő kódnak már tartalmaznia kell ezt a workflow-t.

1. Válaszd ki a kiadás commitját, és készíts például `v1.2.0` vagy `v1.2.0-rc.1` taget. A teljes tag legfeljebb 31 ASCII karakter; hibás verziót a build elutasít.
2. Publikáld a hozzá tartozó GitHub release-t. Az ellenőrzések (regressziós tesztek, tényleges LVGL render, firmware-build és csomagolás) lefut.
3. Siker után az Assets között megjelenik:

   - `ZoleeEnergyFlowDisplay-v1.2.0-ota.bin`: ezt töltsd fel a `http://ESZKOZ-IP/update` oldalon.
   - `ZoleeEnergyFlowDisplay-v1.2.0-usb.zip`: teljes USB első telepítő, magyar útmutatóval és `flash.py` indítóval.
   - `ZoleeEnergyFlowDisplay-slave-4b-v1.2.0-ota.bin`: a 720×720-as 4B Slave saját, automatikus OTA-ja.
   - `ZoleeEnergyFlowDisplay-slave-4b-v1.2.0-usb.zip`: a 4B Slave első USB telepítője, SD nélkül.
   - `slave-4b-build-info.json`: a 4B fordítás eredete és ellenőrzőösszegei.
   - `SHA256SUMS.txt`: mindkét hardver OTA, USB és build-info SHA256 ellenőrzőösszegei.
   - `build-info.json`: tag, commit, célhardver, ESP-IDF/esptool verzió és flash-fájlok ellenőrzőösszegei.

Az OTA és az USB csomag alkalmazásképe bájtra azonos. A ZIP a build összes flash-bemenetét tartalmazza, az eredeti címekkel és hordozható fájlutakkal. A rögzített esptool verzió `5.5.0`; [USB útmutató](usb-install.md).

A release-verziót a generált `version.txt` adja az ESP-IDF-nek; a csomagoló a binárisba írt verziót is ellenőrzi. Helyi, verziófájl nélküli fejlesztői build alapértéke `0.1.0`. A PR, a kézi CI és a közvetlen main push `v0.0.0-ci` tesztcsomagot készít; igazolt main merge-nél a közös build már a végleges kiválasztott verzióval fut.

## Újrafuttatás és hozzáférés

Sikertelen feltöltésnél az Actions **Re-run failed jobs** újra felhasználja a már elkészült artifactot. Azonos nevű, azonos SHA256-ú release-fájl kimarad a feltöltésből. Eltérő vagy nem ellenőrizhető fájlt a folyamat nem töröl és nem ír felül: hibával megáll. Teljes újrafordításnál a fordítási időbélyeg miatt változhat a bináris; ilyen esetben új verzió kiadása szükséges.

Csak a publikáló job kap `contents: write` jogosultságot, a teszt/build jobok olvasást. Párhuzamos main/PR build és másik release nem szakítja meg a kiadást. A publikálás előtt a tag commitját és az artifact eredetét ismét ellenőrzi a folyamat.

Ha a repository azonnal **immutable** állapotba zárja a publikált release-t, a GitHub nem enged utólag fájlokat hozzáadni. Ezt a workflow világos hibával jelzi, az elkészült csomagok az Actions artifactban megmaradnak; a repository védelmét nem módosítja. Ilyen beállításnál külön, publikálás előtti csomagfeltöltési folyamat szükséges.

A tesztek a HTTP hibákat, konfigurációt, közös adatfeldolgozást, timeout/OTA állapotokat, a Pico C minta ciklusait, az USB-csomag teljességét és a release-feltöltés újrafuttatását is ellenőrzik. A fizikai USB/OTA telepítés és a Miniserver Gen 2 / 17.x integráció hardveres átvételi próba marad.

Az USB ZIP a `docs/` teljes aktuális útmutatókészletét, az `examples/` mintáit és az assetjegyzéket is tartalmazza; a gyökérbeli `TELEPITES.md` a telepítési útmutatóra vezet. A repó aktuális `docs/` állományai is bekerülnek; a zárási áttekintés megkülönbözteti a kész funkciókat a még nem mért hardveres terheléstől.

## Actions artifactok megőrzése

A négy firmware-, sablon- és Windows-szimulátor-feltöltés
`retention-days: 7` beállítást használnak. A GitHub repó/szervezet felső korlátja
rövidebb tényleges lejáratot is meghatározhat; ezt az Actions felületén ellenőrizd.
A beállítás az új artifactokra vonatkozik, a korábbiak lejáratát nem módosítja.
A release Assets alá publikált firmware-ek megőrzését ez nem érinti.

A release-ek és Assets mellékleteik nem Actions artifactok: a hét napos artifact-retenció nem járatja le őket. A repository workflow-jai nem törölnek kiadásokat vagy release-mellékleteket. A kiadások kézi törlésig, illetve a repository elérhetőségéig maradnak letölthetők; ez nem azonos a módosíthatatlansági védelemmel. Ellenőrzéskor a v1.0.0–v1.0.2 kiadások OTA/USB Assets fájljai elérhetők voltak.

## Workflow-karbantartás

Helyi ellenőrzés: `python3 -m pip install -r tests/requirements.txt`, majd
`python3 tests/run_tests.py`. A workflow- és release-útvonal szerződéstesztjei,
a Markdown-hivatkozásellenőrzés is ebben futnak. A YAML-linter rögzített
v1.7.12 binárisa SHA256-ellenőrzött. Ez a verzió a GitHub új `queue` mezőjét
még nem ismeri; kizárólag ezt a szintaktikai diagnosztikát hagyjuk ki, a queue /
cancel-in-progress összeillőséget külön teszt védi.
Hivatalos háttér: [újrafelhasználható workflow-k](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows),
[párhuzamosság és sorba állítás](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency).
