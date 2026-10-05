# Automatikus firmware-release

A `.github/workflows/release.yml` a GitHub **Publish release** eseményére indul, normál release és prerelease esetén is. A tag által jelölt commitot fordítja ESP-IDF 5.5.4-gyel ESP32-P4 célra. A release-tagben levő kódnak már tartalmaznia kell ezt a workflow-t.

1. Válaszd ki a kiadás commitját, és készíts például `v1.2.0` vagy `v1.2.0-rc.1` taget. A teljes tag legfeljebb 31 ASCII karakter; hibás verziót a build elutasít.
2. Publikáld a hozzá tartozó GitHub release-t. A három ellenőrzés (regressziós tesztek, tényleges LVGL render, firmware-build és csomagolás) lefut.
3. Siker után az Assets között megjelenik:

   - `ZoleeEnergyFlowDisplay-v1.2.0-ota.bin`: ezt töltsd fel a `http://ESZKOZ-IP/update` oldalon.
   - `ZoleeEnergyFlowDisplay-v1.2.0-usb.zip`: teljes USB első telepítő, magyar útmutatóval és `flash.py` indítóval.
   - `SHA256SUMS.txt`: az OTA, USB és build-info SHA256 ellenőrzőösszegei.
   - `build-info.json`: tag, commit, célhardver, ESP-IDF/esptool verzió és flash-fájlok ellenőrzőösszegei.

Az OTA és az USB csomag alkalmazásképe bájtra azonos. A ZIP a build összes flash-bemenetét tartalmazza, az eredeti címekkel és hordozható fájlutakkal. A rögzített esptool verzió `5.1.0`; [USB útmutató](usb-install.md).

A release-verziót a generált `version.txt` adja az ESP-IDF-nek; a csomagoló a binárisba írt verziót is ellenőrzi. Helyi, verziófájl nélküli fejlesztői build alapértéke `0.1.0`. A PR/main ellenőrzés ugyanazt a csomagolást `v0.0.0-ci` verzióval futtatja, és Actions artifactként teszi elérhetővé; ez nem publikál GitHub release-t.

## Újrafuttatás és hozzáférés

Sikertelen feltöltésnél az Actions **Re-run failed jobs** újra felhasználja a már elkészült artifactot. Azonos nevű, azonos SHA256-ú release-fájl kimarad a feltöltésből. Eltérő vagy nem ellenőrizhető fájlt a folyamat nem töröl és nem ír felül: hibával megáll. Teljes újrafordításnál a fordítási időbélyeg miatt változhat a bináris; ilyen esetben új verzió kiadása szükséges.

Csak a publikáló job kap `contents: write` jogosultságot, a teszt/build jobok olvasást. Párhuzamos main/PR build és másik release nem szakítja meg a kiadást. A publikálás előtt a tag commitját és az artifact eredetét ismét ellenőrzi a folyamat.

Ha a repository azonnal **immutable** állapotba zárja a publikált release-t, a GitHub nem enged utólag fájlokat hozzáadni. Ezt a workflow világos hibával jelzi, az elkészült csomagok az Actions artifactban megmaradnak; a repository védelmét nem módosítja. Ilyen beállításnál külön, publikálás előtti csomagfeltöltési folyamat szükséges.

A tesztek a HTTP hibákat, konfigurációt, közös adatfeldolgozást, timeout/OTA állapotokat, a Pico C minta ciklusait, az USB-csomag teljességét és a release-feltöltés újrafuttatását is ellenőrzik. A fizikai USB/OTA telepítés és a Miniserver Gen 2 / 17.x integráció hardveres átvételi próba marad.
