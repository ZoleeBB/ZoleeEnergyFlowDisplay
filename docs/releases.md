# Automatikus firmware-release

## Automatikus kiadás PR-merge után

A `Release merged PR` workflow a `main` ágra **ténylegesen összevont** PR lezárásakor indul. A puszta jóváhagyás, a merge nélküli bezárás vagy a közvetlen main push nem készít kiadást.

A folyamat automatikusan növeli a javítóverziót: `v1.2.3 → v1.2.4`. A létező stabil verziótagek és release-ek közül a számszerűen legnagyobbat veszi alapul; prerelease és `v0.0.0-ci` nem számít bele. A régi `Prod` tagű, `0.0.1` című kiadást is felismeri. Korábbi verzió nélkül `v0.0.1`-gyel indul. A minor/major verzió kézzel kiadott új stabil taggel emelhető, például `v1.0.0`; utána `v1.0.1` következik.

A kiválasztott verzióval a PR pontos merge-commitja fordul újra, és minden regressziós/LVGL ellenőrzés lefut. A workflow csak ezután hoz létre draft release-t, feltölti az OTA/USB-csomagokat és a kísérőfájlokat, végül publikálja a kész kiadást. Ez az immutable release beállítással is összeegyeztethető: a fájlok a lezárás előtt kerülnek fel.

A verzióválasztástól a publikálásig egy kiadás fut egyszerre. A GitHub `queue: max` sorába legfeljebb 100 futás várhat; a gyors egymásutáni merge-ek nem írják felül a várakozó futást. A sorrend a GitHub sorba állítását követi. A workflow a GITHUB_TOKEN használata miatt közvetlenül hívja a közös buildet és publikálót; nem támaszkodik egy második, automatikusan kiváltott release-eseményre. Külön PAT nem szükséges.

Sikertelen teszt/build esetén nincs új tag vagy release. A publikáló job a sikeres build után előbb annotált verziótaget hoz létre, benne a PR és a merge SHA azonosítójával, majd draftot. Az előkészítő ebből a tagből olvasási joggal is felismeri a félbeszakadt kiadást, akkor is, ha a draft számára nem látható vagy még létre sem jött. A sikertelen publikálás így lefoglalhat egy verziót, amelyet újrafuttatáskor folytatunk. Feltöltési hiba esetén a draft megmarad, és az Actions **Re-run failed jobs** ugyanazokat a firmware-fájlokat használhatja újra. A már publikált merge újrafuttatása nem növeli újra a verziót. Meglévő tag más commitra mozgatása és release-fájl felülírása tiltott. Egy manuális kiadással ütköző verzió esetén a folyamat hibával leáll.

A `pull_request_target: closed` trigger kizárólag `merged == true` esetben indul tovább; soha nem checkoutolja a nyitott PR fejét. A merge SHA-t és a main célágat a GitHub API-val is ellenőrzi. Írási jogosultság csak a publikáló jobnak jár.

## Kézi release publikálása

A `.github/workflows/release.yml` a GitHub **Publish release** eseményére indul, normál release és prerelease esetén is. A tag által jelölt commitot fordítja ESP-IDF 5.5.4-gyel ESP32-P4 célra. A release-tagben levő kódnak már tartalmaznia kell ezt a workflow-t.

1. Válaszd ki a kiadás commitját, és készíts például `v1.2.0` vagy `v1.2.0-rc.1` taget. A teljes tag legfeljebb 31 ASCII karakter; hibás verziót a build elutasít.
2. Publikáld a hozzá tartozó GitHub release-t. Az ellenőrzések (regressziós tesztek, tényleges LVGL render, firmware-build és csomagolás) lefut.
3. Siker után az Assets között megjelenik:

   - `ZoleeEnergyFlowDisplay-v1.2.0-ota.bin`: ezt töltsd fel a `http://ESZKOZ-IP/update` oldalon.
   - `ZoleeEnergyFlowDisplay-v1.2.0-usb.zip`: teljes USB első telepítő, magyar útmutatóval és `flash.py` indítóval.
   - `SHA256SUMS.txt`: az OTA, USB és build-info SHA256 ellenőrzőösszegei.
   - `build-info.json`: tag, commit, célhardver, ESP-IDF/esptool verzió és flash-fájlok ellenőrzőösszegei.

Az OTA és az USB csomag alkalmazásképe bájtra azonos. A ZIP a build összes flash-bemenetét tartalmazza, az eredeti címekkel és hordozható fájlutakkal. A rögzített esptool verzió `5.5.0`; [USB útmutató](usb-install.md).

A release-verziót a generált `version.txt` adja az ESP-IDF-nek; a csomagoló a binárisba írt verziót is ellenőrzi. Helyi, verziófájl nélküli fejlesztői build alapértéke `0.1.0`. A PR/main ellenőrzés ugyanazt a csomagolást `v0.0.0-ci` verzióval futtatja, és Actions artifactként teszi elérhetővé; ez nem publikál GitHub release-t.

## Újrafuttatás és hozzáférés

Sikertelen feltöltésnél az Actions **Re-run failed jobs** újra felhasználja a már elkészült artifactot. Azonos nevű, azonos SHA256-ú release-fájl kimarad a feltöltésből. Eltérő vagy nem ellenőrizhető fájlt a folyamat nem töröl és nem ír felül: hibával megáll. Teljes újrafordításnál a fordítási időbélyeg miatt változhat a bináris; ilyen esetben új verzió kiadása szükséges.

Csak a publikáló job kap `contents: write` jogosultságot, a teszt/build jobok olvasást. Párhuzamos main/PR build és másik release nem szakítja meg a kiadást. A publikálás előtt a tag commitját és az artifact eredetét ismét ellenőrzi a folyamat.

Ha a repository azonnal **immutable** állapotba zárja a publikált release-t, a GitHub nem enged utólag fájlokat hozzáadni. Ezt a workflow világos hibával jelzi, az elkészült csomagok az Actions artifactban megmaradnak; a repository védelmét nem módosítja. Ilyen beállításnál külön, publikálás előtti csomagfeltöltési folyamat szükséges.

A tesztek a HTTP hibákat, konfigurációt, közös adatfeldolgozást, timeout/OTA állapotokat, a Pico C minta ciklusait, az USB-csomag teljességét és a release-feltöltés újrafuttatását is ellenőrzik. A fizikai USB/OTA telepítés és a Miniserver Gen 2 / 17.x integráció hardveres átvételi próba marad.

Az USB ZIP a `docs/` teljes aktuális útmutatókészletét, az `examples/` mintáit és az assetjegyzéket is tartalmazza; a gyökérbeli `TELEPITES.md` a telepítési útmutatóra vezet. A fejlesztési munkanaplók és elvetett javaslatok nem kerülnek a csomagba.

## Actions artifactok megőrzése

A firmware-, sablon-, LVGL-előnézet- és Windows-szimulátor-feltöltések
`retention-days: 7` beállítást használnak. A GitHub repó/szervezet felső korlátja
rövidebb tényleges lejáratot is meghatározhat; ezt az Actions felületén ellenőrizd.
A beállítás az új artifactokra vonatkozik, a korábbiak lejáratát nem módosítja.
A release Assets alá publikált firmware-ek megőrzését ez nem érinti.
